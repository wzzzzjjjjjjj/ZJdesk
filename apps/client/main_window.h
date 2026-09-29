#pragma once

#include "media/media_environment.h"
#include "session/session_coordinator.h"
#include <QMainWindow>

class QLabel;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QHBoxLayout;
class QVBoxLayout;
class VideoSurface;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QString &serverUrl, const QString &roomId, const QString &name,
                        QWidget *parent = nullptr);

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
    void expandVideo(bool expanded);
    QList<QWidget *> normalWidgets_;
    Qt::WindowStates normalWindowState_;
    QPushButton *expandButton_ = nullptr;
    QVBoxLayout *contentLayout_ = nullptr;
    QVBoxLayout *workspaceLayout_ = nullptr;
    QHBoxLayout *remoteLayout_ = nullptr;
    zjdesk::SessionCoordinator session_;
    QLineEdit *serverEdit_ = nullptr;
    QLineEdit *roomEdit_ = nullptr;
    QLineEdit *nameEdit_ = nullptr;
    QPushButton *joinButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *statusDetail_ = nullptr;
    QLabel *memberLabel_ = nullptr;
    QLabel *peerLabel_ = nullptr;
    QLabel *trafficLabel_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    VideoSurface *video_ = nullptr;
    QComboBox *sourceCombo_ = nullptr;
    QPushButton *previewButton_ = nullptr;
    QPushButton *audioButton_ = nullptr;
    QPushButton *refreshDevicesButton_ = nullptr;
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
