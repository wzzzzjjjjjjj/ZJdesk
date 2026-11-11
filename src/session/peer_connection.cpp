#include "peer_connection.h"
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QSignalBlocker>
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
    for (const auto &value : {lanAddress, tailscaleAddress})
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
    if (!lanAddress.isEmpty() && inTailscale(lanAddress))
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
            for (auto &route : routes_) probe(route);
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
                    candidate->deadline->stop();
                    candidate->probing = false;
                    candidate->healthy = true;
                    select();
                });
            connect(route.deadline, &QTimer::timeout, this, [this, candidate] { failed(*candidate); });
            connect(route.socket, &QWebSocket::disconnected, this, [this, candidate] { failed(*candidate); });
            connect(route.socket, &QWebSocket::errorOccurred, this,
                [this, candidate](QAbstractSocket::SocketError) { failed(*candidate); });
        }
    }

    void configure(const PeerProfile &profile, const QUrl &serverOverride, quint64 generation,
        const std::function<NetworkConfiguration(const QString &, const QUrl &, bool)> &resolve)
    {
        tick_->stop();
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
        routes_[0].address = profile.lanAddress;
        routes_[1].address = profile.tailscaleAddress;
        // Mark both candidates pending before either can report an error.
        for (auto &route : routes_) route.probing = !route.address.isEmpty();
        for (auto &route : routes_) probe(route);
        tick_->start();
    }

signals:
    void selected(quint64 generation, const zjdesk::NetworkConfiguration &configuration);
    void lost(quint64 generation);

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
    };

    void reset(Route &route)
    {
        route.deadline->stop();
        route.healthy = false;
        route.probing = false;
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
        if (route.socket->state() == QAbstractSocket::ConnectedState)
            route.socket->ping(route.nonce);
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
    auto *worker = static_cast<PeerRouteWorker *>(worker_);
    QMetaObject::invokeMethod(worker_, [worker, profile, serverOverride, generation, resolve] {
        worker->configure(profile, serverOverride, generation, resolve);
    });
}

void PeerConnection::stop()
{
    start({}, {});
}
}

#include "peer_connection.moc"
