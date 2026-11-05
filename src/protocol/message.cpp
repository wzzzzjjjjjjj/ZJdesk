#include "message.h"
#include <QJsonDocument>
#include <QRegularExpression>

namespace zjdesk::protocol
{
QString encode(const Message &message)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"version", Version},
        {"type", message.type}, {"payload", message.payload}}).toJson(QJsonDocument::Compact));
}

std::optional<Message> decode(const QString &text, QString *error)
{
    const auto reject = [error](const QString &reason) -> std::optional<Message> {
        if (error)
            *error = reason;
        return std::nullopt;
    };
    const QByteArray bytes = text.toUtf8();
    if (bytes.size() > MaxMessageBytes)
        return reject("message_too_large");
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return reject("invalid_json");
    const auto object = document.object();
    if (object.value("version").toDouble(-1) != Version)
        return reject("unsupported_version");
    const auto type = object.value("type").toString();
    if (type.isEmpty() || type.size() > 32 || !object.value("payload").isObject())
        return reject("invalid_envelope");
    return Message{type, object.value("payload").toObject()};
}

bool isRoomIdValid(const QString &roomId)
{
    static const QRegularExpression pattern("\\A[A-Za-z0-9_-]{1,32}\\z");
    return pattern.match(roomId).hasMatch();
}

bool isPeerNameValid(const QString &name)
{
    return !name.trimmed().isEmpty() && name.size() <= 40
           && !name.contains(QChar::Null) && !name.contains('\n') && !name.contains('\r');
}

bool isRelayType(const QString &type)
{
    return type == "offer" || type == "answer" || type == "ice" || type == "share" || type == "control"
        || type == "conference";
}

bool isRemoteCodeValid(const QString &code)
{
    static const QRegularExpression pattern("\\A[0-9]{9}\\z");
    return pattern.match(code).hasMatch();
}
bool isRemoteIdentityValid(const QString &identity)
{
    static const QRegularExpression pattern("\\A[0-9a-f]{32}\\z");
    return pattern.match(identity).hasMatch();
}
} // namespace zjdesk::protocol
