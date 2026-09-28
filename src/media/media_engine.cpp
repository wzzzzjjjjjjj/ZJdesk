#include "media_engine.h"
#include <QElapsedTimer>
#include <QHash>
#include <QMutexLocker>
#ifdef ZJDESK_HAS_GSTREAMER
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#endif

namespace zjdesk
{
class MediaWorker final : public QObject
{
    Q_OBJECT
public:
    explicit MediaWorker(PreviewMailbox *mailbox) : mailbox_(mailbox) {}
    ~MediaWorker() override { shutdown(); }

    void refreshDevices()
    {
        QList<MediaDevice> result;
#ifdef ZJDESK_HAS_GSTREAMER
        clearDevices();
        auto *monitor = gst_device_monitor_new();
        gst_device_monitor_add_filter(monitor, "Video/Source", nullptr);
        gst_device_monitor_add_filter(monitor, "Audio/Source", nullptr);
        gst_device_monitor_add_filter(monitor, "Audio/Sink", nullptr);
        if (!gst_device_monitor_start(monitor))
        {
            gst_object_unref(monitor);
            emit devicesReady(result, QStringLiteral("设备枚举失败，请检查设备插件"));
            return;
        }
        ++deviceGeneration_;
        auto *devices = gst_device_monitor_get_devices(monitor);
        for (auto *item = devices; item; item = item->next)
        {
            auto *device = GST_DEVICE(item->data);
            // The legacy Kernel Streaming source failed on the current camera;
            // Media Foundation is its supported replacement in this SDK.
            auto *probe = gst_device_create_element(device, nullptr);
            if (probe)
            {
                auto *factory = gst_element_get_factory(probe);
                const bool legacyCamera = factory
                    && QString::fromUtf8(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory))) == "ksvideosrc";
                gst_object_unref(probe);
                if (legacyCamera)
                    continue;
            }
            auto *name = gst_device_get_display_name(device);
            auto *kind = gst_device_get_device_class(device);
            QString category = QString::fromUtf8(kind);
            if (category.contains("Video") && category.contains("Source"))
                category = "Video/Source";
            else if (category.contains("Audio") && category.contains("Source"))
                category = "Audio/Source";
            else if (category.contains("Audio") && category.contains("Sink"))
                category = "Audio/Sink";
            const QString id = QString("device-%1-%2").arg(deviceGeneration_).arg(devices_.size());
            result.append({id, QString::fromUtf8(name), category});
            devices_.insert(id, GST_DEVICE(gst_object_ref(device)));
            g_free(name);
            g_free(kind);
        }
        g_list_free_full(devices, gst_object_unref);
        gst_device_monitor_stop(monitor);
        gst_object_unref(monitor);
        emit devicesReady(result, {});
#else
        emit devicesReady(result, QStringLiteral("当前构建未启用 GStreamer"));
#endif
    }

    void startVideo(const QString &sourceId, quint64 epoch)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        releaseVideo();
        videoEpoch_ = epoch;
        QString error;
        GError *parseError = nullptr;
        const char *tail = "capsfilter caps=\"video/x-raw,width=640,height=480,framerate=30/1\" ! "
            "videoconvert ! videoscale ! "
            "video/x-raw,format=RGB,width=640,height=480 ! "
            "appsink name=frames max-buffers=1 drop=true sync=false wait-on-eos=false";
        if (sourceId == "test")
        {
            video_ = gst_parse_launch("videotestsrc is-live=true pattern=ball ! "
                "video/x-raw,width=640,height=480,framerate=30/1 ! videoconvert ! "
                "video/x-raw,format=RGB ! "
                "appsink name=frames max-buffers=1 drop=true sync=false wait-on-eos=false", &parseError);
        }
        else if (devices_.contains(sourceId))
        {
            auto *source = gst_device_create_element(devices_.value(sourceId), "camera");
            auto *conversion = gst_parse_bin_from_description(tail, TRUE, &parseError);
            video_ = gst_pipeline_new("local-preview");
            if (source && conversion && !parseError)
            {
                gst_bin_add_many(GST_BIN(video_), source, conversion, nullptr);
                if (!gst_element_link(source, conversion))
                    error = QStringLiteral("摄像头格式无法接入预览管线");
            }
            else
            {
                if (source)
                    gst_object_unref(source);
                if (conversion)
                    gst_object_unref(conversion);
                error = QStringLiteral("无法创建设备采集管线");
            }
        }
        else
            error = QStringLiteral("摄像头不存在或已刷新，请重新选择设备");
        if (parseError)
        {
            error = QString::fromUtf8(parseError->message);
            g_error_free(parseError);
        }
        if (!video_ || !error.isEmpty())
        {
            releaseVideo();
            emit videoStatus(epoch, "failed", error.isEmpty() ? QStringLiteral("无法创建预览管线") : error);
            return;
        }
        sink_ = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(video_), "frames"));
        videoBus_ = gst_element_get_bus(video_);
        startup_.restart();
        firstFrame_ = false;
        if (!sink_ || gst_element_set_state(video_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        {
            releaseVideo();
            emit videoStatus(epoch, "failed", QStringLiteral("无法启动预览，请检查设备权限或占用情况"));
            return;
        }
        ensureTimer();
#else
        Q_UNUSED(sourceId)
        emit videoStatus(epoch, "failed", QStringLiteral("当前构建未启用 GStreamer"));
#endif
    }

    void stopVideo(quint64 epoch)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        releaseVideo();
#endif
        emit videoStatus(epoch, "stopped", QStringLiteral("本地预览已停止"));
    }

    void startAudio(bool audible, quint64 epoch)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        releaseAudio();
        audioEpoch_ = epoch;
        GError *error = nullptr;
        audio_ = gst_parse_launch(audible
            ? "audiotestsrc is-live=true volume=0.03 ! audioconvert ! audioresample ! autoaudiosink"
            : "audiotestsrc is-live=true ! audioconvert ! fakesink sync=false", &error);
        if (error || !audio_)
        {
            const auto detail = QString::fromUtf8(error ? error->message : "Unable to create audio test");
            if (error)
                g_error_free(error);
            releaseAudio();
            emit audioStatus(epoch, "failed", detail);
            return;
        }
        audioBus_ = gst_element_get_bus(audio_);
        audioAudible_ = audible;
        audioStarted_ = false;
        audioStartup_.restart();
        if (gst_element_set_state(audio_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        {
            releaseAudio();
            emit audioStatus(epoch, "failed", QStringLiteral("音频测试启动失败"));
            return;
        }
        ensureTimer();
#else
        Q_UNUSED(audible)
        emit audioStatus(epoch, "failed", QStringLiteral("当前构建未启用 GStreamer"));
#endif
    }

    void stopAudio(quint64 epoch)
    {
#ifdef ZJDESK_HAS_GSTREAMER
        releaseAudio();
#endif
        emit audioStatus(epoch, "stopped", QStringLiteral("音频测试已停止"));
    }

    void shutdown()
    {
#ifdef ZJDESK_HAS_GSTREAMER
        if (pollTimer_)
            pollTimer_->stop();
        releaseVideo();
        releaseAudio();
        clearDevices();
#endif
    }

signals:
    void devicesReady(const QList<zjdesk::MediaDevice> &devices, const QString &error);
    void videoStatus(quint64 epoch, const QString &status, const QString &detail);
    void audioStatus(quint64 epoch, const QString &status, const QString &detail);

private:
#ifdef ZJDESK_HAS_GSTREAMER
    void clearDevices()
    {
        for (auto *device : devices_)
            gst_object_unref(device);
        devices_.clear();
    }

    void releaseVideo()
    {
        if (video_)
        {
            gst_element_set_state(video_, GST_STATE_NULL);
            gst_object_unref(video_);
            video_ = nullptr;
        }
        if (sink_)
            gst_object_unref(sink_);
        sink_ = nullptr;
        if (videoBus_)
            gst_object_unref(videoBus_);
        videoBus_ = nullptr;
    }

    void releaseAudio()
    {
        if (audio_)
        {
            gst_element_set_state(audio_, GST_STATE_NULL);
            gst_object_unref(audio_);
            audio_ = nullptr;
        }
        if (audioBus_)
            gst_object_unref(audioBus_);
        audioBus_ = nullptr;
    }

    void ensureTimer()
    {
        if (!pollTimer_)
        {
            pollTimer_ = new QTimer(this);
            connect(pollTimer_, &QTimer::timeout, this, &MediaWorker::poll);
        }
        pollTimer_->start(10);
    }

    QString busError(GstBus *bus)
    {
        while (auto *message = gst_bus_pop(bus))
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
                error = QStringLiteral("媒体流已结束");
            gst_message_unref(message);
            if (!error.isEmpty())
                return error;
        }
        return {};
    }

    void poll()
    {
        if (video_)
        {
            const auto error = busError(videoBus_);
            if (!error.isEmpty())
            {
                releaseVideo();
                emit videoStatus(videoEpoch_, "failed", error);
            }
            else if (auto *sample = gst_app_sink_try_pull_sample(sink_, 0))
            {
                GstVideoInfo info;
                GstVideoFrame frame;
                if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample))
                    && gst_video_frame_map(&frame, &info, gst_sample_get_buffer(sample), GST_MAP_READ))
                {
                    const QImage image(static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0)),
                        GST_VIDEO_FRAME_WIDTH(&frame), GST_VIDEO_FRAME_HEIGHT(&frame),
                        GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0), QImage::Format_RGB888);
                    {
                        QMutexLocker lock(&mailbox_->mutex);
                        if (mailbox_->epoch == videoEpoch_)
                        {
                            if (!mailbox_->image.isNull())
                                ++mailbox_->stats.droppedFrames;
                            mailbox_->image = image.copy();
                            ++mailbox_->stats.capturedFrames;
                            mailbox_->stats.pendingFrames = 1;
                        }
                    }
                    gst_video_frame_unmap(&frame);
                    if (!firstFrame_)
                    {
                        firstFrame_ = true;
                        emit videoStatus(videoEpoch_, "running", QStringLiteral("本地预览正在运行 · 640 × 480"));
                    }
                }
                gst_sample_unref(sample);
            }
            if (video_ && !firstFrame_ && startup_.elapsed() > 8000)
            {
                releaseVideo();
                emit videoStatus(videoEpoch_, "failed", QStringLiteral("预览未收到画面，请检查设备权限或占用情况"));
            }
        }
        if (audio_)
        {
            const auto error = busError(audioBus_);
            if (!error.isEmpty())
            {
                releaseAudio();
                emit audioStatus(audioEpoch_, "failed", error);
            }
            else if (!audioStarted_)
            {
                GstState state;
                gst_element_get_state(audio_, &state, nullptr, 0);
                if (state == GST_STATE_PLAYING)
                {
                    audioStarted_ = true;
                    emit audioStatus(audioEpoch_, "running", audioAudible_
                        ? QStringLiteral("正在播放测试音，请手动确认扬声器声音")
                        : QStringLiteral("音频测试管线正在运行（静音）"));
                }
                else if (audioStartup_.elapsed() > 8000)
                {
                    releaseAudio();
                    emit audioStatus(audioEpoch_, "failed", QStringLiteral("音频测试启动超时，请检查输出设备"));
                }
            }
        }
        if (!video_ && !audio_)
            pollTimer_->stop();
    }

    QHash<QString, GstDevice *> devices_;
    quint64 deviceGeneration_ = 0;
    GstElement *video_ = nullptr;
    GstElement *audio_ = nullptr;
    GstAppSink *sink_ = nullptr;
    GstBus *videoBus_ = nullptr;
    GstBus *audioBus_ = nullptr;
    QTimer *pollTimer_ = nullptr;
    QElapsedTimer startup_;
    QElapsedTimer audioStartup_;
    quint64 videoEpoch_ = 0;
    quint64 audioEpoch_ = 0;
    bool firstFrame_ = false;
    bool audioStarted_ = false;
    bool audioAudible_ = false;
#endif
    PreviewMailbox *mailbox_;
};

MediaEngine::MediaEngine(QObject *parent) : QObject(parent), environment_(MediaEnvironment::inspect())
{
    qRegisterMetaType<QList<MediaDevice>>();
    worker_ = new MediaWorker(&mailbox_);
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &MediaWorker::devicesReady, this, &MediaEngine::devicesReady);
    connect(worker_, &MediaWorker::videoStatus, this, &MediaEngine::videoStatus);
    connect(worker_, &MediaWorker::audioStatus, this, &MediaEngine::audioStatus);
    connect(&frameTimer_, &QTimer::timeout, this, [this] {
        QImage image;
        quint64 epoch;
        {
            QMutexLocker lock(&mailbox_.mutex);
            image.swap(mailbox_.image);
            epoch = mailbox_.epoch;
            mailbox_.stats.pendingFrames = 0;
        }
        if (!image.isNull())
            emit frameReady(epoch, image);
    });
    frameTimer_.start(33);
    thread_.setObjectName("media-worker");
    thread_.start();
}

MediaEngine::~MediaEngine()
{
    frameTimer_.stop();
    QMetaObject::invokeMethod(worker_, [this] { worker_->shutdown(); }, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
}

void MediaEngine::refreshDevices()
{
    if (!environment_.sdkLinked || !environment_.ready)
    {
        emit devicesReady({}, environment_.description);
        return;
    }
    QMetaObject::invokeMethod(worker_, [this] { worker_->refreshDevices(); });
}

void MediaEngine::startVideo(const QString &sourceId, quint64 epoch)
{
    {
        QMutexLocker lock(&mailbox_.mutex);
        mailbox_.image = {};
        mailbox_.epoch = epoch;
        mailbox_.stats = {};
    }
    QMetaObject::invokeMethod(worker_, [this, sourceId, epoch] { worker_->startVideo(sourceId, epoch); });
}

void MediaEngine::stopVideo(quint64 epoch)
{
    {
        QMutexLocker lock(&mailbox_.mutex);
        mailbox_.epoch = epoch;
        mailbox_.image = {};
        mailbox_.stats.pendingFrames = 0;
    }
    QMetaObject::invokeMethod(worker_, [this, epoch] { worker_->stopVideo(epoch); });
}

void MediaEngine::startAudio(bool audible, quint64 epoch)
{
    QMetaObject::invokeMethod(worker_, [this, audible, epoch] { worker_->startAudio(audible, epoch); });
}

void MediaEngine::stopAudio(quint64 epoch)
{
    QMetaObject::invokeMethod(worker_, [this, epoch] { worker_->stopAudio(epoch); });
}

PreviewStats MediaEngine::stats() const
{
    QMutexLocker lock(&mailbox_.mutex);
    return mailbox_.stats;
}
} // namespace zjdesk

#include "media_engine.moc"
