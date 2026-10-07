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
    background.setColorAt(0, QColor("#102735"));
    background.setColorAt(1, QColor("#203e4b"));
    painter.fillRect(rect(), background);
    painter.setPen(QPen(QColor(148, 195, 199, 14), 1));
    for (int x = 24; x < width(); x += 40)
        painter.drawLine(x, 0, x, height());
    for (int y = 24; y < height(); y += 40)
        painter.drawLine(0, y, width(), y);
    const QPointF center(width() / 2.0, height() / 2.0 - 34);
    QRadialGradient glow(center, 160);
    glow.setColorAt(0, QColor(57, 164, 145, 45));
    glow.setColorAt(1, QColor(57, 164, 145, 0));
    painter.fillRect(rect(), glow);
    painter.setPen(QPen(QColor("#3a6c75"), 1));
    painter.setBrush(QColor("#1a3946"));
    painter.drawRoundedRect(QRectF(center.x() - 58, center.y() - 44, 116, 82), 12, 12);
    painter.setPen(QPen(QColor("#83cbbd"), 2));
    painter.setBrush(QColor("#244b55"));
    painter.drawRoundedRect(QRectF(center.x() - 34, center.y() - 26, 68, 44), 5, 5);
    painter.drawLine(QPointF(center.x(), center.y() + 19), QPointF(center.x(), center.y() + 27));
    painter.drawLine(QPointF(center.x() - 14, center.y() + 27), QPointF(center.x() + 14, center.y() + 27));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#83cbbd"));
    painter.drawEllipse(QPointF(center.x() + 46, center.y() - 32), 3, 3);
    auto font = painter.font();
    font.setPointSize(13);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(QColor("#e3f2f4"));
    const QRect titleRect(24, int(center.y()) + 56, width() - 48, 30);
    painter.drawText(titleRect, Qt::AlignCenter,
        painter.fontMetrics().elidedText(name_, Qt::ElideRight, titleRect.width()));
    font.setPointSize(9);
    font.setBold(false);
    painter.setFont(font);
    painter.setPen(QColor("#a9c3ce"));
    painter.drawText(QRect(28, int(center.y()) + 94, width() - 56, 52),
                     Qt::AlignHCenter | Qt::TextWordWrap, caption_);
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
