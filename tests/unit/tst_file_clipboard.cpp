#include "remote-control/file_clipboard.h"
#include <QDir>
#include <QFile>
#include <QtTest>

using zjdesk::FileClipboard;

class FileClipboardTests : public QObject
{
    Q_OBJECT
private slots:
    void gnomeAndUriFileLists()
    {
        QMimeData mime;
        mime.setData("x-special/gnome-copied-files", "copy\nfile:///tmp/a%20b\nfile:///tmp/%E4%B8%AD%E6%96%87\n");
        QCOMPARE(FileClipboard::copiedFiles(&mime), (QList<QUrl>{QUrl::fromLocalFile("/tmp/a b"), QUrl::fromLocalFile(QStringLiteral("/tmp/中文"))}));
        mime.setUrls({QUrl::fromLocalFile("/tmp/other")});
        QCOMPARE(FileClipboard::copiedFiles(&mime), mime.urls());
        mime.clear(); mime.setText("/tmp/not-a-file-copy");
        QVERIFY(FileClipboard::copiedFiles(&mime).isEmpty());
        mime.setUrls({QUrl::fromLocalFile("/tmp/received")});
        mime.setData("application/x-zjdesk-file-clipboard", "received-batch");
        QVERIFY(FileClipboard::copiedFiles(&mime).isEmpty()); // Imported files must not echo back.
    }

    void filesFoldersBinaryAndEmptyRoundTrip()
    {
        QTemporaryDir source;
        QVERIFY(source.isValid());
        QVERIFY(QDir().mkpath(source.filePath(QStringLiteral("中文目录/empty"))));
        const QByteArray payload = QByteArray::fromHex("00ff01aa") + QByteArray(160000, '\x91');
        QFile original(source.filePath(QStringLiteral("中文目录/a b.bin")));
        QVERIFY(original.open(QIODevice::WriteOnly)); QCOMPARE(original.write(payload), payload.size()); original.close();
        QFile empty(source.filePath("zero")); QVERIFY(empty.open(QIODevice::WriteOnly)); empty.close();
        FileClipboard sender, receiver;
        connect(&sender, &FileClipboard::sendMessage, &receiver, &FileClipboard::receive, Qt::QueuedConnection);
        connect(&receiver, &FileClipboard::sendMessage, &sender, &FileClipboard::receive, Qt::QueuedConnection);
        QSignalSpy ready(&receiver, &FileClipboard::filesReady);
        sender.start(true, 7); receiver.start(false, 7);
        sender.offerFiles({QUrl::fromLocalFile(source.filePath(QStringLiteral("中文目录"))), QUrl::fromLocalFile(empty.fileName())});
        QTRY_COMPARE(ready.size(), 1);
        const auto urls = qvariant_cast<QList<QUrl>>(ready.first().first());
        QCOMPARE(urls.size(), 2);
        QFile received(QDir(urls[0].toLocalFile()).filePath("a b.bin"));
        QVERIFY(received.open(QIODevice::ReadOnly)); QCOMPARE(received.readAll(), payload); received.close();
        QVERIFY(QFileInfo(QDir(urls[0].toLocalFile()).filePath("empty")).isDir());
        QCOMPARE(QFileInfo(urls[1].toLocalFile()).size(), qint64(0));
        sender.stop(); receiver.stop();
        QVERIFY(QFileInfo::exists(urls[1].toLocalFile())); // Completed clipboard files survive revocation.
    }

    void rejectsInvalidEntries_data()
    {
        QTest::addColumn<QString>("path");
        for (const auto &path : {"../escape", "/absolute", "C:/escape", "dir\\escape", "a/../../escape",
            "CON", "nul.txt", "com1.txt", "name.", "name ", "bad:name", "a//b", "missing-parent/file"})
            QTest::newRow(path) << QString::fromLatin1(path);
    }

    void rejectsInvalidEntries()
    {
        QFETCH(QString, path);
        FileClipboard receiver;
        QSignalSpy ready(&receiver, &FileClipboard::filesReady), sent(&receiver, &FileClipboard::sendMessage);
        receiver.start(false, 9);
        receiver.receive({{"action", "offer"}, {"epoch", "9"}, {"id", "batch"}, {"count", 1}});
        receiver.receive({{"action", "entry"}, {"epoch", "9"}, {"id", "batch"}, {"index", 0},
            {"path", path}, {"size", "0"}, {"offset", "0"}, {"data", ""}, {"eof", true}});
        QCOMPARE(ready.size(), 0);
        QCOMPARE(sent.last().first().toJsonObject().value("action").toString(), QString("cancel"));
    }

    void staleEpochAndRevokeDiscardPartialFiles()
    {
        FileClipboard receiver;
        QSignalSpy ready(&receiver, &FileClipboard::filesReady), sent(&receiver, &FileClipboard::sendMessage);
        const QJsonObject offer{{"action", "offer"}, {"epoch", "1"}, {"id", "batch"}, {"count", 1}};
        receiver.receive(offer); QCOMPARE(sent.size(), 0);
        receiver.start(false, 2); receiver.receive(offer); QCOMPARE(sent.size(), 0);
        auto current = offer; current["epoch"] = "2"; receiver.receive(current); QCOMPARE(sent.size(), 1);
        QJsonObject chunk{{"action", "entry"}, {"epoch", "2"}, {"id", "batch"}, {"index", 0},
            {"path", "partial"}, {"size", "2"}, {"offset", "0"}, {"data", "YQ=="}, {"eof", false}};
        receiver.receive(chunk); QCOMPARE(sent.size(), 2);
        receiver.stop();
        chunk["offset"] = "1"; chunk["eof"] = true;
        receiver.receive(chunk); QCOMPARE(ready.size(), 0); QCOMPARE(sent.size(), 2);
        receiver.start(false, 3); receiver.receive(chunk); QCOMPARE(ready.size(), 0);
    }

    void malformedChunksAndCaseCollision()
    {
        for (int scenario = 0; scenario < 4; ++scenario)
        {
            FileClipboard receiver;
            QSignalSpy ready(&receiver, &FileClipboard::filesReady), sent(&receiver, &FileClipboard::sendMessage);
            receiver.start(false, 1);
            receiver.receive({{"action", "offer"}, {"epoch", "1"}, {"id", "id"}, {"count", 2}});
            QJsonObject chunk{{"action", "entry"}, {"epoch", "1"}, {"id", "id"}, {"index", 0},
                {"path", "File"}, {"size", "0"}, {"offset", "0"}, {"data", ""}, {"eof", true}};
            if (scenario == 0) chunk["data"] = "!!!";
            if (scenario == 1) chunk["offset"] = "-1";
            if (scenario == 2) chunk["eof"] = false;
            receiver.receive(chunk);
            if (scenario == 3) { chunk["path"] = "file"; chunk["index"] = 1; receiver.receive(chunk); }
            QCOMPARE(ready.size(), 0);
            QCOMPARE(sent.last().first().toJsonObject().value("action").toString(), QString("cancel"));
        }
    }

    void sourceMutationAndNewClipboardCancelOldBatch()
    {
        QTemporaryDir source;
        QFile file(source.filePath("file")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("original"); file.close();
        FileClipboard sender;
        QSignalSpy sent(&sender, &FileClipboard::sendMessage);
        const QList<QUrl> urls{QUrl::fromLocalFile(file.fileName())};
        sender.offerFiles(urls); QCOMPARE(sent.size(), 0);
        sender.start(true, 1); sender.offerFiles(urls);
        auto offer = sent.last().first().toJsonObject();
        QVERIFY(file.open(QIODevice::Append)); file.write("changed"); file.close();
        sender.receive({{"action", "read"}, {"epoch", "1"}, {"id", offer["id"]}, {"index", 0}, {"offset", "0"}});
        QCOMPARE(sent.last().first().toJsonObject().value("action").toString(), QString("cancel"));
        sender.offerFiles(urls); offer = sent.last().first().toJsonObject();
        sender.offerFiles({});
        QCOMPARE(sent.last().first().toJsonObject().value("action").toString(), QString("cancel"));
        const auto count = sent.size();
        sender.receive({{"action", "read"}, {"epoch", "1"}, {"id", offer["id"]}, {"index", 0}, {"offset", "0"}});
        QCOMPARE(sent.size(), count);
    }
};

QTEST_GUILESS_MAIN(FileClipboardTests)
#include "tst_file_clipboard.moc"
