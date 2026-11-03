#include "input_injector.h"
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <cmath>
#include "input_injector_win_p.h"
#ifdef ZJDESK_HAS_XTEST
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xfixes.h>
#endif

namespace zjdesk
{
#ifdef Q_OS_WIN
QImage windowsCursorImage(HCURSOR cursor, const ICONINFO &icon)
{
    BITMAP shape{};
    if (!GetObject(icon.hbmColor ? icon.hbmColor : icon.hbmMask, sizeof(shape), &shape))
        return {};
    const int width = shape.bmWidth;
    const int height = icon.hbmColor ? shape.bmHeight : shape.bmHeight / 2;
    if (width <= 0 || height <= 0 || width > 128 || height > 128)
        return {};
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    void *pixels = nullptr;
    const auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap)
        return {};
    const auto dc = CreateCompatibleDC(nullptr);
    if (!dc)
    {
        DeleteObject(bitmap);
        return {};
    }
    const auto previous = SelectObject(dc, bitmap);
    // Render onto both backgrounds to recover transparency for color and
    // monochrome cursors; QImage::fromHICON can fail or lose their AND mask.
    const auto render = [&](quint32 background) {
        std::fill_n(static_cast<quint32 *>(pixels), width * height, background);
        if (!DrawIconEx(dc, 0, 0, cursor, width, height, 0, nullptr, DI_NORMAL))
            return QImage();
        GdiFlush();
        return QImage(static_cast<uchar *>(pixels), width, height, QImage::Format_RGB32).copy();
    };
    const auto black = render(0);
    const auto white = render(0x00ffffff);
    SelectObject(dc, previous);
    DeleteDC(dc);
    DeleteObject(bitmap);
    if (black.isNull() || white.isNull())
        return {};
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    QList<QPoint> inverted;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto b = black.pixel(x, y), w = white.pixel(x, y);
            if (qRed(w) < qRed(b) || qGreen(w) < qGreen(b) || qBlue(w) < qBlue(b))
            {
                image.setPixel(x, y, qRgb(0, 0, 0));
                inverted.append(QPoint(x, y));
                continue;
            }
            const int alpha = 255 - qBound(0, std::max({qRed(w) - qRed(b), qGreen(w) - qGreen(b), qBlue(w) - qBlue(b)}), 255);
            image.setPixel(x, y, qRgba(qMin(qRed(b), alpha), qMin(qGreen(b), alpha), qMin(qBlue(b), alpha), alpha));
        }
    // RGBA cannot invert the background. Outline inverted pixels so text
    // cursors remain visible against both light and dark remote content.
    for (const auto &point : inverted)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                const auto neighbor = point + QPoint(dx, dy);
                if (image.rect().contains(neighbor) && qAlpha(image.pixel(neighbor)) == 0)
                    image.setPixel(neighbor, qRgb(255, 255, 255));
            }
    return image;
}

INPUT windowsKeyEvent(unsigned int key, bool pressed)
{
    INPUT event{};
    event.type = INPUT_KEYBOARD;
    event.ki.wVk = WORD(key);
    const auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
    event.ki.wScan = WORD(scan & 0xff);
    // Pause/PrintScreen have special sequences; let Windows synthesize them from VK.
    if (scan && key != VK_PAUSE && key != VK_SNAPSHOT)
    {
        // Some layouts map navigation VKs to the numpad scan without the E0 prefix.
        const bool extended = (scan & 0xff00) == 0xe000 || (key >= VK_PRIOR && key <= VK_DOWN)
            || key == VK_INSERT || key == VK_DELETE || key == VK_NUMLOCK;
        event.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    }
    if (!pressed)
        event.ki.dwFlags |= KEYEVENTF_KEYUP;
    return event;
}

static unsigned int windowsVirtualKey(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return 'A' + key - Qt::Key_A;
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return '0' + key - Qt::Key_0;
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return VK_F1 + key - Qt::Key_F1;
    switch (key)
    {
    case Qt::Key_Shift: return VK_LSHIFT;
    case Qt::Key_Control: return VK_LCONTROL;
    case Qt::Key_Alt: return VK_LMENU;
    case Qt::Key_Meta: return VK_LWIN;
    case Qt::Key_Return: case Qt::Key_Enter: return VK_RETURN;
    case Qt::Key_Tab: case Qt::Key_Backtab: return VK_TAB;
    case Qt::Key_Escape: return VK_ESCAPE;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Delete: return VK_DELETE;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_CapsLock: return VK_CAPITAL;
    case Qt::Key_NumLock: return VK_NUMLOCK;
    case Qt::Key_ScrollLock: return VK_SCROLL;
    case Qt::Key_Print: return VK_SNAPSHOT;
    case Qt::Key_Pause: return VK_PAUSE;
    default: break;
    }
    if (key >= 0x20 && key <= 0x7e)
    {
        const SHORT code = VkKeyScanW(WCHAR(key));
        if (code != -1)
            return code & 0xff; // Modifiers arrive as their own press/release events.
    }
    return 0;
}

QList<INPUT> windowsInputEvents(const QJsonObject &input, const QRect &screen, const QRect &desktop)
{
    const auto kind = input.value("kind").toString();
    if (kind == "key")
    {
        const auto key = windowsVirtualKey(input.value("key").toInt());
        return key && input.value("pressed").isBool()
            ? QList<INPUT>{windowsKeyEvent(key, input.value("pressed").toBool())} : QList<INPUT>{};
    }
    if ((kind != "move" && kind != "button" && kind != "wheel") || screen.isEmpty() || desktop.isEmpty())
        return {};
    const double x = input.value("x").toDouble(-1), y = input.value("y").toDouble(-1);
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1)
        return {};
    INPUT move{};
    move.type = INPUT_MOUSE;
    move.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    move.mi.dx = qRound(double(screen.x() + qRound(x * (screen.width() - 1)) - desktop.x())
        * 65535 / qMax(1, desktop.width() - 1));
    move.mi.dy = qRound(double(screen.y() + qRound(y * (screen.height() - 1)) - desktop.y())
        * 65535 / qMax(1, desktop.height() - 1));
    if (kind == "move")
        return {move};
    INPUT event{};
    event.type = INPUT_MOUSE;
    if (kind == "wheel")
    {
        const int steps = input.value("steps").toInt();
        if (steps == 0 || steps < -10 || steps > 10)
            return {};
        event.mi.dwFlags = MOUSEEVENTF_WHEEL;
        event.mi.mouseData = DWORD(steps * WHEEL_DELTA);
    }
    else
    {
        if (!input.value("pressed").isBool())
            return {};
        const bool pressed = input.value("pressed").toBool();
        switch (input.value("button").toInt())
        {
        case Qt::LeftButton: event.mi.dwFlags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
        case Qt::RightButton: event.mi.dwFlags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
        case Qt::MiddleButton: event.mi.dwFlags = pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
        default: return {};
        }
    }
    return {move, event};
}
#endif

InputInjector::~InputInjector()
{
    releaseAll();
#ifdef ZJDESK_HAS_XTEST
    if (display_)
        XCloseDisplay(static_cast<Display *>(display_));
#endif
}

bool InputInjector::available()
{
#ifdef Q_OS_WIN
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        return false;
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor))
        return false;
    bounds_ = QRect(monitor.rcMonitor.left, monitor.rcMonitor.top,
        monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top);
    return !bounds_.isEmpty();
#elif defined(ZJDESK_HAS_XTEST)
    if (display_)
        return true;
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())
        || qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland")
        return false;
    auto *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return false;
    auto *display = XOpenDisplay(nullptr);
    int event, error, major, minor;
    if (!display || !XTestQueryExtension(display, &event, &error, &major, &minor))
    {
        if (display)
            XCloseDisplay(display);
        return false;
    }
    display_ = display;
    cursorAvailable_ = XFixesQueryExtension(display, &event, &error);
    const auto ratio = screen->devicePixelRatio();
    bounds_ = QRect(screen->geometry().topLeft() * ratio, screen->size() * ratio);
    return true;
#else
    return false;
#endif
}

RemoteCursor InputInjector::cursor()
{
    RemoteCursor result;
#ifdef Q_OS_WIN
    if (!available())
        return result;
    CURSORINFO cursor{sizeof(CURSORINFO)};
    ICONINFO icon{};
    if (!GetCursorInfo(&cursor) || !cursor.hCursor || !GetIconInfo(cursor.hCursor, &icon))
        return result;
    result.image = windowsCursorImage(cursor.hCursor, icon);
    result.hotspot = QPoint(int(icon.xHotspot), int(icon.yHotspot));
    if (icon.hbmColor) DeleteObject(icon.hbmColor);
    if (icon.hbmMask) DeleteObject(icon.hbmMask);
    if (result.image.isNull() || result.image.width() > 128 || result.image.height() > 128)
        return {};
    result.position = QPointF(double(cursor.ptScreenPos.x - bounds_.x()) / qMax(1, bounds_.width() - 1),
        double(cursor.ptScreenPos.y - bounds_.y()) / qMax(1, bounds_.height() - 1));
    result.screenSize = bounds_.size();
    result.serial = quint64(reinterpret_cast<quintptr>(cursor.hCursor));
    result.visible = (cursor.flags & CURSOR_SHOWING) && bounds_.contains(QPoint(cursor.ptScreenPos.x, cursor.ptScreenPos.y));
#elif defined(ZJDESK_HAS_XTEST)
    if (!available() || !cursorAvailable_)
        return result;
    auto *cursor = XFixesGetCursorImage(static_cast<Display *>(display_));
    if (!cursor)
        return result;
    if (cursor->width > 0 && cursor->width <= 128 && cursor->height > 0 && cursor->height <= 128)
    {
        result.image = QImage(cursor->width, cursor->height, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < cursor->height; ++y)
            for (int x = 0; x < cursor->width; ++x)
                reinterpret_cast<QRgb *>(result.image.scanLine(y))[x] = QRgb(cursor->pixels[y * cursor->width + x]);
        result.hotspot = QPoint(cursor->xhot, cursor->yhot);
        result.position = QPointF(double(cursor->x - bounds_.x()) / qMax(1, bounds_.width() - 1),
            double(cursor->y - bounds_.y()) / qMax(1, bounds_.height() - 1));
        result.screenSize = bounds_.size();
        result.serial = cursor->cursor_serial;
        result.visible = bounds_.contains(QPoint(cursor->x, cursor->y));
    }
    XFree(cursor);
#endif
    return result;
}

bool InputInjector::inject(const QJsonObject &input)
{
#ifdef Q_OS_WIN
    if (input.value("kind").toString() == "release")
    {
        releaseAll();
        return keys_.isEmpty() && buttons_.isEmpty();
    }
    if (!available())
        return false;
    const QRect desktop(GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    auto events = windowsInputEvents(input, bounds_, desktop);
    if (events.isEmpty())
        return false;
    const auto sent = SendInput(UINT(events.size()), events.data(), sizeof(INPUT));
    // Track only events Windows accepted, including a partial batch.
    for (UINT i = 0; i < sent; ++i)
    {
        const auto &event = events.at(i);
        if (event.type == INPUT_KEYBOARD)
        {
            if (event.ki.dwFlags & KEYEVENTF_KEYUP) keys_.remove(event.ki.wVk);
            else keys_.insert(event.ki.wVk);
        }
        else if (event.mi.dwFlags & (MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_MIDDLEDOWN))
            buttons_.insert(event.mi.dwFlags);
        else if (event.mi.dwFlags & (MOUSEEVENTF_LEFTUP | MOUSEEVENTF_RIGHTUP | MOUSEEVENTF_MIDDLEUP))
            buttons_.remove(event.mi.dwFlags >> 1);
    }
    return sent == UINT(events.size());
#elif defined(ZJDESK_HAS_XTEST)
    if (!available())
        return false;
    auto *display = static_cast<Display *>(display_);
    const auto kind = input.value("kind").toString();
    if (kind == "release")
    {
        releaseAll();
        return true;
    }
    if (kind == "move" || kind == "button" || kind == "wheel")
    {
        const double x = input.value("x").toDouble(-1), y = input.value("y").toDouble(-1);
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1)
            return false;
        XTestFakeMotionEvent(display, -1, bounds_.x() + qRound(x * (bounds_.width() - 1)),
            bounds_.y() + qRound(y * (bounds_.height() - 1)), CurrentTime);
    }
    if (kind == "button")
    {
        const int button = input.value("button").toInt();
        const unsigned int code = button == Qt::LeftButton ? 1 : button == Qt::RightButton ? 3
            : button == Qt::MiddleButton ? 2 : 0;
        if (!code || !input.value("pressed").isBool())
            return false;
        const bool pressed = input.value("pressed").toBool();
        XTestFakeButtonEvent(display, code, pressed, CurrentTime);
        if (pressed)
            buttons_.insert(code);
        else
            buttons_.remove(code);
    }
    else if (kind == "wheel")
    {
        const int steps = input.value("steps").toInt();
        if (steps == 0 || qAbs(steps) > 10)
            return false;
        for (int i = 0; i < qAbs(steps); ++i)
        {
            XTestFakeButtonEvent(display, steps > 0 ? 4 : 5, True, CurrentTime);
            XTestFakeButtonEvent(display, steps > 0 ? 4 : 5, False, CurrentTime);
        }
    }
    else if (kind == "key")
    {
        const int key = input.value("key").toInt();
        if (!input.value("pressed").isBool())
            return false;
        KeySym symbol = NoSymbol;
        if (key >= Qt::Key_A && key <= Qt::Key_Z)
            symbol = XK_a + key - Qt::Key_A;
        else if (key >= 0x20 && key <= 0x7e)
            symbol = key;
        else if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
            symbol = XK_F1 + key - Qt::Key_F1;
        else
            switch (key)
            {
            case Qt::Key_Shift: symbol = XK_Shift_L; break;
            case Qt::Key_Control: symbol = XK_Control_L; break;
            case Qt::Key_Alt: symbol = XK_Alt_L; break;
            case Qt::Key_Meta: symbol = XK_Super_L; break;
            case Qt::Key_Return: case Qt::Key_Enter: symbol = XK_Return; break;
            case Qt::Key_Tab: case Qt::Key_Backtab: symbol = XK_Tab; break;
            case Qt::Key_Escape: symbol = XK_Escape; break;
            case Qt::Key_Backspace: symbol = XK_BackSpace; break;
            case Qt::Key_Delete: symbol = XK_Delete; break;
            case Qt::Key_Insert: symbol = XK_Insert; break;
            case Qt::Key_Left: symbol = XK_Left; break;
            case Qt::Key_Right: symbol = XK_Right; break;
            case Qt::Key_Up: symbol = XK_Up; break;
            case Qt::Key_Down: symbol = XK_Down; break;
            case Qt::Key_Home: symbol = XK_Home; break;
            case Qt::Key_End: symbol = XK_End; break;
            case Qt::Key_PageUp: symbol = XK_Page_Up; break;
            case Qt::Key_PageDown: symbol = XK_Page_Down; break;
            case Qt::Key_CapsLock: symbol = XK_Caps_Lock; break;
            default: break;
            }
        const unsigned int code = symbol == NoSymbol ? 0 : XKeysymToKeycode(display, symbol);
        if (!code)
            return false;
        const bool pressed = input.value("pressed").toBool();
        XTestFakeKeyEvent(display, code, pressed, CurrentTime);
        if (pressed)
            keys_.insert(code);
        else
            keys_.remove(code);
    }
    else if (kind != "move")
        return false;
    XFlush(display);
    return true;
#else
    Q_UNUSED(input)
    return false;
#endif
}

void InputInjector::releaseAll()
{
#ifdef Q_OS_WIN
    for (unsigned int key : keys_.values())
    {
        auto event = windowsKeyEvent(key, false);
        if (SendInput(1, &event, sizeof(INPUT)) == 1)
            keys_.remove(key);
    }
    for (unsigned int button : buttons_.values())
    {
        INPUT event{};
        event.type = INPUT_MOUSE;
        event.mi.dwFlags = button << 1;
        if (SendInput(1, &event, sizeof(INPUT)) == 1)
            buttons_.remove(button);
    }
#else
#ifdef ZJDESK_HAS_XTEST
    if (display_)
    {
        auto *display = static_cast<Display *>(display_);
        for (unsigned int code : keys_)
            XTestFakeKeyEvent(display, code, False, CurrentTime);
        for (unsigned int code : buttons_)
            XTestFakeButtonEvent(display, code, False, CurrentTime);
        XFlush(display);
    }
#endif
    keys_.clear();
    buttons_.clear();
#endif
}
} // namespace zjdesk
