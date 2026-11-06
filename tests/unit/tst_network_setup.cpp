#include "session/network_setup.h"
#include "signaling/room_server.h"
#include <QDir>
#include <QFile>
#include <QNetworkProxy>
#include <QTemporaryDir>
#include <QWebSocketServer>
#include <QtTest>

class NetworkSetupTests : public QObject
{
    Q_OBJECT
    QTemporaryDir temporary_;
    QByteArray originalPath_;
    QByteArray originalBinding_;
    QString local_;

    zjdesk::NetworkConfiguration configuration(int port) const
    {
        zjdesk::NetworkConfiguration result;
#ifdef Q_OS_WIN
        result.windowsAddress = local_;
        result.linuxAddress = "203.0.113.9";
#else
        result.windowsAddress = "203.0.113.9";
        result.linuxAddress = local_;
#endif
        result.serverUrl = QUrl(QString("ws://%1:%2").arg(local_).arg(port));
        return result;
    }

private slots:
    void peerAddressSelectsLocalRouteAndLinuxService()
    {
        const auto configuration = zjdesk::NetworkSetup::configurationForPeer("203.0.113.9");
        QCOMPARE(configuration.peerAddress(), QString("203.0.113.9"));
        QVERIFY(zjdesk::NetworkSetup::localAddresses().contains(configuration.localAddress()));
        QCOMPARE(configuration.serverUrl.host(), configuration.linuxAddress);
        QCOMPARE(configuration.serverUrl.port(), 8765);
        const auto loopback = zjdesk::NetworkSetup::configurationForPeer("203.0.113.9", QUrl("ws://127.0.0.1:1234"));
        QCOMPARE(loopback.serverUrl.host(), loopback.linuxAddress);
        QCOMPARE(loopback.serverUrl.port(), 1234);
    }

    void invalidOrOwnPeerCannotVerify()
    {
        zjdesk::NetworkSetup setup;
        QSignalSpy result(&setup, &zjdesk::NetworkSetup::finished);
        for (const auto &peer : QStringList{"", "invalid", "127.0.0.1", "169.254.1.1", local_})
        {
            setup.verifyPeer(peer);
            QVERIFY(!setup.verified());
        }
        QCOMPARE(result.size(), 5);
    }

    void automaticLocalServiceAndExistingService()
    {
        zjdesk::NetworkSetup owner;
        QVERIFY(owner.startLocalServer(0));
        const auto port = owner.server_->port();
        QVERIFY(port != 0);
        QVERIFY(owner.startLocalServer(port));
        zjdesk::NetworkSetup another;
        QVERIFY(!another.startLocalServer(port));
        QSignalSpy result(&another, &zjdesk::NetworkSetup::finished);
        another.verify(configuration(port));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(another.verified());
    }

    void initTestCase()
    {
        QVERIFY(temporary_.isValid());
        const auto addresses = zjdesk::NetworkSetup::localAddresses();
        if (addresses.isEmpty()) QSKIP("No active IPv4 interface for network setup tests");
        local_ = addresses.first();
        originalPath_ = qgetenv("PATH");
        originalBinding_ = qgetenv("ZJDESK_WEBRTC_LOCAL_ADDRESS");
#ifdef Q_OS_WIN
        const auto suffix = QStringLiteral(".exe");
#else
        const auto suffix = QString();
#endif
        const auto source = QCoreApplication::applicationDirPath() + "/zjdesk-network-ping-stub" + suffix;
        const auto destination = temporary_.filePath("ping" + suffix);
        QVERIFY(QFile::copy(source, destination));
        QFile::setPermissions(destination, QFile::permissions(destination) | QFile::ExeOwner);
        qputenv("PATH", temporary_.path().toUtf8() + QDir::listSeparator().toLatin1() + originalPath_);
    }

    void init()
    {
        qputenv("ZJDESK_TEST_PING", "ok");
        qputenv("ZJDESK_WEBRTC_LOCAL_ADDRESS", "previous-binding");
    }

    void cleanupTestCase()
    {
        qputenv("PATH", originalPath_);
        if (originalBinding_.isNull()) qunsetenv("ZJDESK_WEBRTC_LOCAL_ADDRESS");
        else qputenv("ZJDESK_WEBRTC_LOCAL_ADDRESS", originalBinding_);
        qunsetenv("ZJDESK_TEST_PING");
    }

    void invalidAddress_data()
    {
        QTest::addColumn<QString>("address");
        for (const auto &value : {"", "not-an-ip", "192.168.1.999", "192.168.1.2/24", "127.0.0.1",
                                 "169.254.1.2", "224.0.0.1", "255.255.255.255", "0.0.0.0", "::1", "10.1"})
            QTest::newRow(value) << QString(value);
    }

    void invalidAddress()
    {
        QFETCH(QString, address);
        for (bool windows : {true, false})
        {
            auto config = configuration(8765);
            if (windows) config.windowsAddress = address;
            else config.linuxAddress = address;
            zjdesk::NetworkSetup setup;
            QSignalSpy result(&setup, &zjdesk::NetworkSetup::finished);
            setup.verify(config);
            QCOMPARE(result.size(), 1);
            QVERIFY(!result.first().first().toBool());
            QVERIFY(!setup.verified());
            QCOMPARE(qgetenv("ZJDESK_WEBRTC_LOCAL_ADDRESS"), QByteArray("previous-binding"));
        }
    }

    void rejectsSameStaleAndUnrelatedServer()
    {
        auto config = configuration(8765);
        QVERIFY(zjdesk::NetworkSetup::configurationError(config).isEmpty());
        config.windowsAddress = config.linuxAddress;
        QVERIFY(!zjdesk::NetworkSetup::configurationError(config).isEmpty());
        config = configuration(8765);
#ifdef Q_OS_WIN
        config.windowsAddress = "203.0.113.8";
#else
        config.linuxAddress = "203.0.113.8";
#endif
        QVERIFY(!zjdesk::NetworkSetup::configurationError(config).isEmpty());
        config = configuration(8765);
        config.serverUrl = QUrl("ws://203.0.113.7:8765");
        QVERIFY(!zjdesk::NetworkSetup::configurationError(config).isEmpty());
        config.serverUrl = QUrl(QString("http://%1:8765").arg(local_));
        QVERIFY(!zjdesk::NetworkSetup::configurationError(config).isEmpty());
        config = configuration(0);
        QVERIFY(!zjdesk::NetworkSetup::configurationError(config).isEmpty());
    }

    void successfulProbeBindsMedia()
    {
        QWebSocketServer server("network fixture", QWebSocketServer::NonSecureMode);
        server.setProxy(QNetworkProxy::NoProxy);
        QVERIFY(server.listen(QHostAddress(local_), 0));
        zjdesk::NetworkSetup setup;
        QSignalSpy result(&setup, &zjdesk::NetworkSetup::finished);
        setup.verify(configuration(server.serverPort()));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(result.first().first().toBool());
        QVERIFY(setup.verified());
        QCOMPARE(qgetenv("ZJDESK_WEBRTC_LOCAL_ADDRESS"), local_.toUtf8());
        // Previously verified data does not authorize a subsequent failed attempt.
        setup.verify(configuration(0));
        QVERIFY(!setup.verified());
    }

    void failedPingDoesNotConnectServer_data()
    {
        QTest::addColumn<QByteArray>("mode");
        QTest::newRow("nonzero-exit") << QByteArray("fail");
#ifdef Q_OS_WIN
        QTest::newRow("windows-unreachable-exit-zero") << QByteArray("unreachable");
#endif
    }

    void failedPingDoesNotConnectServer()
    {
        QFETCH(QByteArray, mode);
        qputenv("ZJDESK_TEST_PING", mode);
        QWebSocketServer server("network fixture", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress(local_), 0));
        QSignalSpy connections(&server, &QWebSocketServer::newConnection);
        zjdesk::NetworkSetup setup;
        QSignalSpy result(&setup, &zjdesk::NetworkSetup::finished);
        setup.verify(configuration(server.serverPort()));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!setup.verified());
        QCOMPARE(connections.size(), 0);
        QCOMPARE(qgetenv("ZJDESK_WEBRTC_LOCAL_ADDRESS"), QByteArray("previous-binding"));
    }

    void refusedServerAndRetry()
    {
        QWebSocketServer server("network fixture", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress(local_), 0));
        const auto port = server.serverPort();
        server.close();
        zjdesk::NetworkSetup setup;
        QSignalSpy result(&setup, &zjdesk::NetworkSetup::finished);
        setup.verify(configuration(port));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!setup.verified());
        QVERIFY(result.first().at(1).toString().contains(QString::number(port)));
        QVERIFY(server.listen(QHostAddress(local_), port));
        setup.verify(configuration(port));
        QTRY_COMPARE(result.size(), 2);
        QVERIFY(setup.verified());
    }

    void timeoutCancelAndRetry()
    {
        qputenv("ZJDESK_TEST_PING", "slow");
        zjdesk::NetworkSetup setup;
        QSignalSpy result(&setup, &zjdesk::NetworkSetup::finished);
        setup.verify(configuration(8765));
        setup.deadline_.start(20);
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!setup.verified());
        setup.verify(configuration(8765));
        setup.cancel();
        QTest::qWait(50);
        QCOMPARE(result.size(), 1);
        QVERIFY(!setup.verified());
        qputenv("ZJDESK_TEST_PING", "ok");
        QWebSocketServer server("network fixture", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress(local_), 0));
        setup.verify(configuration(server.serverPort()));
        QTRY_COMPARE(result.size(), 2);
        QVERIFY(setup.verified());
    }
};

QTEST_GUILESS_MAIN(NetworkSetupTests)
#include "tst_network_setup.moc"
