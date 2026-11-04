#include "conference_media.h"
#include "camera_mode.h"
#include "share_quality.h"
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QTimer>
#include <memory>
#include <cmath>
#include <atomic>
#ifdef ZJDESK_HAS_GSTREAMER
#define GST_USE_UNSTABLE_API
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <gst/webrtc/webrtc.h>
#endif

namespace zjdesk
{
#ifdef ZJDESK_HAS_GSTREAMER
struct ConferenceGuard
{
    QMutex mutex;
    ConferenceWorker *worker = nullptr;
    quint64 epoch = 0;
};
struct ConferenceCallback
{
    std::shared_ptr<ConferenceGuard> guard;
    quint64 epoch;
    QString type;
};
#endif

class ConferenceWorker final : public QObject
{
    Q_OBJECT
public:
    explicit ConferenceWorker(ConferenceMailbox *mailbox) : mailbox_(mailbox)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        guard_->worker = this;
#endif
    }
    ~ConferenceWorker() override
    {
        shutdown();
#ifdef ZJDESK_HAS_GSTREAMER
        QMutexLocker lock(&guard_->mutex);
        guard_->worker = nullptr;
#endif
    }
    void start(bool offerer, const ConferenceDevices &devices, quint64 epoch,
        bool microphoneMuted, bool cameraHidden, bool speakerMuted, bool shared = false, const CameraMode &mode = {})
    {
        shutdown();
        epoch_ = epoch;
#ifdef ZJDESK_HAS_GSTREAMER
        { QMutexLocker lock(&guard_->mutex); guard_->epoch = epoch; }
        offerer_ = offerer;
        remoteReady_ = running_ = offered_ = sawRemote_ = false;
        audioBuffers_ = 0;
        const char *description =
            "webrtcbin name=rtc bundle-policy=max-bundle latency=100 "
            "videorate name=videoentry drop-only=true ! videoscale ! videoconvert ! "
            "capsfilter name=cameraprofile ! videobalance name=cameragate ! tee name=v "
            "v. ! queue max-size-buffers=1 leaky=downstream ! videoconvert ! video/x-raw,format=RGB ! "
            "appsink name=local max-buffers=1 drop=true sync=false async=false wait-on-eos=false "
            "v. ! queue max-size-buffers=2 leaky=downstream ! videoconvert ! "
            "vp8enc name=cameraencoder deadline=1 cpu-used=8 keyframe-max-dist=60 ! "
            "rtpvp8pay pt=96 ! application/x-rtp,media=video,encoding-name=VP8,payload=96,clock-rate=90000 ! rtc. "
            "audioconvert name=audioentry ! audioresample ! audio/x-raw,rate=48000,channels=1 ! "
            "volume name=microphonegate ! opusenc bitrate=32000 ! rtpopuspay pt=97 ! "
            "application/x-rtp,media=audio,encoding-name=OPUS,payload=97,clock-rate=48000,encoding-params=(string)2 ! rtc. "
            "rtpvp8depay name=videoin ! vp8dec ! videoconvert ! video/x-raw,format=RGB ! "
            "appsink name=remote max-buffers=1 drop=true sync=false async=false wait-on-eos=false "
            "rtpopusdepay name=audioin ! opusdec ! audioconvert ! audioresample ! "
            "audio/x-raw,format=F32LE,rate=48000,channels=1 ! volume name=speakergate ! tee name=a "
            "a. ! queue ! appsink name=audioframes max-buffers=8 drop=true sync=false async=false wait-on-eos=false "
            "a. ! queue ! audioconvert name=outputentry";
        GError *error = nullptr;
        pipeline_ = gst_parse_launch(description, &error);
        if (error || !pipeline_)
        {
            const auto detail = QString::fromUtf8(error ? error->message : "无法创建会议管线");
            if (error) g_error_free(error);
            return fail(detail);
        }
        rtc_ = gst_bin_get_by_name(GST_BIN(pipeline_), "rtc");
        local_ = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline_), "local"));
        remote_ = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline_), "remote"));
        audio_ = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline_), "audioframes"));
        bus_ = gst_element_get_bus(pipeline_);
        QString deviceError;
        nativeMode_ = mode;
        auto *camera = shared ? gst_element_factory_make("appsrc", "sharedvideo")
            : device(devices.camera, "Video/Source", "videotestsrc", deviceError, &nativeMode_);
        if (camera && nativeMode_.size.isEmpty())
        {
            gst_object_unref(camera);
            camera = nullptr;
            deviceError = QStringLiteral("摄像头未公布可用的采集模式");
        }
        auto *microphone = shared ? gst_element_factory_make("appsrc", "sharedaudio")
            : device(devices.microphone, "Audio/Source", "audiotestsrc", deviceError);
        auto *speaker = shared ? gst_element_factory_make("appsink", "sharedspeaker")
            : device(devices.speaker, "Audio/Sink", "fakesink", deviceError);
        if (!camera || !microphone || !speaker)
        {
            if (camera) gst_object_unref(camera);
            if (microphone) gst_object_unref(microphone);
            if (speaker) gst_object_unref(speaker);
            return fail(deviceError);
        }
        if (shared)
        {
            for (auto *source : {camera, microphone})
                g_object_set(source, "is-live", TRUE, "format", GST_FORMAT_TIME, "do-timestamp", TRUE,
                    "max-buffers", guint64(8), "leaky-type", 2, nullptr);
            g_object_set(speaker, "max-buffers", guint(8), "drop", TRUE, "sync", FALSE, "wait-on-eos", FALSE, nullptr);
        }
        quality_ = std::make_unique<ShareQuality>(nativeMode_.size,
            qRound(double(nativeMode_.fpsNumerator) / nativeMode_.fpsDenominator), 15);
        applyCameraQuality();
        auto *encoder = gst_bin_get_by_name(GST_BIN(pipeline_), "cameraencoder");
        g_object_set(encoder, "threads", qBound(1, QThread::idealThreadCount() - 1, shared ? 2 : 8), nullptr);
        auto *encoded = gst_element_get_static_pad(encoder, "src");
        encodedFrames_ = 0;
        gst_pad_add_probe(encoded, GST_PAD_PROBE_TYPE_BUFFER,
            [](GstPad *, GstPadProbeInfo *, gpointer data) {
                ++*static_cast<std::atomic<quint64> *>(data);
                return GST_PAD_PROBE_OK;
            }, &encodedFrames_, nullptr);
        gst_object_unref(encoded);
        gst_object_unref(encoder);
        performance_.invalidate();
        // The output must not wait for remote preroll before local live capture can start.
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(speaker), "async"))
            g_object_set(speaker, "async", FALSE, nullptr);
        gst_bin_add_many(GST_BIN(pipeline_), camera, microphone, speaker, nullptr);
        auto *videoEntry = gst_bin_get_by_name(GST_BIN(pipeline_), "videoentry");
        auto *audioEntry = gst_bin_get_by_name(GST_BIN(pipeline_), "audioentry");
        auto *outputEntry = gst_bin_get_by_name(GST_BIN(pipeline_), "outputentry");
        // Decode the highest native camera mode before adapting the encoder output.
        auto *decode = shared ? nullptr : gst_element_factory_make("decodebin", nullptr);
        auto *captureMode = shared ? nullptr : gst_element_factory_make("capsfilter", nullptr);
        bool linked = decode && captureMode;
        if (linked)
        {
            auto *caps = gst_caps_from_string(nativeMode_.caps.toUtf8().constData());
            g_object_set(captureMode, "caps", caps, nullptr);
            gst_caps_unref(caps);
            gst_bin_add_many(GST_BIN(pipeline_), decode, captureMode, nullptr);
        }
        else
        {
            if (decode) gst_object_unref(decode);
            if (captureMode) gst_object_unref(captureMode);
        }
        if (linked)
        {
            auto *pad = gst_element_get_static_pad(videoEntry, "sink");
            g_signal_connect_data(decode, "pad-added", G_CALLBACK(decodedCamera), pad,
                [](gpointer value, GClosure *) { gst_object_unref(value); }, GConnectFlags(0));
            linked = gst_element_link_many(camera, captureMode, decode, nullptr);
        }
        if (shared) linked = gst_element_link(camera, videoEntry);
        linked = linked && gst_element_link(microphone, audioEntry) && gst_element_link(outputEntry, speaker);
        gst_object_unref(videoEntry);
        gst_object_unref(audioEntry);
        gst_object_unref(outputEntry);
        if (!linked) return fail(QStringLiteral("会议设备格式无法连接"));
        auto *videoIn = gst_bin_get_by_name(GST_BIN(pipeline_), "videoin");
        auto *audioIn = gst_bin_get_by_name(GST_BIN(pipeline_), "audioin");
        auto *pads = new Incoming{gst_element_get_static_pad(videoIn, "sink"),
            gst_element_get_static_pad(audioIn, "sink")};
        gst_object_unref(videoIn);
        gst_object_unref(audioIn);
        g_signal_connect_data(rtc_, "pad-added", G_CALLBACK(incoming), pads,
            [](gpointer value, GClosure *) {
                auto *p = static_cast<Incoming *>(value);
                gst_object_unref(p->video); gst_object_unref(p->audio); delete p;
            }, GConnectFlags(0));
        g_signal_connect_data(rtc_, "on-ice-candidate", G_CALLBACK(candidate),
            callback(), destroySignal, GConnectFlags(0));
        g_signal_connect_data(rtc_, "on-negotiation-needed", G_CALLBACK(negotiation),
            callback(), destroySignal, GConnectFlags(0));
        // Apply join defaults before any capture or output can begin.
        setMuted(microphoneMuted, cameraHidden, speakerMuted, epoch);
        started_.restart();
        if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
            return fail(QStringLiteral("会议设备启动失败，请检查权限或设备占用"));
        if (!timer_)
        {
            timer_ = new QTimer(this);
            timer_->setTimerType(Qt::PreciseTimer);
            connect(timer_, &QTimer::timeout, this, &ConferenceWorker::poll);
        }
        timer_->start(qBound(1, int(500.0 / quality_->profile().fps), 10));
#else
        Q_UNUSED(offerer)
        Q_UNUSED(devices)
        Q_UNUSED(microphoneMuted) Q_UNUSED(cameraHidden) Q_UNUSED(speakerMuted) Q_UNUSED(shared) Q_UNUSED(mode)
        emit statusChanged(epoch, "failed", QStringLiteral("当前构建未启用 GStreamer"));
#endif
    }
    void stop(quint64 epoch)
    {
        shutdown();
        epoch_ = epoch;
        emit statusChanged(epoch, "stopped", QStringLiteral("会议已结束"));
    }
    void negotiate(const QString &type, const QJsonObject &payload, quint64 epoch)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        if (!rtc_ || epoch != epoch_) return;
        if (type == "ice")
        {
            if (!remoteReady_)
            {
                if (pendingIce_.size() >= 256) return fail(QStringLiteral("会议 ICE 消息过多"));
                pendingIce_.append(payload);
            }
            else
                g_signal_emit_by_name(rtc_, "add-ice-candidate", guint(payload.value("sdpMLineIndex").toInt()),
                    payload.value("candidate").toString().toUtf8().constData());
            return;
        }
        if ((offerer_ && type != "answer") || (!offerer_ && type != "offer") || descriptionPending_ || remoteReady_)
            return;
        auto *sdp = parse(type, payload.value("sdp").toString());
        if (!sdp) return fail(QStringLiteral("会议 SDP 无效"));
        descriptionPending_ = true;
        auto *promise = gst_promise_new_with_change_func(remoteSet, callback(type), destroyPromise);
        g_signal_emit_by_name(rtc_, "set-remote-description", sdp, promise);
        gst_webrtc_session_description_free(sdp);
#else
        Q_UNUSED(type) Q_UNUSED(payload) Q_UNUSED(epoch)
#endif
    }
    void setMuted(bool microphone, bool camera, bool speaker, quint64 epoch)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        if (!pipeline_ || epoch != epoch_) return;
        auto *mic = gst_bin_get_by_name(GST_BIN(pipeline_), "microphonegate");
        auto *video = gst_bin_get_by_name(GST_BIN(pipeline_), "cameragate");
        auto *output = gst_bin_get_by_name(GST_BIN(pipeline_), "speakergate");
        g_object_set(mic, "mute", gboolean(microphone), nullptr);
        g_object_set(video, "brightness", camera ? -1.0 : 0.0, "saturation", camera ? 0.0 : 1.0, nullptr);
        g_object_set(output, "mute", gboolean(speaker), nullptr);
        gst_object_unref(mic); gst_object_unref(video); gst_object_unref(output);
#else
        Q_UNUSED(microphone) Q_UNUSED(camera) Q_UNUSED(speaker) Q_UNUSED(epoch)
#endif
    }
    void shutdown()
    {
#ifdef ZJDESK_HAS_GSTREAMER
        { QMutexLocker lock(&guard_->mutex); guard_->epoch = 0; }
        if (timer_) timer_->stop();
        if (pipeline_) { gst_element_set_state(pipeline_, GST_STATE_NULL); gst_object_unref(pipeline_); }
        pipeline_ = nullptr;
        for (auto *object : {GST_OBJECT(rtc_), GST_OBJECT(local_), GST_OBJECT(remote_), GST_OBJECT(audio_), GST_OBJECT(bus_)})
            if (object) gst_object_unref(object);
        rtc_ = nullptr; local_ = remote_ = audio_ = nullptr; bus_ = nullptr;
        quality_.reset();
        remoteFrameRate_ = 30;
        pendingIce_.clear();
        descriptionPending_ = false;
#endif
    }
signals:
    void negotiationReady(quint64 epoch, const QString &type, const QJsonObject &payload);
    void statusChanged(quint64 epoch, const QString &status, const QString &detail);
    void receivedAudio(quint64 epoch, quint64 buffers, double peak);
    void framesAvailable(quint64 epoch);
private:
    friend class MeetingWorker;
    ConferenceMailbox *mailbox_;
    quint64 epoch_ = 0;
#ifdef ZJDESK_HAS_GSTREAMER
    std::shared_ptr<ConferenceGuard> guard_ = std::make_shared<ConferenceGuard>();
    GstElement *pipeline_ = nullptr, *rtc_ = nullptr;
    GstAppSink *local_ = nullptr, *remote_ = nullptr, *audio_ = nullptr;
    GstBus *bus_ = nullptr;
    QTimer *timer_ = nullptr;
    QElapsedTimer started_;
    QElapsedTimer performance_;
    std::unique_ptr<ShareQuality> quality_;
    CameraMode nativeMode_;
    std::atomic<quint64> encodedFrames_{0};
    quint64 previousEncoded_ = 0, previousDelivered_ = 0;
    double remoteFrameRate_ = 30;
    bool offerer_ = false, offered_ = false, remoteReady_ = false, running_ = false, descriptionPending_ = false, sawRemote_ = false;
    QList<QJsonObject> pendingIce_;
    quint64 audioBuffers_ = 0;
    struct Incoming { GstPad *video, *audio; };
    ConferenceCallback *callback(const QString &type = {}) { return new ConferenceCallback{guard_, epoch_, type}; }
    static void destroySignal(gpointer p, GClosure *) { delete static_cast<ConferenceCallback *>(p); }
    static void destroyPromise(gpointer p) { delete static_cast<ConferenceCallback *>(p); }
    template<class F> static void queue(const ConferenceCallback &c, F function)
    {
        QMutexLocker lock(&c.guard->mutex);
        if (!c.guard->worker || c.guard->epoch != c.epoch) return;
        QMetaObject::invokeMethod(c.guard->worker, [c, function] {
            ConferenceWorker *worker;
            { QMutexLocker lock(&c.guard->mutex);
              if (!c.guard->worker || c.guard->epoch != c.epoch) return;
              worker = c.guard->worker; }
            function(worker);
        }, Qt::QueuedConnection);
    }
    static GstElement *device(const QString &name, const char *kind, const char *test, QString &error,
        CameraMode *mode = nullptr)
    {
        if (name == "test")
        {
            auto *element = gst_element_factory_make(test, nullptr);
            if (element && QByteArray(test) != "fakesink") g_object_set(element, "is-live", TRUE, nullptr);
            if (element && QByteArray(test) == "fakesink") g_object_set(element, "async", FALSE, nullptr);
            if (!element) error = QStringLiteral("会议测试插件不可用");
            if (mode) *mode = maximumCameraMode("video/x-raw,width=640,height=480,framerate=30/1");
            return element;
        }
        auto *monitor = gst_device_monitor_new();
        gst_device_monitor_add_filter(monitor, kind, nullptr);
        gst_device_monitor_start(monitor);
        auto *devices = gst_device_monitor_get_devices(monitor);
        GstElement *result = nullptr;
        for (auto *item = devices; item && !result; item = item->next)
        {
            auto *d = GST_DEVICE(item->data);
            auto *display = gst_device_get_display_name(d);
            const bool matches = name.isEmpty() || name == QString::fromUtf8(display);
            g_free(display);
            if (!matches) continue;
            auto *element = gst_device_create_element(d, nullptr);
            if (!element) continue;
            auto *factory = gst_element_get_factory(element);
            if (factory && QByteArray(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory))) == "ksvideosrc")
                gst_object_unref(element);
            else
            {
                result = element;
                if (mode)
                {
                    auto *caps = gst_device_get_caps(d);
                    auto *text = gst_caps_to_string(caps);
                    *mode = maximumCameraMode(QString::fromUtf8(text));
                    g_free(text);
                    gst_caps_unref(caps);
                }
            }
        }
        g_list_free_full(devices, gst_object_unref);
        gst_device_monitor_stop(monitor); gst_object_unref(monitor);
        if (!result) error = QStringLiteral("会议设备不可用：%1 %2").arg(QString::fromLatin1(kind), name);
        return result;
    }
    static void decodedCamera(GstElement *, GstPad *pad, gpointer data)
    {
        auto *sink = GST_PAD(data);
        if (!gst_pad_is_linked(sink)) gst_pad_link(pad, sink);
    }
    static void incoming(GstElement *, GstPad *pad, gpointer data)
    {
        if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) return;
        auto *caps = gst_pad_get_current_caps(pad);
        if (!caps) caps = gst_pad_query_caps(pad, nullptr);
        const char *media = caps && gst_caps_get_size(caps)
            ? gst_structure_get_string(gst_caps_get_structure(caps, 0), "media") : nullptr;
        auto *p = static_cast<Incoming *>(data);
        auto *sink = g_strcmp0(media, "video") == 0 ? p->video : g_strcmp0(media, "audio") == 0 ? p->audio : nullptr;
        if (sink && !gst_pad_is_linked(sink)) gst_pad_link(pad, sink);
        if (caps) gst_caps_unref(caps);
    }
    static void candidate(GstElement *, guint line, gchar *value, gpointer data)
    {
        // A null/empty candidate marks end-of-gathering, not a relayable address.
        if (!value || !*value) return;
        const QJsonObject payload{{"sdpMLineIndex", int(line)}, {"candidate", QString::fromUtf8(value)}};
        queue(*static_cast<ConferenceCallback *>(data), [payload](auto *w) { emit w->negotiationReady(w->epoch_, "ice", payload); });
    }
    static void negotiation(GstElement *, gpointer data)
    {
        queue(*static_cast<ConferenceCallback *>(data), [](auto *w) {
            if (w->offerer_ && !w->offered_) { w->offered_ = true; w->create("offer"); }
        });
    }
    static GstWebRTCSessionDescription *parse(const QString &type, const QString &text)
    {
        GstSDPMessage *message = nullptr;
        gst_sdp_message_new(&message);
        const auto bytes = text.toUtf8();
        if (bytes.isEmpty() || gst_sdp_message_parse_buffer(reinterpret_cast<const guint8 *>(bytes.constData()),
                guint(bytes.size()), message) != GST_SDP_OK || gst_sdp_message_medias_len(message) != 2)
        { gst_sdp_message_free(message); return nullptr; }
        return gst_webrtc_session_description_new(type == "offer" ? GST_WEBRTC_SDP_TYPE_OFFER : GST_WEBRTC_SDP_TYPE_ANSWER, message);
    }
    void create(const QString &type)
    {
        auto *promise = gst_promise_new_with_change_func(created, callback(type), destroyPromise);
        g_signal_emit_by_name(rtc_, type == "offer" ? "create-offer" : "create-answer", nullptr, promise);
    }
    static void created(GstPromise *promise, gpointer data)
    {
        const auto c = *static_cast<ConferenceCallback *>(data);
        GstWebRTCSessionDescription *sdp = nullptr;
        const auto *reply = gst_promise_wait(promise) == GST_PROMISE_RESULT_REPLIED ? gst_promise_get_reply(promise) : nullptr;
        if (reply) gst_structure_get(reply, c.type.toUtf8().constData(), GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &sdp, nullptr);
        QString text;
        if (sdp) { auto *s = gst_sdp_message_as_text(sdp->sdp); text = QString::fromUtf8(s); g_free(s); gst_webrtc_session_description_free(sdp); }
        gst_promise_unref(promise);
        queue(c, [text, type = c.type](auto *w) {
            auto *description = parse(type, text);
            if (!description) return w->fail(QStringLiteral("会议 SDP 生成失败"));
            g_signal_emit_by_name(w->rtc_, "set-local-description", description, nullptr);
            gst_webrtc_session_description_free(description);
            emit w->negotiationReady(w->epoch_, type, {{"sdp", text}});
        });
    }
    static void remoteSet(GstPromise *promise, gpointer data)
    {
        const auto c = *static_cast<ConferenceCallback *>(data);
        const bool replied = gst_promise_wait(promise) == GST_PROMISE_RESULT_REPLIED;
        const auto *reply = replied ? gst_promise_get_reply(promise) : nullptr;
        const bool failed = !replied || (reply && gst_structure_has_field(reply, "error"));
        gst_promise_unref(promise);
        queue(c, [failed, type = c.type](auto *w) {
            if (failed) return w->fail(QStringLiteral("会议 SDP 应用失败"));
            w->remoteReady_ = true;
            const auto pending = w->pendingIce_; w->pendingIce_.clear();
            for (const auto &ice : pending) w->negotiate("ice", ice, w->epoch_);
            if (type == "offer") w->create("answer");
        });
    }
    void fail(const QString &detail) { shutdown(); emit statusChanged(epoch_, "failed", detail); }
    void applyCameraQuality()
    {
        const auto profile = quality_->profile();
        auto *filter = gst_bin_get_by_name(GST_BIN(pipeline_), "cameraprofile");
        auto *caps = gst_caps_from_string(cameraOutputCaps(nativeMode_, profile, "I420").toUtf8().constData());
        g_object_set(filter, "caps", caps, nullptr);
        gst_caps_unref(caps);
        gst_object_unref(filter);
        auto *encoder = gst_bin_get_by_name(GST_BIN(pipeline_), "cameraencoder");
        g_object_set(encoder, "target-bitrate", profile.bitrate(), nullptr);
        gst_object_unref(encoder);
        if (running_)
            emit statusChanged(epoch_, "running", cameraDetail());
    }
    QString cameraDetail() const
    {
        const auto profile = quality_->profile();
        return QStringLiteral("双向视频与语音已连接 · 本机摄像头（清晰度优先·自动）%1 × %2 · 目标 %3 fps")
            .arg(profile.size.width()).arg(profile.size.height())
            .arg(qMin(double(profile.fps), double(nativeMode_.fpsNumerator) / nativeMode_.fpsDenominator), 0, 'f', 2);
    }
    void measureCameraPerformance()
    {
        if (!running_) return;
        quint64 delivered;
        { QMutexLocker lock(&mailbox_->mutex); delivered = mailbox_->localDelivered; }
        const quint64 encoded = encodedFrames_.load();
        if (!performance_.isValid())
        {
            performance_.start();
            previousEncoded_ = encoded; previousDelivered_ = delivered;
            return;
        }
        const auto elapsed = performance_.elapsed();
        if (elapsed < 1000) return;
        const double encodedFps = (encoded - previousEncoded_) * 1000.0 / elapsed;
        const double deliveredFps = (delivered - previousDelivered_) * 1000.0 / elapsed;
        previousEncoded_ = encoded; previousDelivered_ = delivered;
        performance_.restart();
        if (quality_->observe(encodedFps, deliveredFps))
        {
            applyCameraQuality();
            performance_.invalidate();
        }
    }
    static QImage pull(GstAppSink *sink, double *frameRate)
    {
        QImage image;
        if (auto *sample = gst_app_sink_try_pull_sample(sink, 0))
        {
            GstVideoInfo info;
            GstVideoFrame frame;
            if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample))
                && gst_video_frame_map(&frame, &info, gst_sample_get_buffer(sample), GST_MAP_READ))
            {
                image = QImage(static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0)),
                    info.width, info.height, GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0), QImage::Format_RGB888).copy();
                if (info.fps_d > 0 && info.fps_n > 0) *frameRate = double(info.fps_n) / info.fps_d;
                gst_video_frame_unmap(&frame);
            }
            gst_sample_unref(sample);
        }
        return image;
    }
    void poll()
    {
        if (!pipeline_) return;
        while (auto *message = gst_bus_pop(bus_))
        {
            QString detail;
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
            {
                GError *error = nullptr; gst_message_parse_error(message, &error, nullptr);
                detail = QString::fromUtf8(error->message); g_error_free(error);
            }
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) detail = QStringLiteral("会议媒体流结束");
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_LATENCY) gst_bin_recalculate_latency(GST_BIN(pipeline_));
            gst_message_unref(message);
            if (!detail.isEmpty()) return fail(detail);
        }
        double localFrameRate = quality_->profile().fps;
        auto local = pull(local_, &localFrameRate), remote = pull(remote_, &remoteFrameRate_);
        const double cadence = qMax(localFrameRate, remoteFrameRate_);
        timer_->setInterval(qBound(1, int(500 / qMax(1.0, cadence)), 10));
        if (!remote.isNull()) sawRemote_ = true;
        bool notify = false;
        { QMutexLocker lock(&mailbox_->mutex);
          if (mailbox_->epoch == epoch_)
          {
              if ((!local.isNull() || !remote.isNull()) && !mailbox_->framePending)
              { mailbox_->framePending = true; notify = true; }
              if (!local.isNull()) mailbox_->local = std::move(local);
              if (!remote.isNull()) mailbox_->remote = std::move(remote);
          } }
        if (notify) emit framesAvailable(epoch_);
        double peak = 0;
        bool audioReceived = false;
        while (auto *sample = gst_app_sink_try_pull_sample(audio_, 0))
        {
            ++audioBuffers_; audioReceived = true;
            GstMapInfo info;
            if (gst_buffer_map(gst_sample_get_buffer(sample), &info, GST_MAP_READ))
            {
                const auto *values = reinterpret_cast<const float *>(info.data);
                for (gsize i = 0; i < info.size / sizeof(float); ++i) peak = qMax(peak, double(std::abs(values[i])));
                gst_buffer_unmap(gst_sample_get_buffer(sample), &info);
            }
            gst_sample_unref(sample);
        }
        if (audioReceived) emit receivedAudio(epoch_, audioBuffers_, peak);
        GstWebRTCPeerConnectionState state;
        g_object_get(rtc_, "connection-state", &state, nullptr);
        if (state == GST_WEBRTC_PEER_CONNECTION_STATE_FAILED || state == GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED
            || state == GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED)
            return fail(QStringLiteral("会议媒体连接已断开"));
        if (!running_ && state == GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED && audioBuffers_ > 0 && sawRemote_)
        { running_ = true; emit statusChanged(epoch_, "running", cameraDetail()); }
        measureCameraPerformance();
        if (!running_ && started_.elapsed() > 45000) fail(QStringLiteral("会议媒体连接超时"));
    }
#endif
};

// All shared capture, mixing and peer pipelines run on this one media thread.
class MeetingWorker final : public QObject
{
public:
    MeetingWorker(MeetingMedia *owner, MeetingMailbox *mailbox) : owner_(owner), mailbox_(mailbox) {}
    ~MeetingWorker() override { shutdown(); }
    void start(const ConferenceDevices &devices, quint64 epoch, bool micMuted, bool cameraHidden, bool speakerMuted)
    {
        shutdown(); epoch_ = epoch; devices_ = devices;
#ifdef ZJDESK_HAS_GSTREAMER
        GError *error = nullptr;
        pipeline_ = gst_parse_launch(
            "videoconvert name=videoentry ! videoscale ! videorate ! "
            "capsfilter name=captureprofile ! videobalance name=cameragate ! "
            "appsink name=capturevideo max-buffers=1 drop=true sync=false async=false wait-on-eos=false "
            "audioconvert name=audioentry ! audioresample ! audio/x-raw,format=F32LE,rate=48000,channels=1,layout=interleaved ! "
            "volume name=microphonegate ! appsink name=captureaudio max-buffers=8 drop=true sync=false async=false wait-on-eos=false "
            "audiomixer name=mix force-live=true ignore-inactive-pads=true latency=40000000 ! "
            "volume name=speakergate ! tee name=mixed "
            "mixed. ! queue ! appsink name=mixedframes max-buffers=8 drop=true sync=false async=false wait-on-eos=false "
            "mixed. ! queue ! audioconvert name=outputentry "
            "appsrc name=play0 is-live=true do-timestamp=true format=time max-buffers=8 leaky-type=downstream ! queue ! mix. "
            "appsrc name=play1 is-live=true do-timestamp=true format=time max-buffers=8 leaky-type=downstream ! queue ! mix. "
            "appsrc name=play2 is-live=true do-timestamp=true format=time max-buffers=8 leaky-type=downstream ! queue ! mix.", &error);
        if (error || !pipeline_)
        {
            const auto detail = QString::fromUtf8(error ? error->message : "无法创建多人会议管线");
            if (error) g_error_free(error);
            return fail(detail);
        }
        QString detail;
        auto *camera = ConferenceWorker::device(devices.camera, "Video/Source", "videotestsrc", detail, &nativeMode_);
        auto *mic = ConferenceWorker::device(devices.microphone, "Audio/Source", "audiotestsrc", detail);
        auto *speaker = ConferenceWorker::device(devices.speaker, "Audio/Sink", "fakesink", detail);
        if (!camera || !mic || !speaker || nativeMode_.size.isEmpty())
        {
            for (auto *element : {camera, mic, speaker}) if (element) gst_object_unref(element);
            return fail(detail);
        }
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(speaker), "async")) g_object_set(speaker, "async", FALSE, nullptr);
        auto *decode = gst_element_factory_make("decodebin", nullptr);
        auto *captureMode = gst_element_factory_make("capsfilter", nullptr);
        if (!decode || !captureMode)
        {
            if (decode) gst_object_unref(decode);
            if (captureMode) gst_object_unref(captureMode);
            gst_object_unref(camera); gst_object_unref(mic); gst_object_unref(speaker);
            return fail(QStringLiteral("缺少摄像头解码插件"));
        }
        auto *caps = gst_caps_from_string(nativeMode_.caps.toUtf8().constData());
        g_object_set(captureMode, "caps", caps, nullptr); gst_caps_unref(caps);
        auto *profile = gst_bin_get_by_name(GST_BIN(pipeline_), "captureprofile");
        caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "RGB",
            "width", G_TYPE_INT, nativeMode_.size.width(), "height", G_TYPE_INT, nativeMode_.size.height(),
            "framerate", GST_TYPE_FRACTION, nativeMode_.fpsNumerator, nativeMode_.fpsDenominator, nullptr);
        g_object_set(profile, "caps", caps, nullptr); gst_caps_unref(caps); gst_object_unref(profile);
        gst_bin_add_many(GST_BIN(pipeline_), camera, mic, speaker, decode, captureMode, nullptr);
        auto *videoEntry = gst_bin_get_by_name(GST_BIN(pipeline_), "videoentry");
        auto *audioEntry = gst_bin_get_by_name(GST_BIN(pipeline_), "audioentry");
        auto *outputEntry = gst_bin_get_by_name(GST_BIN(pipeline_), "outputentry");
        auto *pad = gst_element_get_static_pad(videoEntry, "sink");
        g_signal_connect_data(decode, "pad-added", G_CALLBACK(ConferenceWorker::decodedCamera), pad,
            [](gpointer p, GClosure *) { gst_object_unref(p); }, GConnectFlags(0));
        const bool linked = gst_element_link_many(camera, captureMode, decode, nullptr)
            && gst_element_link(mic, audioEntry) && gst_element_link(outputEntry, speaker);
        gst_object_unref(videoEntry); gst_object_unref(audioEntry); gst_object_unref(outputEntry);
        if (!linked) return fail(QStringLiteral("多人会议设备格式无法连接"));
        setMuted(micMuted, cameraHidden, speakerMuted, epoch);
        video_ = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline_), "capturevideo"));
        audio_ = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline_), "captureaudio"));
        mixed_ = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline_), "mixedframes"));
        mixedBuffers_ = 0;
        bus_ = gst_element_get_bus(pipeline_);
        if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
            return fail(QStringLiteral("多人会议设备启动失败"));
        if (!timer_)
        {
            timer_ = new QTimer(this); timer_->setInterval(10);
            connect(timer_, &QTimer::timeout, this, [this] { poll(); });
        }
        timer_->start();
        emit owner_->statusChanged(epoch_, "ready", QStringLiteral("共享采集已启动，等待成员音视频"));
#else
        Q_UNUSED(micMuted) Q_UNUSED(cameraHidden) Q_UNUSED(speakerMuted)
        fail(QStringLiteral("当前构建未启用 GStreamer"));
#endif
    }
    void stop(quint64 epoch) { shutdown(); epoch_ = epoch; emit owner_->statusChanged(epoch, "stopped", {}); }
    void addPeer(const QString &id, bool offerer, quint64 epoch)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        if (!pipeline_ || epoch != epoch_ || peers_.contains(id) || peers_.size() >= 3) return;
        auto peer = std::make_shared<Peer>();
        peer->worker = new ConferenceWorker(&peer->mailbox);
        peer->mailbox.epoch = epoch;
        for (int slot = 0; slot < 3; ++slot)
        {
            bool used = false;
            for (const auto &other : peers_) if (other->slot == slot) used = true;
            if (!used) { peer->slot = slot; break; }
        }
        peer->output = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(pipeline_), qPrintable(QString("play%1").arg(peer->slot))));
        peers_.insert(id, peer);
        connect(peer->worker, &ConferenceWorker::negotiationReady, this,
            [this, id](quint64 e, const QString &type, const QJsonObject &payload) { emit owner_->negotiationReady(e, id, type, payload); });
        connect(peer->worker, &ConferenceWorker::statusChanged, this,
            [this, id](quint64 e, const QString &status, const QString &) {
                if (e == epoch_ && peers_.contains(id)) emit owner_->peerStatusChanged(e, id, status);
            });
        connect(peer->worker, &ConferenceWorker::receivedAudio, this,
            [this, id](quint64 e, quint64 count, double peak) { emit owner_->receivedAudio(e, id, count, peak); });
        peer->worker->start(offerer, devices_, epoch, false, false, false, true, nativeMode_);
#else
        Q_UNUSED(id) Q_UNUSED(offerer) Q_UNUSED(epoch)
#endif
    }
    void removePeer(const QString &id, quint64 epoch)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        if (epoch != epoch_) return;
        const auto peer = peers_.take(id);
        if (peer) { delete peer->worker; gst_object_unref(peer->output); }
        QMutexLocker lock(&mailbox_->mutex); mailbox_->remote.remove(id);
#else
        Q_UNUSED(id) Q_UNUSED(epoch)
#endif
    }
    void negotiate(const QString &id, const QString &type, const QJsonObject &payload, quint64 epoch)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        if (epoch == epoch_ && peers_.contains(id)) peers_[id]->worker->negotiate(type, payload, epoch);
#else
        Q_UNUSED(id) Q_UNUSED(type) Q_UNUSED(payload) Q_UNUSED(epoch)
#endif
    }
    void setMuted(bool mic, bool camera, bool speaker, quint64 epoch)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        if (!pipeline_ || epoch != epoch_) return;
        auto *m = gst_bin_get_by_name(GST_BIN(pipeline_), "microphonegate");
        auto *v = gst_bin_get_by_name(GST_BIN(pipeline_), "cameragate");
        auto *s = gst_bin_get_by_name(GST_BIN(pipeline_), "speakergate");
        g_object_set(m, "mute", gboolean(mic), nullptr);
        g_object_set(v, "brightness", camera ? -1.0 : 0.0, "saturation", camera ? 0.0 : 1.0, nullptr);
        g_object_set(s, "mute", gboolean(speaker), nullptr);
        gst_object_unref(m); gst_object_unref(v); gst_object_unref(s);
#else
        Q_UNUSED(mic) Q_UNUSED(camera) Q_UNUSED(speaker) Q_UNUSED(epoch)
#endif
    }
    void shutdown()
    {
#ifdef ZJDESK_HAS_GSTREAMER
        if (timer_) timer_->stop();
        for (const auto &peer : peers_) { delete peer->worker; gst_object_unref(peer->output); }
        peers_.clear();
        if (pipeline_) { gst_element_set_state(pipeline_, GST_STATE_NULL); gst_object_unref(pipeline_); }
        for (auto *object : {GST_OBJECT(video_), GST_OBJECT(audio_), GST_OBJECT(mixed_), GST_OBJECT(bus_)}) if (object) gst_object_unref(object);
        pipeline_ = nullptr; video_ = audio_ = mixed_ = nullptr; bus_ = nullptr;
#endif
    }
private:
    MeetingMedia *owner_;
    MeetingMailbox *mailbox_;
    ConferenceDevices devices_;
    quint64 epoch_ = 0;
    void fail(const QString &detail) { shutdown(); emit owner_->statusChanged(epoch_, "failed", detail); }
#ifdef ZJDESK_HAS_GSTREAMER
    struct Peer { ConferenceMailbox mailbox; ConferenceWorker *worker = nullptr; GstAppSrc *output = nullptr; int slot = 0; };
    QHash<QString, std::shared_ptr<Peer>> peers_;
    CameraMode nativeMode_;
    GstElement *pipeline_ = nullptr;
    GstAppSink *video_ = nullptr, *audio_ = nullptr, *mixed_ = nullptr;
    quint64 mixedBuffers_ = 0;
    GstBus *bus_ = nullptr;
    QTimer *timer_ = nullptr;
    static void push(GstAppSrc *source, GstSample *sample)
    {
        gst_app_src_set_caps(source, gst_sample_get_caps(sample));
        auto *buffer = gst_buffer_copy(gst_sample_get_buffer(sample));
        // Each pipeline has its own clock base. Timestamp on entry to that pipeline.
        GST_BUFFER_PTS(buffer) = GST_CLOCK_TIME_NONE; GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
        gst_app_src_push_buffer(source, buffer);
    }
    void distribute(GstAppSink *sink, const char *name, bool video)
    {
        while (auto *sample = gst_app_sink_try_pull_sample(sink, 0))
        {
            for (const auto &peer : peers_)
            {
                if (!peer->worker->pipeline_) continue;
                auto *source = gst_bin_get_by_name(GST_BIN(peer->worker->pipeline_), name);
                push(GST_APP_SRC(source), sample); gst_object_unref(source);
            }
            if (video)
            {
                GstVideoInfo info; GstVideoFrame frame;
                if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample))
                    && gst_video_frame_map(&frame, &info, gst_sample_get_buffer(sample), GST_MAP_READ))
                {
                    auto image = QImage(static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0)), info.width,
                        info.height, GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0), QImage::Format_RGB888).copy();
                    gst_video_frame_unmap(&frame);
                    QMutexLocker lock(&mailbox_->mutex);
                    if (mailbox_->epoch == epoch_) mailbox_->local = std::move(image);
                }
            }
            gst_sample_unref(sample);
        }
    }
    void poll()
    {
        if (!pipeline_) return;
        while (auto *message = gst_bus_pop(bus_))
        {
            QString detail;
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
            {
                GError *error = nullptr; gst_message_parse_error(message, &error, nullptr);
                detail = QString::fromUtf8(error->message); g_error_free(error);
            }
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) detail = QStringLiteral("会议设备已停止");
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_LATENCY) gst_bin_recalculate_latency(GST_BIN(pipeline_));
            gst_message_unref(message);
            if (!detail.isEmpty()) return fail(detail);
        }
        distribute(video_, "sharedvideo", true); distribute(audio_, "sharedaudio", false);
        double peak = 0;
        bool received = false;
        while (auto *sample = gst_app_sink_try_pull_sample(mixed_, 0))
        {
            received = true; ++mixedBuffers_;
            GstMapInfo info;
            if (gst_buffer_map(gst_sample_get_buffer(sample), &info, GST_MAP_READ))
            {
                const auto *values = reinterpret_cast<const float *>(info.data);
                for (gsize i = 0; i < info.size / sizeof(float); ++i) peak = qMax(peak, double(std::abs(values[i])));
                gst_buffer_unmap(gst_sample_get_buffer(sample), &info);
            }
            gst_sample_unref(sample);
        }
        if (received) emit owner_->mixedAudio(epoch_, mixedBuffers_, peak);
        quint64 delivered;
        { QMutexLocker lock(&mailbox_->mutex); delivered = mailbox_->localDelivered; }
        for (auto it = peers_.cbegin(); it != peers_.cend(); ++it)
        {
            const auto &peer = it.value();
            if (peer->worker->pipeline_)
            {
                auto *sink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(peer->worker->pipeline_), "sharedspeaker"));
                while (auto *sample = gst_app_sink_try_pull_sample(sink, 0)) { push(peer->output, sample); gst_sample_unref(sample); }
                gst_object_unref(sink);
            }
            QMutexLocker peerLock(&peer->mailbox.mutex);
            if (!peer->mailbox.remote.isNull())
            {
                QMutexLocker lock(&mailbox_->mutex);
                if (mailbox_->epoch == epoch_) mailbox_->remote.insert(it.key(), std::move(peer->mailbox.remote));
                peer->mailbox.remote = {};
            }
            peer->mailbox.local = {};
            peer->mailbox.localDelivered = delivered;
            peer->mailbox.framePending = false;
        }
    }
#endif
};

MeetingMedia::MeetingMedia(QObject *parent) : QObject(parent), worker_(new MeetingWorker(this, &mailbox_))
{
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    thread_.start(); frames_.setInterval(10);
    connect(&frames_, &QTimer::timeout, this, [this] {
        QImage local; QHash<QString, QImage> remote; quint64 epoch;
        { QMutexLocker lock(&mailbox_.mutex); epoch = mailbox_.epoch; local.swap(mailbox_.local); remote.swap(mailbox_.remote);
          if (!local.isNull()) ++mailbox_.localDelivered; }
        if (!local.isNull()) emit localFrame(epoch, local);
        for (auto it = remote.cbegin(); it != remote.cend(); ++it) emit remoteFrame(epoch, it.key(), it.value());
    });
}
MeetingMedia::~MeetingMedia()
{
    frames_.stop(); QMetaObject::invokeMethod(worker_, [this] { worker_->shutdown(); }, Qt::BlockingQueuedConnection);
    thread_.quit(); thread_.wait();
}
void MeetingMedia::start(const ConferenceDevices &devices, quint64 epoch, bool micMuted, bool cameraHidden, bool speakerMuted)
{
    { QMutexLocker lock(&mailbox_.mutex); mailbox_.epoch = epoch; mailbox_.local = {}; mailbox_.remote.clear(); mailbox_.localDelivered = 0; }
    frames_.start(); QMetaObject::invokeMethod(worker_, [=] { worker_->start(devices, epoch, micMuted, cameraHidden, speakerMuted); }, Qt::QueuedConnection);
}
void MeetingMedia::stop(quint64 epoch)
{
    frames_.stop();
    { QMutexLocker lock(&mailbox_.mutex); mailbox_.epoch = epoch; mailbox_.local = {}; mailbox_.remote.clear(); }
    QMetaObject::invokeMethod(worker_, [=] { worker_->stop(epoch); }, Qt::QueuedConnection);
}
void MeetingMedia::addPeer(const QString &id, bool offerer, quint64 epoch)
{ QMetaObject::invokeMethod(worker_, [=] { worker_->addPeer(id, offerer, epoch); }, Qt::QueuedConnection); }
void MeetingMedia::removePeer(const QString &id, quint64 epoch)
{ QMetaObject::invokeMethod(worker_, [=] { worker_->removePeer(id, epoch); }, Qt::QueuedConnection); }
void MeetingMedia::negotiate(const QString &id, const QString &type, const QJsonObject &payload, quint64 epoch)
{ QMetaObject::invokeMethod(worker_, [=] { worker_->negotiate(id, type, payload, epoch); }, Qt::QueuedConnection); }
void MeetingMedia::setMuted(bool mic, bool camera, bool speaker, quint64 epoch)
{ QMetaObject::invokeMethod(worker_, [=] { worker_->setMuted(mic, camera, speaker, epoch); }, Qt::QueuedConnection); }

ConferenceMedia::ConferenceMedia(QObject *parent) : QObject(parent), worker_(new ConferenceWorker(&mailbox_))
{
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &ConferenceWorker::negotiationReady, this, &ConferenceMedia::negotiationReady);
    connect(worker_, &ConferenceWorker::statusChanged, this, &ConferenceMedia::statusChanged);
    connect(worker_, &ConferenceWorker::receivedAudio, this, &ConferenceMedia::receivedAudio);
    thread_.start();
    connect(worker_, &ConferenceWorker::framesAvailable, this, [this](quint64 epoch) {
        QImage local, remote;
        { QMutexLocker lock(&mailbox_.mutex);
          if (mailbox_.epoch != epoch) return;
          local.swap(mailbox_.local); remote.swap(mailbox_.remote);
          mailbox_.framePending = false;
          if (!local.isNull()) ++mailbox_.localDelivered;
        }
        if (!local.isNull()) emit localFrame(epoch, local);
        if (!remote.isNull()) emit remoteFrame(epoch, remote);
    });
}
ConferenceMedia::~ConferenceMedia()
{
    QMetaObject::invokeMethod(worker_, [this] { worker_->shutdown(); }, Qt::BlockingQueuedConnection);
    thread_.quit(); thread_.wait();
}
void ConferenceMedia::start(bool offerer, const ConferenceDevices &devices, quint64 epoch,
    bool microphoneMuted, bool cameraHidden, bool speakerMuted)
{
    { QMutexLocker lock(&mailbox_.mutex); mailbox_.epoch = epoch; mailbox_.local = {}; mailbox_.remote = {};
      mailbox_.framePending = false; mailbox_.localDelivered = 0; }
    QMetaObject::invokeMethod(worker_, [=] {
        worker_->start(offerer, devices, epoch, microphoneMuted, cameraHidden, speakerMuted);
    }, Qt::QueuedConnection);
}
void ConferenceMedia::stop(quint64 epoch)
{
    { QMutexLocker lock(&mailbox_.mutex); mailbox_.epoch = epoch; mailbox_.local = {}; mailbox_.remote = {};
      mailbox_.framePending = false; }
    QMetaObject::invokeMethod(worker_, [=] { worker_->stop(epoch); }, Qt::QueuedConnection);
}
void ConferenceMedia::negotiate(const QString &type, const QJsonObject &payload, quint64 epoch)
{
    QMetaObject::invokeMethod(worker_, [=] { worker_->negotiate(type, payload, epoch); }, Qt::QueuedConnection);
}
void ConferenceMedia::setMuted(bool microphone, bool camera, bool speaker, quint64 epoch)
{
    QMetaObject::invokeMethod(worker_, [=] { worker_->setMuted(microphone, camera, speaker, epoch); }, Qt::QueuedConnection);
}
} // namespace zjdesk
#include "conference_media.moc"
