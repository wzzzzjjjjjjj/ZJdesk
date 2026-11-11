#pragma once

#include <QHostAddress>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

class NetworkSetupTests;

namespace zjdesk
{
class RoomServer;
struct NetworkConfiguration
{
    QString windowsAddress;
    QString linuxAddress;
    QUrl serverUrl;
    QString localAddress() const;
    QString peerAddress() const;
};

class NetworkSetup final : public QObject
{
    Q_OBJECT
public:
    explicit NetworkSetup(QObject *parent = nullptr);
    ~NetworkSetup() override;
    static QStringList localAddresses();
    static QStringList suggestedPeerAddresses(const QStringList &history);
    static NetworkConfiguration configurationForPeer(const QString &peerAddress, const QUrl &serverUrl = {});
    static QString configurationError(const NetworkConfiguration &configuration);
    void verifyPeer(const QString &peerAddress, const QUrl &serverUrl = {});
    bool startLocalServer(quint16 port = 8765);
    void verify(const NetworkConfiguration &configuration);
    void cancel();
    bool verified() const { return verified_; }
    NetworkConfiguration configuration() const { return configuration_; }

signals:
    void progress(const QString &message);
    void finished(bool success, const QString &message);

private:
    friend class ::NetworkSetupTests;
    void probeServer();
    void complete(bool success, const QString &message);
    NetworkConfiguration configuration_;
    QProcess *ping_ = nullptr;
    QWebSocket *socket_ = nullptr;
    RoomServer *server_ = nullptr;
    QTimer deadline_;
    bool busy_ = false;
    bool verified_ = false;
};
}
