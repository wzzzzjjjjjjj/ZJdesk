#pragma once

#include <QFile>
#include <QJsonObject>
#include <QMimeData>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <memory>
#include <vector>

namespace zjdesk
{
// Activated only for an authorized remote-control epoch by SessionCoordinator.
class FileClipboard final : public QObject
{
    Q_OBJECT
public:
    explicit FileClipboard(QObject *parent = nullptr);
    void start(bool sender, quint64 epoch);
    void stop();
    bool active() const { return epoch_ != 0; }
    QString status() const { return status_; }
    void receive(const QJsonObject &message);
    void offerFiles(const QList<QUrl> &urls);
    static QList<QUrl> copiedFiles(const QMimeData *mime);

signals:
    void sendMessage(const QJsonObject &message);
    void statusChanged();
    void filesReady(const QList<QUrl> &urls);

private:
    struct Entry { QString source, path; qint64 size; qint64 modified; bool directory; };
    bool appendEntry(const QString &source, const QString &path);
    void clipboardChanged();
    void resetTransfer();
    void fail(const QString &reason);
    void setStatus(const QString &text);
    void send(QJsonObject message);
    void requestNext();
    void serve(const QJsonObject &message);
    void accept(const QJsonObject &message);
    bool sender_ = false, settingClipboard_ = false;
    quint64 epoch_ = 0;
    QString id_, status_;
    QList<Entry> entries_;
    QSet<QString> paths_, directories_;
    int index_ = 0, count_ = 0;
    qint64 offset_ = 0;
    QFile file_;
    QString currentPath_;
    qint64 currentSize_ = 0;
    QList<QUrl> roots_;
    QTimer timeout_;
    std::unique_ptr<QTemporaryDir> pending_;
    // Clipboard file URLs must remain readable while Explorer uses them, including after revoke.
    std::vector<std::unique_ptr<QTemporaryDir>> completed_;
};
} // namespace zjdesk
