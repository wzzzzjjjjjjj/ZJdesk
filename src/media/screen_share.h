#pragma once

#include "media_engine.h"
#include <QJsonObject>

namespace zjdesk
{
class ScreenShareWorker;

class ScreenShare final : public QObject
{
    Q_OBJECT
public:
    explicit ScreenShare(QObject *parent = nullptr);
    ~ScreenShare() override;
    void start(bool sender, const QString &source, quint64 epoch);
    void stop(quint64 epoch);
    void negotiate(const QString &type, const QJsonObject &payload, quint64 epoch);

signals:
    void statusChanged(quint64 epoch, const QString &status, const QString &detail);
    void negotiationReady(quint64 epoch, const QString &type, const QJsonObject &payload);
    void frameReady(quint64 epoch, const QImage &image);

private:
    PreviewMailbox mailbox_;
    QThread thread_;
    ScreenShareWorker *worker_ = nullptr;
    QTimer frames_;
};
} // namespace zjdesk
