#pragma once
#include <QOpenGLWidget>
#include <QImage>

class VideoSurface final : public QOpenGLWidget
{
public:
    explicit VideoSurface(QWidget *parent = nullptr);
    void setParticipant(const QString &name, const QString &caption);
    void setFrame(const QImage &frame);
    bool hasFrame() const { return !frame_.isNull(); }

protected:
    void paintGL() override;

private:
    QString name_ = QStringLiteral("等待另一位成员");
    QString caption_ = QStringLiteral("加入同一个房间，即可建立连接");
    QImage frame_;
};
