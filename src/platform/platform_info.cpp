#include "platform_info.h"
#include <QSysInfo>

namespace zjdesk
{
PlatformInfo PlatformInfo::current()
{
    return {QSysInfo::prettyProductName(), QSysInfo::buildCpuArchitecture(),
            qEnvironmentVariable("XDG_SESSION_TYPE", "desktop")};
}
} // namespace zjdesk
