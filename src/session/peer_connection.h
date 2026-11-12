#pragma once

#include "network_setup.h"
#include <QThread>
#include <functional>

class PeerConnectionTests;

namespace zjdesk
{
struct PeerProfile
{
    QString alias;
    QString lanAddress;
    QString tailscaleAddress;
    bool autoDiscover = false;
    QStringList lanAddresses;
    QString sshUser;
    QString deviceName;
    QString error() const;
};

// Owns the probe thread. Only the coordinator applies a selected route to a session.
class PeerConnection final : public QObject
{
    Q_OBJECT
public:
    explicit PeerConnection(QObject *parent = nullptr);
    ~PeerConnection() override;
    void start(const PeerProfile &profile, const QUrl &serverOverride = {});
    void stop();

signals:
    void routeChanged(const zjdesk::NetworkConfiguration &configuration);
    void unavailable();
    void networkDiscovered(const QString &tailscaleAddress, const QStringList &addresses, const QString &deviceName = {});
    void discoveryStatus(const QString &message);

private:
    friend class ::PeerConnectionTests;
    std::function<NetworkConfiguration(const QString &, const QUrl &, bool)> resolve_;
    std::function<QStringList(const QStringList &)> matchLan_;
    QThread thread_;
    QObject *worker_ = nullptr;
    quint64 generation_ = 0;
};
}

Q_DECLARE_METATYPE(zjdesk::NetworkConfiguration)
