#pragma once

#include <QtGlobal>

// Native event translation is separate from SendInput so validation never injects
// test keystrokes into the user's active desktop.
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <QJsonObject>
#include <QImage>
#include <QList>
#include <QRect>

namespace zjdesk
{
QImage windowsCursorImage(HCURSOR cursor, const ICONINFO &icon);
INPUT windowsKeyEvent(unsigned int key, bool pressed);
QList<INPUT> windowsInputEvents(const QJsonObject &input, const QRect &screen, const QRect &desktop);
} // namespace zjdesk
#endif
