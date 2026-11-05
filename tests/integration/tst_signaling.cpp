#include "session/session_coordinator.h"
#include "signaling/room_server.h"
#include <QNetworkProxy>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QtTest>

using State = zjdesk::SessionCoordinator::State;

class SignalingTests : public QObject
{
    Q_OBJECT
private slots:
    void remoteCodesPairWithoutJoiningRoomsAndRecoverAfterDisconnect()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator a, b, c;
        QVERIFY(a.startRemoteAccess(url, "Windows"));
        QVERIFY(b.startRemoteAccess(url, "Linux"));
        QVERIFY(c.startRemoteAccess(url, "Other"));
        QTRY_VERIFY(!a.remoteCode().isEmpty() && !b.remoteCode().isEmpty() && !c.remoteCode().isEmpty());
        QVERIFY(zjdesk::protocol::isRemoteCodeValid(a.remoteCode()));
        QVERIFY(a.remoteCode() != b.remoteCode());
        QVERIFY(!a.connectRemote("12345678"));
        QVERIFY(!a.connectRemote(QString::fromUtf8("１２３４５６７８９")));
        QVERIFY(!a.connectRemote(a.remoteCode()));
        QVERIFY(a.connectRemote("000000000"));
        QVERIFY(!a.connectRemote(b.remoteCode())); // Only one lookup may be outstanding.
        QTRY_VERIFY(a.detail().contains(QStringLiteral("校验码")));
        QCOMPARE(a.state(), State::WaitingForPeer);
        QVERIFY(a.connectRemote(b.remoteCode()));
        QTRY_COMPARE(a.state(), State::PeerAvailable);
        QTRY_COMPARE(b.state(), State::PeerAvailable);
        QVERIFY(c.connectRemote(b.remoteCode()));
        QTRY_VERIFY(c.detail().contains(QStringLiteral("请稍后重试")));
        QCOMPARE(c.state(), State::WaitingForPeer);
        const auto hostCode = b.remoteCode();
        a.disconnectRemote();
        QTRY_COMPARE(a.state(), State::WaitingForPeer);
        QTRY_COMPARE(b.state(), State::WaitingForPeer);
        QCOMPARE(b.remoteCode(), hostCode);
        QVERIFY(c.connectRemote(hostCode));
        QTRY_COMPARE(b.state(), State::PeerAvailable);
        const auto retiredCode = c.remoteCode();
        c.leave();
        QTRY_COMPARE(b.state(), State::WaitingForPeer);
        QVERIFY(c.remoteCode().isEmpty());
        QVERIFY(b.connectRemote(retiredCode));
        QTRY_VERIFY(!b.remoteConnecting());
        QCOMPARE(b.state(), State::WaitingForPeer);
        QVERIFY(b.detail().contains(QStringLiteral("校验码")));
        a.leave(); b.leave();
        QCOMPARE(server.roomCount(), 0);
    }

    void meetingRejectsLateScreenShare()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator a, b, c;
        using RT = zjdesk::SessionCoordinator::RoomType;
        QVERIFY(a.join(url, "late-share", "A", RT::Meeting));
        QVERIFY(b.join(url, "late-share", "B", RT::Meeting));
        QTRY_COMPARE(a.memberCount(), 2);
        QVERIFY(c.join(url, "late-share", "C", RT::Meeting));
        for (auto *peer : {&a, &b, &c}) QTRY_COMPARE(peer->memberCount(), 3);
        QSignalSpy received(&a, &zjdesk::SessionCoordinator::negotiationReceived);
        QVERIFY(b.sendNegotiation("share", {{"action", "start"}, {"streamId", "late-stream"}}));
        QTRY_COMPARE(received.count(), 1);
        QVERIFY(!a.screenActive());
        QVERIFY(!a.receivingScreen());
    }

    void meetingFourMembersAndTargetedRelay()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator a, b, c, d, fifth;
        using RT = zjdesk::SessionCoordinator::RoomType;
        QVERIFY(a.join(url, "mesh", "A", RT::Meeting));
        QVERIFY(b.join(url, "mesh", "B", RT::Meeting));
        QTRY_COMPARE(a.memberCount(), 2);
        QVERIFY(c.join(url, "mesh", "C", RT::Meeting));
        QTRY_COMPARE(a.memberCount(), 3);
        QVERIFY(d.join(url, "mesh", "D", RT::Meeting));
        for (auto *session : {&a, &b, &c, &d}) QTRY_COMPARE(session->memberCount(), 4);
        QVERIFY(fifth.join(url, "mesh", "E", RT::Meeting));
        QTRY_COMPARE(fifth.state(), State::Failed);
        QSignalSpy receivedA(&a, &zjdesk::SessionCoordinator::negotiationReceived);
        QSignalSpy receivedB(&b, &zjdesk::SessionCoordinator::negotiationReceived);
        QSignalSpy receivedC(&c, &zjdesk::SessionCoordinator::negotiationReceived);
        QString target;
        const auto members = d.conferencePeers();
        for (auto it = members.cbegin(); it != members.cend(); ++it)
            if (it.value() == "B") target = it.key();
        QVERIFY(!target.isEmpty());
        QVERIFY(d.sendNegotiation("offer", {{"targetPeerId", target}, {"sdp", "targeted"}}));
        QTRY_COMPARE(receivedB.count(), 1);
        QTest::qWait(100);
        QVERIFY(receivedA.isEmpty()); QVERIFY(receivedC.isEmpty());
        QCOMPARE(qvariant_cast<zjdesk::protocol::Message>(receivedB.first().first()).payload.value("sdp").toString(), "targeted");
        c.leave();
        for (auto *session : {&a, &b, &d}) QTRY_COMPARE(session->memberCount(), 3);
        QVERIFY(c.join(url, "mesh", "C", RT::Meeting));
        for (auto *session : {&a, &b, &c, &d}) QTRY_COMPARE(session->memberCount(), 4);
    }

    void roomTypesIsolateIdenticalCodes()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        const QString room(32, 'a');
        using RoomType = zjdesk::SessionCoordinator::RoomType;
        zjdesk::SessionCoordinator assistanceA, assistanceB, meetingA, meetingB;
        QVERIFY(assistanceA.join(url, room, "A"));
        QVERIFY(meetingA.join(url, room, "M", RoomType::Meeting));
        QTRY_COMPARE(assistanceA.state(), State::WaitingForPeer);
        QTRY_COMPARE(meetingA.state(), State::WaitingForPeer);
        QVERIFY(!assistanceA.join(url, room, "A", RoomType::Meeting));
        QCOMPARE(assistanceA.state(), State::WaitingForPeer);
        QVERIFY(!meetingA.join(url, room, "M", RoomType::Assistance));
        QCOMPARE(meetingA.state(), State::WaitingForPeer);
        QVERIFY(!meetingA.canRequestControl());
        QCOMPARE(server.roomCount(), 2);
        QVERIFY(assistanceB.join(url, room, "B"));
        QVERIFY(meetingB.join(url, room, "N", RoomType::Meeting));
        QTRY_COMPARE(assistanceA.state(), State::PeerAvailable);
        QTRY_COMPARE(meetingA.state(), State::PeerAvailable);
        QVERIFY(!assistanceA.startConference());
        QVERIFY(!meetingA.canRequestControl());
        QCOMPARE(assistanceA.peerName(), QString("B"));
        QCOMPARE(meetingA.peerName(), QString("N"));
        meetingB.leave();
        QTRY_COMPARE(meetingA.state(), State::WaitingForPeer);
        QCOMPARE(assistanceA.state(), State::PeerAvailable);
    }

    void meetingRejectsLegacyServer()
    {
        QWebSocketServer server(QStringLiteral("legacy"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        connect(&server, &QWebSocketServer::newConnection, &server, [&server] {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QWebSocket::textMessageReceived, socket, [socket](const QString &text) {
                const auto message = zjdesk::protocol::decode(text);
                if (message && message->type == "join")
                    socket->sendTextMessage(zjdesk::protocol::encode({"joined", {{"roomId", "room"},
                        {"peerId", "peer"}, {"peers", QJsonArray{}}}}));
            });
        });
        zjdesk::SessionCoordinator client;
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.serverPort()));
        QVERIFY(client.join(url, "room", "Tester", zjdesk::SessionCoordinator::RoomType::Meeting));
        QTRY_COMPARE(client.state(), State::Failed);
        QVERIFY(client.detail().contains(QStringLiteral("更新服务端")));
    }

    void conferenceSignalingIsBoundToRoomAndSender()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SignalingClient alice, bob;
        QSignalSpy ac(&alice, &zjdesk::SignalingClient::connected);
        QSignalSpy bc(&bob, &zjdesk::SignalingClient::connected);
        QSignalSpy am(&alice, &zjdesk::SignalingClient::messageReceived);
        QSignalSpy bm(&bob, &zjdesk::SignalingClient::messageReceived);
        alice.open(url);
        bob.open(url);
        QTRY_COMPARE(ac.count(), 1);
        QTRY_COMPARE(bc.count(), 1);
        QVERIFY(alice.send({"conference", {{"action", "start"}, {"sessionId", "call1"}}}));
        QTRY_COMPARE(am.count(), 1);
        QCOMPARE(qvariant_cast<zjdesk::protocol::Message>(am.takeFirst().at(0)).payload.value("code").toString(), "not_joined");
        QVERIFY(alice.send({"join", {{"roomId", "call"}, {"name", "Alice"}}}));
        QTRY_COMPARE(am.count(), 1);
        const auto identity = qvariant_cast<zjdesk::protocol::Message>(am.takeFirst().at(0)).payload.value("peerId").toString();
        QVERIFY(bob.send({"join", {{"roomId", "call"}, {"name", "Bob"}}}));
        QTRY_COMPARE(bm.count(), 1);
        QTRY_COMPARE(am.count(), 1);
        am.clear();
        bm.clear();
        const QList<QJsonObject> messages = {
            {{"action", "start"}, {"sessionId", "call1"}, {"fromPeerId", "forged"}},
            {{"action", "accept"}, {"sessionId", "call1"}},
            {{"action", "offer"}, {"sessionId", "call1"}, {"sdp", "offer"}},
            {{"action", "answer"}, {"sessionId", "call1"}, {"sdp", "answer"}},
            {{"action", "ice"}, {"sessionId", "call1"}, {"candidate", "candidate"}, {"sdpMLineIndex", 1}},
            {{"action", "camera"}, {"sessionId", "call1"}, {"hidden", true}},
            {{"action", "camera"}, {"sessionId", "call1"}, {"hidden", false}},
            {{"action", "stop"}, {"sessionId", "call1"}}};
        for (const auto &message : messages)
            QVERIFY(alice.send({"conference", message}));
        QTRY_COMPARE(bm.count(), messages.size());
        for (int i = 0; i < messages.size(); ++i)
        {
            const auto received = qvariant_cast<zjdesk::protocol::Message>(bm.at(i).at(0));
            QCOMPARE(received.type, "conference");
            QCOMPARE(received.payload.value("action"), messages.at(i).value("action"));
            QCOMPARE(received.payload.value("fromPeerId").toString(), identity);
        }
        QVERIFY(alice.send({"conference", {{"action", "ice"}, {"sessionId", "call1"},
            {"candidate", "candidate"}, {"sdpMLineIndex", -1}}}));
        QTRY_COMPARE(am.count(), 1);
        QCOMPARE(qvariant_cast<zjdesk::protocol::Message>(am.takeFirst().at(0)).payload.value("code").toString(), "invalid_conference");
        QCOMPARE(bm.count(), messages.size());
        QVERIFY(alice.send({"conference", {{"action", "camera"}, {"sessionId", "call1"}, {"hidden", "true"}}}));
        QTRY_COMPARE(am.count(), 1);
        QCOMPARE(qvariant_cast<zjdesk::protocol::Message>(am.takeFirst().at(0)).payload.value("code").toString(), "invalid_conference");
        QCOMPARE(bm.count(), messages.size());
        zjdesk::protocol::Message large{"conference", {{"action", "offer"}, {"sessionId", "call1"}, {"sdp", ""}}};
        const auto overhead = zjdesk::protocol::encode(large).toUtf8().size();
        large.payload.insert("sdp", QString(zjdesk::protocol::MaxMessageBytes - overhead, 'x'));
        QCOMPARE(zjdesk::protocol::encode(large).toUtf8().size(), zjdesk::protocol::MaxMessageBytes);
        QVERIFY(alice.send(large));
        QTRY_COMPARE(am.count(), 1);
        QCOMPARE(qvariant_cast<zjdesk::protocol::Message>(am.takeFirst().at(0)).payload.value("code").toString(), "message_too_large");
        QCOMPARE(bm.count(), messages.size());
        QVERIFY(alice.send({"conference", {{"action", "stop"}, {"sessionId", "call1"}}}));
        QTRY_COMPARE(bm.count(), messages.size() + 1);
    }

    void twoPeersRelayAndThirdPeerRejected()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator alice, bob, carol;
        QVERIFY(alice.join(url, "demo", "Alice"));
        QTRY_COMPARE(alice.state(), State::WaitingForPeer);
        QVERIFY(bob.join(url, "demo", "Bob"));
        QTRY_COMPARE(alice.state(), State::PeerAvailable);
        QTRY_COMPARE(bob.state(), State::PeerAvailable);
        QCOMPARE(alice.peerName(), "Bob");
        QCOMPARE(bob.peerName(), "Alice");
        QCOMPARE(alice.memberCount(), 2);

        QSignalSpy bobRelay(&bob, &zjdesk::SessionCoordinator::negotiationReceived);
        QSignalSpy aliceRelay(&alice, &zjdesk::SessionCoordinator::negotiationReceived);
        QVERIFY(alice.sendNegotiation("offer", {{"sdp", "test-offer"}, {"fromPeerId", "forged"}}));
        QTRY_COMPARE(bobRelay.count(), 1);
        auto offer = qvariant_cast<zjdesk::protocol::Message>(bobRelay.at(0).at(0));
        QCOMPARE(offer.type, "offer");
        QCOMPARE(offer.payload.value("sdp").toString(), "test-offer");
        QVERIFY(!offer.payload.value("fromPeerId").toString().isEmpty());
        QVERIFY(offer.payload.value("fromPeerId").toString() != "forged");
        QVERIFY(bob.sendNegotiation("answer", {{"sdp", "test-answer"}}));
        QVERIFY(bob.sendNegotiation("ice", {{"candidate", "test-ice"}}));
        QTRY_COMPARE(aliceRelay.count(), 2);

        QVERIFY(carol.join(url, "demo", "Carol"));
        QTRY_COMPARE(carol.state(), State::Failed);
        QVERIFY(carol.detail().contains(QStringLiteral("房间已满")));
        QCOMPARE(server.roomCount(), 1);
        bob.leave();
        QTRY_COMPARE(alice.state(), State::WaitingForPeer);
        QVERIFY(alice.peerName().isEmpty());
        QVERIFY(carol.join(url, "demo", "Carol"));
        QTRY_COMPARE(carol.state(), State::PeerAvailable);
        QTRY_COMPARE(alice.peerName(), QString("Carol"));
        alice.leave();
        carol.leave();
        QTRY_COMPARE(server.roomCount(), 0);
    }

    void enteredRoomCodesDetermineMembership()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator alice, bob;
        QVERIFY(!alice.join(url, "", "Alice"));
        QCOMPARE(server.roomCount(), 0);
        QVERIFY(alice.join(url, "room_123", "Alice"));
        QVERIFY(bob.join(url, "room_456", "Bob"));
        QTRY_COMPARE(alice.state(), State::WaitingForPeer);
        QTRY_COMPARE(bob.state(), State::WaitingForPeer);
        QCOMPARE(server.roomCount(), 2);
        bob.leave();
        QTRY_COMPARE(server.roomCount(), 1);
        QVERIFY(bob.join(url, "room_123", "Bob"));
        QTRY_COMPARE(alice.state(), State::PeerAvailable);
        QTRY_COMPARE(bob.state(), State::PeerAvailable);
        QCOMPARE(alice.peerName(), QString("Bob"));
    }

    void cancelAndRepeatedJoinLeave()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator client;
        QVERIFY(client.join(url, "demo", "Tester"));
        client.leave();
        QCOMPARE(client.state(), State::Idle);
        for (int i = 0; i < 20; ++i)
        {
            QVERIFY(client.join(url, "demo", "Tester"));
            QTRY_COMPARE(client.state(), State::WaitingForPeer);
            QCOMPARE(client.memberCount(), 1);
            client.leave();
            QTRY_COMPARE(server.roomCount(), 0);
            QCOMPARE(client.state(), State::Idle);
        }
    }

    void serverShutdownAndManualRejoin()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        auto url = QUrl(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator client;
        QVERIFY(client.join(url, "demo", "Tester"));
        QTRY_COMPARE(client.state(), State::WaitingForPeer);
        server.close();
        QTRY_COMPARE(client.state(), State::Failed);
        QCOMPARE(client.memberCount(), 0);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        url = QUrl(QString("ws://127.0.0.1:%1").arg(server.port()));
        QVERIFY(client.join(url, "demo", "Tester"));
        QTRY_COMPARE(client.state(), State::WaitingForPeer);
    }

    void roomsAreIsolatedAndJoinIsRequired()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SignalingClient a, b;
        QSignalSpy aConnected(&a, &zjdesk::SignalingClient::connected);
        QSignalSpy bConnected(&b, &zjdesk::SignalingClient::connected);
        QSignalSpy aMessages(&a, &zjdesk::SignalingClient::messageReceived);
        QSignalSpy bMessages(&b, &zjdesk::SignalingClient::messageReceived);
        a.open(url);
        b.open(url);
        QTRY_COMPARE(aConnected.count(), 1);
        QTRY_COMPARE(bConnected.count(), 1);
        QVERIFY(a.send({"offer", {{"sdp", "unauthorized"}}}));
        QTRY_COMPARE(aMessages.count(), 1);
        auto response = qvariant_cast<zjdesk::protocol::Message>(aMessages.takeFirst().at(0));
        QCOMPARE(response.payload.value("code").toString(), "not_joined");
        QVERIFY(a.send({"join", {{"roomId", "room_a"}, {"name", "A"}}}));
        QVERIFY(b.send({"join", {{"roomId", "room_b"}, {"name", "B"}}}));
        QTRY_COMPARE(aMessages.count(), 1);
        QTRY_COMPARE(bMessages.count(), 1);
        aMessages.clear();
        bMessages.clear();
        QVERIFY(a.send({"offer", {{"sdp", "cross-room"}}}));
        QTRY_COMPARE(aMessages.count(), 1);
        response = qvariant_cast<zjdesk::protocol::Message>(aMessages.at(0).at(0));
        QCOMPARE(response.payload.value("code").toString(), "peer_unavailable");
        QCOMPARE(bMessages.count(), 0);
        QCOMPARE(server.roomCount(), 2);
    }

    void malformedMessageDoesNotDestroyRoomService()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QWebSocket socket;
        socket.setProxy(QNetworkProxy::NoProxy);
        QSignalSpy connected(&socket, &QWebSocket::connected);
        QSignalSpy received(&socket, &QWebSocket::textMessageReceived);
        socket.open(QUrl(QString("ws://127.0.0.1:%1").arg(server.port())));
        QTRY_COMPARE(connected.count(), 1);
        socket.sendTextMessage("{invalid-json");
        QTRY_COMPARE(received.count(), 1);
        auto message = zjdesk::protocol::decode(received.takeFirst().at(0).toString());
        QVERIFY(message.has_value());
        QCOMPARE(message->payload.value("code").toString(), "invalid_json");
        socket.sendTextMessage(zjdesk::protocol::encode({"join", {{"roomId", "demo"}, {"name", "A"}}}));
        QTRY_COMPARE(received.count(), 1);
        message = zjdesk::protocol::decode(received.at(0).at(0).toString());
        QVERIFY(message.has_value());
        QCOMPARE(message->type, "joined");
        QCOMPARE(server.roomCount(), 1);
    }

    void controlRequestsRequireRoomAndBindSender()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SignalingClient alice, bob;
        QSignalSpy aConnected(&alice, &zjdesk::SignalingClient::connected);
        QSignalSpy bConnected(&bob, &zjdesk::SignalingClient::connected);
        QSignalSpy atAlice(&alice, &zjdesk::SignalingClient::messageReceived);
        QSignalSpy atBob(&bob, &zjdesk::SignalingClient::messageReceived);
        alice.open(url);
        bob.open(url);
        QTRY_COMPARE(aConnected.count(), 1);
        QTRY_COMPARE(bConnected.count(), 1);
        QVERIFY(alice.send({"control", {{"action", "request"}, {"requestId", "request1"}}}));
        QTRY_COMPARE(atAlice.count(), 1);
        QCOMPARE(qvariant_cast<zjdesk::protocol::Message>(atAlice.takeFirst().at(0))
            .payload.value("code").toString(), "not_joined");
        QVERIFY(alice.send({"join", {{"roomId", "control"}, {"name", "Alice"}}}));
        QTRY_COMPARE(atAlice.count(), 1);
        const auto identity = qvariant_cast<zjdesk::protocol::Message>(atAlice.first().at(0))
            .payload.value("peerId").toString();
        QVERIFY(bob.send({"join", {{"roomId", "control"}, {"name", "Bob"}}}));
        QTRY_COMPARE(atBob.count(), 1);
        QTRY_COMPARE(atAlice.count(), 2);
        atAlice.clear();
        atBob.clear();
        QVERIFY(alice.send({"control", {{"action", "request"}, {"requestId", "request1"}, {"fromPeerId", "forged"}}}));
        QTRY_COMPARE(atBob.count(), 1);
        const auto request = qvariant_cast<zjdesk::protocol::Message>(atBob.takeFirst().at(0));
        QCOMPARE(request.payload.value("fromPeerId").toString(), identity);
        QCOMPARE(request.payload.value("requestId").toString(), "request1");
        for (const auto &action : {"grant", "input"})
        {
            QVERIFY(alice.send({"control", {{"action", action}, {"requestId", "request1"}}}));
            QTRY_COMPARE(atAlice.count(), 1);
            QCOMPARE(qvariant_cast<zjdesk::protocol::Message>(atAlice.takeFirst().at(0))
                .payload.value("code").toString(), "invalid_control");
        }
        QCOMPARE(atBob.count(), 0);
        QVERIFY(bob.send({"control", {{"action", "revoke"}, {"requestId", "request1"}}}));
        QTRY_COMPARE(atAlice.count(), 1);
        QCOMPARE(qvariant_cast<zjdesk::protocol::Message>(atAlice.takeFirst().at(0))
            .payload.value("action").toString(), "revoke");
        bob.close();
        QTRY_COMPARE(atAlice.count(), 1);
        atAlice.clear();
        QVERIFY(alice.send({"control", {{"action", "request"}, {"requestId", "request2"}}}));
        QTRY_COMPARE(atAlice.count(), 1);
        QCOMPARE(qvariant_cast<zjdesk::protocol::Message>(atAlice.takeFirst().at(0))
            .payload.value("code").toString(), "peer_unavailable");
    }

    void shareMessagesValidateActionAndBindSender()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SignalingClient alice, bob;
        QSignalSpy aConnected(&alice, &zjdesk::SignalingClient::connected);
        QSignalSpy bConnected(&bob, &zjdesk::SignalingClient::connected);
        QSignalSpy aMessages(&alice, &zjdesk::SignalingClient::messageReceived);
        QSignalSpy bMessages(&bob, &zjdesk::SignalingClient::messageReceived);
        alice.open(url);
        bob.open(url);
        QTRY_COMPARE(aConnected.count(), 1);
        QTRY_COMPARE(bConnected.count(), 1);
        QVERIFY(alice.send({"join", {{"roomId", "share"}, {"name", "Alice"}}}));
        QTRY_COMPARE(aMessages.count(), 1);
        const auto identity = qvariant_cast<zjdesk::protocol::Message>(aMessages.first().at(0))
            .payload.value("peerId").toString();
        QVERIFY(bob.send({"join", {{"roomId", "share"}, {"name", "Bob"}}}));
        QTRY_COMPARE(bMessages.count(), 1);
        QTRY_COMPARE(aMessages.count(), 2);
        aMessages.clear();
        bMessages.clear();
        QVERIFY(alice.send({"share", {{"action", "invalid"}, {"streamId", "stream"}}}));
        QTRY_COMPARE(aMessages.count(), 1);
        QCOMPARE(qvariant_cast<zjdesk::protocol::Message>(aMessages.takeFirst().at(0))
            .payload.value("code").toString(), "invalid_share");
        QCOMPARE(bMessages.count(), 0);
        QVERIFY(alice.send({"share", {{"action", "start"}, {"streamId", "stream"}, {"fromPeerId", "forged"}}}));
        QTRY_COMPARE(bMessages.count(), 1);
        const auto message = qvariant_cast<zjdesk::protocol::Message>(bMessages.first().at(0));
        QCOMPARE(message.payload.value("fromPeerId").toString(), identity);
        QCOMPARE(message.payload.value("streamId").toString(), "stream");
        bMessages.clear();
        QVERIFY(alice.send({"share", {{"action", "stats"}, {"streamId", "stream"},
            {"revision", 1}, {"fps", -10}}}));
        QTRY_COMPARE(aMessages.count(), 1);
        QCOMPARE(qvariant_cast<zjdesk::protocol::Message>(aMessages.takeFirst().at(0))
            .payload.value("code").toString(), "invalid_share");
        QVERIFY(alice.send({"share", {{"action", "profile"}, {"streamId", "stream"},
            {"revision", 2}, {"fps", 30}, {"width", 1280}, {"height", 720}}}));
        QTRY_COMPARE(bMessages.count(), 1);
        QCOMPARE(qvariant_cast<zjdesk::protocol::Message>(bMessages.first().at(0))
            .payload.value("revision").toInt(), 2);
        for (int fps : {5, 10, 20, 24, 50, 75, 144, 240})
        {
            bMessages.clear();
            QVERIFY(alice.send({"share", {{"action", "profile"}, {"streamId", "stream"},
                {"revision", 3}, {"fps", fps}, {"width", 1920}, {"height", 1080}}}));
            QTRY_COMPARE(bMessages.count(), 1);
            QCOMPARE(qvariant_cast<zjdesk::protocol::Message>(bMessages.first().at(0))
                .payload.value("fps").toInt(), fps);
        }
        for (double fps : {0.0, 4.0, 30.5, 241.0})
        {
            QVERIFY(alice.send({"share", {{"action", "profile"}, {"streamId", "stream"},
                {"revision", 3}, {"fps", fps}, {"width", 1920}, {"height", 1080}}}));
            QTRY_COMPARE(aMessages.count(), 1);
            QCOMPARE(qvariant_cast<zjdesk::protocol::Message>(aMessages.takeFirst().at(0))
                .payload.value("code").toString(), "invalid_share");
        }
    }
};

QTEST_GUILESS_MAIN(SignalingTests)
#include "tst_signaling.moc"
