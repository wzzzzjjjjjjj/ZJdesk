#include "room_server.h"
#include <QJsonArray>
#include <QUuid>
#include <QWebSocket>

namespace zjdesk
{
RoomServer::RoomServer(QObject *parent)
    : QObject(parent), server_("ZJdesk signaling", QWebSocketServer::NonSecureMode)
{
    clock_.start();
    connect(&server_, &QWebSocketServer::newConnection, this, &RoomServer::acceptConnection);
    heartbeat_.setInterval(10000);
    connect(&heartbeat_, &QTimer::timeout, this, [this] {
        const auto now = clock_.elapsed();
        for (auto *socket : peers_.keys())
        {
            if (now - peers_.value(socket).lastPong > 30000)
                socket->abort();
            else
                socket->ping();
        }
    });
}

RoomServer::~RoomServer()
{
    close();
}

bool RoomServer::listen(const QHostAddress &address, quint16 port)
{
    if (!server_.listen(address, port))
        return false;
    heartbeat_.start();
    return true;
}

void RoomServer::close()
{
    heartbeat_.stop();
    server_.close();
    for (auto *socket : peers_.keys())
    {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    peers_.clear();
    rooms_.clear();
}

void RoomServer::acceptConnection()
{
    while (server_.hasPendingConnections())
    {
        auto *socket = server_.nextPendingConnection();
        socket->setParent(this);
        socket->setMaxAllowedIncomingMessageSize(protocol::MaxMessageBytes);
        socket->setMaxAllowedIncomingFrameSize(protocol::MaxMessageBytes);
        peers_.insert(socket, {QUuid::createUuid().toString(QUuid::WithoutBraces), {}, {},
                               clock_.elapsed()});
        connect(socket, &QWebSocket::textMessageReceived, this,
                [this, socket](const QString &text) { receive(socket, text); });
        connect(socket, &QWebSocket::binaryMessageReceived, this,
                [this, socket] { reject(socket, "text_messages_only"); });
        connect(socket, &QWebSocket::pong, this, [this, socket] {
            if (peers_.contains(socket))
                peers_[socket].lastPong = clock_.elapsed();
        });
        connect(socket, &QWebSocket::disconnected, this, [this, socket] {
            leave(socket);
            peers_.remove(socket);
            socket->deleteLater();
        });
    }
}

void RoomServer::send(QWebSocket *socket, const protocol::Message &message)
{
    // A slow peer must not build an unbounded signaling queue.
    if (socket->bytesToWrite() > 4 * protocol::MaxMessageBytes)
        socket->abort();
    else
        socket->sendTextMessage(protocol::encode(message));
}

void RoomServer::reject(QWebSocket *socket, const QString &code)
{
    send(socket, {"error", {{"code", code}}});
}

QWebSocket *RoomServer::otherPeer(QWebSocket *socket) const
{
    const auto members = rooms_.value(peers_.value(socket).room);
    for (auto *member : members)
        if (member != socket)
            return member;
    return nullptr;
}

void RoomServer::leave(QWebSocket *socket)
{
    if (!peers_.contains(socket) || peers_.value(socket).room.isEmpty())
        return;
    const auto peer = peers_.value(socket);
    auto *other = otherPeer(socket);
    rooms_[peer.room].removeAll(socket);
    if (rooms_[peer.room].isEmpty())
        rooms_.remove(peer.room);
    peers_[socket].room.clear();
    if (other)
        send(other, {"peer_left", {{"peerId", peer.id}}});
    emit event("Participant left; room count: " + QString::number(rooms_.size()));
}

void RoomServer::receive(QWebSocket *socket, const QString &text)
{
    QString error;
    const auto message = protocol::decode(text, &error);
    if (!message)
    {
        reject(socket, error);
        return;
    }
    if (message->type == "join")
    {
        const auto room = message->payload.value("roomId").toString();
        const auto name = message->payload.value("name").toString().trimmed();
        if (!peers_[socket].room.isEmpty())
            return reject(socket, "already_joined");
        if (!protocol::isRoomIdValid(room) || !protocol::isPeerNameValid(name))
            return reject(socket, "invalid_join");
        if (rooms_.value(room).size() >= 2)
            return reject(socket, "room_full");
        QJsonArray members;
        for (auto *member : rooms_.value(room))
            members.append(QJsonObject{{"peerId", peers_[member].id}, {"name", peers_[member].name}});
        peers_[socket].room = room;
        peers_[socket].name = name;
        rooms_[room].append(socket);
        send(socket, {"joined", {{"roomId", room}, {"peerId", peers_[socket].id},
                                 {"peers", members}, {"initiator", members.isEmpty()}}});
        if (auto *other = otherPeer(socket))
            send(other, {"peer_joined", {{"peerId", peers_[socket].id}, {"name", name}}});
        emit event("Participant joined; room count: " + QString::number(rooms_.size()));
        return;
    }
    if (message->type == "leave")
    {
        leave(socket);
        send(socket, {"left", {}});
        return;
    }
    if (protocol::isRelayType(message->type))
    {
        if (message->type == "conference")
        {
            const auto action = message->payload.value("action").toString();
            const auto &payload = message->payload;
            if (!protocol::isRoomIdValid(payload.value("sessionId").toString())
                || (action != "start" && action != "stop" && action != "accept" && action != "offer"
                    && action != "answer" && action != "ice")
                || ((action == "offer" || action == "answer") &&
                    (!payload.value("sdp").isString() || payload.value("sdp").toString().isEmpty()))
                || (action == "ice" && (!payload.value("candidate").isString()
                    || payload.value("candidate").toString().isEmpty()
                    || !payload.value("sdpMLineIndex").isDouble()
                    || payload.value("sdpMLineIndex").toDouble() < 0
                    || payload.value("sdpMLineIndex").toDouble() > 255
                    || payload.value("sdpMLineIndex").toDouble() != payload.value("sdpMLineIndex").toInt())))
                return reject(socket, "invalid_conference");
        }
        if (message->type == "control")
        {
            const auto action = message->payload.value("action").toString();
            if ((action != "capabilities" && action != "request" && action != "revoke")
                || (action == "capabilities" && !message->payload.value("control").isBool())
                || (action != "capabilities"
                    && !protocol::isRoomIdValid(message->payload.value("requestId").toString())))
                return reject(socket, "invalid_control");
        }
        if (message->type == "share")
        {
            const auto action = message->payload.value("action").toString();
            const auto stream = message->payload.value("streamId").toString();
            if (!protocol::isRoomIdValid(stream)
                || (action != "start" && action != "stop" && action != "profile" && action != "stats"))
                return reject(socket, "invalid_share");
            if (action == "profile" || action == "stats")
            {
                const auto &payload = message->payload;
                const double revision = payload.value("revision").toDouble(-1);
                const double fps = payload.value("fps").toDouble(-1);
                if (revision < 1 || revision > 1000000 || revision != int(revision) || fps < 0 || fps > 240)
                    return reject(socket, "invalid_share");
                if (action == "profile" && ((fps < 5 || fps != int(fps))
                    || payload.value("width").toInt() < 2 || payload.value("width").toInt() > 16384
                    || payload.value("height").toInt() < 2 || payload.value("height").toInt() > 16384))
                    return reject(socket, "invalid_share");
            }
        }
        if (peers_[socket].room.isEmpty())
            return reject(socket, "not_joined");
        auto *other = otherPeer(socket);
        if (!other)
            return reject(socket, "peer_unavailable");
        auto payload = message->payload;
        payload.insert("fromPeerId", peers_[socket].id);
        if (message->type == "conference"
            && protocol::encode({message->type, payload}).toUtf8().size() > protocol::MaxMessageBytes)
            return reject(socket, "message_too_large");
        send(other, {message->type, payload});
        return;
    }
    reject(socket, "unknown_type");
}
} // namespace zjdesk
