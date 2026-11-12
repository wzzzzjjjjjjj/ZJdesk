#include "session/peer_connection.h"
#include <QElapsedTimer>
#include <QTcpServer>
#include <QWebSocketServer>
#include <QtTest>
#include "protocol/message.h"
#include <QJsonArray>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QScopeGuard>

class PeerConnectionTests : public QObject
{
    Q_OBJECT
    static void useLoopbackRoutes(zjdesk::PeerConnection &connection)
    {
        // Isolate OS interface discovery while exercising real sockets and the worker thread.
        connection.resolve_ = [](const QString &address, const QUrl &url, bool) {
            zjdesk::NetworkConfiguration configuration;
#ifdef Q_OS_WIN
            configuration.windowsAddress = "192.0.2.1";
            configuration.linuxAddress = address;
#else
            configuration.windowsAddress = address;
            configuration.linuxAddress = "192.0.2.1";
#endif
            configuration.serverUrl = url;
            return configuration;
        };
    }

    static QString selectedPeer(const QSignalSpy &routes)
    {
        if (routes.isEmpty()) return {};
        return qvariant_cast<zjdesk::NetworkConfiguration>(routes.last().at(0)).peerAddress();
    }

private slots:
    void cachedLanWaitsForTailscaleBootstrap()
    {
        QWebSocketServer lan("LAN", QWebSocketServer::NonSecureMode);
        QWebSocketServer overlay("Tailscale", QWebSocketServer::NonSecureMode);
        QVERIFY(lan.listen(QHostAddress("127.0.0.2"), 0));
        bool overlayConnected = false;
        bool lanBeforeOverlay = false;
        int lanConnections = 0;
        for (auto *server : {&lan, &overlay})
            connect(server, &QWebSocketServer::newConnection, this, [&, server] {
                auto *socket = server->nextPendingConnection();
                socket->setParent(server);
                if (server == &overlay) overlayConnected = true;
                else { ++lanConnections; lanBeforeOverlay = !overlayConnected; }
                connect(socket, &QWebSocket::textMessageReceived, socket, [socket](const QString &text) {
                    const auto request = zjdesk::protocol::decode(text);
                    if (!request || request->type != "network_info") return;
                    socket->sendTextMessage(zjdesk::protocol::encode({"network_info", {
                        {"requestId", request->payload.value("requestId")},
                        {"addresses", QJsonArray{"127.0.0.1", "127.0.0.2"}}, {"deviceName", "Test-PC"}}}));
                });
            });
        zjdesk::PeerConnection connection;
        useLoopbackRoutes(connection);
        connection.matchLan_ = [](const QStringList &) { return QStringList{"127.0.0.2"}; };
        zjdesk::PeerProfile profile{"peer", "127.0.0.2", "127.0.0.1"};
        profile.autoDiscover = true;
        profile.lanAddresses = {"127.0.0.2"};
        QSignalSpy routes(&connection, &zjdesk::PeerConnection::routeChanged);
        QSignalSpy discovered(&connection, &zjdesk::PeerConnection::networkDiscovered);
        connection.start(profile, QUrl(QString("ws://127.0.0.1:%1").arg(lan.serverPort())));
        QTest::qWait(150);
        QCOMPARE(lanConnections, 0);
        QVERIFY(routes.isEmpty());
        QVERIFY(overlay.listen(QHostAddress::LocalHost, lan.serverPort()));
        connection.start(profile, QUrl(QString("ws://127.0.0.1:%1").arg(lan.serverPort())));
        QTRY_COMPARE_WITH_TIMEOUT(selectedPeer(routes), QString("127.0.0.2"), 8500);
        QVERIFY(!lanBeforeOverlay);
        QVERIFY(!discovered.isEmpty());
        QCOMPARE(discovered.last().at(2).toString(), QString("Test-PC"));
    }

    void automaticDiscoveryRefreshesDhcpAndRejectsWrongLanPeer()
    {
        QWebSocketServer server("Discovery", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::AnyIPv4, 0));
        QStringList inventory{"127.0.0.1", "127.0.0.2"};
        bool wrongLan = false;
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            auto *socket = server.nextPendingConnection();
            socket->setParent(&server);
            connect(socket, &QWebSocket::textMessageReceived, socket, [&, socket](const QString &text) {
                const auto request = zjdesk::protocol::decode(text);
                if (!request || request->type != "network_info") return;
                auto addresses = inventory;
                if (wrongLan && socket->localAddress() == QHostAddress("127.0.0.3")) addresses.removeAll("127.0.0.1");
                socket->sendTextMessage(zjdesk::protocol::encode({"network_info", {
                    {"requestId", request->payload.value("requestId")}, {"addresses", QJsonArray::fromStringList(addresses)}}}));
            });
        });
        zjdesk::PeerConnection connection;
        useLoopbackRoutes(connection);
        connection.matchLan_ = [](const QStringList &addresses) {
            auto result = addresses;
            result.removeAll("127.0.0.1");
            return result;
        };
        QSignalSpy routes(&connection, &zjdesk::PeerConnection::routeChanged);
        QSignalSpy discovered(&connection, &zjdesk::PeerConnection::networkDiscovered);
        const QUrl url(QString("ws://127.0.0.1:%1").arg(server.serverPort()));
        connection.start({"peer", {}, "127.0.0.1", true}, url);
        QTRY_COMPARE(selectedPeer(routes), QString("127.0.0.2"));
        QVERIFY(!discovered.isEmpty());
        inventory = {"127.0.0.1", "127.0.0.3"};
        QTRY_COMPARE_WITH_TIMEOUT(selectedPeer(routes), QString("127.0.0.3"), 7500);
        wrongLan = true;
        QTRY_COMPARE_WITH_TIMEOUT(selectedPeer(routes), QString("127.0.0.1"), 10000);
        wrongLan = false;
        QTRY_COMPARE_WITH_TIMEOUT(selectedPeer(routes), QString("127.0.0.3"), 8500);
        inventory = {"127.0.0.1"};
        QTRY_COMPARE_WITH_TIMEOUT(selectedPeer(routes), QString("127.0.0.1"), 7500);
        connection.stop();
        const auto count = discovered.size();
        QTest::qWait(200);
        QCOMPARE(discovered.size(), count);
    }

    void sshBackupQueriesAddressesWithoutBlockingOverlay_data()
    {
        QTest::addColumn<bool>("overlayAvailable");
        QTest::newRow("legacy-overlay") << true;
        QTest::newRow("overlay-service-unavailable") << false;
    }

    void sshBackupQueriesAddressesWithoutBlockingOverlay()
    {
        QFETCH(bool, overlayAvailable);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
#ifdef Q_OS_WIN
        const auto suffix = QStringLiteral(".exe");
#else
        const auto suffix = QString();
#endif
        const auto stub = QCoreApplication::applicationDirPath() + "/zjdesk-network-ping-stub" + suffix;
        const auto ssh = temporary.filePath("ssh" + suffix);
        QVERIFY(QFile::copy(stub, ssh));
        QFile::setPermissions(ssh, QFile::permissions(ssh) | QFile::ExeOwner);
        const auto originalPath = qgetenv("PATH");
        auto cleanup = qScopeGuard([&] { qputenv("PATH", originalPath); qunsetenv("ZJDESK_TEST_SSH_ADDRESSES"); });
        qputenv("PATH", temporary.path().toUtf8() + QDir::listSeparator().toLatin1() + originalPath);
        qputenv("ZJDESK_TEST_SSH_ADDRESSES", "1: ts inet 127.0.0.1/32 scope global\n2: eth0 inet 127.0.0.2/24 scope global\n");
        QWebSocketServer server("Old service", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::AnyIPv4, 0));
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            auto *socket = server.nextPendingConnection();
            socket->setParent(&server);
            if (!overlayAvailable && socket->localAddress() == QHostAddress("127.0.0.1")) socket->abort();
        });
        zjdesk::PeerConnection connection;
        useLoopbackRoutes(connection);
        connection.matchLan_ = [](const QStringList &addresses) {
            return addresses.contains("127.0.0.2") ? QStringList{"127.0.0.2"} : QStringList{};
        };
        QSignalSpy routes(&connection, &zjdesk::PeerConnection::routeChanged);
        QSignalSpy discovered(&connection, &zjdesk::PeerConnection::networkDiscovered);
        connection.start({"peer", {}, "127.0.0.1", true}, QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())));
        if (overlayAvailable) QTRY_COMPARE(selectedPeer(routes), QString("127.0.0.1"));
        QTRY_COMPARE_WITH_TIMEOUT(discovered.size(), 1, 4500);
        QVERIFY(discovered.first().at(1).toStringList().contains("127.0.0.2"));
        QTRY_COMPARE_WITH_TIMEOUT(selectedPeer(routes), QString("127.0.0.2"), 4500);
        connection.stop();
    }

    void profileValidation()
    {
        QVERIFY(zjdesk::PeerProfile({"linux-wzj", "203.0.113.9", "100.100.100.100"}).error().isEmpty());
        QVERIFY(zjdesk::PeerProfile({"peer", "obsolete DHCP address", "100.100.100.100", true}).error().isEmpty());
        QVERIFY(!zjdesk::PeerProfile({"peer", "203.0.113.9", {}, true}).error().isEmpty());
        zjdesk::PeerProfile invalidUser{"peer", {}, "100.100.100.100", true};
        invalidUser.sshUser = "user@other-host";
        QVERIFY(!invalidUser.error().isEmpty());
        for (const auto &profile : {
            zjdesk::PeerProfile{"bad name", "203.0.113.9", {}},
            zjdesk::PeerProfile{"linux-wzj", {}, {}},
            zjdesk::PeerProfile{"linux-wzj", "127.0.0.1", {}},
            zjdesk::PeerProfile{"linux-wzj", "100.100.100.100", {}},
            zjdesk::PeerProfile{"linux-wzj", {}, "203.0.113.9"},
            zjdesk::PeerProfile{"linux-wzj", "203.0.113.009", {}}})
            QVERIFY(!profile.error().isEmpty());
    }

    void aliasReplacesStartupServerHostAndKeepsPort()
    {
        zjdesk::PeerConnection connection;
        const auto route = zjdesk::NetworkSetup::configurationForPeer("203.0.113.9");
        const bool overlay = QHostAddress(route.localAddress()).isInSubnet(QHostAddress("100.64.0.0"), 10);
        const auto selected = connection.resolve_("203.0.113.9", QUrl("ws://198.51.100.7:18765"), overlay);
        QCOMPARE(selected.peerAddress(), QString("203.0.113.9"));
        QCOMPARE(selected.serverUrl.host(), route.linuxAddress);
        QCOMPARE(selected.serverUrl.port(), 18765);
    }

    void lanPriorityFailoverAndRecovery()
    {
        QWebSocketServer lan("LAN", QWebSocketServer::NonSecureMode);
        QWebSocketServer overlay("Tailscale", QWebSocketServer::NonSecureMode);
        QVERIFY(lan.listen(QHostAddress("127.0.0.1"), 0));
        const auto port = lan.serverPort();
        QVERIFY(overlay.listen(QHostAddress("127.0.0.2"), port));
        QWebSocket *lanPeer = nullptr;
        connect(&lan, &QWebSocketServer::newConnection, this, [&] {
            lanPeer = lan.nextPendingConnection();
            lanPeer->setParent(&lan);
        });
        connect(&overlay, &QWebSocketServer::newConnection, this, [&] {
            overlay.nextPendingConnection()->setParent(&overlay);
        });
        zjdesk::PeerConnection connection;
        useLoopbackRoutes(connection);
        QSignalSpy routes(&connection, &zjdesk::PeerConnection::routeChanged);
        QSignalSpy lost(&connection, &zjdesk::PeerConnection::unavailable);
        const zjdesk::PeerProfile profile{"linux-wzj", "127.0.0.1", "127.0.0.2"};
        const QUrl url(QString("ws://127.0.0.1:%1").arg(port));
        connection.start(profile, url);
        QTRY_COMPARE(selectedPeer(routes), QString("127.0.0.1"));
        QCOMPARE(routes.size(), 1);
        QVERIFY(lanPeer);
        lan.close();
        lanPeer->abort();
        QTRY_COMPARE(selectedPeer(routes), QString("127.0.0.2"));
        QCOMPARE(lost.size(), 0);
        QVERIFY(lan.listen(QHostAddress("127.0.0.1"), port));
        QTRY_COMPARE_WITH_TIMEOUT(selectedPeer(routes), QString("127.0.0.1"), 7000);
        connection.stop();
        const auto count = routes.size();
        lanPeer->abort();
        QTest::qWait(100);
        QCOMPARE(routes.size(), count);
        QCOMPARE(lost.size(), 0);
    }

    void unresponsiveLanTimesOutBeforeOverlayAndStopDiscardsCallbacks()
    {
        QTcpServer silent;
        QVERIFY(silent.listen(QHostAddress("127.0.0.1"), 0));
        QWebSocketServer overlay("Tailscale", QWebSocketServer::NonSecureMode);
        QVERIFY(overlay.listen(QHostAddress("127.0.0.2"), silent.serverPort()));
        connect(&overlay, &QWebSocketServer::newConnection, this, [&] {
            overlay.nextPendingConnection()->setParent(&overlay);
        });
        zjdesk::PeerConnection connection;
        useLoopbackRoutes(connection);
        QSignalSpy routes(&connection, &zjdesk::PeerConnection::routeChanged);
        QElapsedTimer elapsed;
        elapsed.start();
        const auto url = QUrl(QString("ws://127.0.0.1:%1").arg(silent.serverPort()));
        connection.start({"linux-wzj", "127.0.0.1", "127.0.0.2"}, url);
        QTest::qWait(100);
        QVERIFY(routes.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(selectedPeer(routes), QString("127.0.0.2"), 4500);
        QVERIFY(elapsed.elapsed() >= 2700);
        routes.clear();
        connection.start({"replacement", {}, "127.0.0.2"}, url);
        connection.stop();
        QTest::qWait(200);
        QVERIFY(routes.isEmpty());
    }
};

QTEST_GUILESS_MAIN(PeerConnectionTests)
#include "tst_peer_connection.moc"
