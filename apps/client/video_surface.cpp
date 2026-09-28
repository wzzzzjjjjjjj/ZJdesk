#include "video_surface.h"
#include <QPainter>

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
