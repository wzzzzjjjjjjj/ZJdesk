#pragma once

#include <QJsonObject>
#include <QRect>
#include <QSet>

namespace zjdesk
{
class InputInjector
{
public:
    ~InputInjector();
    bool available();
    bool inject(const QJsonObject &input);
    void releaseAll();
private:
    void *display_ = nullptr;
    QRect bounds_;
    QSet<unsigned int> keys_;
    QSet<unsigned int> buttons_;
};
} // namespace zjdesk
