#include "video_surface.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QCursor>

VideoSurface::VideoSurface(QWidget *parent) : QOpenGLWidget(parent)
{
    setMinimumSize(320, 220);
    motionTimer_.setSingleShot(true);
    motionTimer_.setInterval(8);
    motionTimer_.setTimerType(Qt::PreciseTimer);
    connect(&motionTimer_, &QTimer::timeout, this, &VideoSurface::flushMotion);
    connect(this, &QOpenGLWidget::frameSwapped, this, [this] {
        if (!awaitingSwap_)
            return;
        awaitingSwap_ = false;
        ++presentedFrames_;
        lastPresentedFrameId_ = awaitingFrameId_;
        receiveToSwap_.record((zjdesk::FrameTiming::nowNs() - awaitingReceivedNs_) / 1e6);
        if (presentationClock_.isValid())
        {
            const double gapMs = presentationClock_.nsecsElapsed() / 1000000.0;
            presentationGapMaxMs_ = qMax(presentationGapMaxMs_, gapMs);
            presentationGaps_.record(gapMs);
        }
        presentationClock_.restart();
    });
}

zjdesk::VideoPresentationStats VideoSurface::presentationStatistics() const
{
    return {presentationGaps_.summary(), receiveToPaint_.summary(), paintTiming_.summary(),
        receiveToSwap_.summary(), supersededFrames_, lastPresentedFrameId_};
}

double VideoSurface::takePresentationGapMaxMs()
{
    const double result = presentationGapMaxMs_;
    presentationGapMaxMs_ = -1;
    return result;
}

void VideoSurface::setParticipant(const QString &name, const QString &caption)
{
    name_ = name;
    caption_ = caption;
    update();
}

void VideoSurface::paintGL()
{
    const auto paintStartedNs = zjdesk::FrameTiming::nowNs();
    const bool paintingNewFrame = pendingFrame_ && !frame_.isNull();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!frame_.isNull())
    {
        if (pendingFrame_)
        {
            awaitingSwap_ = true;
            awaitingReceivedNs_ = receivedNs_;
            awaitingFrameId_ = frameId_;
            receiveToPaint_.record((paintStartedNs - receivedNs_) / 1e6);
            pendingFrame_ = false;
        }
        painter.fillRect(rect(), Qt::black);
        const auto size = frame_.size().scaled(this->size(), Qt::KeepAspectRatio);
        const QRect target((width() - size.width()) / 2, (height() - size.height()) / 2,
                           size.width(), size.height());
        painter.drawImage(target, frame_);
        if (controlEnabled_ && hasRemoteCursor())
        {
            const double scale = double(target.width()) / remoteCursor_.screenSize.width();
            const QPointF position(target.x() + remoteCursor_.position.x() * (target.width() - 1),
                target.y() + remoteCursor_.position.y() * (target.height() - 1));
            painter.save();
            painter.setClipRect(target);
            painter.drawImage(QRectF(position - remoteCursor_.hotspot * scale,
                remoteCursor_.image.size() * scale), remoteCursor_.image);
            painter.restore();
        }
        painter.setPen(Qt::white);
        painter.drawText(QRect(16, 12, width() - 32, 24), Qt::AlignLeft,
                         frameCaption_);
        painter.end();
        if (paintingNewFrame)
            paintTiming_.record((zjdesk::FrameTiming::nowNs() - paintStartedNs) / 1e6);
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
    const bool hadFrame = !frame_.isNull();
    if (!frame.isNull())
    {
        supersededFrames_ += pendingFrame_;
        receivedNs_ = zjdesk::FrameTiming::nowNs();
        ++frameId_;
    }
    frame_ = frame;
    pendingFrame_ = !frame.isNull();
    if (frame.isNull())
    {
        awaitingSwap_ = false;
        presentationClock_.invalidate();
        presentationGapMaxMs_ = -1;
        presentationGaps_ = {};
        receiveToPaint_ = {};
        paintTiming_ = {};
        receiveToSwap_ = {};
        receivedNs_ = awaitingReceivedNs_ = 0;
        frameId_ = awaitingFrameId_ = lastPresentedFrameId_ = supersededFrames_ = 0;
        motionTimer_.stop();
        pendingMotion_ = {};
        remoteCursor_ = {};
        if (hadFrame && controlEnabled_)
        {
            remoteButtons_ = Qt::NoButton;
            emit remoteInput({{"kind", "release"}});
        }
        unsetCursor();
    }
    else if (!hadFrame)
        updatePointerCursor(mapFromGlobal(QCursor::pos()));
    update();
}

void VideoSurface::setControlEnabled(bool enabled)
{
    if (!enabled)
    {
        motionTimer_.stop();
        pendingMotion_ = {};
        remoteCursor_ = {};
        remoteButtons_ = Qt::NoButton;
        if (controlEnabled_)
            emit remoteInput({{"kind", "release"}});
    }
    controlEnabled_ = enabled;
    setMouseTracking(enabled);
    setFocusPolicy(enabled ? Qt::StrongFocus : Qt::NoFocus);
    updatePointerCursor(mapFromGlobal(QCursor::pos()));
}

void VideoSurface::setRemoteCursor(const zjdesk::RemoteCursor &cursor)
{
    remoteCursor_ = cursor;
    update();
}

void VideoSurface::flushMotion()
{
    motionTimer_.stop();
    const auto input = pendingMotion_;
    pendingMotion_ = {};
    if (!input.isEmpty())
        emit remoteInput(input);
}

void VideoSurface::updatePointerCursor(const QPointF &position)
{
    QJsonObject input;
    if (pointerInput(position, input))
        setCursor(Qt::BlankCursor);
    else
        unsetCursor();
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
    updatePointerCursor(event->position());
    QJsonObject input{{"kind", "move"}};
    if (pointerInput(event->position(), input))
    {
        pendingMotion_ = input;
        if (!motionTimer_.isActive())
            motionTimer_.start();
    }
}

void VideoSurface::mousePressEvent(QMouseEvent *event)
{
    QJsonObject input{{"kind", "button"}, {"button", int(event->button())}, {"pressed", true}};
    if (pointerInput(event->position(), input))
    {
        setFocus(Qt::MouseFocusReason);
        flushMotion();
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
    flushMotion();
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
        flushMotion();
        emit remoteInput(input);
        event->accept();
    }
}

void VideoSurface::keyPressEvent(QKeyEvent *event)
{
    if (controlEnabled_)
    {
        flushMotion();
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
        flushMotion();
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
        motionTimer_.stop();
        pendingMotion_ = {};
        remoteButtons_ = Qt::NoButton;
        emit remoteInput({{"kind", "release"}});
    }
    QOpenGLWidget::focusOutEvent(event);
}
