#include "session_coordinator.h"
#include "protocol/annotation.h"
#include <QUuid>

namespace zjdesk
{
bool SessionCoordinator::startMeetingShare(const QString &source, int targetFps)
{
    if (state_ != State::PeerAvailable || screenActive() || !pendingShareId_.isEmpty()
        || shareState_ == PreviewState::Stopping) return false;
    if (!media_.environment().ready)
    {
        shareState_ = PreviewState::Failed;
        shareDetail_ = media_.environment().description;
        emit shareChanged();
        return false;
    }
    pendingShareId_ = QUuid::createUuid().toString(QUuid::Id128);
    meetingShareSource_ = source;
    meetingShareFps_ = targetFps;
    bool embedded = false;
#ifdef Q_OS_LINUX
    // X11 desktop capture includes the transparent marking window.
    embedded = source == "desktop";
#endif
    if (!signaling_.send({"annotation", {{"action", "start"}, {"streamId", pendingShareId_},
            {"annotationsInVideo", embedded}}}))
    {
        pendingShareId_.clear();
        return false;
    }
    shareState_ = PreviewState::Starting;
    shareDetail_ = QStringLiteral("正在申请会议屏幕共享");
    emit shareChanged();
    const auto request = pendingShareId_;
    QTimer::singleShot(10000, this, [this, request] {
        if (pendingShareId_ != request) return;
        clearMeetingShare(true);
        shareState_ = PreviewState::Failed;
        shareDetail_ = QStringLiteral("共享请求超时，请检查信令服务版本与网络");
        emit shareChanged();
    });
    return true;
}

void SessionCoordinator::clearMeetingShare(bool notifyPeer)
{
    if (notifyPeer && (ownsSharedScreen() || !pendingShareId_.isEmpty()))
        signaling_.send({"annotation", {{"action", "stop"},
            {"streamId", streamId_.isEmpty() ? pendingShareId_ : streamId_}}});
    ++shareEpoch_;
    const auto peers = meetingSharePeers_;
    meetingSharePeers_.clear();
    for (auto *lane : peers)
    {
        lane->disconnect(this);
        lane->stop(shareEpoch_);
        lane->deleteLater();
    }
    meetingShareRunning_.clear();
    pendingShareId_.clear();
    shareOwnerId_.clear();
    streamId_.clear();
    receivingScreen_ = false;
    annotations_ = {};
    annotationsInVideo_ = false;
    annotationDetail_.clear();
    shareStats_ = {};
    shareState_ = PreviewState::Stopped;
    shareDetail_ = QStringLiteral("屏幕共享已停止");
    emit annotationsChanged();
    emit shareFrame({});
    emit shareChanged();
}

void SessionCoordinator::addMeetingSharePeer(const QString &id)
{
    if (streamId_.isEmpty() || !conferencePeers_.contains(id) || meetingSharePeers_.contains(id)) return;
    auto *lane = new ScreenShare(this);
    meetingSharePeers_.insert(id, lane);
    const auto epoch = shareEpoch_;
    connect(lane, &ScreenShare::negotiationReady, this,
        [this, id, epoch](quint64 generation, const QString &type, QJsonObject payload) {
            if (generation != epoch || epoch != shareEpoch_ || !meetingSharePeers_.contains(id)) return;
            payload.insert("streamId", streamId_);
            payload.insert("targetPeerId", id);
            if (!sendNegotiation(type, payload)) clearMeetingShare(true);
        });
    connect(lane, &ScreenShare::frameReady, this, [this, id, epoch](quint64 generation, const QImage &frame) {
        if (generation == epoch && epoch == shareEpoch_ && !meetingSharePeers_.isEmpty()
            && id == meetingSharePeers_.firstKey()) emit shareFrame(frame);
    });
    connect(lane, &ScreenShare::statisticsReady, this, [this, id, epoch](quint64 generation, const ShareStats &stats) {
        if (generation != epoch || epoch != shareEpoch_ || meetingSharePeers_.isEmpty()
            || id != meetingSharePeers_.firstKey()) return;
        shareStats_ = stats;
        emit shareStatisticsChanged(stats);
    });
    connect(lane, &ScreenShare::statusChanged, this,
        [this, id, epoch](quint64 generation, const QString &status, const QString &detail) {
            if (generation != epoch || epoch != shareEpoch_ || !meetingSharePeers_.contains(id)) return;
            if (status == "failed")
            {
                removeMeetingSharePeer(id);
                if (!screenActive()) shareState_ = PreviewState::Failed;
                shareDetail_ = QStringLiteral("屏幕共享连接失败：") + detail;
            }
            else if (status == "running")
            {
                meetingShareRunning_.insert(id);
                shareState_ = PreviewState::Running;
                shareDetail_ = receivingScreen_ ? QStringLiteral("正在接收共享屏幕")
                    : QStringLiteral("屏幕共享 · 已连接 %1/%2 位成员")
                        .arg(meetingShareRunning_.size()).arg(conferencePeers_.size());
            }
            else return;
            emit shareChanged();
        });
    lane->start(!receivingScreen_, receivingScreen_ ? QString() : meetingShareSource_, epoch, meetingShareFps_);
}

void SessionCoordinator::removeMeetingSharePeer(const QString &id)
{
    if (id == shareOwnerId_) { clearMeetingShare(false); return; }
    if (auto *lane = meetingSharePeers_.take(id))
    {
        lane->disconnect(this);
        lane->stop(shareEpoch_);
        lane->deleteLater();
    }
    meetingShareRunning_.remove(id);
    if (ownsSharedScreen() && meetingSharePeers_.isEmpty()) clearMeetingShare(true);
}

bool SessionCoordinator::submitAnnotation(const QJsonObject &stroke)
{
    if (!canAnnotate() || !protocol::isAnnotationStrokeValid(stroke)) return false;
    auto payload = stroke;
    payload.insert("action", "stroke");
    payload.insert("streamId", streamId_);
    return signaling_.send({"annotation", payload});
}

bool SessionCoordinator::clearAnnotations(bool all)
{
    if (!canAnnotate() || (all && !ownsSharedScreen())) return false;
    return signaling_.send({"annotation", {{"action", "clear"}, {"streamId", streamId_}, {"all", all}}});
}

void SessionCoordinator::receiveAnnotation(const QJsonObject &payload)
{
    const auto action = payload.value("action").toString();
    const auto streamId = payload.value("streamId").toString();
    if (action == "started")
    {
        const auto owner = payload.value("ownerId").toString();
        if (!protocol::isRoomIdValid(streamId) || (owner != ownPeerId_ && !conferencePeers_.contains(owner))) return;
        if (streamId == streamId_) return;
        if (owner == ownPeerId_ && pendingShareId_ != streamId)
        {
            signaling_.send({"annotation", {{"action", "stop"}, {"streamId", streamId}}});
            return;
        }
        clearMeetingShare(false);
        streamId_ = streamId;
        shareOwnerId_ = owner;
        annotationsInVideo_ = payload.value("annotationsInVideo").toBool();
        receivingScreen_ = owner != ownPeerId_;
        desktopSource_ = !receivingScreen_ && meetingShareSource_ == "desktop";
        shareState_ = PreviewState::Starting;
        shareDetail_ = QStringLiteral("正在连接会议屏幕共享");
        annotationDetail_ = QStringLiteral("在共享画面上拖动标注 · 共享者桌面同步显示");
        if (receivingScreen_) addMeetingSharePeer(owner);
        else for (const auto &id : conferencePeers_.keys()) addMeetingSharePeer(id);
        emit annotationsChanged();
        emit shareChanged();
        return;
    }
    if (action == "busy" || action == "error")
    {
        if (!pendingShareId_.isEmpty())
        {
            pendingShareId_.clear();
            if (!screenActive()) shareState_ = PreviewState::Stopped;
        }
        annotationDetail_ = action == "busy" ? QStringLiteral("已有成员正在共享屏幕")
            : payload.value("code") == "annotation_limit" ? QStringLiteral("标注已达 128 条，请先清除后继续")
            : QStringLiteral("标注请求未被接受：") + payload.value("code").toString();
        emit annotationsChanged();
        emit shareChanged();
        return;
    }
    if (streamId != streamId_ || !canAnnotate()) return;
    if (action == "stopped") { clearMeetingShare(false); return; }
    if (action == "clear")
    {
        for (qsizetype i = annotations_.size(); i-- > 0;)
            if (payload.value("all").toBool()
                || annotations_.at(i).toObject().value("authorId") == payload.value("authorId")) annotations_.removeAt(i);
        emit annotationsCleared(payload.value("all").toBool() ? QString() : payload.value("authorId").toString());
    }
    else if (action == "stroke" && protocol::isAnnotationStrokeValid(payload))
    {
        qsizetype index = -1;
        for (qsizetype i = 0; i < annotations_.size(); ++i)
            if (annotations_.at(i).toObject().value("strokeId") == payload.value("strokeId")) { index = i; break; }
        if (index >= 0) annotations_.replace(index, payload);
        else if (annotations_.size() < protocol::MaximumAnnotationStrokes) annotations_.append(payload);
    }
    else return;
    emit annotationsChanged();
}
} // namespace zjdesk
