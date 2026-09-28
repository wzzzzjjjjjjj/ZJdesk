#include "signaling_client.h"
#include <QNetworkProxy>
#include <QWebSocket>

namespace zjdesk
{
SignalingClient::SignalingClient(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<protocol::Message>();
}

SignalingClient::~SignalingClient()
{
    close();
}

void SignalingClient::open(const QUrl &url)
{
    close();
    auto *socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    socket_ = socket;
    socket->setMaxAllowedIncomingMessageSize(protocol::MaxMessageBytes);
    socket->setMaxAllowedIncomingFrameSize(protocol::MaxMessageBytes);
    // LAN signaling must not accidentally follow the system's HTTP proxy.
    socket->setProxy(QNetworkProxy::NoProxy);
    connect(socket, &QWebSocket::connected, this, [this, socket] {
        if (socket_ == socket)
            emit connected();
    });
    connect(socket, &QWebSocket::disconnected, this, [this, socket] {
        if (socket_ == socket)
            emit disconnected();
    });
    connect(socket, &QWebSocket::errorOccurred, this, [this, socket] {
        if (socket_ == socket)
            emit failed(socket->errorString());
    });
    connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &text) {
        if (socket_ != socket)
            return;
        QString error;
        const auto message = protocol::decode(text, &error);
        if (!message)
        {
            emit failed(error);
            return;
        }
        emit traffic(false);
        emit messageReceived(*message);
    });
    socket->open(url);
}

void SignalingClient::close()
{
    auto *socket = socket_.data();
    socket_.clear();
    if (socket)
    {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
}

bool SignalingClient::send(const protocol::Message &message)
{
    const auto text = protocol::encode(message);
    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState
        || text.toUtf8().size() > protocol::MaxMessageBytes
        || socket_->bytesToWrite() > 4 * protocol::MaxMessageBytes)
        return false;
    if (socket_->sendTextMessage(text) < 0)
        return false;
    emit traffic(true);
    return true;
}
} // namespace zjdesk
