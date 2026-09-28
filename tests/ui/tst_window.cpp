#include "main_window.h"
#include "video_surface.h"
#include "signaling/room_server.h"
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QScreen>
#include <QtTest>

class WindowTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QApplication::setStyle("Fusion");
        QFont font;
        font.setFamilies({"Microsoft YaHei UI", "Noto Sans CJK SC", "sans-serif"});
        font.setPointSize(10);
        QApplication::setFont(font);
        QFile style(":/resources/styles.qss");
        QVERIFY(style.open(QIODevice::ReadOnly));
        qApp->setStyleSheet(QString::fromUtf8(style.readAll()));
    }

    void actualWindowsJoinAndLeave()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const auto url = QString("ws://127.0.0.1:%1").arg(server.port());
        MainWindow alice(url, "demo", "Alice");
        MainWindow bob(url, "demo", "Bob");
        alice.show();
        QVERIFY(QTest::qWaitForWindowExposed(&alice));
        bob.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bob));
        auto *aliceJoin = alice.findChild<QPushButton *>("primaryButton");
        auto *bobJoin = bob.findChild<QPushButton *>("primaryButton");
        auto *aliceStatus = alice.findChild<QLabel *>("statusBadge");
        auto *bobStatus = bob.findChild<QLabel *>("statusBadge");
        auto *members = alice.findChild<QLabel *>("memberCount");
        auto *surface = alice.findChild<QOpenGLWidget *>();
        QVERIFY(aliceJoin && bobJoin && aliceStatus && bobStatus && members && surface);
        QTRY_VERIFY(surface->isValid());
        QTest::mouseClick(aliceJoin, Qt::LeftButton);
        QTRY_COMPARE(aliceStatus->text(), QStringLiteral("等待成员"));
        QTest::mouseClick(bobJoin, Qt::LeftButton);
        QTRY_COMPARE(aliceStatus->text(), QStringLiteral("双方已入会"));
        QTRY_COMPARE(bobStatus->text(), QStringLiteral("双方已入会"));
        QCOMPARE(members->text(), QStringLiteral("2 / 2 人"));
        QVERIFY(QDir().mkpath("output"));
        alice.raise();
        QTest::qWait(150);
        // Capture the composed native window, including the OpenGL framebuffer.
        QVERIFY(alice.screen()->grabWindow(alice.winId()).save("output/ui-workspace.png"));
        const auto navButtons = alice.findChildren<QPushButton *>("navButton");
        QVERIFY(navButtons.size() == 2);
        QTest::mouseClick(navButtons.at(1), Qt::LeftButton);
        QTest::qWait(100);
        QVERIFY(alice.screen()->grabWindow(alice.winId()).save("output/ui-diagnostics.png"));
        QTest::mouseClick(bobJoin, Qt::LeftButton);
        QTRY_COMPARE(aliceStatus->text(), QStringLiteral("等待成员"));
        QTest::mouseClick(aliceJoin, Qt::LeftButton);
        QTRY_COMPARE(server.roomCount(), 0);
    }

    void localPreviewDisplaysRealFramesAndStops()
    {
        MainWindow window("ws://127.0.0.1:8765", "demo", "Tester");
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *button = window.findChild<QPushButton *>("previewButton");
        auto *surface = dynamic_cast<VideoSurface *>(window.findChild<QOpenGLWidget *>());
        QVERIFY(button && surface);
        if (!button->isEnabled())
            QSKIP("GStreamer is not enabled in this build");
        QTRY_VERIFY(surface->isValid());
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(surface->hasFrame(), 10000);
        QCOMPARE(button->text(), QStringLiteral("停止预览"));
        const auto first = surface->grabFramebuffer();
        QTest::qWait(200);
        const auto second = surface->grabFramebuffer();
        QVERIFY(!first.isNull());
        QVERIFY(first != second);
        QVERIFY(QDir().mkpath("output"));
        window.raise();
        QTest::qWait(100);
        QVERIFY(window.screen()->grabWindow(window.winId()).save("output/ui-local-preview.png"));
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_COMPARE(button->text(), QStringLiteral("开始预览"));
        QVERIFY(!surface->hasFrame());
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY(surface->hasFrame());
    }

    void screenShareCapturesDesktopAndDisplaysAtPeer()
    {
        zjdesk::RoomServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const auto url = QString("ws://127.0.0.1:%1").arg(server.port());
        MainWindow alice(url, "desktop", "Alice"), bob(url, "desktop", "Bob");
        alice.show();
        bob.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bob));
        auto *aliceShare = alice.findChild<QPushButton *>("shareButton");
        auto *bobShare = bob.findChild<QPushButton *>("shareButton");
        auto *bobStatus = bob.findChild<QLabel *>("shareStatus");
        auto *surface = dynamic_cast<VideoSurface *>(bob.findChild<QOpenGLWidget *>());
        QVERIFY(aliceShare && bobShare && bobStatus && surface);
        if (!alice.findChild<QPushButton *>("previewButton")->isEnabled())
            QSKIP("GStreamer is not enabled in this build");
        QVERIFY(!aliceShare->isEnabled());
        alice.findChild<QPushButton *>("primaryButton")->click();
        bob.findChild<QPushButton *>("primaryButton")->click();
        QTRY_VERIFY(aliceShare->isEnabled());
        // A known visible desktop marker proves the peer receives captured pixels.
        QWidget marker(nullptr, Qt::Tool | Qt::WindowStaysOnTopHint);
        marker.resize(400, 300);
        marker.move(QGuiApplication::primaryScreen()->geometry().topLeft());
        marker.setStyleSheet("background: #ff00ff;");
        marker.show();
        QVERIFY(QTest::qWaitForWindowExposed(&marker));
        aliceShare->click();
        QTRY_VERIFY_WITH_TIMEOUT(bobStatus->text().contains(QStringLiteral("正在接收对方屏幕"))
            && surface->hasFrame(), 20000);
        QVERIFY(!bobShare->isEnabled());
        const auto hasColor = [surface](bool green) {
            const auto image = surface->grabFramebuffer();
            int count = 0;
            for (int y = 0; y < image.height(); y += 4)
                for (int x = 0; x < image.width(); x += 4)
                {
                    const auto color = image.pixelColor(x, y);
                    if (green ? color.green() > 200 && color.red() < 60 && color.blue() < 60
                              : color.red() > 200 && color.blue() > 200 && color.green() < 60)
                        ++count;
                }
            return count > 100;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasColor(false), 10000);
        marker.setStyleSheet("background: #00ff00;");
        QTRY_VERIFY_WITH_TIMEOUT(hasColor(true), 10000);
        qInfo().noquote() << "Desktop receiver:" << bobStatus->text();
        marker.hide();
        bob.raise();
        QTest::qWait(150);
        QVERIFY(QDir().mkpath("output"));
        QVERIFY(bob.screen()->grabWindow(bob.winId()).save("output/ui-screen-share.png"));
        aliceShare->click();
        QTRY_COMPARE(aliceShare->text(), QStringLiteral("共享主屏幕"));
        QTRY_COMPARE(bobShare->text(), QStringLiteral("共享主屏幕"));
        QVERIFY(!surface->hasFrame());
        QTRY_VERIFY(bobShare->isEnabled());
        bobShare->click();
        auto *aliceSurface = dynamic_cast<VideoSurface *>(alice.findChild<QOpenGLWidget *>());
        QTRY_VERIFY_WITH_TIMEOUT(aliceSurface->hasFrame(), 20000);
        bob.findChild<QPushButton *>("primaryButton")->click();
        QTRY_VERIFY(!aliceSurface->hasFrame());
        QVERIFY(!aliceShare->isEnabled());
    }
};

QTEST_MAIN(WindowTests)
#include "tst_window.moc"
