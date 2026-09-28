#pragma once

#include "protocol/message.h"
#include <QObject>
#include <QPointer>
#include <QUrl>

class QWebSocket;

namespace zjdesk
{
class SignalingClient final : public QObject
{
    Q_OBJECT
public:
    explicit SignalingClient(QObject *parent = nullptr);
    ~SignalingClient() override;
    void open(const QUrl &url);
    void close();
    bool send(const protocol::Message &message);

signals:
    void connected();
    void disconnected();
    void failed(const QString &reason);
    void messageReceived(const zjdesk::protocol::Message &message);
    void traffic(bool outgoing);

private:
    QPointer<QWebSocket> socket_;
};
} // namespace zjdesk
