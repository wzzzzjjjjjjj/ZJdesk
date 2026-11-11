#include "session/peer_connection.h"
#include <QElapsedTimer>
#include <QTcpServer>
#include <QWebSocketServer>
#include <QtTest>

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
    void profileValidation()
    {
        QVERIFY(zjdesk::PeerProfile({"linux-wzj", "203.0.113.9", "100.100.100.100"}).error().isEmpty());
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
