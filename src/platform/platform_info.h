#pragma once
#include <QString>

namespace zjdesk
{
struct PlatformInfo
{
    QString system;
    QString architecture;
    QString desktopSession;
    static PlatformInfo current();
};
} // namespace zjdesk
