#include "peer_connection.h"
#include <QNetworkProxy>
#include <QHostInfo>
#include <QRegularExpression>
#include <QSignalBlocker>
#include "signaling/room_server.h"
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <array>

namespace zjdesk
{
QString PeerProfile::error() const
{
    static const QRegularExpression name("^[A-Za-z0-9](?:[A-Za-z0-9_-]{0,61}[A-Za-z0-9])?$");
    if (!name.match(alias).hasMatch() || QHostAddress(alias).protocol() == QAbstractSocket::IPv4Protocol)
        return QStringLiteral("别名须为 1–63 位字母、数字、下划线或连字符，以字母或数字开头和结尾，例如 linux-wzj。");
    if (lanAddress.isEmpty() && tailscaleAddress.isEmpty())
        return QStringLiteral("请至少填写一个对端地址。");
    if (autoDiscover && tailscaleAddress.isEmpty())
        return QStringLiteral("自动获取局域网地址需要填写对端 Tailscale IPv4。");
    static const QRegularExpression user("\\A[A-Za-z0-9_][A-Za-z0-9_.-]{0,31}\\z");
    if (!sshUser.isEmpty() && !user.match(sshUser).hasMatch())
        return QStringLiteral("SSH 用户名须为 1–32 位字母、数字、下划线、点或连字符。");
    for (const auto &value : {autoDiscover ? QString() : lanAddress, tailscaleAddress})
    {
        if (value.isEmpty()) continue;
        const QHostAddress address(value);
        const auto ip = address.toIPv4Address();
        if (address.protocol() != QAbstractSocket::IPv4Protocol || address.toString() != value
            || ip < 0x01000000 || ip >= 0xe0000000 || address.isLoopback()
            || address.isInSubnet(QHostAddress("169.254.0.0"), 16)
            || NetworkSetup::localAddresses().contains(value))
            return QStringLiteral("请填写另一台电脑的有效 IPv4 地址。");
    }
    const auto inTailscale = [](const QString &value) {
        return QHostAddress(value).isInSubnet(QHostAddress("100.64.0.0"), 10);
    };
    if (!autoDiscover && !lanAddress.isEmpty() && inTailscale(lanAddress))
        return QStringLiteral("局域网地址不能使用 Tailscale 的 100.64.0.0/10 地址。");
    if (!tailscaleAddress.isEmpty() && !inTailscale(tailscaleAddress))
        return QStringLiteral("Tailscale IPv4 须属于 100.64.0.0/10。");
    return {};
}

class PeerRouteWorker final : public QObject
{
    Q_OBJECT
public:
    PeerRouteWorker()
    {
        tick_ = new QTimer(this);
        tick_->setInterval(5000);
        connect(tick_, &QTimer::timeout, this, [this] {
            refreshLan();
            for (auto &route : routes_) probe(route);
        });
        inventoryDeadline_ = new QTimer(this);
        inventoryDeadline_->setSingleShot(true);
        connect(inventoryDeadline_, &QTimer::timeout, this, &PeerRouteWorker::querySsh);
        ssh_ = new QProcess(this);
        sshDeadline_ = new QTimer(this);
        sshDeadline_->setSingleShot(true);
        connect(sshDeadline_, &QTimer::timeout, ssh_, &QProcess::kill);
        connect(ssh_, &QProcess::readyReadStandardOutput, this, [this] {
            sshOutput_ += ssh_->readAllStandardOutput();
            if (sshOutput_.size() > 65536) { sshOutput_.clear(); ssh_->kill(); }
        });
        connect(ssh_, &QProcess::readyReadStandardError, this, [this] { ssh_->readAllStandardError(); });
        connect(ssh_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            emit status(generation_, QStringLiteral("SSH 地址查询失败，继续使用 Tailscale；请检查 SSH 配置和授权。"));
        });
        connect(ssh_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus exit) {
            sshDeadline_->stop();
            if (code != 0 || exit != QProcess::NormalExit)
            {
                emit status(generation_, QStringLiteral("SSH 地址查询失败，继续使用 Tailscale；请检查 SSH 用户、主机信任和免交互授权。"));
                return;
            }
            QStringList addresses;
            static const QRegularExpression ipv4("\\binet ([0-9.]+)/[0-9]+\\b");
            auto matches = ipv4.globalMatch(QString::fromUtf8(sshOutput_));
            while (matches.hasNext() && addresses.size() < 64) addresses.append(matches.next().captured(1));
            if (!addresses.contains(profile_.tailscaleAddress)) return;
            updateInventory(addresses, true);
        });
        for (auto &route : routes_)
        {
            auto *candidate = &route;
            route.socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
            route.socket->setProxy(QNetworkProxy::NoProxy);
            route.deadline = new QTimer(this);
            route.deadline->setSingleShot(true);
            connect(route.socket, &QWebSocket::connected, this, [this, candidate] {
                candidate->socket->ping(candidate->nonce);
            });
            connect(route.socket, &QWebSocket::pong, this,
                [this, candidate](quint64, const QByteArray &payload) {
                    if (!candidate->probing || payload != candidate->nonce) return;
                    // SSH verifies the inventory for services without network_info support.
                    if (profile_.autoDiscover && candidate == &routes_[0] && !sshInventory_) return;
                    candidate->deadline->stop();
                    candidate->probing = false;
                    candidate->healthy = true;
                    select();
                });
            route.socket->setMaxAllowedIncomingMessageSize(protocol::MaxMessageBytes);
            route.socket->setMaxAllowedIncomingFrameSize(protocol::MaxMessageBytes);
            connect(route.socket, &QWebSocket::textMessageReceived, this, [this, candidate](const QString &text) {
                const auto message = protocol::decode(text);
                if (!profile_.autoDiscover || !candidate->inventoryPending || !message || message->type != "network_info"
                    || message->payload.value("requestId").toString().toUtf8() != candidate->nonce) return;
                const auto inventory = message->payload.value("addresses").toArray();
                if (inventory.size() > 64) return;
                QStringList addresses;
                for (const auto &value : inventory) addresses.append(value.toString());
                if (!addresses.contains(profile_.tailscaleAddress)) return;
                if (candidate == &routes_[0] && !addresses.contains(candidate->address)) return;
                inventoryDeadline_->stop();
                stopSsh();
                candidate->inventoryPending = false;
                candidate->deadline->stop();
                candidate->probing = false;
                candidate->healthy = true;
                const auto deviceName = message->payload.value("deviceName").toString().trimmed().left(128);
                updateInventory(addresses, false, deviceName);
                select();
            });
            connect(route.socket, &QWebSocket::connected, this, [this, candidate] { requestInventory(*candidate); });
            connect(route.deadline, &QTimer::timeout, this, [this, candidate] { failed(*candidate); });
            connect(route.socket, &QWebSocket::disconnected, this, [this, candidate] { failed(*candidate); });
            connect(route.socket, &QWebSocket::errorOccurred, this,
                [this, candidate](QAbstractSocket::SocketError) { failed(*candidate); });
        }
    }

    ~PeerRouteWorker() override { stopSsh(); }

    void configure(const PeerProfile &profile, const QUrl &serverOverride, quint64 generation,
        const std::function<NetworkConfiguration(const QString &, const QUrl &, bool)> &resolve,
        const std::function<QStringList(const QStringList &)> &matchLan)
    {
        tick_->stop();
        inventoryDeadline_->stop();
        stopSsh();
        profile_ = profile;
        sshInventory_ = false;
        bootstrapComplete_ = false;
        addresses_ = profile.lanAddresses;
        if (!profile.lanAddress.isEmpty() && !addresses_.contains(profile.lanAddress)) addresses_.prepend(profile.lanAddress);
        nextLan_ = 0;
        sshLastAttempt_.invalidate();
        generation_ = generation;
        active_ = {};
        unavailableReported_ = false;
        for (auto &route : routes_)
        {
            route.address.clear();
            reset(route);
        }
        if (profile.alias.isEmpty()) return;
        serverOverride_ = serverOverride;
        resolve_ = resolve;
        matchLan_ = matchLan;
        routes_[0].address = profile.autoDiscover ? QString() : profile.lanAddress;
        routes_[1].address = profile.tailscaleAddress;
        refreshLan();
        // Mark both candidates pending before either can report an error.
        for (auto &route : routes_) route.probing = !route.address.isEmpty();
        for (auto &route : routes_) probe(route);
        tick_->start();
    }

signals:
    void selected(quint64 generation, const zjdesk::NetworkConfiguration &configuration);
    void lost(quint64 generation);
    void discovered(quint64 generation, const QString &tailscaleAddress, const QStringList &addresses, const QString &deviceName);
    void status(quint64 generation, const QString &message);

private:
    struct Route
    {
        QString address;
        NetworkConfiguration configuration;
        QWebSocket *socket = nullptr;
        QTimer *deadline = nullptr;
        QByteArray nonce;
        bool healthy = false;
        bool probing = false;
        bool inventoryPending = false;
    };

    void stopSsh()
    {
        sshDeadline_->stop();
        if (ssh_->state() == QProcess::NotRunning) return;
        const QSignalBlocker blocker(ssh_);
        ssh_->kill();
        ssh_->waitForFinished(1000);
    }

    void querySsh()
    {
        if (!profile_.autoDiscover || ssh_->state() != QProcess::NotRunning
            || (sshLastAttempt_.isValid() && sshLastAttempt_.elapsed() < 30000)) return;
        sshLastAttempt_.start();
        const auto program = QStandardPaths::findExecutable("ssh");
        if (program.isEmpty())
        {
            emit status(generation_, QStringLiteral("对端未返回网卡信息，且未找到 SSH，继续使用 Tailscale。"));
            return;
        }
        const auto target = profile_.sshUser.isEmpty() ? profile_.tailscaleAddress : profile_.sshUser + "@" + profile_.tailscaleAddress;
        sshOutput_.clear();
        ssh_->start(program, {"-n", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes",
            "-o", "ConnectTimeout=5", target, "ip -4 -o addr show up scope global"});
        sshDeadline_->start(7000);
        emit status(generation_, QStringLiteral("正在通过 Tailscale 上的 SSH 查询对端地址…"));
    }

    void refreshLan()
    {
        if (!profile_.autoDiscover || !bootstrapComplete_) return;
        const auto candidates = matchLan_(addresses_);
        auto &route = routes_[0];
        if ((route.healthy || route.probing) && candidates.contains(route.address)) return;
        const auto address = candidates.isEmpty() ? QString() : candidates[nextLan_++ % candidates.size()];
        if (route.address == address) return;
        reset(route);
        route.address = address;
    }

    void updateInventory(const QStringList &addresses, bool fromSsh = false, const QString &deviceName = {})
    {
        bootstrapComplete_ = true;
        sshInventory_ = fromSsh;
        addresses_ = addresses;
        refreshLan();
        emit discovered(generation_, profile_.tailscaleAddress, addresses, deviceName);
        if (!routes_[0].healthy && !routes_[0].probing) probe(routes_[0]);
    }

    void requestInventory(Route &route)
    {
        if (!profile_.autoDiscover) return;
        route.inventoryPending = true;
        route.socket->sendTextMessage(protocol::encode({"network_info", {{"requestId", QString::fromUtf8(route.nonce)},
            {"addresses", QJsonArray::fromStringList(RoomServer::networkAddresses())}, {"deviceName", QHostInfo::localHostName().left(128)}}}));
        if (&route == &routes_[1]) inventoryDeadline_->start(2500);
    }

    void reset(Route &route)
    {
        route.deadline->stop();
        route.healthy = false;
        route.probing = false;
        route.inventoryPending = false;
        const QSignalBlocker blocker(route.socket);
        route.socket->abort();
    }

    void failed(Route &route)
    {
        reset(route);
        select();
    }

    void probe(Route &route)
    {
        if (route.address.isEmpty()) return;
        const auto configuration = resolve_(route.address, serverOverride_, &route == &routes_[1]);
        if (configuration.peerAddress().isEmpty()) return failed(route);
        if (configuration.localAddress() != route.configuration.localAddress()) reset(route);
        route.configuration = configuration;
        route.nonce = QByteArray::number(++sequence_);
        route.probing = true;
        route.deadline->start(3000);
        if (profile_.autoDiscover && &route == &routes_[1]) inventoryDeadline_->start(2500);
        if (route.socket->state() == QAbstractSocket::ConnectedState)
        {
            route.socket->ping(route.nonce);
            requestInventory(route);
        }
        else
            route.socket->open(QUrl(QString("ws://%1:%2").arg(route.address).arg(serverOverride_.port(8765))));
    }

    void select()
    {
        const Route *route = nullptr;
        if (routes_[0].healthy) route = &routes_[0];
        else if (routes_[1].healthy && (!routes_[0].probing || !active_.peerAddress().isEmpty())) route = &routes_[1];
        if (!route)
        {
            if (!unavailableReported_ && (!active_.peerAddress().isEmpty() || (!routes_[0].probing && !routes_[1].probing)))
            {
                active_ = {};
                unavailableReported_ = true;
                emit lost(generation_);
            }
            return;
        }
        const auto &configuration = route->configuration;
        if (active_.peerAddress() == configuration.peerAddress()
            && active_.localAddress() == configuration.localAddress()) return;
        active_ = configuration;
        unavailableReported_ = false;
        emit selected(generation_, active_);
    }

    std::array<Route, 2> routes_;
    std::function<NetworkConfiguration(const QString &, const QUrl &, bool)> resolve_;
    std::function<QStringList(const QStringList &)> matchLan_;
    PeerProfile profile_;
    bool sshInventory_ = false;
    bool bootstrapComplete_ = false;
    QStringList addresses_;
    qsizetype nextLan_ = 0;
    QProcess *ssh_;
    QTimer *inventoryDeadline_, *sshDeadline_;
    QByteArray sshOutput_;
    QElapsedTimer sshLastAttempt_;
    QTimer *tick_;
    QUrl serverOverride_;
    NetworkConfiguration active_;
    quint64 generation_ = 0;
    quint64 sequence_ = 0;
    bool unavailableReported_ = false;
};

PeerConnection::PeerConnection(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<NetworkConfiguration>();
    matchLan_ = &NetworkSetup::lanPeerAddresses;
    resolve_ = [](const QString &address, const QUrl &serverOverride, bool tailscale) {
        const QUrl automaticServer(QString("ws://127.0.0.1:%1").arg(serverOverride.port(8765)));
        const auto configuration = NetworkSetup::configurationForPeer(address, automaticServer);
        const bool overlay = QHostAddress(configuration.localAddress()).isInSubnet(QHostAddress("100.64.0.0"), 10);
        if (!NetworkSetup::configurationError(configuration).isEmpty() || overlay != tailscale)
            return NetworkConfiguration{};
        return configuration;
    };
    auto *worker = new PeerRouteWorker;
    worker_ = worker;
    worker->moveToThread(&thread_);
    thread_.setObjectName("peer-connection");
    connect(&thread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &PeerRouteWorker::selected, this, [this](quint64 generation, const NetworkConfiguration &configuration) {
        if (generation == generation_) emit routeChanged(configuration);
    });
    connect(worker, &PeerRouteWorker::lost, this, [this](quint64 generation) {
        if (generation == generation_) emit unavailable();
    });
    connect(worker, &PeerRouteWorker::discovered, this, [this](quint64 generation, const QString &address, const QStringList &addresses, const QString &deviceName) {
        if (generation == generation_) emit networkDiscovered(address, addresses, deviceName);
    });
    connect(worker, &PeerRouteWorker::status, this, [this](quint64 generation, const QString &message) {
        if (generation == generation_) emit discoveryStatus(message);
    });
    thread_.start();
}

PeerConnection::~PeerConnection()
{
    thread_.quit();
    thread_.wait();
}

void PeerConnection::start(const PeerProfile &profile, const QUrl &serverOverride)
{
    const auto generation = ++generation_;
    const auto resolve = resolve_;
    const auto matchLan = matchLan_;
    auto *worker = static_cast<PeerRouteWorker *>(worker_);
    QMetaObject::invokeMethod(worker_, [worker, profile, serverOverride, generation, resolve, matchLan] {
        worker->configure(profile, serverOverride, generation, resolve, matchLan);
    });
}

void PeerConnection::stop()
{
    start({}, {});
}
}

#include "peer_connection.moc"
