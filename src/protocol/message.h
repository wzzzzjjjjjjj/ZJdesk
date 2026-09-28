#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <optional>

namespace zjdesk::protocol
{
inline constexpr int Version = 1;
inline constexpr int MaxMessageBytes = 65536;

struct Message
{
    QString type;
    QJsonObject payload;
};

QString encode(const Message &message);
std::optional<Message> decode(const QString &text, QString *error = nullptr);
bool isRoomIdValid(const QString &roomId);
bool isPeerNameValid(const QString &name);
bool isRelayType(const QString &type);
} // namespace zjdesk::protocol

Q_DECLARE_METATYPE(zjdesk::protocol::Message)
