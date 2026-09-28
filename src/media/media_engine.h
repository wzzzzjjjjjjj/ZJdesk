#pragma once

#include "media_environment.h"
#include "camera_mode.h"
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QTimer>

namespace zjdesk
{
struct MediaDevice
{
    QString id;
    QString name;
    QString kind;
    CameraMode maximumMode;
};

struct PreviewStats
{
    quint64 capturedFrames = 0;
    quint64 droppedFrames = 0;
    int pendingFrames = 0;
};

// One owned image is shared across threads; the appsink holds at most one more.
struct PreviewMailbox
{
    QMutex mutex;
    QImage image;
    quint64 epoch = 0;
    PreviewStats stats;
};

class MediaWorker;

class MediaEngine final : public QObject
{
    Q_OBJECT
public:
    explicit MediaEngine(QObject *parent = nullptr);
    ~MediaEngine() override;
    const MediaEnvironment &environment() const { return environment_; }
    void refreshDevices();
    void startVideo(const QString &sourceId, quint64 epoch);
    void stopVideo(quint64 epoch);
    void startAudio(bool audible, quint64 epoch);
    void stopAudio(quint64 epoch);
    PreviewStats stats() const;

signals:
    void devicesReady(const QList<zjdesk::MediaDevice> &devices, const QString &error);
    void videoStatus(quint64 epoch, const QString &status, const QString &detail);
    void audioStatus(quint64 epoch, const QString &status, const QString &detail);
    void frameReady(quint64 epoch, const QImage &image);

private:
    MediaEnvironment environment_;
    mutable PreviewMailbox mailbox_;
    QThread thread_;
    MediaWorker *worker_ = nullptr;
    QTimer frameTimer_;
};
} // namespace zjdesk

Q_DECLARE_METATYPE(zjdesk::MediaDevice)
Q_DECLARE_METATYPE(QList<zjdesk::MediaDevice>)
