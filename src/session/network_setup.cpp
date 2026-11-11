#include "network_setup.h"
#include "signaling/room_server.h"
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QStandardPaths>
#include <QUdpSocket>

namespace zjdesk
{
namespace
{
bool usableAddress(const QString &value)
{
    const QHostAddress address(value);
    if (address.protocol() != QAbstractSocket::IPv4Protocol || address.toString() != value) return false;
    const auto ipv4 = address.toIPv4Address();
    return ipv4 >= 0x01000000 && ipv4 < 0xe0000000 && !address.isLoopback()
        && !address.isInSubnet(QHostAddress("169.254.0.0"), 16);
}
}

QString NetworkConfiguration::localAddress() const
{
#ifdef Q_OS_WIN
    return windowsAddress;
#else
    return linuxAddress;
#endif
}

QString NetworkConfiguration::peerAddress() const
{
#ifdef Q_OS_WIN
    return linuxAddress;
#else
    return windowsAddress;
#endif
}

NetworkSetup::NetworkSetup(QObject *parent) : QObject(parent)
{
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        complete(false, QStringLiteral("验证超时。请确认对端已开机、网络互通，且信令服务已启动；检查防火墙是否允许 ping 和服务端口。"));
    });
}

NetworkSetup::~NetworkSetup()
{
    cancel();
}

QStringList NetworkSetup::localAddresses()
{
    QStringList result;
    for (const auto &interface : QNetworkInterface::allInterfaces())
    {
        if (!(interface.flags() & QNetworkInterface::IsUp)
            || !(interface.flags() & QNetworkInterface::IsRunning)) continue;
        for (const auto &entry : interface.addressEntries())
            if (usableAddress(entry.ip().toString())) result.append(entry.ip().toString());
    }
    result.removeDuplicates();
    return result;
}

QString NetworkSetup::configurationError(const NetworkConfiguration &configuration)
{
    if (!usableAddress(configuration.windowsAddress))
        return QStringLiteral("请填写有效的 Windows IPv4 地址，不要填写回环、链路本地、广播或组播地址。在 Windows 电脑双击 tools\\ip-info.cmd 可查到地址。");
    if (!usableAddress(configuration.linuxAddress))
        return QStringLiteral("请填写有效的 Linux IPv4 地址。在 Linux 电脑运行 sh tools/ip-info.sh 可查到地址。");
    if (QHostAddress(configuration.windowsAddress) == QHostAddress(configuration.linuxAddress))
        return QStringLiteral("Windows 和 Linux 必须填写不同电脑的地址。");
    if (!localAddresses().contains(configuration.localAddress()))
        return QStringLiteral("本机地址 %1 不属于当前已连接的网卡。请重新查询，检查网线、Wi-Fi 或 VPN；不要沿用旧的 DHCP 地址。")
            .arg(configuration.localAddress());
    const auto &url = configuration.serverUrl;
    if (!url.isValid() || (url.scheme() != "ws" && url.scheme() != "wss")
        || url.port(8765) < 1 || url.port(8765) > 65535
        || (QHostAddress(url.host()) != QHostAddress(configuration.windowsAddress)
            && QHostAddress(url.host()) != QHostAddress(configuration.linuxAddress)))
        return QStringLiteral("信令服务须使用所填 Windows 或 Linux 地址，端口为 1–65535；两端须选择同一台服务电脑和相同端口。");
    return {};
}

NetworkConfiguration NetworkSetup::configurationForPeer(const QString &peerAddress, const QUrl &serverUrl)
{
    NetworkConfiguration result;
    QUdpSocket route;
    // UDP connect selects a source address without sending a datagram.
    if (usableAddress(peerAddress)) route.connectToHost(QHostAddress(peerAddress), 9);
    const auto local = route.localAddress().toString();
#ifdef Q_OS_WIN
    result.windowsAddress = local;
    result.linuxAddress = peerAddress;
#else
    result.windowsAddress = peerAddress;
    result.linuxAddress = local;
#endif
    result.serverUrl = serverUrl;
    if (serverUrl.isEmpty() || QHostAddress(serverUrl.host()).isLoopback())
    {
        result.serverUrl = QUrl(QString("ws://%1:%2").arg(result.linuxAddress).arg(serverUrl.port(8765)));
    }
    return result;
}

void NetworkSetup::verifyPeer(const QString &peerAddress, const QUrl &serverUrl)
{
    cancel();
    if (!usableAddress(peerAddress))
    {
        emit finished(false, QStringLiteral("请填写对端电脑的有效 IPv4 地址，不要填写回环、169.254.* 或带 /24 的地址。"));
        return;
    }
    if (localAddresses().contains(peerAddress))
    {
        emit finished(false, QStringLiteral("这是本机地址，请填写另一台电脑的 IPv4 地址。"));
        return;
    }
    verify(configurationForPeer(peerAddress, serverUrl));
}

bool NetworkSetup::startLocalServer(quint16 port)
{
    if (server_) return server_->port() == port;
    auto *server = new RoomServer(this);
    if (!server->listen(QHostAddress::AnyIPv4, port))
    {
        delete server;
        return false; // An existing service may already own this port; verification checks it.
    }
    server_ = server;
    return true;
}

void NetworkSetup::verify(const NetworkConfiguration &configuration)
{
    cancel();
    configuration_ = configuration;
    const auto error = configurationError(configuration);
    if (!error.isEmpty())
    {
        emit finished(false, error);
        return;
    }
    busy_ = true;
    const auto program = QStandardPaths::findExecutable("ping");
    if (program.isEmpty())
    {
        complete(false, QStringLiteral("找不到 ping 命令。Linux 请先安装 iputils-ping 后重试。"));
        return;
    }
    ping_ = new QProcess(this);
    connect(ping_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        complete(false, QStringLiteral("无法执行 ping。请检查系统 ping 命令和运行权限后重试。"));
    });
    connect(ping_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int exitCode, QProcess::ExitStatus status) {
            bool success = status == QProcess::NormalExit && exitCode == 0;
#ifdef Q_OS_WIN
            // Windows can return zero for a destination-unreachable ICMP response.
            success = success && ping_->readAllStandardOutput().toUpper().contains("TTL=");
#endif
            if (!success)
            {
                complete(false, QStringLiteral("对端 %1 没有回应 ping。请检查地址、网线 / Wi-Fi、VPN 路由和对端防火墙的 ICMP 回应规则，再重新验证。")
                    .arg(configuration_.peerAddress()));
                return;
            }
            probeServer();
        });
    emit progress(QStringLiteral("本机网卡匹配，正在 ping 对端 %1…").arg(configuration.peerAddress()));
    deadline_.start(10000);
#ifdef Q_OS_WIN
    ping_->start(program, {"-4", "-n", "1", "-w", "3000", configuration.peerAddress()});
#else
    ping_->start(program, {"-4", "-n", "-c", "1", "-W", "3", "-I", configuration.localAddress(), configuration.peerAddress()});
#endif
}

void NetworkSetup::probeServer()
{
    socket_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    socket_->setProxy(QNetworkProxy::NoProxy);
    connect(socket_, &QWebSocket::connected, this, [this] {
        if (!localAddresses().contains(configuration_.localAddress()))
        {
            complete(false, QStringLiteral("验证期间本机网卡已变化，请重新查询地址后验证。"));
            return;
        }
        qputenv("ZJDESK_WEBRTC_LOCAL_ADDRESS", configuration_.localAddress().toUtf8());
        qputenv("ZJDESK_WEBRTC_PEER_ADDRESS", configuration_.peerAddress().toUtf8());
        complete(true, QStringLiteral("本机地址、对端 ping 和信令 WebSocket 验证通过。实际远控、音视频仍需两端防火墙允许 WebRTC 动态端口。"));
    });
    connect(socket_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        complete(false, QStringLiteral("无法连接信令服务 %1：%2。请确认两端已打开新版程序，并放行 TCP %3；默认服务由 Linux 客户端提供。")
            .arg(configuration_.serverUrl.toString(), socket_->errorString())
            .arg(configuration_.serverUrl.port(8765)));
    });
    emit progress(QStringLiteral("对端 ping 已通过，正在连接信令服务 %1…").arg(configuration_.serverUrl.toString()));
    socket_->open(configuration_.serverUrl);
}

void NetworkSetup::cancel()
{
    deadline_.stop();
    busy_ = false;
    verified_ = false;
    if (ping_)
    {
        ping_->disconnect(this);
        if (ping_->state() != QProcess::NotRunning)
        {
            ping_->kill();
            ping_->waitForFinished(1000);
        }
        ping_->deleteLater();
        ping_ = nullptr;
    }
    if (socket_)
    {
        socket_->disconnect(this);
        socket_->abort();
        socket_->deleteLater();
        socket_ = nullptr;
    }
}

void NetworkSetup::complete(bool success, const QString &message)
{
    if (!busy_) return;
    cancel();
    verified_ = success;
    emit finished(success, message);
}
}
