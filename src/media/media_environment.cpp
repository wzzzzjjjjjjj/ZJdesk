#include "media_environment.h"
#ifdef ZJDESK_HAS_GSTREAMER
#include <gst/gst.h>
#endif

namespace zjdesk
{
MediaEnvironment MediaEnvironment::inspect()
{
    MediaEnvironment result;
#ifdef ZJDESK_HAS_GSTREAMER
    result.sdkLinked = true;
    GError *error = nullptr;
    if (!gst_init_check(nullptr, nullptr, &error))
    {
        result.description = QString::fromUtf8(error ? error->message : "GStreamer initialization failed");
        if (error)
            g_error_free(error);
        return result;
    }
    guint major = 0, minor = 0, micro = 0, nano = 0;
    gst_version(&major, &minor, &micro, &nano);
    result.version = QString("%1.%2.%3").arg(major).arg(minor).arg(micro);
    for (const auto *name : {"webrtcbin", "vp8enc", "opusenc", "nicesrc", "dtlssrtpenc", "rtpbin"})
    {
        if (auto *factory = gst_element_factory_find(name))
            gst_object_unref(factory);
        else
            result.missingPlugins.append(QString::fromLatin1(name));
    }
    result.description = result.missingPlugins.isEmpty()
        ? QStringLiteral("媒体依赖就绪；当前版本尚未开启音视频")
        : QStringLiteral("缺少媒体插件：") + result.missingPlugins.join(", ");
#else
    result.description = QStringLiteral("当前构建未启用媒体依赖，音视频暂不可用");
#endif
    return result;
}
} // namespace zjdesk
