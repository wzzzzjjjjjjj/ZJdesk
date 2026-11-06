#pragma once

#include <QJsonObject>
#include <QRect>
#include <QSet>
#include <QImage>
#include <QPointF>

namespace zjdesk
{
struct RemoteCursor
{
    QImage image;
    QPoint hotspot;
    QPointF position;
    QSize screenSize;
    quint64 serial = 0;
    bool visible = false;
};

class InputInjector
{
public:
    ~InputInjector();
    bool available();
    bool inject(const QJsonObject &input);
    QString lastError() const { return lastError_; }
    void releaseAll();
    RemoteCursor cursor();
private:
    void *display_ = nullptr;
    QString lastError_;
    QRect bounds_;
    bool cursorAvailable_ = false;
    QSet<unsigned int> keys_;
    QSet<unsigned int> buttons_;
};
} // namespace zjdesk

Q_DECLARE_METATYPE(zjdesk::RemoteCursor)
