#include "session/session_coordinator.h"
#include "signaling/room_server.h"
#include "protocol/annotation.h"
#include <QNetworkProxy>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QCryptographicHash>
#include <QDateTime>

using State = zjdesk::SessionCoordinator::State;

class SignalingTests : public QObject
{
    Q_OBJECT
private slots:
    void meetingAnnotationsBroadcastValidateAndReplay()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SignalingClient clients[5];
        QString ids[5];
        QList<QJsonObject> events[5];
        int connected = 0;
        for (int i = 0; i < 5; ++i)
        {
            connect(&clients[i], &zjdesk::SignalingClient::connected, this, [&connected] { ++connected; });
            connect(&clients[i], &zjdesk::SignalingClient::messageReceived, this,
                [&, i](const zjdesk::protocol::Message &message) {
                    if (message.type == "joined") ids[i] = message.payload.value("peerId").toString();
                    if (message.type == "annotation") events[i].append(message.payload);
                });
            clients[i].open(url);
        }
        QTRY_COMPARE(connected, 5);
        for (int i = 0; i < 5; ++i)
            QVERIFY(clients[i].send({"join", {{"roomId", i == 4 ? "other" : "drawing"},
                {"roomType", "meeting"}, {"name", QString("Member%1").arg(i)}}}));
        QTRY_VERIFY(!ids[4].isEmpty() && !ids[3].isEmpty());
        QVERIFY(clients[0].send({"annotation", {{"action", "start"}, {"streamId", "screen1"}}}));
        QTRY_COMPARE(events[0].size(), 1);
        for (int i = 0; i < 4; ++i)
        {
            QTRY_COMPARE(events[i].size(), 1);
            QCOMPARE(events[i].last().value("action").toString(), "started");
            QCOMPARE(events[i].last().value("ownerId").toString(), ids[0]);
        }
        QVERIFY(events[4].isEmpty());
        QVERIFY(clients[1].send({"annotation", {{"action", "start"}, {"streamId", "screen2"}}}));
        QTRY_COMPARE(events[1].last().value("action").toString(), "busy");
        QCOMPARE(events[0].size(), 1);
        QJsonObject stroke{{"action", "stroke"}, {"streamId", "screen1"}, {"strokeId", "circle1"},
            {"tool", "circle"}, {"points", QJsonArray{QJsonArray{0.2, 0.3}, QJsonArray{0.6, 0.7}}},
            {"authorId", ids[1]}, {"name", "spoofed"}, {"color", "#ffffff"}};
        QVERIFY(clients[0].send({"annotation", stroke}));
        QTRY_COMPARE(events[0].last().value("action").toString(), "stroke");
        for (int i = 0; i < 4; ++i)
        {
            QTRY_COMPARE(events[i].last().value("strokeId").toString(), "circle1");
            QCOMPARE(events[i].last().value("authorId").toString(), ids[0]);
            QCOMPARE(events[i].last().value("name").toString(), "Member0");
            QVERIFY(events[i].last().value("color").toString() != "#ffffff");
        }
        QVERIFY(clients[1].send({"annotation", stroke})); // Another author cannot overwrite the circle.
        QTRY_COMPARE(events[1].last().value("action").toString(), "error");
        QCOMPARE(events[1].last().value("code").toString(), "annotation_not_authorized");
        QVERIFY(clients[1].send({"annotation", {{"action", "request_permission"}, {"streamId", "screen1"}}}));
        QTRY_COMPARE(events[0].last().value("action").toString(), "permission_requested");
        QCOMPARE(events[0].last().value("peerId").toString(), ids[1]);
        QVERIFY(clients[2].send({"annotation", {{"action", "permission_response"}, {"streamId", "screen1"},
            {"peerId", ids[1]}, {"approved", true}}}));
        QTRY_COMPARE(events[2].last().value("code").toString(), "not_share_owner");
        QVERIFY(clients[0].send({"annotation", {{"action", "permission_response"}, {"streamId", "screen1"},
            {"peerId", ids[1]}, {"approved", false}}}));
        QTRY_COMPARE(events[1].last().value("action").toString(), "permission_result");
        QVERIFY(!events[1].last().value("approved").toBool());
        QVERIFY(clients[1].send({"annotation", stroke}));
        QTRY_COMPARE(events[1].last().value("code").toString(), "annotation_not_authorized");
        const auto requestsBeforeRetry = events[0].size();
        QVERIFY(clients[1].send({"annotation", {{"action", "request_permission"}, {"streamId", "screen1"}}}));
        QTRY_COMPARE(events[0].size(), requestsBeforeRetry + 1);
        QCOMPARE(events[0].last().value("action").toString(), "permission_requested");
        QVERIFY(clients[0].send({"annotation", {{"action", "permission_response"}, {"streamId", "screen1"},
            {"peerId", ids[1]}, {"approved", true}}}));
        QTRY_COMPARE(events[1].last().value("action").toString(), "permission_result");
        QVERIFY(events[1].last().value("approved").toBool());
        auto invalid = stroke;
        invalid.insert("strokeId", "invalid");
        for (const auto &points : {QJsonArray{QJsonArray{-0.01, 0.3}},
                QJsonArray{QJsonArray{0.3, 1.01}}, QJsonArray{QJsonArray{"0.3", 0.4}}})
        {
            const auto count = events[0].size();
            invalid.insert("points", points);
            QVERIFY(clients[0].send({"annotation", invalid}));
            QTRY_COMPARE(events[0].size(), count + 1);
            QCOMPARE(events[0].last().value("action").toString(), "error");
        }
        invalid = stroke;
        invalid.insert("streamId", "oldScreen");
        QVERIFY(clients[0].send({"annotation", invalid}));
        QTRY_COMPARE(events[0].last().value("code").toString(), "inactive_stream");
        stroke.insert("strokeId", "arrow1");
        stroke.insert("tool", "arrow");
        QVERIFY(clients[1].send({"annotation", stroke}));
        QTRY_COMPARE(events[1].last().value("strokeId").toString(), "arrow1");
        QVERIFY(events[1].last().value("color") != events[0].at(1).value("color"));
        QVERIFY(clients[1].send({"annotation", {{"action", "clear"}, {"streamId", "screen1"}, {"all", true}}}));
        QTRY_COMPARE(events[1].last().value("action").toString(), "error");
        QVERIFY(clients[1].send({"annotation", {{"action", "clear"}, {"streamId", "screen1"}}}));
        QTRY_COMPARE(events[0].last().value("action").toString(), "clear");
        QCOMPARE(events[0].last().value("authorId").toString(), ids[1]);
        clients[3].close();
        QTest::qWait(50);
        zjdesk::SignalingClient late;
        QList<QJsonObject> replay;
        QSignalSpy ready(&late, &zjdesk::SignalingClient::connected);
        connect(&late, &zjdesk::SignalingClient::messageReceived, this,
            [&replay](const zjdesk::protocol::Message &message) {
                if (message.type == "annotation") replay.append(message.payload);
            });
        late.open(url);
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(late.send({"join", {{"roomId", "drawing"}, {"roomType", "meeting"}, {"name", "Late"}}}));
        QTRY_COMPARE(replay.size(), 2);
        QCOMPARE(replay.first().value("action").toString(), "started");
        QCOMPARE(replay.last().value("strokeId").toString(), "circle1");
        // A full, point-heavy canvas must replay without overflowing the signaling queue.
        late.close();
        QTest::qWait(50);
        QJsonArray points;
        for (int i = 0; i < zjdesk::protocol::MaximumAnnotationPoints; ++i)
            points.append(QJsonArray{double(i) / 257, double(i + 1) / 259});
        stroke.insert("tool", "pen");
        stroke.insert("points", points);
        for (int i = 1; i < zjdesk::protocol::MaximumAnnotationStrokes; ++i)
        {
            stroke.insert("strokeId", QString("dense%1").arg(i));
            QVERIFY(clients[0].send({"annotation", stroke}));
            QTRY_COMPARE(events[0].last().value("strokeId").toString(), QString("dense%1").arg(i));
        }
        stroke.insert("strokeId", "overflow");
        QVERIFY(clients[0].send({"annotation", stroke}));
        QTRY_COMPARE(events[0].last().value("code").toString(), "annotation_limit");
        replay.clear();
        late.open(url);
        QTRY_COMPARE(ready.size(), 2);
        QVERIFY(late.send({"join", {{"roomId", "drawing"}, {"roomType", "meeting"}, {"name", "Late"}}}));
        QTRY_COMPARE_WITH_TIMEOUT(replay.size(), zjdesk::protocol::MaximumAnnotationStrokes + 1, 10000);
        QVERIFY(clients[0].send({"annotation", {{"action", "stop"}, {"streamId", "screen1"}}}));
        QTRY_COMPARE(replay.last().value("action").toString(), "stopped");
        QVERIFY(clients[0].send({"annotation", {{"action", "start"}, {"streamId", "screen3"}}}));
        QTRY_COMPARE(replay.last().value("streamId").toString(), "screen3");
        QVERIFY(clients[1].send({"annotation", {{"action", "request_permission"}, {"streamId", "screen3"}}}));
        QTRY_COMPARE(events[1].last().value("action").toString(), "permission_result");
        QVERIFY(events[1].last().value("approved").toBool());
        const auto previousId = ids[1];
        clients[1].close();
        QTest::qWait(100);
        clients[1].open(url);
        QTRY_COMPARE(connected, 6);
        QVERIFY(clients[1].send({"join", {{"roomId", "drawing"}, {"roomType", "meeting"}, {"name", "Member1"}}}));
        QTRY_VERIFY(ids[1] != previousId);
        stroke.insert("streamId", "screen3");
        QVERIFY(clients[1].send({"annotation", stroke}));
        QTRY_COMPARE(events[1].last().value("code").toString(), "annotation_not_authorized");
        clients[0].close();
        QTRY_COMPARE(replay.last().value("action").toString(), "stopped");
        QVERIFY(events[4].isEmpty()); // No room crossover, including snapshots and owner departure.
    }

    void remoteCodePoliciesPersistAndExpire_data()
    {
        QTest::addColumn<QString>("policy");
        QTest::newRow("never") << QString("never");
        QTest::newRow("three-days") << QString("three_days");
    }

    void remoteCodePoliciesPersistAndExpire()
    {
        QFETCH(QString, policy);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto registry = directory.filePath("codes.ini");
        const QString identity(32, 'a');
        const auto key = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
        QString initial;
        {
            zjdesk::RoomServer server(nullptr, registry);
            QVERIFY(server.listen(QHostAddress::LocalHost, 0));
            const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
            zjdesk::SessionCoordinator host, duplicate;
            QVERIFY(host.setRemoteCodePolicy(policy, identity));
            QVERIFY(!host.setRemoteCodePolicy("bad", identity));
            QVERIFY(!host.setRemoteCodePolicy(policy, "bad"));
            QVERIFY(host.startRemoteAccess(url, "Host"));
            QTRY_VERIFY(!host.remoteCode().isEmpty());
            initial = host.remoteCode();
            QVERIFY(duplicate.setRemoteCodePolicy(policy, identity));
            QVERIFY(duplicate.startRemoteAccess(url, "Duplicate"));
            QTRY_COMPARE(duplicate.state(), State::Failed);
            QVERIFY(duplicate.detail().contains(QStringLiteral("另一个实例")));
        }
        {
            zjdesk::RoomServer server(nullptr, registry);
            QVERIFY(server.listen(QHostAddress::LocalHost, 0));
            const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
            zjdesk::SessionCoordinator host, controller;
            QVERIFY(host.setRemoteCodePolicy(policy, identity));
            QVERIFY(host.startRemoteAccess(url, "Restarted"));
            QTRY_COMPARE(host.remoteCode(), initial); // Client and server both recreated.
            QSettings settings(registry, QSettings::IniFormat);
            settings.setValue(key + "/createdMs", QDateTime::currentMSecsSinceEpoch() - 71LL * 60 * 60 * 1000);
            settings.sync();
            QVERIFY(QMetaObject::invokeMethod(&server, "refreshRemoteCodes", Qt::DirectConnection));
            QTest::qWait(50);
            QCOMPARE(host.remoteCode(), initial); // Not yet 72 hours old.
            QVERIFY(controller.startRemoteAccess(url, "Controller"));
            QTRY_VERIFY(!controller.remoteCode().isEmpty());
            QVERIFY(controller.connectRemote(initial));
            QTRY_COMPARE(host.state(), State::PeerAvailable);
            settings.setValue(key + "/createdMs", QDateTime::currentMSecsSinceEpoch() - 72LL * 60 * 60 * 1000);
            settings.sync();
            QVERIFY(QMetaObject::invokeMethod(&server, "refreshRemoteCodes", Qt::DirectConnection));
            QCOMPARE(host.remoteCode(), initial); // Never interrupt an existing connection.
            controller.disconnectRemote();
            QTRY_COMPARE(host.state(), State::WaitingForPeer);
            QVERIFY(QMetaObject::invokeMethod(&server, "refreshRemoteCodes", Qt::DirectConnection));
            if (policy == "three_days") QTRY_VERIFY(host.remoteCode() != initial);
            else { QTest::qWait(50); QCOMPARE(host.remoteCode(), initial); }
        }
    }

    void remoteCodePolicyRejectsLegacyServer()
    {
        QWebSocketServer server("legacy", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        connect(&server, &QWebSocketServer::newConnection, &server, [&server] {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QWebSocket::textMessageReceived, socket, [socket](const QString &) {
                socket->sendTextMessage(zjdesk::protocol::encode({"remote_registered", {{"code", "123456789"}}}));
            });
        });
        zjdesk::SessionCoordinator host;
        QVERIFY(host.setRemoteCodePolicy("never", QString(32, 'b')));
        QVERIFY(host.startRemoteAccess(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())), "Host"));
        QTRY_COMPARE(host.state(), State::Failed);
        QVERIFY(host.detail().contains(QStringLiteral("更新服务端")));
    }

    void remoteCodePolicyReportsRegistryFailure()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        // An existing directory cannot be used as the registry INI file.
        zjdesk::RoomServer server(nullptr, directory.path());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        zjdesk::SessionCoordinator host;
        QVERIFY(host.setRemoteCodePolicy("never", QString(32, 'c')));
        QVERIFY(host.startRemoteAccess(QUrl(QString("ws://127.0.0.1:%1").arg(server.port())), "Host"));
        QTRY_COMPARE(host.state(), State::Failed);
        QVERIFY(host.remoteCode().isEmpty());
        QVERIFY(host.detail().contains(QStringLiteral("读写权限")));
    }

    void remoteIpRequiresMutualAddressAndHandlesAmbiguity()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator a, b, c;
        QVERIFY(a.startRemoteAccess(url, "A", "127.0.0.1"));
        QVERIFY(b.startRemoteAccess(url, "B", "127.0.0.2"));
        QTRY_VERIFY(!a.remoteCode().isEmpty() && !b.remoteCode().isEmpty());
        QVERIFY(a.connectRemoteIp());
        QTRY_VERIFY(!a.remoteConnecting());
        QCOMPARE(a.state(), State::WaitingForPeer);
        QVERIFY(a.detail().contains(QStringLiteral("未验证")));
        b.leave();
        QVERIFY(b.startRemoteAccess(url, "B", "127.0.0.1"));
        QTRY_VERIFY(!b.remoteCode().isEmpty());
        QVERIFY(c.startRemoteAccess(url, "C", "127.0.0.1"));
        QTRY_VERIFY(!c.remoteCode().isEmpty());
        QVERIFY(a.connectRemoteIp());
        QTRY_VERIFY(!a.remoteConnecting());
        QCOMPARE(a.state(), State::WaitingForPeer);
        c.leave();
        QTest::qWait(100); // Allow the server to process C's socket closure.
        QVERIFY(a.connectRemoteIp());
        QTRY_COMPARE(a.state(), State::PeerAvailable);
        QTRY_COMPARE(b.state(), State::PeerAvailable);
        a.disconnectRemote();
        QTRY_COMPARE(b.state(), State::WaitingForPeer);
    }

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

    void endingRemoteControlDisconnectsBothPeers_data()
    {
        QTest::addColumn<bool>("hostEnds");
        QTest::newRow("controller-ends") << false;
        QTest::newRow("host-ends") << true;
    }

    void endingRemoteControlDisconnectsBothPeers()
    {
        QFETCH(bool, hostEnds);
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.port()));
        zjdesk::SessionCoordinator controller, host;
        QSignalSpy controllerHistory(&controller, &zjdesk::SessionCoordinator::remotePeerConnected);
        QSignalSpy hostHistory(&host, &zjdesk::SessionCoordinator::remotePeerConnected);
        QVERIFY(controller.startRemoteAccess(url, "Controller"));
        QVERIFY(host.startRemoteAccess(url, "Host"));
        QTRY_VERIFY(!controller.remoteCode().isEmpty() && !host.remoteCode().isEmpty());
        const auto hostCode = host.remoteCode();
        const auto controllerCode = controller.remoteCode();
        QVERIFY(controller.connectRemote(hostCode));
        QTRY_COMPARE(host.state(), State::PeerAvailable);
        QTRY_COMPARE(controllerHistory.count(), 1);
        QCOMPARE(controllerHistory.first(), QVariantList({hostCode, QString("Host")}));
        QCOMPARE(hostHistory.first(), QVariantList({controllerCode, QString("Controller")}));
        (hostEnds ? host : controller).stopControl();
        QTRY_COMPARE(controller.state(), State::WaitingForPeer);
        QTRY_COMPARE(host.state(), State::WaitingForPeer);
        QCOMPARE(controller.remoteState(), zjdesk::SessionCoordinator::RemoteState::Idle);
        QCOMPARE(host.remoteState(), zjdesk::SessionCoordinator::RemoteState::Idle);
        QVERIFY(controller.peerId().isEmpty() && host.peerId().isEmpty());
        QVERIFY(!controller.canRequestControl() && !host.canRequestControl());
        QCOMPARE(server.roomCount(), 0);
        QCOMPARE(controller.remoteCode(), controllerCode);
        QCOMPARE(host.remoteCode(), hostCode);
        QTest::qWait(100);
        QCOMPARE(controllerHistory.count(), 1); // No automatic redial after ending control.
        QVERIFY(!controller.connectRemote({}));
        QVERIFY(controller.connectRemote(hostCode)); // A new explicit code submission is required.
        QTRY_COMPARE(controllerHistory.count(), 2);
        QTRY_COMPARE(hostHistory.count(), 2);
        controller.disconnectRemote();
        QTRY_COMPARE(host.state(), State::WaitingForPeer);
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
