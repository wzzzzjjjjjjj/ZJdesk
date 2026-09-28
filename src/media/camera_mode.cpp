#include "camera_mode.h"
#ifdef ZJDESK_HAS_GSTREAMER
#include <gst/gst.h>
#endif

namespace zjdesk
{
QString CameraMode::description() const
{
    if (size.isEmpty())
        return QStringLiteral("未发现支持的采集模式");
    return QStringLiteral("%1 × %2 · %3 fps").arg(size.width()).arg(size.height())
        .arg(double(fpsNumerator) / fpsDenominator, 0, 'f', 2);
}

CameraMode maximumCameraMode(const QString &advertisedCaps)
{
    CameraMode best;
#ifdef ZJDESK_HAS_GSTREAMER
    auto *caps = gst_caps_from_string(advertisedCaps.toUtf8().constData());
    if (!caps)
        return best;
    for (guint i = 0; i < gst_caps_get_size(caps); ++i)
    {
        const auto *source = gst_caps_get_structure(caps, i);
        const bool jpeg = gst_structure_has_name(source, "image/jpeg");
        if ((!jpeg && !gst_structure_has_name(source, "video/x-raw"))
            || !gst_structure_has_field(source, "width")
            || !gst_structure_has_field(source, "height")
            || !gst_structure_has_field(source, "framerate"))
            continue;
        auto *candidate = gst_caps_copy_nth(caps, i);
        auto *structure = gst_caps_get_structure(candidate, 0);
        gst_structure_fixate_field_nearest_int(structure, "width", G_MAXINT);
        gst_structure_fixate_field_nearest_int(structure, "height", G_MAXINT);
        gst_structure_fixate_field_nearest_fraction(structure, "framerate", 1000000, 1);
        candidate = gst_caps_fixate(candidate);
        structure = gst_caps_get_structure(candidate, 0);
        int width = 0, height = 0, numerator = 0, denominator = 1;
        if (gst_structure_get_int(structure, "width", &width)
            && gst_structure_get_int(structure, "height", &height)
            && gst_structure_get_fraction(structure, "framerate", &numerator, &denominator)
            && width > 0 && height > 0 && numerator > 0 && denominator > 0)
        {
            const qint64 pixels = qint64(width) * height;
            const qint64 previous = qint64(best.size.width()) * best.size.height();
            if (best.caps.isEmpty() || pixels > previous
                || (pixels == previous && qint64(numerator) * best.fpsDenominator
                    > qint64(best.fpsNumerator) * denominator))
            {
                auto *text = gst_caps_to_string(candidate);
                best = {{width, height}, numerator, denominator, QString::fromUtf8(text), jpeg};
                g_free(text);
            }
        }
        gst_caps_unref(candidate);
    }
    gst_caps_unref(caps);
#else
    Q_UNUSED(advertisedCaps)
#endif
    return best;
}
} // namespace zjdesk
