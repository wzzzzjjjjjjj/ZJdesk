#pragma once

#include "media/media_environment.h"
#include "session/session_coordinator.h"
#include <QMainWindow>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class VideoSurface;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QString &serverUrl, const QString &roomId, const QString &name,
                        QWidget *parent = nullptr);

private:
    void refreshSession();
    void exportEvents();
    zjdesk::SessionCoordinator session_;
    zjdesk::MediaEnvironment media_;
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
};
