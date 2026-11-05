#pragma once

#include "protocol/message.h"
#include <QHash>
#include <QHostAddress>
#include <QElapsedTimer>
#include <QTimer>
#include <QSettings>
#include <QWebSocketServer>

class QWebSocket;

namespace zjdesk
{
class RoomServer final : public QObject
{
    Q_OBJECT
public:
    explicit RoomServer(QObject *parent = nullptr, const QString &remoteRegistryFile = {});
    ~RoomServer() override;
    bool listen(const QHostAddress &address, quint16 port);
    void close();
    quint16 port() const { return server_.serverPort(); }
    QString errorString() const { return server_.errorString(); }
    int roomCount() const { return rooms_.size(); }

signals:
    void event(const QString &message);

private slots:
    void refreshRemoteCodes();

private:
    struct Peer
    {
        QString id;
        QString room;
        QString name;
        qint64 lastPong = 0;
        QString remoteCode;
        QString remoteIdentity;
        QString remotePolicy;
    };
    void acceptConnection();
    void receive(QWebSocket *socket, const QString &text);
    void leave(QWebSocket *socket);
    void send(QWebSocket *socket, const protocol::Message &message);
    void reject(QWebSocket *socket, const QString &code);
    QWebSocket *otherPeer(QWebSocket *socket) const;
    bool assignRemoteCode(QWebSocket *socket, const QString &identity, const QString &policy);

    QWebSocketServer server_;
    QHash<QWebSocket *, Peer> peers_;
    QHash<QString, QList<QWebSocket *>> rooms_;
    QHash<QString, QWebSocket *> remoteCodes_;
    QSettings remoteRegistry_;
    QHash<QString, QString> reservedCodes_;
    QTimer heartbeat_;
    QElapsedTimer clock_;
};
} // namespace zjdesk
