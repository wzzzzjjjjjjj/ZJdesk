#include "annotation_layer.h"
#include "protocol/annotation.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <QUuid>
#include <cmath>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

AnnotationLayer::AnnotationLayer(QWidget *parent, bool desktop) : QWidget(parent), desktop_(desktop)
{
    setObjectName(desktop ? "desktopAnnotations" : "sharedScreenAnnotations");
    setAttribute(Qt::WA_NoSystemBackground);
    setTool({});
    if (desktop)
    {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
            | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
#ifdef Q_OS_WIN
        // Keep the transparent desktop markings out of the screen-capture stream.
        SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()), 0x00000011);
#endif
    }
    publish_.setInterval(40);
    publish_.setSingleShot(true);
    connect(&publish_, &QTimer::timeout, this, [this] {
        if (!draft_.isEmpty()) emit strokeReady(draft_);
    });
}

void AnnotationLayer::setAnnotations(const QJsonArray &strokes, const QJsonObject &author)
{
    strokes_ = strokes;
    author_ = author;
    if (!drawing_ && !draft_.isEmpty())
        for (const auto &stroke : strokes_)
            if (stroke.toObject().value("strokeId") == draft_.value("strokeId")
                && stroke.toObject().value("points") == draft_.value("points")) { draft_ = {}; break; }
    update();
}

void AnnotationLayer::cancelDraft()
{
    publish_.stop();
    drawing_ = false;
    draft_ = {};
    update();
}

void AnnotationLayer::setTool(const QString &tool)
{
    const auto next = tool == "pen" || tool == "circle" || tool == "arrow" ? tool : QString();
    setAttribute(Qt::WA_TransparentForMouseEvents, next.isEmpty());
    if (tool_ != next)
    {
        cancelDraft();
        if (desktop_ && tool_.isEmpty() != next.isEmpty())
        {
            const bool wasVisible = isVisible();
            setWindowFlag(Qt::WindowTransparentForInput, next.isEmpty());
#ifdef Q_OS_WIN
            SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()), 0x00000011);
#endif
            if (wasVisible) show();
        }
    }
    tool_ = next;
    setCursor(tool_.isEmpty() ? Qt::ArrowCursor : Qt::CrossCursor);
}

QPointF AnnotationLayer::normalized(const QPointF &point) const
{
    return {qBound(0.0, point.x() / qMax(1, width()), 1.0),
        qBound(0.0, point.y() / qMax(1, height()), 1.0)};
}

void AnnotationLayer::appendPoint(const QPointF &point)
{
    auto points = draft_.value("points").toArray();
    const auto position = normalized(point);
    const QJsonArray next{position.x(), position.y()};
    if (tool_ == "pen")
    {
        if (!points.isEmpty() && points.last() == next) return;
        if (points.size() >= zjdesk::protocol::MaximumAnnotationPoints)
        {
            emit strokeReady(draft_);
            draft_.insert("strokeId", QUuid::createUuid().toString(QUuid::Id128));
            points = {points.last()};
        }
        points.append(next);
    }
    else points.replace(1, next);
    draft_.insert("points", points);
    update();
    if (!publish_.isActive()) publish_.start();
}

void AnnotationLayer::mousePressEvent(QMouseEvent *event)
{
    if (desktop_ && !tool_.isEmpty() && event->button() == Qt::RightButton)
    {
        emit drawingExitRequested();
        event->accept();
        return;
    }
    if (tool_.isEmpty() || event->button() != Qt::LeftButton) return;
    cancelDraft();
    drawing_ = true;
    draft_ = author_;
    draft_.insert("strokeId", QUuid::createUuid().toString(QUuid::Id128));
    draft_.insert("tool", tool_);
    const auto point = normalized(event->position());
    QJsonArray points;
    points.append(QJsonArray{point.x(), point.y()});
    if (tool_ != "pen") points.append(points.first());
    draft_.insert("points", points);
    publish_.start();
    update();
    event->accept();
}

void AnnotationLayer::mouseMoveEvent(QMouseEvent *event)
{
    if (!drawing_) return;
    appendPoint(event->position());
    event->accept();
}

void AnnotationLayer::mouseReleaseEvent(QMouseEvent *event)
{
    if (!drawing_ || event->button() != Qt::LeftButton) return;
    appendPoint(event->position());
    publish_.stop();
    drawing_ = false;
    emit strokeReady(draft_);
    const auto id = draft_.value("strokeId");
    QTimer::singleShot(2000, this, [this, id] {
        if (!drawing_ && draft_.value("strokeId") == id) { draft_ = {}; update(); }
    });
    event->accept();
}

void AnnotationLayer::hideEvent(QHideEvent *event)
{
    cancelDraft();
    QWidget::hideEvent(event);
}

void AnnotationLayer::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    if (desktop_)
    {
        // Windows ignores fully transparent pixels during native mouse hit testing.
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), tool_.isEmpty() ? QColor(0, 0, 0, 0) : QColor(0, 0, 0, 1));
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    painter.setRenderHint(QPainter::Antialiasing);
    // X11 already captures the owner's desktop layer; avoid drawing it twice.
    auto strokes = embedded_ ? QJsonArray() : strokes_;
    if (!draft_.isEmpty())
    {
        for (qsizetype i = strokes.size(); i-- > 0;)
            if (strokes.at(i).toObject().value("strokeId") == draft_.value("strokeId")) strokes.removeAt(i);
        strokes.append(draft_);
    }
    for (const auto &value : strokes)
    {
        const auto stroke = value.toObject();
        if (!zjdesk::protocol::isAnnotationStrokeValid(stroke)) continue;
        const auto points = stroke.value("points").toArray();
        const auto position = [this](const QJsonValue &value) {
            const auto pair = value.toArray();
            return QPointF(pair.at(0).toDouble() * width(), pair.at(1).toDouble() * height());
        };
        const auto first = position(points.first()), last = position(points.last());
        const QColor color(stroke.value("color").toString());
        painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const auto tool = stroke.value("tool").toString();
        if (tool == "circle") painter.drawEllipse(QRectF(first, last).normalized());
        else if (tool == "arrow")
        {
            painter.drawLine(first, last);
            const double angle = std::atan2(last.y() - first.y(), last.x() - first.x());
            for (double offset : {-0.5, 0.5})
                painter.drawLine(last, last - QPointF(std::cos(angle + offset), std::sin(angle + offset)) * 14);
        }
        else
        {
            QPainterPath path(first);
            for (qsizetype i = 1; i < points.size(); ++i) path.lineTo(position(points.at(i)));
            if (points.size() == 1) painter.drawPoint(first);
            else painter.drawPath(path);
        }
    }
    // The X11 desktop layer supplies the legend along with the embedded markings.
    if (embedded_) return;
    QStringList authors;
    int row = 0;
    for (const auto &value : strokes)
    {
        const auto stroke = value.toObject();
        if (!zjdesk::protocol::isAnnotationStrokeValid(stroke)) continue;
        const auto id = stroke.value("authorId").toString();
        if (authors.contains(id)) continue;
        authors.append(id);
        const auto name = painter.fontMetrics().elidedText(stroke.value("name").toString(), Qt::ElideRight,
            qMax(0, qMin(120, width() - 48)));
        const int labelWidth = painter.fontMetrics().horizontalAdvance(name) + 30;
        // Leave room for the shared preview's expand button above the legend.
        const QRect label(qMax(0, width() - labelWidth - 8), 44 + row++ * 26, labelWidth, 22);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(28, 34, 44, 220));
        painter.drawRoundedRect(label, 4, 4);
        painter.setBrush(QColor(stroke.value("color").toString()));
        painter.drawEllipse(QPointF(label.left() + 10, label.center().y()), 4, 4);
        painter.setPen(Qt::white);
        painter.drawText(label.adjusted(20, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, name);
    }
}
