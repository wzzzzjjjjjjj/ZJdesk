#include "video_surface.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QFocusEvent>

VideoSurface::VideoSurface(QWidget *parent) : QOpenGLWidget(parent)
{
    setMinimumSize(320, 220);
}

void VideoSurface::setParticipant(const QString &name, const QString &caption)
{
    name_ = name;
    caption_ = caption;
    update();
}

void VideoSurface::paintGL()
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!frame_.isNull())
    {
        if (pendingFrame_)
        {
            ++presentedFrames_;
            pendingFrame_ = false;
        }
        painter.fillRect(rect(), Qt::black);
        const auto size = frame_.size().scaled(this->size(), Qt::KeepAspectRatio);
        const QRect target((width() - size.width()) / 2, (height() - size.height()) / 2,
                           size.width(), size.height());
        painter.drawImage(target, frame_);
        painter.setPen(Qt::white);
        painter.drawText(QRect(16, 12, width() - 32, 24), Qt::AlignLeft,
                         frameCaption_);
        return;
    }
    QLinearGradient background(0, 0, width(), height());
    background.setColorAt(0, QColor("#172740"));
    background.setColorAt(1, QColor("#263b59"));
    painter.fillRect(rect(), background);
    const QPointF center(width() / 2.0, height() / 2.0 - 24);
    painter.setPen(QPen(QColor("#344e70"), 1));
    painter.setBrush(QColor("#263e60"));
    painter.drawEllipse(center, 45, 45);
    auto font = painter.font();
    font.setPixelSize(30);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor("#dbe9ff"));
    painter.drawText(QRectF(center.x() - 45, center.y() - 45, 90, 90), Qt::AlignCenter,
                     name_.isEmpty() ? "Z" : name_.left(1).toUpper());
    font.setPixelSize(16);
    painter.setFont(font);
    painter.drawText(QRect(12, int(center.y()) + 58, width() - 24, 28), Qt::AlignCenter, name_);
    font.setPixelSize(12);
    font.setBold(false);
    painter.setFont(font);
    painter.setPen(QColor("#a4b7d1"));
    painter.drawText(QRect(12, int(center.y()) + 92, width() - 24, 38),
                     Qt::AlignHCenter | Qt::TextWordWrap, caption_);
    painter.setPen(QColor("#7790b3"));
    painter.drawText(QRect(20, 18, width() - 40, 22), Qt::AlignLeft, QStringLiteral("远端画面"));
}

void VideoSurface::setFrame(const QImage &frame)
{
    frame_ = frame;
    pendingFrame_ = !frame.isNull();
    update();
}

void VideoSurface::setControlEnabled(bool enabled)
{
    if (controlEnabled_ && !enabled)
        emit remoteInput({{"kind", "release"}});
    controlEnabled_ = enabled;
    if (!enabled)
        remoteButtons_ = Qt::NoButton;
    setMouseTracking(enabled);
    setFocusPolicy(enabled ? Qt::StrongFocus : Qt::NoFocus);
}

bool VideoSurface::pointerInput(const QPointF &position, QJsonObject &input) const
{
    if (!controlEnabled_ || frame_.isNull())
        return false;
    const auto size = frame_.size().scaled(this->size(), Qt::KeepAspectRatio);
    const QRectF target((width() - size.width()) / 2.0, (height() - size.height()) / 2.0,
                        size.width(), size.height());
    if (!target.contains(position))
        return false;
    input.insert("x", (position.x() - target.x()) / target.width());
    input.insert("y", (position.y() - target.y()) / target.height());
    return true;
}

void VideoSurface::mouseMoveEvent(QMouseEvent *event)
{
    QJsonObject input{{"kind", "move"}};
    if (pointerInput(event->position(), input) && (!motionClock_.isValid() || motionClock_.elapsed() >= 16))
    {
        emit remoteInput(input);
        motionClock_.restart();
    }
}

void VideoSurface::mousePressEvent(QMouseEvent *event)
{
    QJsonObject input{{"kind", "button"}, {"button", int(event->button())}, {"pressed", true}};
    if (pointerInput(event->position(), input))
    {
        setFocus(Qt::MouseFocusReason);
        remoteButtons_ |= event->button();
        emit remoteInput(input);
        event->accept();
    }
}

void VideoSurface::mouseReleaseEvent(QMouseEvent *event)
{
    QJsonObject input{{"kind", "button"}, {"button", int(event->button())}, {"pressed", false}};
    if (!remoteButtons_.testFlag(event->button()))
        return;
    remoteButtons_ &= ~event->button();
    if (pointerInput(event->position(), input))
        emit remoteInput(input);
    else if (controlEnabled_)
    {
        remoteButtons_ = Qt::NoButton;
        emit remoteInput({{"kind", "release"}});
    }
}

void VideoSurface::wheelEvent(QWheelEvent *event)
{
    const int steps = qBound(-10, event->angleDelta().y() / 120, 10);
    QJsonObject input{{"kind", "wheel"}, {"steps", steps}};
    if (steps && pointerInput(event->position(), input))
    {
        emit remoteInput(input);
        event->accept();
    }
}

void VideoSurface::keyPressEvent(QKeyEvent *event)
{
    if (controlEnabled_)
    {
        emit remoteInput({{"kind", "key"}, {"key", event->key()}, {"pressed", true}});
        event->accept();
    }
    else
        QOpenGLWidget::keyPressEvent(event);
}

void VideoSurface::keyReleaseEvent(QKeyEvent *event)
{
    if (controlEnabled_ && !event->isAutoRepeat())
    {
        emit remoteInput({{"kind", "key"}, {"key", event->key()}, {"pressed", false}});
        event->accept();
    }
    else
        QOpenGLWidget::keyReleaseEvent(event);
}

void VideoSurface::focusOutEvent(QFocusEvent *event)
{
    if (controlEnabled_)
    {
        remoteButtons_ = Qt::NoButton;
        emit remoteInput({{"kind", "release"}});
    }
    QOpenGLWidget::focusOutEvent(event);
}
