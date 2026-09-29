#pragma once
#include <QOpenGLWidget>
#include <QImage>
#include <QJsonObject>
#include <QElapsedTimer>

class VideoSurface final : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit VideoSurface(QWidget *parent = nullptr);
    void setParticipant(const QString &name, const QString &caption);
    void setFrame(const QImage &frame);
    bool hasFrame() const { return !frame_.isNull(); }
    quint64 presentedFrames() const { return presentedFrames_; }
    void setFrameCaption(const QString &caption) { frameCaption_ = caption; update(); }
    void setControlEnabled(bool enabled);

signals:
    void remoteInput(const QJsonObject &input);

protected:
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
    QString frameCaption_ = QStringLiteral("本地预览 · 尚未发送给对方");
    bool controlEnabled_ = false;
    Qt::MouseButtons remoteButtons_ = Qt::NoButton;
    QElapsedTimer motionClock_;
    bool pointerInput(const QPointF &position, QJsonObject &input) const;
};
