#pragma once
#include <QString>
#include <QStringList>

namespace zjdesk
{
struct MediaEnvironment
{
    bool sdkLinked = false;
    bool ready = false;
    QString version;
    QStringList missingPlugins;
    QString description;
    static MediaEnvironment inspect();
};
} // namespace zjdesk
