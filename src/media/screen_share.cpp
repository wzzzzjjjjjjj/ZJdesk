#include "screen_share.h"
#include <QElapsedTimer>
#include <QMutexLocker>
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
    explicit ScreenShareWorker(PreviewMailbox *mailbox) : mailbox_(mailbox)
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

    void start(bool sender, const QString &source, quint64 epoch)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        release();
        epoch_ = epoch;
        {
            QMutexLocker lock(&guard_->mutex);
            guard_->epoch = epoch;
        }
        sender_ = sender;
        remoteReady_ = running_ = firstFrame_ = false;
        pendingIce_.clear();
        QString description;
        if (sender)
        {
            QString capture;
            if (source == "test")
                capture = "videotestsrc is-live=true pattern=ball ! "
                    "video/x-raw,width=640,height=480,framerate=15/1 ! ";
#ifdef Q_OS_WIN
            else
                capture = "d3d11screencapturesrc monitor-index=-1 show-cursor=true ! "
                    "d3d11download ! videoconvert ! video/x-raw,framerate=15/1 ! ";
#else
            else if (qEnvironmentVariable("XDG_SESSION_TYPE") != "wayland")
                capture = "ximagesrc use-damage=false show-pointer=true ! videoconvert ! "
                    "videorate ! video/x-raw,framerate=15/1 ! ";
            else
            {
                fail(QStringLiteral("屏幕共享暂只支持 Windows 和 Linux X11 桌面"));
                return;
            }
#endif
            description = capture + "tee name=split "
                "split. ! queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream ! "
                "videoconvert ! video/x-raw,format=RGB ! "
                "appsink name=frames max-buffers=1 drop=true sync=false wait-on-eos=false "
                "split. ! queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream ! "
                "videoconvert ! video/x-raw,format=I420 ! "
                "vp8enc deadline=1 cpu-used=8 threads=4 target-bitrate=4000000 keyframe-max-dist=30 ! "
                "rtpvp8pay pt=96 picture-id-mode=15-bit ! "
                "application/x-rtp,media=video,encoding-name=VP8,payload=96 ! "
                "webrtcbin name=rtc bundle-policy=max-bundle";
        }
        else
            description = "webrtcbin name=rtc bundle-policy=max-bundle "
                "rtpvp8depay name=incoming ! vp8dec ! videoconvert ! video/x-raw,format=RGB ! "
                "appsink name=frames max-buffers=1 drop=true sync=false wait-on-eos=false";
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
        sink_ = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline_), "frames"));
        bus_ = gst_element_get_bus(pipeline_);
        g_signal_connect_data(rtc_, "on-ice-candidate", G_CALLBACK(iceCandidate),
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
        timer_->start(10);
#else
        Q_UNUSED(sender)
        Q_UNUSED(source)
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
            || gst_sdp_message_medias_len(message) != 1)
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
            emit statusChanged(epoch_, "running", QStringLiteral("%1 · %2 × %3 · 目标 15 fps")
                .arg(sender_ ? QStringLiteral("正在共享主屏幕") : QStringLiteral("正在接收对方屏幕"))
                .arg(size_.width()).arg(size_.height()));
        }
        if (!running_ && startup_.elapsed() > 15000)
            fail(QStringLiteral("屏幕共享连接超时，请检查双方网络和媒体插件"));
    }

    std::shared_ptr<ShareCallbacks> guard_ = std::make_shared<ShareCallbacks>();
    GstElement *pipeline_ = nullptr;
    GstElement *rtc_ = nullptr;
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
#endif
    PreviewMailbox *mailbox_;
};

ScreenShare::ScreenShare(QObject *parent) : QObject(parent)
{
    worker_ = new ScreenShareWorker(&mailbox_);
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &ScreenShareWorker::statusChanged, this, &ScreenShare::statusChanged);
    connect(worker_, &ScreenShareWorker::negotiationReady, this, &ScreenShare::negotiationReady);
    connect(&frames_, &QTimer::timeout, this, [this] {
        QImage image;
        quint64 epoch;
        {
            QMutexLocker lock(&mailbox_.mutex);
            image.swap(mailbox_.image);
            epoch = mailbox_.epoch;
        }
        if (!image.isNull())
            emit frameReady(epoch, image);
    });
    frames_.start(33);
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

void ScreenShare::start(bool sender, const QString &source, quint64 epoch)
{
    {
        QMutexLocker lock(&mailbox_.mutex);
        mailbox_.epoch = epoch;
        mailbox_.image = {};
    }
    QMetaObject::invokeMethod(worker_, [this, sender, source, epoch] { worker_->start(sender, source, epoch); });
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
} // namespace zjdesk

#include "screen_share.moc"
