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
};

QTEST_MAIN(WindowTests)
#include "tst_window.moc"
