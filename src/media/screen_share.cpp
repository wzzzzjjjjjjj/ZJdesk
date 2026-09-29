#include "screen_share.h"
#include "share_quality.h"
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QGuiApplication>
#include <QScreen>
#include <QJsonDocument>
#include <atomic>
#include <functional>
#include <memory>
#ifdef ZJDESK_HAS_GSTREAMER
#define GST_USE_UNSTABLE_API
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <gst/webrtc/webrtc.h>
#endif

namespace zjdesk
{
class ScreenShareWorker;
#ifdef ZJDESK_HAS_GSTREAMER
struct ShareCallbacks
{
    QMutex mutex;
    ScreenShareWorker *worker = nullptr;
    quint64 epoch = 0;
};
struct ShareCallback
{
    std::shared_ptr<ShareCallbacks> guard;
    quint64 epoch;
    QString type;
};
#endif

class ScreenShareWorker final : public QObject
{
    Q_OBJECT
public:
    explicit ScreenShareWorker(ShareMailbox *mailbox) : mailbox_(mailbox)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        guard_->worker = this;
#endif
    }
    ~ScreenShareWorker() override
    {
        shutdown();
#ifdef ZJDESK_HAS_GSTREAMER
        QMutexLocker lock(&guard_->mutex);
        guard_->worker = nullptr;
#endif
    }

    void start(bool sender, const QString &source, quint64 epoch, int targetFps, const QSize &nativeSize, int maximumFps)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        release();
        epoch_ = epoch;
        {
            QMutexLocker lock(&guard_->mutex);
            guard_->epoch = epoch;
        }
        sender_ = sender;
        automatic_ = targetFps == 0;
        quality_ = ShareQuality(nativeSize, maximumFps);
        profile_ = quality_.profile();
        if (!automatic_)
            profile_.fps = targetFps;
        revision_ = 1;
        peerFps_ = -1;
        profileClock_.restart();
        feedbackClock_.invalidate();
        targetFps_ = profile_.fps;
        processedFrames_ = 0;
        previousProcessed_ = previousDelivered_ = 0;
        statisticsClock_.restart();
        networkClock_.invalidate();
        previousNetworkBytes_ = 0;
        networkMbps_ = jitterMs_ = webRtcRttMs_ = -1;
        packetsReceived_ = packetsLost_ = -1;
        statsPending_ = false;
        remoteReady_ = running_ = firstFrame_ = false;
        pendingIce_.clear();
        QString description;
        if (sender)
        {
            QString capture;
            if (source == "test")
                capture = "videotestsrc is-live=true pattern=ball ! "
                    "video/x-raw,width=640,height=480 ! ";
#ifdef Q_OS_WIN
            else
                capture = "d3d11screencapturesrc monitor-index=-1 show-cursor=true ! "
                    "d3d11download ! videoconvert ! ";
#else
            else if (qEnvironmentVariable("XDG_SESSION_TYPE") != "wayland")
                capture = "ximagesrc use-damage=false show-pointer=true ! videoconvert ! "
                    "videorate drop-only=true ! ";
            else
            {
                fail(QStringLiteral("屏幕共享暂只支持 Windows 和 Linux X11 桌面"));
                return;
            }
#endif
            description = capture + QString("videoscale add-borders=false ! "
                "capsfilter name=quality caps=\"video/x-raw,width=%1,height=%2,framerate=%3/1,pixel-aspect-ratio=1/1\" ! ")
                .arg(profile_.size.width()).arg(profile_.size.height()).arg(targetFps_) + "tee name=split "
                "split. ! queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream ! "
                "videoconvert ! video/x-raw,format=RGB ! "
                "appsink name=frames max-buffers=1 drop=true sync=false wait-on-eos=false "
                "split. ! queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream ! "
                "videoconvert ! video/x-raw,format=Y444,colorimetry=1:3:5:1 ! "
                "vp9enc name=encoder deadline=1 cpu-used=8 threads=4 row-mt=true tile-columns=2 lag-in-frames=0 "
                "min-quantizer=0 max-quantizer=0 "
                "target-bitrate=8000000 keyframe-max-dist=60 ! "
                "rtpvp9pay pt=96 picture-id-mode=15-bit ! "
                "application/x-rtp,media=video,encoding-name=VP9,payload=96 ! "
                "webrtcbin name=rtc bundle-policy=max-bundle";
        }
        else
        {
            bool configured = false;
            const int requested = qEnvironmentVariableIntValue("ZJDESK_WEBRTC_LATENCY_MS", &configured);
            const int latency = configured && (requested == 30 || requested == 50
                || requested == 100 || requested == 150 || requested == 200) ? requested : 200;
            description = QString("webrtcbin name=rtc bundle-policy=max-bundle latency=%1 ").arg(latency) +
                "rtpvp9depay name=incoming request-keyframe=true wait-for-keyframe=true ! "
                "video/x-vp9,colorimetry=1:3:5:1 ! "
                "vp9dec name=decoder ! videoconvert ! video/x-raw,format=RGB ! "
                "appsink name=frames max-buffers=1 drop=true sync=false wait-on-eos=false";
        }
        GError *error = nullptr;
        pipeline_ = gst_parse_launch(description.toUtf8().constData(), &error);
        if (error || !pipeline_)
        {
            const auto detail = QString::fromUtf8(error ? error->message : "无法创建共享管线");
            if (error)
                g_error_free(error);
            fail(detail);
            return;
        }
        rtc_ = gst_bin_get_by_name(GST_BIN(pipeline_), "rtc");
        g_signal_connect(rtc_, "on-new-transceiver", G_CALLBACK(configureTransceiver), nullptr);
        GArray *transceivers = nullptr;
        g_signal_emit_by_name(rtc_, "get-transceivers", &transceivers);
        if (transceivers)
        {
            for (guint i = 0; i < transceivers->len; ++i)
                configureTransceiver(rtc_, g_array_index(transceivers, GstWebRTCRTPTransceiver *, i), nullptr);
            g_array_unref(transceivers);
        }
        sink_ = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline_), "frames"));
        bus_ = gst_element_get_bus(pipeline_);
        auto *codec = gst_bin_get_by_name(GST_BIN(pipeline_), sender ? "encoder" : "decoder");
        if (sender)
            g_object_set(codec, "threads", guint(qBound(1, QThread::idealThreadCount() / 2, 8)),
                "target-bitrate", profile_.bitrate(), nullptr);
        auto *codecPad = gst_element_get_static_pad(codec, "src");
        gst_pad_add_probe(codecPad, GST_PAD_PROBE_TYPE_BUFFER,
            [](GstPad *, GstPadProbeInfo *, gpointer data) {
                static_cast<std::atomic<quint64> *>(data)->fetch_add(1, std::memory_order_relaxed);
                return GST_PAD_PROBE_OK;
            }, &processedFrames_, nullptr);
        gst_object_unref(codecPad);
        gst_object_unref(codec);
        g_signal_connect_data(rtc_, "on-ice-candidate", G_CALLBACK(iceCandidate),
            new ShareCallback{guard_, epoch, {}}, destroySignal, GConnectFlags(0));
        g_signal_connect_data(rtc_, "prepare-data-channel", G_CALLBACK(prepareChannel),
            new ShareCallback{guard_, epoch, {}}, destroySignal, GConnectFlags(0));
        if (sender)
            g_signal_connect_data(rtc_, "on-negotiation-needed", G_CALLBACK(negotiationNeeded),
                new ShareCallback{guard_, epoch, {}}, destroySignal, GConnectFlags(0));
        else
        {
            auto *decoder = gst_bin_get_by_name(GST_BIN(pipeline_), "incoming");
            auto *pad = gst_element_get_static_pad(decoder, "sink");
            gst_object_unref(decoder);
            // Incoming RTP pads must be linked before the streaming callback returns.
            g_signal_connect_data(rtc_, "pad-added", G_CALLBACK(incomingPad), pad,
                [](gpointer data, GClosure *) { gst_object_unref(data); }, GConnectFlags(0));
        }
        if (sender)
        {
            gst_element_set_state(pipeline_, GST_STATE_READY);
            GstWebRTCDataChannel *channel = nullptr;
            g_signal_emit_by_name(rtc_, "create-data-channel", "zjdesk-control", nullptr, &channel);
            if (!channel)
                return fail(QStringLiteral("无法创建远控数据通道"));
            g_object_unref(channel);
        }
        startup_.restart();
        if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        {
            fail(QStringLiteral("无法启动屏幕共享管线"));
            return;
        }
        if (!timer_)
        {
            timer_ = new QTimer(this);
            connect(timer_, &QTimer::timeout, this, &ScreenShareWorker::poll);
        }
        timer_->setTimerType(Qt::PreciseTimer);
        timer_->start(4);
        if (sender_)
            announceProfile();
#else
        Q_UNUSED(sender)
        Q_UNUSED(source)
        Q_UNUSED(targetFps)
        Q_UNUSED(nativeSize)
        Q_UNUSED(maximumFps)
        emit statusChanged(epoch, "failed", QStringLiteral("当前构建未启用 GStreamer"));
#endif
    }

    void stop(quint64 epoch)
    {
        shutdown();
        emit statusChanged(epoch, "stopped", QStringLiteral("屏幕共享已停止"));
    }

    void negotiate(const QString &type, const QJsonObject &payload, quint64 epoch)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        if (!rtc_ || epoch != epoch_)
            return;
        if (type == "share")
        {
            const auto action = payload.value("action").toString();
            if (!sender_ && action == "profile")
            {
                revision_ = payload.value("revision").toInt();
                targetFps_ = payload.value("fps").toInt();
                profileClock_.restart();
            }
            else if (sender_ && action == "stats" && payload.value("revision").toInt() == revision_)
            {
                peerFps_ = payload.value("fps").toDouble();
                feedbackClock_.restart();
            }
            return;
        }
        if (type == "ice")
        {
            const auto candidate = payload.value("candidate").toString();
            const auto line = payload.value("sdpMLineIndex").toInt(-1);
            if (candidate.isEmpty() || line < 0 || line > 32)
                return;
            if (!remoteReady_)
            {
                if (pendingIce_.size() < 256)
                    pendingIce_.append(payload);
            }
            else
                g_signal_emit_by_name(rtc_, "add-ice-candidate", guint(line), candidate.toUtf8().constData());
            return;
        }
        if ((type != "offer" || sender_) && (type != "answer" || !sender_))
            return;
        auto *sdp = parseDescription(type, payload.value("sdp").toString());
        if (!sdp)
        {
            fail(QStringLiteral("收到无效的共享 SDP"));
            return;
        }
        auto *promise = gst_promise_new_with_change_func(remoteDescriptionSet,
            new ShareCallback{guard_, epoch_, type}, destroyPromise);
        g_signal_emit_by_name(rtc_, "set-remote-description", sdp, promise);
        gst_webrtc_session_description_free(sdp);
#else
        Q_UNUSED(type)
        Q_UNUSED(payload)
        Q_UNUSED(epoch)
#endif
    }

    void sendControl(const QJsonObject &message, quint64 epoch)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        if (epoch != epoch_ || !channel_)
            return;
        const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
        if (bytes.size() > 4096)
            return;
        GError *error = nullptr;
        if (!gst_webrtc_data_channel_send_string_full(channel_, bytes.constData(), &error))
            emit controlChannelChanged(epoch_, false);
        if (error)
            g_error_free(error);
#else
        Q_UNUSED(message)
        Q_UNUSED(epoch)
#endif
    }

    void shutdown()
    {
#ifdef ZJDESK_HAS_GSTREAMER
        if (timer_)
            timer_->stop();
        release();
#endif
    }

signals:
    void statusChanged(quint64 epoch, const QString &status, const QString &detail);
    void negotiationReady(quint64 epoch, const QString &type, const QJsonObject &payload);
    void statisticsReady(quint64 epoch, const zjdesk::ShareStats &stats);
    void controlChannelChanged(quint64 epoch, bool ready);
    void controlReceived(quint64 epoch, const QJsonObject &message);

private:
#ifdef ZJDESK_HAS_GSTREAMER
    static void queue(const ShareCallback &callback, std::function<void(ScreenShareWorker *)> action)
    {
        QMutexLocker lock(&callback.guard->mutex);
        auto *worker = callback.guard->worker;
        if (!worker || callback.epoch != callback.guard->epoch)
            return;
        QMetaObject::invokeMethod(worker, [callback, worker, action] {
            {
                QMutexLocker guardLock(&callback.guard->mutex);
                if (callback.epoch != callback.guard->epoch)
                    return;
            }
            action(worker);
        }, Qt::QueuedConnection);
    }
    static void destroySignal(gpointer data, GClosure *) { delete static_cast<ShareCallback *>(data); }
    static void destroyPromise(gpointer data) { delete static_cast<ShareCallback *>(data); }
    static bool controlChannel(GstWebRTCDataChannel *channel)
    {
        gchar *label = nullptr;
        g_object_get(channel, "label", &label, nullptr);
        const bool accepted = label && QByteArray(label) == "zjdesk-control";
        g_free(label);
        return accepted;
    }
    static void channelOpened(GstWebRTCDataChannel *channel, gpointer data)
    {
        if (!controlChannel(channel))
            return;
        auto owned = std::shared_ptr<GstWebRTCDataChannel>(
            GST_WEBRTC_DATA_CHANNEL(g_object_ref(channel)), [](auto *p) { g_object_unref(p); });
        queue(*static_cast<ShareCallback *>(data), [owned](auto *worker) {
            if (worker->channel_)
                g_object_unref(worker->channel_);
            worker->channel_ = GST_WEBRTC_DATA_CHANNEL(g_object_ref(owned.get()));
            emit worker->controlChannelChanged(worker->epoch_, true);
        });
    }
    static void channelClosed(GstWebRTCDataChannel *channel, gpointer data)
    {
        if (!controlChannel(channel))
            return;
        queue(*static_cast<ShareCallback *>(data), [](auto *worker) {
            emit worker->controlChannelChanged(worker->epoch_, false);
        });
    }
    static void channelMessage(GstWebRTCDataChannel *channel, gchar *text, gpointer data)
    {
        if (!text || !controlChannel(channel))
            return;
        const QByteArray bytes(text);
        if (bytes.size() > 4096)
            return;
        const auto message = QJsonDocument::fromJson(bytes);
        if (!message.isObject())
            return;
        queue(*static_cast<ShareCallback *>(data), [object = message.object()](auto *worker) {
            emit worker->controlReceived(worker->epoch_, object);
        });
    }
    static void prepareChannel(GstElement *, GstWebRTCDataChannel *channel, gboolean, gpointer data)
    {
        // Remote labels are populated by DCEP after prepare-data-channel.
        const auto callback = *static_cast<ShareCallback *>(data);
        for (const auto &signal : {std::pair<const char *, GCallback>{"on-open", G_CALLBACK(channelOpened)},
                                  {"on-close", G_CALLBACK(channelClosed)},
                                  {"on-message-string", G_CALLBACK(channelMessage)}})
            g_signal_connect_data(channel, signal.first, signal.second,
                new ShareCallback(callback), destroySignal, GConnectFlags(0));
    }
    void announceProfile()
    {
        emit negotiationReady(epoch_, "share", {{"action", "profile"}, {"revision", revision_},
            {"fps", profile_.fps}, {"width", profile_.size.width()}, {"height", profile_.size.height()}});
    }
    void adapt(double encodedFps)
    {
        const double peer = feedbackClock_.isValid() && feedbackClock_.elapsed() < 5000 ? peerFps_ : -1;
        const int settleMs = revision_ == 1 ? 10000 : 4000;
        if (!automatic_ || profileClock_.elapsed() < settleMs || !quality_.observe(encodedFps, peer))
            return;
        profile_ = quality_.profile();
        targetFps_ = profile_.fps;
        auto *filter = gst_bin_get_by_name(GST_BIN(pipeline_), "quality");
        auto *caps = gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, profile_.size.width(),
            "height", G_TYPE_INT, profile_.size.height(), "framerate", GST_TYPE_FRACTION, profile_.fps, 1,
            "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, nullptr);
        g_object_set(filter, "caps", caps, nullptr);
        gst_caps_unref(caps);
        gst_object_unref(filter);
        auto *encoder = gst_bin_get_by_name(GST_BIN(pipeline_), "encoder");
        g_object_set(encoder, "target-bitrate", profile_.bitrate(), nullptr);
        gst_object_unref(encoder);
        ++revision_;
        peerFps_ = -1;
        feedbackClock_.invalidate();
        profileClock_.restart();
        announceProfile();
    }
    static void configureTransceiver(GstElement *, GstWebRTCRTPTransceiver *transceiver, gpointer)
    {
        g_object_set(transceiver, "do-nack", TRUE, nullptr);
    }
    static void incomingPad(GstElement *, GstPad *pad, gpointer data)
    {
        auto *sink = GST_PAD(data);
        if (GST_PAD_DIRECTION(pad) == GST_PAD_SRC && !gst_pad_is_linked(sink))
            gst_pad_link(pad, sink);
    }
    static void negotiationNeeded(GstElement *, gpointer data)
    {
        queue(*static_cast<ShareCallback *>(data), [](auto *worker) { worker->createDescription("offer"); });
    }
    static void iceCandidate(GstElement *, guint line, gchar *candidate, gpointer data)
    {
        const QJsonObject payload{{"sdpMLineIndex", int(line)}, {"candidate", QString::fromUtf8(candidate)}};
        queue(*static_cast<ShareCallback *>(data), [payload](auto *worker) {
            emit worker->negotiationReady(worker->epoch_, "ice", payload);
        });
    }
    static GstWebRTCSessionDescription *parseDescription(const QString &type, const QString &text)
    {
        GstSDPMessage *message = nullptr;
        gst_sdp_message_new(&message);
        const auto bytes = text.toUtf8();
        if (bytes.isEmpty() || gst_sdp_message_parse_buffer(
            reinterpret_cast<const guint8 *>(bytes.constData()), guint(bytes.size()), message) != GST_SDP_OK
            || gst_sdp_message_medias_len(message) < 1 || gst_sdp_message_medias_len(message) > 2)
        {
            gst_sdp_message_free(message);
            return nullptr;
        }
        return gst_webrtc_session_description_new(type == "offer"
            ? GST_WEBRTC_SDP_TYPE_OFFER : GST_WEBRTC_SDP_TYPE_ANSWER, message);
    }
    void createDescription(const QString &type)
    {
        if (!rtc_)
            return;
        auto *promise = gst_promise_new_with_change_func(descriptionCreated,
            new ShareCallback{guard_, epoch_, type}, destroyPromise);
        g_signal_emit_by_name(rtc_, type == "offer" ? "create-offer" : "create-answer", nullptr, promise);
    }
    static void descriptionCreated(GstPromise *promise, gpointer data)
    {
        const auto callback = *static_cast<ShareCallback *>(data);
        GstWebRTCSessionDescription *description = nullptr;
        const auto *reply = gst_promise_wait(promise) == GST_PROMISE_RESULT_REPLIED
            ? gst_promise_get_reply(promise) : nullptr;
        if (reply)
            gst_structure_get(reply, callback.type.toUtf8().constData(),
                GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &description, nullptr);
        QString text;
        if (description)
        {
            auto *sdp = gst_sdp_message_as_text(description->sdp);
            text = QString::fromUtf8(sdp);
            g_free(sdp);
            gst_webrtc_session_description_free(description);
        }
        gst_promise_unref(promise);
        queue(callback, [text, type = callback.type](auto *worker) {
            auto *sdp = parseDescription(type, text);
            if (!sdp)
                return worker->fail(QStringLiteral("无法生成共享 SDP"));
            g_signal_emit_by_name(worker->rtc_, "set-local-description", sdp, nullptr);
            gst_webrtc_session_description_free(sdp);
            emit worker->negotiationReady(worker->epoch_, type, {{"sdp", text}});
        });
    }
    static void remoteDescriptionSet(GstPromise *promise, gpointer data)
    {
        const auto callback = *static_cast<ShareCallback *>(data);
        const bool replied = gst_promise_wait(promise) == GST_PROMISE_RESULT_REPLIED;
        const auto *reply = replied ? gst_promise_get_reply(promise) : nullptr;
        const bool failed = !replied || (reply && gst_structure_has_field(reply, "error"));
        gst_promise_unref(promise);
        queue(callback, [failed, type = callback.type](auto *worker) {
            if (failed)
                return worker->fail(QStringLiteral("无法应用对端共享 SDP"));
            worker->remoteReady_ = true;
            const auto candidates = worker->pendingIce_;
            worker->pendingIce_.clear();
            for (const auto &candidate : candidates)
                worker->negotiate("ice", candidate, worker->epoch_);
            if (type == "offer")
                worker->createDescription("answer");
        });
    }
    static void statisticsReceived(GstPromise *promise, gpointer data)
    {
        const auto callback = *static_cast<ShareCallback *>(data);
        guint64 bytes = 0;
        bool hasBytes = false;
        qint64 receivedPackets = -1;
        gint64 lost = -1;
        double jitterMs = -1, rttMs = -1;
        if (gst_promise_wait(promise) == GST_PROMISE_RESULT_REPLIED)
        {
            const auto *report = gst_promise_get_reply(promise);
            for (int i = 0; report && i < gst_structure_n_fields(report); ++i)
            {
                const auto *value = gst_structure_get_value(report, gst_structure_nth_field_name(report, i));
                if (!value || !GST_VALUE_HOLDS_STRUCTURE(value))
                    continue;
                const auto *record = gst_value_get_structure(value);
                gint type = 0;
                if (!gst_structure_get_enum(record, "type", GST_TYPE_WEBRTC_STATS_TYPE, &type))
                    continue;
                if ((callback.type == "sender-stats" && type == GST_WEBRTC_STATS_OUTBOUND_RTP)
                    || (callback.type == "receiver-stats" && type == GST_WEBRTC_STATS_INBOUND_RTP))
                {
                    hasBytes = gst_structure_get_uint64(record,
                        callback.type == "sender-stats" ? "bytes-sent" : "bytes-received", &bytes);
                    if (callback.type == "receiver-stats")
                    {
                        guint64 received = 0;
                        if (gst_structure_get_uint64(record, "packets-received", &received))
                            receivedPackets = qint64(received);
                        gst_structure_get_int64(record, "packets-lost", &lost);
                        double jitter = 0;
                        if (gst_structure_get_double(record, "jitter", &jitter))
                            jitterMs = jitter * 1000;
                    }
                }
                else if (callback.type == "sender-stats" && type == GST_WEBRTC_STATS_REMOTE_INBOUND_RTP)
                {
                    double rtt = 0;
                    if (gst_structure_get_double(record, "round-trip-time", &rtt))
                        rttMs = rtt * 1000;
                }
            }
        }
        gst_promise_unref(promise);
        queue(callback, [hasBytes, bytes, receivedPackets, lost, jitterMs, rttMs](auto *worker) {
            worker->statsPending_ = false;
            if (hasBytes)
            {
                if (worker->networkClock_.isValid() && worker->networkClock_.elapsed() > 0
                    && bytes >= worker->previousNetworkBytes_)
                    worker->networkMbps_ = double(bytes - worker->previousNetworkBytes_) * 8
                        / (worker->networkClock_.elapsed() * 1000);
                worker->previousNetworkBytes_ = bytes;
                worker->networkClock_.restart();
            }
            if (receivedPackets >= 0)
                worker->packetsReceived_ = receivedPackets;
            if (lost >= 0)
                worker->packetsLost_ = lost;
            if (jitterMs >= 0)
                worker->jitterMs_ = jitterMs;
            if (rttMs > 0)
                worker->webRtcRttMs_ = rttMs;
        });
    }
    void fail(const QString &detail)
    {
        release();
        emit statusChanged(epoch_, "failed", detail);
    }
    void release()
    {
        {
            QMutexLocker lock(&guard_->mutex);
            guard_->epoch = 0;
        }
        if (pipeline_)
        {
            gst_element_set_state(pipeline_, GST_STATE_NULL);
            gst_object_unref(pipeline_);
            pipeline_ = nullptr;
        }
        if (channel_)
        {
            gst_webrtc_data_channel_close(channel_);
            g_object_unref(channel_);
            channel_ = nullptr;
        }
        if (rtc_)
            gst_object_unref(rtc_);
        rtc_ = nullptr;
        if (sink_)
            gst_object_unref(sink_);
        sink_ = nullptr;
        if (bus_)
            gst_object_unref(bus_);
        bus_ = nullptr;
        pendingIce_.clear();
    }
    void poll()
    {
        if (!pipeline_)
        {
            timer_->stop();
            return;
        }
        while (auto *message = gst_bus_pop(bus_))
        {
            QString error;
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
            {
                GError *details = nullptr;
                gst_message_parse_error(message, &details, nullptr);
                error = QString::fromUtf8(details->message);
                g_error_free(details);
            }
            else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS)
                error = QStringLiteral("共享流已结束");
            gst_message_unref(message);
            if (!error.isEmpty())
                return fail(error);
        }
        if (auto *sample = gst_app_sink_try_pull_sample(sink_, 0))
        {
            GstVideoInfo info;
            GstVideoFrame frame;
            if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample))
                && gst_video_frame_map(&frame, &info, gst_sample_get_buffer(sample), GST_MAP_READ))
            {
                const QImage image(static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0)),
                    info.width, info.height, GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0), QImage::Format_RGB888);
                {
                    QMutexLocker lock(&mailbox_->mutex);
                    if (mailbox_->epoch == epoch_)
                        mailbox_->image = image.copy();
                }
                size_ = image.size();
                firstFrame_ = true;
                gst_video_frame_unmap(&frame);
            }
            gst_sample_unref(sample);
        }
        GstWebRTCPeerConnectionState connection;
        g_object_get(rtc_, "connection-state", &connection, nullptr);
        if (connection == GST_WEBRTC_PEER_CONNECTION_STATE_FAILED
            || connection == GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED
            || connection == GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED)
            return fail(QStringLiteral("屏幕共享连接已断开"));
        if (!running_ && firstFrame_ && connection == GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED)
        {
            running_ = true;
            emit statusChanged(epoch_, "running", QStringLiteral("%1 · %2 × %3 · 目标 %4 fps")
                .arg(sender_ ? QStringLiteral("正在共享主屏幕") : QStringLiteral("正在接收对方屏幕"))
                .arg(size_.width()).arg(size_.height()).arg(targetFps_));
        }
        if (running_ && statisticsClock_.elapsed() >= 1000)
        {
            const double seconds = statisticsClock_.restart() / 1000.0;
            const auto processed = processedFrames_.load(std::memory_order_relaxed);
            ShareStats stats;
            {
                QMutexLocker lock(&mailbox_->mutex);
                if (mailbox_->epoch != epoch_)
                    return;
                stats = mailbox_->stats;
                stats.size = size_;
                stats.intervalSeconds = seconds;
                stats.targetFps = targetFps_;
                stats.peerFps = peerFps_;
                stats.revision = revision_;
                stats.processedFrames = processed;
                stats.processedFps = (processed - previousProcessed_) / seconds;
                stats.deliveredFps = (stats.deliveredFrames - previousDelivered_) / seconds;
                stats.networkMbps = networkMbps_;
                stats.packetsReceived = packetsReceived_;
                stats.packetsLost = packetsLost_;
                stats.jitterMs = jitterMs_;
                stats.webRtcRttMs = webRtcRttMs_;
                mailbox_->stats = stats;
            }
            previousProcessed_ = processed;
            previousDelivered_ = stats.deliveredFrames;
            emit statisticsReady(epoch_, stats);
            if (!statsPending_)
            {
                statsPending_ = true;
                auto *promise = gst_promise_new_with_change_func(statisticsReceived,
                    new ShareCallback{guard_, epoch_, sender_ ? "sender-stats" : "receiver-stats"}, destroyPromise);
                g_signal_emit_by_name(rtc_, "get-stats", nullptr, promise);
            }
            if (sender_)
                adapt(stats.processedFps);
            else if (profileClock_.elapsed() > 2000)
            {
                double actual = qMin(stats.processedFps, stats.deliveredFps);
                if (stats.presentedFps >= 0)
                    actual = qMin(actual, stats.presentedFps);
                emit negotiationReady(epoch_, "share", {{"action", "stats"}, {"revision", revision_}, {"fps", actual}});
            }
        }
        if (!running_ && startup_.elapsed() > 15000)
            fail(QStringLiteral("屏幕共享连接超时，请检查双方网络和媒体插件"));
    }

    std::shared_ptr<ShareCallbacks> guard_ = std::make_shared<ShareCallbacks>();
    GstElement *pipeline_ = nullptr;
    GstElement *rtc_ = nullptr;
    GstWebRTCDataChannel *channel_ = nullptr;
    GstAppSink *sink_ = nullptr;
    GstBus *bus_ = nullptr;
    QTimer *timer_ = nullptr;
    QElapsedTimer startup_;
    QList<QJsonObject> pendingIce_;
    quint64 epoch_ = 0;
    bool sender_ = false;
    bool remoteReady_ = false;
    bool running_ = false;
    bool firstFrame_ = false;
    QSize size_;
    int targetFps_ = 60;
    std::atomic<quint64> processedFrames_{0};
    quint64 previousProcessed_ = 0;
    quint64 previousDelivered_ = 0;
    QElapsedTimer statisticsClock_;
    ShareQuality quality_;
    ShareProfile profile_;
    bool automatic_ = true;
    int revision_ = 1;
    double peerFps_ = -1;
    QElapsedTimer profileClock_;
    QElapsedTimer feedbackClock_;
    QElapsedTimer networkClock_;
    quint64 previousNetworkBytes_ = 0;
    double networkMbps_ = -1;
    qint64 packetsReceived_ = -1;
    qint64 packetsLost_ = -1;
    double jitterMs_ = -1;
    double webRtcRttMs_ = -1;
    bool statsPending_ = false;
#endif
    ShareMailbox *mailbox_;
};

ScreenShare::ScreenShare(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<ShareStats>();
    worker_ = new ScreenShareWorker(&mailbox_);
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &ScreenShareWorker::statusChanged, this, &ScreenShare::statusChanged);
    connect(worker_, &ScreenShareWorker::negotiationReady, this, &ScreenShare::negotiationReady);
    connect(worker_, &ScreenShareWorker::statisticsReady, this, &ScreenShare::statisticsReady);
    connect(worker_, &ScreenShareWorker::controlChannelChanged, this, &ScreenShare::controlChannelChanged);
    connect(worker_, &ScreenShareWorker::controlReceived, this, &ScreenShare::controlReceived);
    connect(&frames_, &QTimer::timeout, this, [this] {
        QImage image;
        quint64 epoch;
        {
            QMutexLocker lock(&mailbox_.mutex);
            image.swap(mailbox_.image);
            epoch = mailbox_.epoch;
            if (!image.isNull())
                ++mailbox_.stats.deliveredFrames;
        }
        if (!image.isNull())
            emit frameReady(epoch, image);
    });
    frames_.setTimerType(Qt::PreciseTimer);
    frames_.start(4);
    thread_.setObjectName("screen-share-worker");
    thread_.start();
}

ScreenShare::~ScreenShare()
{
    frames_.stop();
    QMetaObject::invokeMethod(worker_, [this] { worker_->shutdown(); }, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
}

void ScreenShare::start(bool sender, const QString &source, quint64 epoch, int targetFps)
{
    presentationClock_.invalidate();
    QSize nativeSize(640, 480);
    int maximumFps = 60;
    if (source != "test")
    {
        nativeSize = QSize(1920, 1080);
        if (qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
            if (auto *screen = QGuiApplication::primaryScreen())
            {
                nativeSize = screen->size() * screen->devicePixelRatio();
                maximumFps = qRound(screen->refreshRate());
            }
    }
    {
        QMutexLocker lock(&mailbox_.mutex);
        mailbox_.epoch = epoch;
        mailbox_.image = {};
        mailbox_.stats = {};
    }
    QMetaObject::invokeMethod(worker_, [this, sender, source, epoch, targetFps, nativeSize, maximumFps] {
        worker_->start(sender, source, epoch, targetFps, nativeSize, maximumFps);
    });
}

void ScreenShare::reportPresentation(quint64 frames, quint64 epoch, bool active)
{
    QMutexLocker lock(&mailbox_.mutex);
    if (epoch != mailbox_.epoch)
        return;
    if (!active)
    {
        mailbox_.stats.presentedFps = -1;
        previousPresented_ = frames;
        presentationClock_.invalidate();
        return;
    }
    if (presentationClock_.isValid() && presentationClock_.elapsed() > 0)
        mailbox_.stats.presentedFps = (frames - previousPresented_) * 1000.0 / presentationClock_.elapsed();
    previousPresented_ = frames;
    presentationClock_.restart();
}

void ScreenShare::stop(quint64 epoch)
{
    {
        QMutexLocker lock(&mailbox_.mutex);
        mailbox_.epoch = epoch;
        mailbox_.image = {};
    }
    QMetaObject::invokeMethod(worker_, [this, epoch] { worker_->stop(epoch); });
}

void ScreenShare::negotiate(const QString &type, const QJsonObject &payload, quint64 epoch)
{
    QMetaObject::invokeMethod(worker_, [this, type, payload, epoch] { worker_->negotiate(type, payload, epoch); });
}

void ScreenShare::sendControl(const QJsonObject &message, quint64 epoch)
{
    QMetaObject::invokeMethod(worker_, [this, message, epoch] { worker_->sendControl(message, epoch); });
}
} // namespace zjdesk

#include "screen_share.moc"
