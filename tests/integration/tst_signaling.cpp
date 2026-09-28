#include "session/session_coordinator.h"
#include "signaling/room_server.h"
#include <QNetworkProxy>
#include <QWebSocket>
#include <QtTest>

using State = zjdesk::SessionCoordinator::State;

class SignalingTests : public QObject
{
    Q_OBJECT
private slots:
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
    }
};

QTEST_GUILESS_MAIN(SignalingTests)
#include "tst_signaling.moc"
