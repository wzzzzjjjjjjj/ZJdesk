#include "room_server.h"
#include "protocol/annotation.h"
#include <QJsonArray>
#include <QRandomGenerator>
#include <QUuid>
#include <QWebSocket>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QPointer>

namespace zjdesk
{
RoomServer::RoomServer(QObject *parent, const QString &remoteRegistryFile)
    : QObject(parent), server_("ZJdesk signaling", QWebSocketServer::NonSecureMode),
      remoteRegistry_(remoteRegistryFile.isEmpty()
          ? QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath("ZJdesk/remote-codes.ini")
          : remoteRegistryFile, QSettings::IniFormat)
{
    for (const auto &identity : remoteRegistry_.childGroups())
    {
        const auto code = remoteRegistry_.value(identity + "/code").toString();
        if (protocol::isRemoteCodeValid(code)) reservedCodes_.insert(code, identity);
    }
    clock_.start();
    connect(&server_, &QWebSocketServer::newConnection, this, &RoomServer::acceptConnection);
    heartbeat_.setInterval(10000);
    connect(&heartbeat_, &QTimer::timeout, this, &RoomServer::refreshRemoteCodes);
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
    sharedScreens_.clear();
    annotationGrants_.clear();
    remoteCodes_.clear();
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
            remoteCodes_.remove(peers_.value(socket).remoteCode);
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

bool RoomServer::assignRemoteCode(QWebSocket *socket, const QString &identity, const QString &policy)
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    remoteRegistry_.sync();
    if (!identity.isEmpty() && remoteRegistry_.status() != QSettings::NoError)
    {
        reject(socket, "remote_registry_unavailable");
        return false;
    }
    const auto oldCode = identity.isEmpty() ? QString() : remoteRegistry_.value(identity + "/code").toString();
    const auto created = remoteRegistry_.value(identity + "/createdMs").toLongLong();
    if (!oldCode.isEmpty() && remoteCodes_.contains(oldCode) && remoteCodes_.value(oldCode) != socket)
    {
        reject(socket, "remote_identity_in_use");
        return false;
    }
    QString code = oldCode;
    if (!protocol::isRemoteCodeValid(code) || (policy == "three_days" && (created <= 0 || now - created >= 72LL * 60 * 60 * 1000)))
    {
        do { code = QString::number(QRandomGenerator::system()->bounded(100000000u, 1000000000u)); }
        while (remoteCodes_.contains(code) || reservedCodes_.contains(code));
        if (!identity.isEmpty())
        {
            remoteRegistry_.setValue(identity + "/code", code);
            remoteRegistry_.setValue(identity + "/createdMs", now);
            remoteRegistry_.sync();
            if (remoteRegistry_.status() != QSettings::NoError)
            {
                reject(socket, "remote_registry_unavailable");
                return false;
            }
            reservedCodes_.remove(oldCode);
            reservedCodes_.insert(code, identity);
        }
    }
    auto &peer = peers_[socket];
    peer.remoteIdentity = identity;
    peer.remotePolicy = policy;
    if (peer.remoteCode == code) return true;
    remoteCodes_.remove(peer.remoteCode);
    peer.remoteCode = code;
    remoteCodes_.insert(code, socket);
    send(socket, {"remote_registered", {{"code", code}, {"codePolicy", policy}}});
    return true;
}

void RoomServer::refreshRemoteCodes()
{
    for (auto *socket : peers_.keys())
    {
        const auto peer = peers_.value(socket);
        if (peer.remotePolicy == "three_days" && peer.room.isEmpty())
            assignRemoteCode(socket, peer.remoteIdentity, peer.remotePolicy);
    }
}

QWebSocket *RoomServer::otherPeer(QWebSocket *socket) const
{
    const auto members = rooms_.value(peers_.value(socket).room);
    for (auto *member : members)
        if (member != socket)
            return member;
    return nullptr;
}

void RoomServer::leave(QWebSocket *socket, bool deliberate)
{
    if (!peers_.contains(socket) || peers_.value(socket).room.isEmpty())
        return;
    const auto peer = peers_.value(socket);
    if (peer.room.startsWith("remote\x1f"))
    {
        const auto members = rooms_.take(peer.room);
        for (auto *member : members)
        {
            peers_[member].room.clear();
            if (member != socket) send(member, {"remote_ready", {{"reason", deliberate ? "ended" : "disconnected"}}});
        }
        return;
    }
    if (sharedScreens_.value(peer.room).ownerId == peer.id) stopSharedScreen(peer.room);
    if (sharedScreens_.contains(peer.room))
    {
        sharedScreens_[peer.room].annotationRequests.remove(peer.id);
    }
    auto &grants = annotationGrants_[peer.room];
    grants.remove(peer.id);
    for (auto it = grants.begin(); it != grants.end(); ++it) it.value().remove(peer.id);
    rooms_[peer.room].removeAll(socket);
    if (rooms_[peer.room].isEmpty())
    {
        rooms_.remove(peer.room);
        annotationGrants_.remove(peer.room);
    }
    peers_[socket].room.clear();
    for (auto *other : rooms_.value(peer.room))
        send(other, {"peer_left", {{"peerId", peer.id}}});
    emit event("Participant left; room count: " + QString::number(rooms_.size()));
}

void RoomServer::broadcastAnnotation(const QString &room, const QJsonObject &payload)
{
    for (auto *member : rooms_.value(room)) send(member, {"annotation", payload});
}

void RoomServer::stopSharedScreen(const QString &room)
{
    if (!sharedScreens_.contains(room)) return;
    const auto shared = sharedScreens_.take(room);
    broadcastAnnotation(room, {{"action", "stopped"}, {"streamId", shared.streamId}});
}

void RoomServer::receiveAnnotation(QWebSocket *socket, const QJsonObject &payload)
{
    const auto peer = peers_.value(socket);
    const auto error = [this, socket](const QString &code) {
        send(socket, {"annotation", {{"action", "error"}, {"code", code}}});
    };
    if (!peer.room.startsWith(QStringLiteral("meeting") + QChar(0x1f))) return error("not_in_meeting");
    const auto streamId = payload.value("streamId").toString();
    if (!protocol::isRoomIdValid(streamId)) return error("invalid_stream");
    const auto action = payload.value("action").toString();
    if (action == "start")
    {
        if (sharedScreens_.contains(peer.room))
        {
            send(socket, {"annotation", {{"action", "busy"}, {"streamId", streamId}}});
            return;
        }
        if (payload.contains("annotationsInVideo") && !payload.value("annotationsInVideo").isBool())
            return error("invalid_stream");
        const bool embedded = payload.value("annotationsInVideo").toBool();
        sharedScreens_.insert(peer.room, {streamId, peer.id, {}, embedded});
        broadcastAnnotation(peer.room, {{"action", "started"}, {"streamId", streamId}, {"ownerId", peer.id},
            {"annotationsInVideo", embedded}});
        return;
    }
    if (!sharedScreens_.contains(peer.room) || sharedScreens_[peer.room].streamId != streamId)
        return error("inactive_stream");
    auto &shared = sharedScreens_[peer.room];
    if (action == "request_permission")
    {
        if (peer.id == shared.ownerId) return;
        if (annotationGrants_[peer.room][shared.ownerId].contains(peer.id))
        {
            send(socket, {"annotation", {{"action", "permission_result"}, {"streamId", streamId}, {"approved", true}}});
            return;
        }
        if (shared.annotationRequests.contains(peer.id)) return;
        shared.annotationRequests.insert(peer.id);
        for (auto *member : rooms_.value(peer.room))
            if (peers_.value(member).id == shared.ownerId)
                send(member, {"annotation", {{"action", "permission_requested"}, {"streamId", streamId},
                    {"peerId", peer.id}, {"name", peer.name}}});
        return;
    }
    if (action == "permission_response")
    {
        if (peer.id != shared.ownerId) return error("not_share_owner");
        const auto target = payload.value("peerId").toString();
        if (!payload.value("approved").isBool() || !shared.annotationRequests.remove(target))
            return error("invalid_permission_request");
        const bool approved = payload.value("approved").toBool();
        if (approved) annotationGrants_[peer.room][shared.ownerId].insert(target);
        for (auto *member : rooms_.value(peer.room))
            if (peers_.value(member).id == target)
                send(member, {"annotation", {{"action", "permission_result"}, {"streamId", streamId},
                    {"approved", approved}}});
        return;
    }
    if (action == "stop")
    {
        if (shared.ownerId != peer.id) return error("not_share_owner");
        stopSharedScreen(peer.room);
        return;
    }
    if (action == "clear")
    {
        if (payload.contains("all") && !payload.value("all").isBool()) return error("invalid_clear");
        const bool all = payload.value("all").toBool();
        if (all && shared.ownerId != peer.id) return error("not_share_owner");
        for (qsizetype i = shared.strokes.size(); i-- > 0;)
            if (all || shared.strokes.at(i).toObject().value("authorId").toString() == peer.id)
                shared.strokes.removeAt(i);
        broadcastAnnotation(peer.room, {{"action", "clear"}, {"streamId", streamId}, {"all", all}, {"authorId", peer.id}});
        return;
    }
    if (action == "stroke" && peer.id != shared.ownerId && !annotationGrants_[peer.room][shared.ownerId].contains(peer.id))
        return error("annotation_not_authorized");
    if (action != "stroke" || !protocol::isAnnotationStrokeValid(payload)) return error("invalid_stroke");
    qsizetype index = -1;
    for (qsizetype i = 0; i < shared.strokes.size(); ++i)
        if (shared.strokes.at(i).toObject().value("strokeId") == payload.value("strokeId")) { index = i; break; }
    if (index >= 0 && shared.strokes.at(index).toObject().value("authorId").toString() != peer.id)
        return error("stroke_not_owned");
    if (index < 0 && shared.strokes.size() >= protocol::MaximumAnnotationStrokes) return error("annotation_limit");
    const QJsonObject stroke{{"action", "stroke"}, {"streamId", streamId}, {"strokeId", payload.value("strokeId")},
        {"tool", payload.value("tool")}, {"points", payload.value("points")},
        {"authorId", peer.id}, {"name", peer.name}, {"color", peer.annotationColor}};
    if (index < 0) shared.strokes.append(stroke);
    else shared.strokes.replace(index, stroke);
    broadcastAnnotation(peer.room, stroke);
}

void RoomServer::replayAnnotations(QWebSocket *socket, const QString &room, const QString &streamId,
    const QStringList &strokeIds, int next)
{
    if (peers_.value(socket).room != room || sharedScreens_.value(room).streamId != streamId) return;
    while (next < strokeIds.size() && socket->bytesToWrite() < protocol::MaxMessageBytes)
    {
        const auto id = strokeIds.at(next++);
        // Resolve current state so edits/clears during a slow replay cannot resurrect old marks.
        for (const auto &stroke : sharedScreens_.value(room).strokes)
            if (stroke.toObject().value("strokeId").toString() == id)
            {
                send(socket, {"annotation", stroke.toObject()});
                break;
            }
    }
    if (next < strokeIds.size())
        QTimer::singleShot(10, this, [this, socket = QPointer<QWebSocket>(socket), room, streamId, strokeIds, next] {
            if (socket) replayAnnotations(socket, room, streamId, strokeIds, next);
        });
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
    if (message->type == "annotation")
    {
        receiveAnnotation(socket, message->payload);
        return;
    }
    if (message->type == "register_remote")
    {
        auto &peer = peers_[socket];
        if (!peer.room.isEmpty() || !peer.remoteCode.isEmpty()) return reject(socket, "already_registered");
        const auto name = message->payload.value("name").toString().trimmed();
        if (!protocol::isPeerNameValid(name)) return reject(socket, "invalid_name");
        const auto policy = message->payload.value("codePolicy").toString("startup");
        const auto identity = message->payload.value("identity").toString();
        const auto peerAddress = message->payload.value("peerAddress").toString();
        if (!peerAddress.isEmpty() && QHostAddress(peerAddress).protocol() != QAbstractSocket::IPv4Protocol)
            return reject(socket, "invalid_remote_address");
        if (policy != "startup" && policy != "three_days" && policy != "never")
            return reject(socket, "invalid_remote_policy");
        if (policy != "startup" && !protocol::isRemoteIdentityValid(identity))
            return reject(socket, "invalid_remote_identity");
        peer.name = name;
        peer.remotePeerAddress = peerAddress;
        assignRemoteCode(socket, policy == "startup" ? QString()
            : QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex()), policy);
        return;
    }
    if (message->type == "connect_remote" || message->type == "connect_remote_ip")
    {
        if (peers_[socket].remoteCode.isEmpty()) return reject(socket, "not_registered");
        QWebSocket *target = nullptr;
        if (message->type == "connect_remote")
        {
            const auto code = message->payload.value("code").toString();
            if (!protocol::isRemoteCodeValid(code)) return reject(socket, "invalid_remote_code");
            target = remoteCodes_.value(code, nullptr);
        }
        else
        {
            const QHostAddress address(message->payload.value("address").toString());
            if (address.protocol() != QAbstractSocket::IPv4Protocol
                || address != QHostAddress(peers_[socket].remotePeerAddress))
                return reject(socket, "invalid_remote_address");
            const auto source = socket->peerAddress().toIPv4Address();
            for (auto it = peers_.cbegin(); it != peers_.cend(); ++it)
            {
                auto *candidate = it.key();
                if (candidate == socket || it->remoteCode.isEmpty()
                    || candidate->peerAddress().toIPv4Address() != address.toIPv4Address()
                    || QHostAddress(it->remotePeerAddress).toIPv4Address() != source) continue;
                if (target) return reject(socket, "remote_ambiguous");
                target = candidate;
            }
        }
        if (!target || target == socket) return reject(socket, "remote_unavailable");
        if (!peers_[socket].room.isEmpty() || !peers_[target].room.isEmpty()) return reject(socket, "remote_busy");
        const auto room = QUuid::createUuid().toString(QUuid::Id128);
        const auto key = QStringLiteral("remote") + QChar(0x1f) + room;
        rooms_.insert(key, {socket, target});
        peers_[socket].room = peers_[target].room = key;
        for (auto *member : {target, socket})
        {
            auto *other = member == socket ? target : socket;
            send(member, {"remote_paired", {{"roomId", room}, {"roomType", "assistance"},
                {"peerId", peers_[member].id}, {"controller", member == socket},
                {"peers", QJsonArray{QJsonObject{{"peerId", peers_[other].id}, {"name", peers_[other].name},
                    {"code", peers_[other].remoteCode}}}}}});
        }
        return;
    }
    if (message->type == "disconnect_remote")
    {
        if (peers_[socket].remoteCode.isEmpty()) return reject(socket, "not_registered");
        leave(socket, true);
        send(socket, {"remote_ready", {{"reason", "ended"}}});
        return;
    }
    if (message->type == "join")
    {
        const auto room = message->payload.value("roomId").toString();
        const auto name = message->payload.value("name").toString().trimmed();
        const auto roomType = message->payload.value("roomType").toString("assistance");
        if (!peers_[socket].room.isEmpty() || !peers_[socket].remoteCode.isEmpty())
            return reject(socket, "already_joined");
        if (!protocol::isRoomIdValid(room) || !protocol::isPeerNameValid(name)
            || (roomType != "assistance" && roomType != "meeting"))
            return reject(socket, "invalid_join");
        const auto roomKey = roomType + QChar(0x1f) + room;
        if (rooms_.value(roomKey).size() >= (roomType == "meeting" ? 4 : 2))
            return reject(socket, "room_full");
        QJsonArray members;
        for (auto *member : rooms_.value(roomKey))
            members.append(QJsonObject{{"peerId", peers_[member].id}, {"name", peers_[member].name}});
        peers_[socket].room = roomKey;
        peers_[socket].name = name;
        if (roomType == "meeting")
        {
            for (const auto &color : {"#e5484d", "#2474e5", "#168765", "#a345c8"})
            {
                bool used = false;
                for (auto *member : rooms_.value(roomKey))
                    used |= peers_[member].annotationColor == color;
                if (!used) { peers_[socket].annotationColor = color; break; }
            }
        }
        rooms_[roomKey].append(socket);
        send(socket, {"joined", {{"roomId", room}, {"peerId", peers_[socket].id},
                                 {"roomType", roomType}, {"peers", members}, {"initiator", members.isEmpty()},
                                 {"annotationColor", peers_[socket].annotationColor}}});
        if (sharedScreens_.contains(roomKey))
        {
            const auto &shared = sharedScreens_[roomKey];
            send(socket, {"annotation", {{"action", "started"}, {"streamId", shared.streamId}, {"ownerId", shared.ownerId},
                {"annotationsInVideo", shared.annotationsInVideo}}});
            QStringList ids;
            for (const auto &stroke : shared.strokes) ids.append(stroke.toObject().value("strokeId").toString());
            replayAnnotations(socket, roomKey, shared.streamId, ids);
        }
        for (auto *other : rooms_.value(roomKey))
            if (other != socket)
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
                    && action != "answer" && action != "ice" && action != "camera")
                || (action == "camera" && !payload.value("hidden").isBool())
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
        const auto target = message->payload.value("targetPeerId").toString();
        if (!target.isEmpty())
        {
            other = nullptr;
            for (auto *member : rooms_.value(peers_[socket].room))
                if (member != socket && peers_[member].id == target) other = member;
        }
        else if (rooms_.value(peers_[socket].room).size() > 2)
            return reject(socket, "target_required");
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
