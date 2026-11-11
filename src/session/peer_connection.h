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

private:
    friend class ::PeerConnectionTests;
    std::function<NetworkConfiguration(const QString &, const QUrl &, bool)> resolve_;
    QThread thread_;
    QObject *worker_ = nullptr;
    quint64 generation_ = 0;
};
}

Q_DECLARE_METATYPE(zjdesk::NetworkConfiguration)
