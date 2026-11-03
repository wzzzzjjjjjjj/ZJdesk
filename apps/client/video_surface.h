#pragma once
#include <QOpenGLWidget>
#include <QImage>
#include <QJsonObject>
#include <QTimer>
#include <QElapsedTimer>
#include "remote-control/input_injector.h"
#include "media/frame_timing.h"

class VideoSurface final : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit VideoSurface(QWidget *parent = nullptr);
    void setParticipant(const QString &name, const QString &caption);
    void setFrame(const QImage &frame);
    bool hasFrame() const { return !frame_.isNull(); }
    quint64 presentedFrames() const { return presentedFrames_; }
    double takePresentationGapMaxMs();
    zjdesk::VideoPresentationStats presentationStatistics() const;
    void setFrameCaption(const QString &caption) { frameCaption_ = caption; update(); }
    // Meeting-only presentation; the remote-control canvas keeps its existing rendering.
    void setParticipantTile(bool enabled) { participantTile_ = enabled; update(); }
    void setFrameHidden(bool hidden) { frameHidden_ = hidden; update(); }
    void setControlEnabled(bool enabled);
    void setRemoteCursor(const zjdesk::RemoteCursor &cursor);
    bool hasRemoteCursor() const { return remoteCursor_.visible && !remoteCursor_.image.isNull(); }

signals:
    void remoteInput(const QJsonObject &input);
    void frameAvailabilityChanged();

protected:
    bool event(QEvent *event) override;
    void paintGL() override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    QString name_ = QStringLiteral("等待另一位成员");
    QString caption_ = QStringLiteral("加入同一个房间，即可建立连接");
    QImage frame_;
    quint64 presentedFrames_ = 0;
    bool pendingFrame_ = false;
    bool awaitingSwap_ = false;
    QElapsedTimer presentationClock_;
    double presentationGapMaxMs_ = -1;
    zjdesk::FrameTiming presentationGaps_;
    zjdesk::FrameTiming receiveToPaint_;
    zjdesk::FrameTiming paintTiming_;
    zjdesk::FrameTiming receiveToSwap_;
    qint64 receivedNs_ = 0;
    qint64 awaitingReceivedNs_ = 0;
    quint64 frameId_ = 0;
    quint64 awaitingFrameId_ = 0;
    quint64 lastPresentedFrameId_ = 0;
    quint64 supersededFrames_ = 0;
    QString frameCaption_ = QStringLiteral("本地预览 · 尚未发送给对方");
    bool controlEnabled_ = false;
    bool participantTile_ = false;
    bool frameHidden_ = false;
    Qt::MouseButtons remoteButtons_ = Qt::NoButton;
    QTimer motionTimer_;
    QJsonObject pendingMotion_;
    zjdesk::RemoteCursor remoteCursor_;
    void flushMotion();
    void updatePointerCursor(const QPointF &position);
    bool pointerInput(const QPointF &position, QJsonObject &input) const;
};
