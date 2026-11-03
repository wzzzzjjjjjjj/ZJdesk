#pragma once

#include "media/media_environment.h"
#include "session/session_coordinator.h"
#include <QMainWindow>
#include <QStringList>

class QLabel;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;
class QHBoxLayout;
class QScrollArea;
class QDialog;
class QGridLayout;
class QPropertyAnimation;
class VideoSurface;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QString &serverUrl, const QString &roomId, const QString &name,
                        QWidget *parent = nullptr);
    ~MainWindow() override;
    void requestCameraPermission();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    friend class WindowTests;
    void refreshSession();
    void exportEvents();
    void refreshPreview();
    void refreshDevices();
    void refreshShare();
    void refreshControl();
    void refreshMeetingParticipants();
    void setMeetingRoster(const QStringList &names);
    void expandVideo(bool expanded);
    QList<QWidget *> normalWidgets_;
    Qt::WindowStates normalWindowState_;
    QPushButton *expandButton_ = nullptr;
    QVBoxLayout *contentLayout_ = nullptr;
    QVBoxLayout *workspaceLayout_ = nullptr;
    zjdesk::SessionCoordinator session_;
    QLineEdit *serverEdit_ = nullptr;
    QLineEdit *roomEdit_ = nullptr;
    QLineEdit *nameEdit_ = nullptr;
    QPushButton *joinButton_ = nullptr;
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
    VideoSurface *meetingShareVideo_ = nullptr;
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
    QComboBox *sourceCombo_ = nullptr;
    QPushButton *previewButton_ = nullptr;
    QPushButton *audioButton_ = nullptr;
    QPushButton *refreshDevicesButton_ = nullptr;
    bool awaitingCameraPermission_ = false;
    QLabel *previewLabel_ = nullptr;
    QLabel *audioLabel_ = nullptr;
    QLabel *deviceLabel_ = nullptr;
    QLabel *previewStatsLabel_ = nullptr;
    QPushButton *shareButton_ = nullptr;
    QLabel *shareLabel_ = nullptr;
    QPushButton *controlButton_ = nullptr;
    QPushButton *allowControlButton_ = nullptr;
    QPushButton *stopControlButton_ = nullptr;
    QLabel *controlLabel_ = nullptr;
};
