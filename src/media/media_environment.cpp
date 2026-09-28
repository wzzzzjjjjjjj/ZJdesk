#include "media_environment.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
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
    const auto applicationDirectory = QCoreApplication::applicationDirPath();
    const auto deployedRoot = applicationDirectory + "/gstreamer";
    if (QDir(deployedRoot + "/lib/gstreamer-1.0").exists())
    {
        qputenv("PATH", (applicationDirectory + ";" + qEnvironmentVariable("PATH")).toLocal8Bit());
        qputenv("GST_PLUGIN_SYSTEM_PATH_1_0", (deployedRoot + "/lib/gstreamer-1.0").toUtf8());
        qputenv("GST_PLUGIN_PATH_1_0", "");
        qputenv("GST_PLUGIN_SCANNER", (deployedRoot + "/libexec/gstreamer-1.0/gst-plugin-scanner.exe").toUtf8());
        const auto cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QDir().mkpath(cache);
        qputenv("GST_REGISTRY_1_0", (cache + "/gst-registry.bin").toUtf8());
    }
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
    for (const auto *name : {"videotestsrc", "audiotestsrc", "appsink", "videoconvert", "videoscale",
                           "audioconvert", "audioresample", "autoaudiosink",
                           "webrtcbin", "vp8enc", "opusenc", "nicesrc", "dtlssrtpenc", "rtpbin"})
    {
        if (auto *factory = gst_element_factory_find(name))
            gst_object_unref(factory);
        else
            result.missingPlugins.append(QString::fromLatin1(name));
    }
    result.ready = result.missingPlugins.isEmpty();
    result.description = result.ready
        ? QStringLiteral("媒体依赖就绪；支持本地预览与设备枚举，尚无双人通话")
        : QStringLiteral("缺少媒体插件：") + result.missingPlugins.join(", ");
#else
    result.description = QStringLiteral("当前构建未启用媒体依赖，音视频暂不可用");
#endif
    return result;
}
} // namespace zjdesk
