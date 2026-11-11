#pragma once

#include <QImage>
#include <QPainter>
#include <array>
#include <optional>

namespace zjdesk::TestFrameMarker
{
// Large monochrome cells survive VP9 quantization and I420 chroma subsampling.
struct Value { quint32 epoch; quint32 sequence; };
inline constexpr quint32 Magic = 0x5a4a4c54;
inline constexpr int Cell = 8;
inline constexpr int Columns = 32;
inline constexpr int Rows = 4;

inline void draw(QImage &image, Value value)
{
    const std::array<quint32, Rows> words{Magic, value.epoch, value.sequence,
        Magic ^ value.epoch ^ value.sequence};
    QPainter painter(&image);
    for (int row = 0; row < Rows; ++row)
        for (int bit = 0; bit < Columns; ++bit)
            painter.fillRect(bit * Cell, row * Cell, Cell, Cell,
                words[row] & (quint32(1) << bit) ? Qt::white : Qt::black);
}

inline std::optional<Value> read(const QImage &image)
{
    if (image.width() < Columns * Cell || image.height() < Rows * Cell)
        return std::nullopt;
    std::array<quint32, Rows> words{};
    for (int row = 0; row < Rows; ++row)
        for (int bit = 0; bit < Columns; ++bit)
        {
            int luminance = 0;
            for (int y = 3; y <= 5; ++y)
                for (int x = 3; x <= 5; ++x)
                    luminance += qGray(image.pixel(bit * Cell + x, row * Cell + y));
            luminance /= 9;
            if (luminance > 80 && luminance < 175) return std::nullopt;
            if (luminance >= 175) words[row] |= quint32(1) << bit;
        }
    if (words[0] != Magic || !words[1] || !words[2]
        || words[3] != (Magic ^ words[1] ^ words[2])) return std::nullopt;
    return Value{words[1], words[2]};
}
} // namespace zjdesk::TestFrameMarker
