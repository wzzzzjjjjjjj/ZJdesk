#pragma once

#include "media/media_environment.h"
#include "session/session_coordinator.h"
#include "session/network_setup.h"
#include <QMainWindow>
#include <QStringList>

class QLabel;
class QLineEdit;
class QComboBox;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;
class QHBoxLayout;
class QScrollArea;
class QDialog;
class QGridLayout;
class QPropertyAnimation;
class VideoSurface;
class AnnotationLayer;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QString &serverUrl, const QString &roomId, const QString &name,
                        QWidget *parent = nullptr);
    ~MainWindow() override;
    void requestCameraPermission();
    void startRemoteAccess();
    void enableNetworkSetup();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    friend class WindowTests;
    void refreshSession();
    void exportEvents();
    void refreshDeviceControls();
    void refreshDevices();
    void refreshShare();
    void refreshCaptureExclusion();
    void refreshAnnotations();
    void refreshControl();
    void refreshRemoteHistory();
    void rememberRemotePeer(const QString &code, const QString &name);
    void verifyStartupPeer();
    void invalidateNetworkSetup();
    void refreshMeetingParticipants();
    void setMeetingRoster(const QStringList &names);
    void expandVideo(bool expanded);
    void expandMeetingVideo(VideoSurface *surface);
    void restoreMeetingVideo();
    void refreshExpandedMeetingVideo();
    struct VideoLayoutState
    {
        QWidget *widget;
        QSize minimum;
        QSize maximum;
        QMargins margins;
        int spacing;
    };
    VideoSurface *expandedMeetingVideo_ = nullptr;
    QList<VideoLayoutState> videoLayoutStates_;
    QList<QPair<QWidget *, bool>> videoSiblingVisibility_;
    QList<int> videoParticipantStretches_;
    QList<QWidget *> normalWidgets_;
    Qt::WindowStates normalWindowState_;
    QRect normalWindowGeometry_;
    QPushButton *expandButton_ = nullptr;
    QVBoxLayout *contentLayout_ = nullptr;
    QVBoxLayout *networkLayout_ = nullptr;
    QVBoxLayout *workspaceLayout_ = nullptr;
    QHBoxLayout *connectionLayout_ = nullptr;
    QWidget *networkColumn_ = nullptr;
    zjdesk::SessionCoordinator session_;
    zjdesk::NetworkSetup networkSetup_;
    QLineEdit *networkPeer_ = nullptr;
    QString networkPeerKey_;
    bool networkVerifying_ = false;
    QLabel *networkStatus_ = nullptr;
    QLineEdit *serverEdit_ = nullptr;
    QLineEdit *roomEdit_ = nullptr;
    QComboBox *remoteHistory_ = nullptr;
    QLabel *remoteCodeLabel_ = nullptr;
    QPushButton *copyCodeButton_ = nullptr;
    QPushButton *joinButton_ = nullptr;
    QPushButton *remoteConnectButton_ = nullptr;
    QLineEdit *meetingServerEdit_ = nullptr;
    QLineEdit *meetingRoomEdit_ = nullptr;
    QLineEdit *meetingNameEdit_ = nullptr;
    QPushButton *meetingJoinButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QPropertyAnimation *statusAnimation_ = nullptr;
    QLabel *statusDetail_ = nullptr;
    QLabel *memberLabel_ = nullptr;
    QLabel *peerLabel_ = nullptr;
    QLabel *trafficLabel_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    VideoSurface *video_ = nullptr;
    VideoSurface *meetingLocalVideo_ = nullptr;
    VideoSurface *meetingRemoteVideo_ = nullptr;
    QList<VideoSurface *> meetingRemoteVideos_;
    VideoSurface *meetingShareVideo_ = nullptr;
    AnnotationLayer *desktopAnnotations_ = nullptr;
    QComboBox *annotationTool_ = nullptr;
    QPushButton *clearAnnotations_ = nullptr;
    QPushButton *clearAllAnnotations_ = nullptr;
    QPushButton *meetingRequestControl_ = nullptr;
    QPushButton *meetingGrantControl_ = nullptr;
    QPushButton *meetingStopControl_ = nullptr;
    QComboBox *meetingControlMember_ = nullptr;
    QLabel *meetingControlLabel_ = nullptr;
    bool meetingWasControlling_ = false;
    QVBoxLayout *meetingParticipantLayout_ = nullptr;
    QLabel *meetingMemberCount_ = nullptr;
    QWidget *meetingLocalMember_ = nullptr;
    QWidget *meetingRemoteMember_ = nullptr;
    QScrollArea *meetingRosterScroll_ = nullptr;
    QHBoxLayout *meetingMemberBadges_ = nullptr;
    QList<QWidget *> meetingMemberWidgets_;
    QPushButton *meetingRosterStack_ = nullptr;
    QDialog *meetingRosterPopup_ = nullptr;
    QLabel *meetingRosterTitle_ = nullptr;
    QGridLayout *meetingRosterGrid_ = nullptr;
    QList<QWidget *> meetingRosterPopupMembers_;
    QPushButton *audioButton_ = nullptr;
    QPushButton *refreshDevicesButton_ = nullptr;
    bool awaitingCameraPermission_ = false;
    QLabel *audioLabel_ = nullptr;
    QLabel *deviceLabel_ = nullptr;
    QPushButton *shareButton_ = nullptr;
    QLabel *shareLabel_ = nullptr;
    QPushButton *controlButton_ = nullptr;
    QPushButton *allowControlButton_ = nullptr;
    QPushButton *stopControlButton_ = nullptr;
    QLabel *controlLabel_ = nullptr;
};
