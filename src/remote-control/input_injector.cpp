#include "input_injector.h"
#include <QGuiApplication>
#include <QScreen>
#include <cmath>
#ifdef ZJDESK_HAS_XTEST
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xfixes.h>
#endif

namespace zjdesk
{
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
#ifdef ZJDESK_HAS_XTEST
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
#ifdef ZJDESK_HAS_XTEST
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
#ifdef ZJDESK_HAS_XTEST
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
}
} // namespace zjdesk
