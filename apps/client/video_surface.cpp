#include "video_surface.h"
#include <QPainter>
#include <QPainterPath>
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
    const bool showFrame = !frame_.isNull() && !(participantTile_ && frameHidden_);
    const bool paintingNewFrame = pendingFrame_ && showFrame;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (participantTile_)
    {
        painter.fillRect(rect(), QColor("#ffffff"));
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 12, 12);
        painter.setClipPath(clip);
    }
    if (showFrame)
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
        if (participantTile_)
        {
            const QRect captionRect(12, height() - 34, width() - 24, 24);
            QLinearGradient shade(0, height() - 52, 0, height());
            shade.setColorAt(0, Qt::transparent);
            shade.setColorAt(1, QColor(9, 21, 35, 210));
            painter.fillRect(QRect(0, height() - 52, width(), 52), shade);
            painter.drawText(captionRect, Qt::AlignLeft | Qt::AlignVCenter,
                painter.fontMetrics().elidedText(frameCaption_, Qt::ElideRight, captionRect.width()));
        }
        else
            painter.drawText(QRect(16, 12, width() - 32, 24), Qt::AlignLeft, frameCaption_);
        painter.end();
        if (paintingNewFrame)
            paintTiming_.record((zjdesk::FrameTiming::nowNs() - paintStartedNs) / 1e6);
        return;
    }
    if (participantTile_)
    {
        painter.fillRect(rect(), QColor("#eef3f6"));
        const qreal radius = qMin(30.0, height() / 5.0);
        const QPointF center(width() / 2.0, height() / 2.0 - 17);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#d2e9e7"));
        painter.drawEllipse(center, radius, radius);
        auto font = painter.font();
        font.setPixelSize(int(radius));
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor("#197e7d"));
        painter.drawText(QRectF(center.x() - radius, center.y() - radius, radius * 2, radius * 2),
            Qt::AlignCenter, name_.trimmed().left(1).toUpper());
        font.setPointSize(10);
        painter.setFont(font);
        const QRect nameRect(12, int(center.y() + radius) + 6, width() - 24, 22);
        painter.setPen(QColor("#24364e"));
        painter.drawText(nameRect, Qt::AlignCenter,
            painter.fontMetrics().elidedText(name_, Qt::ElideRight, nameRect.width()));
        font.setPointSize(8);
        font.setBold(false);
        painter.setFont(font);
        painter.setPen(QColor("#718397"));
        painter.drawText(nameRect.translated(0, 22), Qt::AlignCenter,
            painter.fontMetrics().elidedText(caption_, Qt::ElideRight, nameRect.width()));
        return;
    }
    QLinearGradient background(0, 0, width(), height());
    background.setColorAt(0, QColor("#122333"));
    background.setColorAt(1, QColor("#1a3845"));
    painter.fillRect(rect(), background);
    const bool compact = height() < 240;
    const QPointF center(width() / 2.0, height() / 2.0 - (compact ? 32 : 38));
    const qreal radius = compact ? qMin(30.0, height() / 6.0) : 52;
    QRadialGradient glow(center, radius * 4);
    glow.setColorAt(0, QColor(73, 174, 157, 32));
    glow.setColorAt(1, QColor(73, 174, 157, 0));
    painter.fillRect(rect(), glow);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(130, 190, 185, 17), 1));
    painter.drawEllipse(center, radius * 2.8, radius * 2.8);
    painter.drawEllipse(center, radius * 1.9, radius * 1.9);
    painter.save();
    painter.translate(center);
    painter.scale(radius / 52, radius / 52);
    painter.setPen(QPen(QColor("#3c6974"), 1));
    painter.setBrush(QColor("#1c3b49"));
    painter.drawRoundedRect(QRectF(-52, -42, 104, 84), 20, 20);
    painter.setPen(QPen(QColor("#93d6c5"), 2));
    painter.setBrush(QColor("#254d58"));
    painter.drawRoundedRect(QRectF(-30, -23, 60, 39), 5, 5);
    painter.drawLine(QPointF(0, 17), QPointF(0, 25));
    painter.drawLine(QPointF(-12, 25), QPointF(12, 25));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#93d6c5"));
    painter.drawEllipse(QPointF(40, -30), 3, 3);
    painter.restore();
    auto font = painter.font();
    font.setPointSize(compact ? 11 : 14);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(QColor("#e3f2f4"));
    const QRect titleRect(24, int(center.y() + radius) + 12, width() - 48, 28);
    painter.drawText(titleRect, Qt::AlignCenter,
        painter.fontMetrics().elidedText(name_, Qt::ElideRight, titleRect.width()));
    font.setPointSize(9);
    font.setBold(false);
    painter.setFont(font);
    painter.setPen(QColor("#8eaeba"));
    painter.drawText(QRect(24, titleRect.bottom() + 6, width() - 48, compact ? 32 : 44),
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
    if (hadFrame != !frame.isNull()) emit frameAvailabilityChanged();
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

bool VideoSurface::event(QEvent *event)
{
    if (controlEnabled_ && hasFocus()
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease))
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab)
        {
            if (event->type() == QEvent::KeyPress)
                keyPressEvent(keyEvent);
            else
                keyReleaseEvent(keyEvent);
            return true;
        }
    }
    return QOpenGLWidget::event(event);
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
