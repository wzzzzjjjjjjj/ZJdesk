#include "main_window.h"
#include "platform/platform_info.h"
#include "video_surface.h"
#include "zjdesk/build_info.h"
#include <QFileDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStackedWidget>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QApplication>
#include <QKeyEvent>
#include <QVBoxLayout>

namespace
{
QLabel *text(const QString &value, const char *name = nullptr)
{
    auto *label = new QLabel(value);
    label->setTextFormat(Qt::PlainText);
    if (name)
        label->setObjectName(QString::fromLatin1(name));
    return label;
}

QFrame *card()
{
    auto *frame = new QFrame;
    frame->setObjectName("card");
    return frame;
}

QWidget *memberBadge(const char *objectName)
{
    auto *badge = new QWidget;
    badge->setObjectName(objectName);
    badge->setFixedWidth(92);
    auto *layout = new QVBoxLayout(badge);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto *avatar = text({}, "memberAvatar");
    avatar->setFixedSize(40, 40);
    avatar->setAlignment(Qt::AlignCenter);
    layout->addWidget(avatar, 0, Qt::AlignHCenter);
    auto *name = text({}, "memberName");
    name->setAlignment(Qt::AlignCenter);
    layout->addWidget(name);
    return badge;
}

void updateMemberBadge(QWidget *badge, const QString &name)
{
    badge->findChild<QLabel *>("memberAvatar")->setText(name.trimmed().left(1).toUpper());
    auto *label = badge->findChild<QLabel *>("memberName");
    label->setText(label->fontMetrics().elidedText(name, Qt::ElideRight, 90));
    badge->setToolTip(name);
    badge->setAccessibleName(name);
}

QIcon memberStackIcon(const QStringList &names)
{
    QPixmap pixmap(304, 88);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2, 2);
    auto font = painter.font();
    font.setPixelSize(14);
    font.setBold(true);
    painter.setFont(font);
    const QColor colors[]{QColor("#d9efeb"), QColor("#e1e9f6"), QColor("#eae2f1"), QColor("#f3e4d9"), QColor("#d4e8eb")};
    for (int i = 0; i < 5; ++i)
    {
        const QRectF circle(2 + i * 26, 2, 40, 40);
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(colors[i]);
        painter.drawEllipse(circle);
        painter.setPen(QColor("#326b70"));
        painter.drawText(circle, Qt::AlignCenter, i < 4
            ? names.at(i).trimmed().left(1).toUpper() : QStringLiteral("+%1").arg(names.size() - 4));
    }
    painter.end();
    pixmap.setDevicePixelRatio(2);
    return QIcon(pixmap);
}

QIcon navigationIcon(int page)
{
    QPixmap pixmap(40, 40);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2, 2);
    painter.setPen(QPen(QColor("#b9d3d5"), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (page == 0)
    {
        painter.drawRoundedRect(QRectF(2, 3, 16, 11), 2, 2);
        painter.drawLine(QPointF(10, 14), QPointF(10, 18));
        painter.drawLine(QPointF(6, 18), QPointF(14, 18));
    }
    else if (page == 1)
    {
        painter.drawRoundedRect(QRectF(2, 5, 11, 11), 2, 2);
        QPainterPath lens;
        lens.moveTo(13, 9); lens.lineTo(18, 6); lens.lineTo(18, 15); lens.lineTo(13, 12);
        painter.drawPath(lens);
    }
    else if (page == 2)
    {
        QPainterPath pulse;
        pulse.moveTo(2, 11); pulse.lineTo(6, 11); pulse.lineTo(9, 4);
        pulse.lineTo(12, 16); pulse.lineTo(15, 9); pulse.lineTo(18, 9);
        painter.drawPath(pulse);
    }
    else
    {
        painter.drawEllipse(QRectF(4, 4, 12, 12));
        painter.drawEllipse(QRectF(7, 7, 6, 6));
        for (int i = 0; i < 8; ++i)
        {
            painter.save();
            painter.translate(10, 10);
            painter.rotate(i * 45);
            painter.drawLine(QPointF(0, -6), QPointF(0, -9));
            painter.restore();
        }
    }
    pixmap.setDevicePixelRatio(2);
    return QIcon(pixmap);
}

QIcon meetingControlIcon(int control)
{
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2, 2);
    painter.setPen(QPen(QColor("#237c77"), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (control == 0)
    {
        painter.drawRoundedRect(QRectF(9, 3, 6, 12), 3, 3);
        painter.drawArc(QRectF(6, 6, 12, 12), 180 * 16, 180 * 16);
        painter.drawLine(QPointF(12, 18), QPointF(12, 22));
        painter.drawLine(QPointF(9, 22), QPointF(15, 22));
    }
    else if (control == 1)
    {
        QPainterPath speaker;
        speaker.moveTo(3, 9); speaker.lineTo(7, 9); speaker.lineTo(12, 5);
        speaker.lineTo(12, 19); speaker.lineTo(7, 15); speaker.lineTo(3, 15); speaker.closeSubpath();
        painter.drawPath(speaker);
        painter.drawArc(QRectF(10, 6, 10, 12), -65 * 16, 130 * 16);
        painter.drawArc(QRectF(9, 3, 15, 18), -60 * 16, 120 * 16);
    }
    else
    {
        painter.drawRoundedRect(QRectF(2, 6, 13, 12), 2, 2);
        QPainterPath lens;
        lens.moveTo(15, 10); lens.lineTo(22, 6); lens.lineTo(22, 18); lens.lineTo(15, 14);
        painter.drawPath(lens);
    }
    painter.end();
    pixmap.setDevicePixelRatio(2);
    return QIcon(pixmap);
}

// QComboBox 只在没有当前项时绘制占位文字，因此无设备时清空列表并把提示写在占位文字里。
// 占位文字默认沿用普通文字颜色，这里用动态属性让样式表把它改成灰色提示。
void updateNoDeviceStyle(QComboBox *combo, bool noDevice)
{
    if (combo->property("noDevice").toBool() == noDevice) return;
    combo->setProperty("noDevice", noDevice);
    combo->style()->unpolish(combo);
    combo->style()->polish(combo);
}

void showNoDeviceHint(QComboBox *combo, const QString &reason)
{
    combo->clear();
    combo->setCurrentIndex(-1);
    combo->setPlaceholderText(QStringLiteral("无可用设备"));
    combo->setToolTip(reason);
    updateNoDeviceStyle(combo, true);
}

void clearNoDeviceHint(QComboBox *combo)
{
    combo->setPlaceholderText(QString());
    combo->setToolTip(QString());
    updateNoDeviceStyle(combo, false);
}

QString cameraHintReason(const zjdesk::SessionCoordinator &session)
{
    if (!session.mediaEnvironment().ready) return session.mediaEnvironment().description;
    switch (session.cameraPermissionStatus())
    {
    case zjdesk::CameraPermission::Denied:
        return QStringLiteral("摄像头尚未授权，点击“刷新设备”打开授权引导；授权后返回程序即可使用");
    case zjdesk::CameraPermission::Unknown:
        return QStringLiteral("无法预先确认摄像头权限；未检测到摄像头时请检查系统权限或设备占用");
    case zjdesk::CameraPermission::Allowed:
        break;
    }
    return QStringLiteral("未检测到摄像头设备；连接摄像头后点击“刷新设备”重新检测");
}
} // namespace

MainWindow::MainWindow(const QString &serverUrl, const QString &roomId, const QString &name,
                       QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("ZJdesk · 跨平台协作工作台"));
    session_.setConferenceAutoConnect(true);
    resize(1280, 820);
    setMinimumSize(1040, 720);
    auto *root = new QWidget;
    setCentralWidget(root);
    auto *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *sidebar = new QFrame;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(180);
    auto *nav = new QVBoxLayout(sidebar);
    nav->setContentsMargins(16, 28, 16, 24);
    nav->setSpacing(8);
    auto *brandRow = new QHBoxLayout;
    brandRow->setSpacing(10);
    auto *brandMark = text("Z", "brandMark");
    brandMark->setAlignment(Qt::AlignCenter);
    brandMark->setFixedSize(34, 34);
    brandRow->addWidget(brandMark);
    brandRow->addWidget(text("ZJdesk", "brand"));
    nav->addLayout(brandRow);
    nav->addSpacing(4);
    nav->addWidget(text(QStringLiteral("让协作跨越系统"), "sidebarSubtitle"));
    nav->addSpacing(32);
    nav->addWidget(text("WORKSPACE", "navCaption"));
    nav->addSpacing(4);
    auto *workspaceButton = new QPushButton(QStringLiteral("协作空间"));
    auto *diagnosticsButton = new QPushButton(QStringLiteral("运行诊断"));
    auto *meetingButton = new QPushButton(QStringLiteral("视频会议"));
    workspaceButton->setAccessibleName(QStringLiteral("协作空间"));
    meetingButton->setAccessibleName(QStringLiteral("视频会议"));
    int pageIndex = 0;
    for (auto *button : {workspaceButton, meetingButton, diagnosticsButton})
    {
        button->setObjectName("navButton");
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setMinimumHeight(44);
        button->setIcon(navigationIcon(pageIndex++));
        button->setIconSize(QSize(20, 20));
        nav->addWidget(button);
    }
    workspaceButton->setChecked(true);
    nav->addStretch();
    nav->addWidget(text("WINDOWS + LINUX", "navCaption"));
    nav->addWidget(text(QStringLiteral("屏幕共享  %1").arg(zjdesk::build::Version), "sidebarSubtitle"));
    auto *settingsButton = new QPushButton(QStringLiteral("设置"));
    settingsButton->setObjectName("settingsButton");
    settingsButton->setIcon(navigationIcon(3));
    settingsButton->setIconSize(QSize(20, 20));
    settingsButton->setToolTip(QStringLiteral("选择麦克风、扬声器和摄像头"));
    auto *settingsRow = new QHBoxLayout;
    settingsRow->addStretch();
    settingsRow->addWidget(settingsButton);
    nav->addLayout(settingsRow);
    rootLayout->addWidget(sidebar);

    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout_ = contentLayout;
    contentLayout->setContentsMargins(30, 26, 30, 22);
    contentLayout->setSpacing(18);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    auto *headerWidget = new QWidget;
    headerWidget->setLayout(header);
    auto *titles = new QVBoxLayout;
    auto *pageTitle = text(QStringLiteral("协作工作台"), "pageTitle");
    auto *pageSubtitle = text(QStringLiteral("连接彼此的桌面，专注眼前的协作。"), "muted");
    titles->setSpacing(5);
    titles->addWidget(text("ZJDESK  /  COLLABORATION", "eyebrow"));
    titles->addWidget(pageTitle);
    titles->addWidget(pageSubtitle);
    header->addLayout(titles);
    header->addStretch();
    statusLabel_ = text(QStringLiteral("未加入房间"), "statusBadge");
    auto *statusEffect = new QGraphicsOpacityEffect(statusLabel_);
    statusEffect->setOpacity(1.0);
    statusLabel_->setGraphicsEffect(statusEffect);
    statusAnimation_ = new QPropertyAnimation(statusEffect, "opacity", this);
    statusAnimation_->setDuration(180);
    statusAnimation_->setStartValue(0.55);
    statusAnimation_->setEndValue(1.0);
    header->addWidget(statusLabel_, 0, Qt::AlignVCenter);
    auto *profile = new QWidget;
    profile->setObjectName("userProfile");
    auto *profileLayout = new QHBoxLayout(profile);
    profileLayout->setContentsMargins(12, 0, 0, 0);
    profileLayout->setSpacing(10);
    auto *profileAvatar = text({}, "profileAvatar");
    profileAvatar->setFixedSize(40, 40);
    profileAvatar->setAlignment(Qt::AlignCenter);
    meetingNameEdit_ = new QLineEdit(name);
    meetingNameEdit_->setObjectName("meetingDisplayName");
    meetingNameEdit_->setMaxLength(40);
    meetingNameEdit_->setFixedWidth(120);
    meetingNameEdit_->setPlaceholderText(QStringLiteral("你的昵称"));
    meetingNameEdit_->setAccessibleName(QStringLiteral("你的昵称"));
    profileLayout->addWidget(profileAvatar);
    profileLayout->addWidget(meetingNameEdit_);
    header->addWidget(profile, 0, Qt::AlignVCenter);
    const auto refreshProfile = [this, profile, profileAvatar] {
        const auto nickname = meetingNameEdit_->text().trimmed();
        profileAvatar->setText(nickname.isEmpty() ? QStringLiteral("我") : nickname.left(1).toUpper());
        profile->setToolTip(nickname);
    };
    connect(meetingNameEdit_, &QLineEdit::textChanged, this, refreshProfile);
    refreshProfile();
    contentLayout->addWidget(headerWidget);

    auto *pages = new QStackedWidget;
    auto *workspace = new QWidget;
    auto *workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout_ = workspaceLayout;
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(14);

    auto *connectionCard = card();
    auto *connectionLayout = new QHBoxLayout(connectionCard);
    connectionLayout->setContentsMargins(20, 16, 20, 16);
    connectionLayout->setSpacing(14);
    serverEdit_ = new QLineEdit(serverUrl);
    roomEdit_ = new QLineEdit(roomId);
    nameEdit_ = new QLineEdit(name);
    serverEdit_->setObjectName("serverAddress");
    roomEdit_->setObjectName("roomId");
    nameEdit_->setObjectName("displayName");
    serverEdit_->setPlaceholderText("ws://192.168.1.100:8765");
    roomEdit_->setPlaceholderText(QStringLiteral("输入房间码"));
    roomEdit_->setToolTip(QStringLiteral("1–32 位字母、数字、下划线或连字符，区分大小写"));
    nameEdit_->setPlaceholderText(QStringLiteral("输入你的昵称"));
    roomEdit_->setMaxLength(32);
    nameEdit_->setMaxLength(40);
    const QList<QPair<QString, QLineEdit *>> fields = {
        {QStringLiteral("服务地址"), serverEdit_}, {QStringLiteral("房间码"), roomEdit_},
        {QStringLiteral("你的昵称"), nameEdit_}};
    for (const auto &field : fields)
    {
        auto *column = new QVBoxLayout;
        column->setSpacing(6);
        auto *label = text(field.first, "fieldLabel");
        label->setBuddy(field.second);
        field.second->setAccessibleName(field.first);
        column->addWidget(label);
        column->addWidget(field.second);
        connectionLayout->addLayout(column, field.second == serverEdit_ ? 3 : 2);
    }
    joinButton_ = new QPushButton(QStringLiteral("加入房间"));
    joinButton_->setObjectName("primaryButton");
    joinButton_->setMinimumWidth(108);
    connectionLayout->addWidget(joinButton_, 0, Qt::AlignBottom);
    workspaceLayout->addWidget(connectionCard);

    auto *stage = new QHBoxLayout;
    stage->setSpacing(16);
    auto *videoColumn = new QVBoxLayout;
    videoColumn->setSpacing(0);
    auto *stageHeader = new QFrame;
    stageHeader->setObjectName("stageHeader");
    auto *remoteLayout = new QHBoxLayout(stageHeader);
    remoteLayout->setContentsMargins(16, 8, 12, 8);
    remoteLayout->addWidget(text(QStringLiteral("共享画面"), "stageTitle"));
    remoteLayout->addStretch();
    remoteLayout->addWidget(text("SCREEN / PREVIEW", "stageMeta"));
    expandButton_ = new QPushButton(QStringLiteral("放大画面"));
    expandButton_->setObjectName("expandVideoButton");
    expandButton_->setCheckable(true);
    remoteLayout->addWidget(expandButton_);
    videoColumn->addWidget(stageHeader);
    video_ = new VideoSurface;
    video_->setObjectName("videoSurface");
    videoColumn->addWidget(video_, 1);
    stage->addLayout(videoColumn, 3);
    auto *sessionCard = card();
    auto *sessionLayout = new QVBoxLayout(sessionCard);
    sessionLayout->setContentsMargins(18, 16, 18, 16);
    sessionLayout->setSpacing(8);
    auto *memberHeader = new QHBoxLayout;
    memberHeader->addWidget(text(QStringLiteral("房间成员"), "sectionTitle"));
    memberLabel_ = text(QStringLiteral("0 / 2 人"), "memberCount");
    memberHeader->addStretch();
    memberHeader->addWidget(memberLabel_);
    sessionLayout->addLayout(memberHeader);
    auto *members = new QFormLayout;
    members->setHorizontalSpacing(12);
    members->setVerticalSpacing(8);
    auto *localName = text(name, "localName");
    localName->setWordWrap(true);
    localName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    members->addRow(text(QStringLiteral("本机"), "fieldLabel"), localName);
    peerLabel_ = text(QStringLiteral("尚未加入"), "peerName");
    peerLabel_->setWordWrap(true);
    peerLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    members->addRow(text(QStringLiteral("对方"), "fieldLabel"), peerLabel_);
    sessionLayout->addLayout(members);
    auto *divider = new QFrame;
    divider->setFrameShape(QFrame::HLine);
    sessionLayout->addWidget(divider);
    statusDetail_ = text(session_.detail(), "muted");
    statusDetail_->setWordWrap(true);
    sessionLayout->addWidget(statusDetail_);
    auto *sessionScroll = new QScrollArea;
    sessionScroll->setObjectName("sessionPanel");
    sessionScroll->setFrameShape(QFrame::NoFrame);
    sessionScroll->setWidgetResizable(true);
    sessionScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sessionScroll->setMinimumWidth(250);
    sessionScroll->setMaximumWidth(270);
    sessionScroll->setWidget(sessionCard);
    stage->addWidget(sessionScroll, 1);
    workspaceLayout->addLayout(stage, 1);

    auto *deviceActionsCard = card();
    auto *deviceActionsLayout = new QHBoxLayout(deviceActionsCard);
    deviceActionsLayout->setContentsMargins(18, 12, 18, 12);
    deviceActionsLayout->setSpacing(10);
    audioButton_ = new QPushButton(QStringLiteral("测试声音"));
    audioButton_->setObjectName("audioTestButton");
    deviceActionsLayout->addWidget(audioButton_);
    refreshDevicesButton_ = new QPushButton(QStringLiteral("刷新设备"));
    deviceActionsLayout->addWidget(refreshDevicesButton_);

    shareButton_ = new QPushButton(QStringLiteral("共享主屏幕"));
    shareButton_->setObjectName("shareButton");
    shareButton_->setMinimumWidth(120);
    shareButton_->setToolTip(QStringLiteral("将整个主屏幕及鼠标指针发送给房间中的另一位成员"));
    shareLabel_ = text(session_.shareDetail(), "shareStatus");
    shareLabel_->setWordWrap(true);
    sessionLayout->addWidget(text(QStringLiteral("远程协助"), "sectionTitle"));
    controlButton_ = new QPushButton(QStringLiteral("请求控制 Linux"));
    controlButton_->setObjectName("controlButton");
    allowControlButton_ = new QPushButton(QStringLiteral("允许控制"));
    allowControlButton_->setObjectName("allowControlButton");
    stopControlButton_ = new QPushButton(QStringLiteral("停止控制"));
    stopControlButton_->setObjectName("stopControlButton");
    controlLabel_ = text(session_.remoteDetail(), "controlStatus");
    controlLabel_->setWordWrap(true);
    sessionLayout->addWidget(controlButton_);
    auto *approvalLayout = new QHBoxLayout;
    approvalLayout->addWidget(allowControlButton_);
    approvalLayout->addWidget(stopControlButton_);
    sessionLayout->addLayout(approvalLayout);
    sessionLayout->addWidget(controlLabel_);
    sessionLayout->addStretch();
    auto *notice = text(QStringLiteral("远程控制需对方本地允许。"), "notice");
    notice->setWordWrap(true);
    sessionLayout->addWidget(notice);
    pages->addWidget(workspace);

    auto *meeting = new QWidget;
    meeting->setObjectName("meetingPage");
    auto *meetingLayout = new QVBoxLayout(meeting);
    meetingLayout->setContentsMargins(0, 0, 0, 0);
    meetingLayout->setSpacing(10);
    auto *meetingToolbar = new QHBoxLayout;
    meetingToolbar->setSpacing(8);
    auto *muteMic = new QPushButton(QStringLiteral("麦克风 · 开"));
    muteMic->setObjectName("conferenceMuteMicrophone");
    auto *muteSpeaker = new QPushButton(QStringLiteral("扬声器 · 开"));
    muteSpeaker->setObjectName("conferenceMuteSpeaker");
    auto *hideCamera = new QPushButton(QStringLiteral("摄像头 · 开"));
    hideCamera->setObjectName("conferenceHideCamera");
    hideCamera->setToolTip(QStringLiteral("隐藏本地与发送画面；摄像头设备仍在采集"));
    muteMic->setToolTip(QStringLiteral("切换麦克风静音"));
    muteSpeaker->setToolTip(QStringLiteral("切换扬声器静音"));
    muteMic->setIcon(meetingControlIcon(0));
    muteSpeaker->setIcon(meetingControlIcon(1));
    hideCamera->setIcon(meetingControlIcon(2));
    for (auto *button : {muteMic, muteSpeaker, hideCamera})
    {
        button->setCheckable(true);
        button->setProperty("meetingToggle", true);
        button->setMinimumWidth(88);
    }
    auto *meetingSettings = new QDialog(this);
    meetingSettings->setObjectName("meetingSettings");
    meetingSettings->setWindowTitle(QStringLiteral("设置"));
    meetingSettings->resize(660, 360);
    auto *settingsLayout = new QVBoxLayout(meetingSettings);
    settingsLayout->addWidget(text(QStringLiteral("音视频设备"), "sectionTitle"));
    meetingServerEdit_ = new QLineEdit(serverUrl);
    meetingRoomEdit_ = new QLineEdit;
    meetingServerEdit_->setObjectName("meetingServerAddress");
    meetingRoomEdit_->setObjectName("meetingRoomId");
    meetingRoomEdit_->setPlaceholderText(QStringLiteral("输入会议房间码"));
    meetingRoomEdit_->setMaxLength(32);
    meetingRoomEdit_->setAccessibleName(QStringLiteral("会议房间码"));
    meetingRoomEdit_->setMinimumWidth(100);
    meetingRoomEdit_->setMaximumWidth(230);
    // The meeting uses the configured service; keep its address out of this UI.
    meetingServerEdit_->setParent(meetingSettings);
    meetingServerEdit_->hide();
    meetingToolbar->addWidget(meetingRoomEdit_, 1);
    meetingJoinButton_ = new QPushButton(QStringLiteral("加入会议"));
    meetingJoinButton_->setObjectName("meetingJoinButton");
    meetingJoinButton_->setProperty("role", "primary");
    meetingToolbar->addWidget(meetingJoinButton_);
    meetingToolbar->addStretch();
    for (auto *button : {muteMic, muteSpeaker, hideCamera}) meetingToolbar->addWidget(button);
    meetingLayout->addLayout(meetingToolbar);
    connect(settingsButton, &QPushButton::clicked, meetingSettings, [meetingSettings] {
        meetingSettings->show();
    });
    connect(meetingRoomEdit_, &QLineEdit::returnPressed, meetingJoinButton_, &QPushButton::click);
    connect(serverEdit_, &QLineEdit::textChanged, meetingServerEdit_, &QLineEdit::setText);
    connect(meetingServerEdit_, &QLineEdit::textChanged, serverEdit_, &QLineEdit::setText);
    connect(nameEdit_, &QLineEdit::textChanged, meetingNameEdit_, &QLineEdit::setText);
    connect(meetingNameEdit_, &QLineEdit::textChanged, nameEdit_, &QLineEdit::setText);
    connect(meetingNameEdit_, &QLineEdit::textChanged, this, &MainWindow::refreshMeetingParticipants);
    auto *meetingInfo = new QWidget;
    auto *meetingInfoLayout = new QHBoxLayout(meetingInfo);
    meetingInfoLayout->setContentsMargins(0, 0, 0, 0);
    meetingInfoLayout->setSpacing(16);
    auto *meetingStatus = text(session_.conferenceDetail(), "conferenceStatus");
    meetingStatus->setWordWrap(true);
    meetingStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    meetingInfoLayout->addWidget(meetingStatus, 1);
    meetingLayout->addWidget(meetingInfo);
    auto *meetingDevices = card();
    auto *deviceRow = new QHBoxLayout(meetingDevices);
    deviceRow->setContentsMargins(16, 10, 16, 12);
    deviceRow->setSpacing(16);
    for (const auto &entry : {qMakePair("conferenceCamera", QStringLiteral("摄像头")),
                             qMakePair("conferenceMicrophone", QStringLiteral("麦克风")),
                             qMakePair("conferenceSpeaker", QStringLiteral("扬声器"))})
    {
        auto *deviceColumn = new QVBoxLayout;
        deviceColumn->setSpacing(6);
        auto *deviceTitle = text(entry.second, "fieldLabel");
        deviceColumn->addWidget(deviceTitle);
        auto *combo = new QComboBox;
        combo->setObjectName(entry.first);
        combo->setAccessibleName(entry.second);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(8);
        combo->addItem(QStringLiteral("默认设备"), QString());
        deviceTitle->setBuddy(combo);
        deviceColumn->addWidget(combo);
        auto *joinEnabled = new QCheckBox(QStringLiteral("入会后默认开启"));
        const auto settingName = QString::fromLatin1(entry.first).mid(10) + "Enabled";
        joinEnabled->setObjectName("join" + settingName);
        joinEnabled->setAccessibleName(entry.second + QStringLiteral("入会后默认开启"));
        const auto settingKey = "join/" + settingName;
        joinEnabled->setProperty("settingsKey", settingKey);
        QSettings preferences(QSettings::IniFormat, QSettings::UserScope, "ZJdesk", "meeting");
        joinEnabled->setChecked(preferences.value(settingKey, true).toBool());
        deviceColumn->addWidget(joinEnabled);
        deviceRow->addLayout(deviceColumn, 1);
    }
    settingsLayout->addWidget(meetingDevices);
    const auto applyJoinDefaults = [this] {
        session_.setConferenceJoinDefaults(findChild<QCheckBox *>("joinMicrophoneEnabled")->isChecked(),
            findChild<QCheckBox *>("joinCameraEnabled")->isChecked(),
            findChild<QCheckBox *>("joinSpeakerEnabled")->isChecked());
    };
    applyJoinDefaults();
    for (const auto *name : {"joinMicrophoneEnabled", "joinCameraEnabled", "joinSpeakerEnabled"})
    {
        auto *check = findChild<QCheckBox *>(name);
        connect(check, &QCheckBox::toggled, this, [check, applyJoinDefaults](bool enabled) {
            QSettings preferences(QSettings::IniFormat, QSettings::UserScope, "ZJdesk", "meeting");
            preferences.setValue(check->property("settingsKey").toString(), enabled);
            applyJoinDefaults();
        });
    }
    settingsLayout->addWidget(deviceActionsCard);
    auto *deviceNotice = text(QStringLiteral("默认使用默认设备，可选择其他设备并设置入会后的开启状态。默认开关自动保存，下次连接生效；不改变当前通话。通话中更换设备请先离会。建议佩戴耳机。"), "muted");
    deviceNotice->setWordWrap(true);
    settingsLayout->addWidget(deviceNotice);
    auto *closeSettings = new QPushButton(QStringLiteral("完成"));
    settingsLayout->addWidget(closeSettings, 0, Qt::AlignRight);
    connect(closeSettings, &QPushButton::clicked, meetingSettings, &QDialog::hide);
    auto *views = new QHBoxLayout;
    views->setSpacing(14);
    auto *localConference = new VideoSurface;
    meetingLocalVideo_ = localConference;
    localConference->setObjectName("conferenceLocalVideo");
    localConference->setParticipantTile(true);
    auto *remoteConference = new VideoSurface;
    meetingRemoteVideo_ = remoteConference;
    remoteConference->setObjectName("conferenceRemoteVideo");
    remoteConference->setParticipantTile(true);
    auto *participants = card();
    participants->setObjectName("meetingParticipants");
    participants->setMinimumWidth(190);
    participants->setMaximumWidth(300);
    meetingParticipantLayout_ = new QVBoxLayout(participants);
    meetingParticipantLayout_->setContentsMargins(10, 12, 10, 12);
    meetingParticipantLayout_->setSpacing(10);
    meetingParticipantLayout_->addWidget(text(QStringLiteral("参会画面"), "sectionTitle"));
    for (auto *surface : {localConference, remoteConference})
    {
        surface->setMinimumSize(160, 110);
        surface->setMaximumHeight(230);
        surface->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        meetingParticipantLayout_->addWidget(surface, 1);
        connect(surface, &VideoSurface::frameAvailabilityChanged, this, &MainWindow::refreshMeetingParticipants);
    }
    auto *waiting = text(QStringLiteral("等待其他成员加入"), "meetingWaiting");
    waiting->setAlignment(Qt::AlignCenter);
    waiting->setWordWrap(true);
    meetingParticipantLayout_->addWidget(waiting, 1);
    meetingParticipantLayout_->addStretch(1);
    meetingShareVideo_ = new VideoSurface;
    meetingShareVideo_->setObjectName("meetingShareVideo");
    meetingShareVideo_->setMinimumSize(300, 200);
    meetingShareVideo_->setParticipant(QStringLiteral("屏幕共享"), QStringLiteral("共享屏幕后，内容将在这里显示"));
    auto *tile = card();
    tile->setObjectName("meetingStage");
    auto *tileLayout = new QVBoxLayout(tile);
    tileLayout->setContentsMargins(0, 0, 0, 0);
    tileLayout->setSpacing(0);
    auto *tileHeader = new QFrame;
    tileHeader->setObjectName("stageHeader");
    auto *tileTitle = new QHBoxLayout(tileHeader);
    tileTitle->setContentsMargins(14, 8, 14, 8);
    tileTitle->addWidget(text(QStringLiteral("屏幕共享"), "stageTitle"));
    tileTitle->addStretch();
    tileTitle->addWidget(text(QStringLiteral("共享内容"), "stageMeta"));
    tileLayout->addWidget(tileHeader);
    tileLayout->addWidget(meetingShareVideo_, 1);
    views->addWidget(tile, 4);
    views->addWidget(participants, 1);
    meetingLayout->addLayout(views, 1);
    auto *controlsCard = card();
    controlsCard->setObjectName("meetingFooter");
    auto *footerColumn = new QVBoxLayout(controlsCard);
    footerColumn->setContentsMargins(16, 10, 16, 10);
    footerColumn->setSpacing(8);
    auto *meetingMembers = new QHBoxLayout;
    meetingMemberCount_ = text(QStringLiteral("尚未入会"), "meetingMemberCount");
    meetingMembers->addWidget(meetingMemberCount_);
    meetingLocalMember_ = memberBadge("meetingLocalMember");
    meetingRemoteMember_ = memberBadge("meetingRemoteMember");
    meetingMemberWidgets_ = {meetingLocalMember_, meetingRemoteMember_};
    auto *memberStrip = new QWidget;
    memberStrip->setObjectName("meetingMemberStrip");
    meetingMemberBadges_ = new QHBoxLayout(memberStrip);
    meetingMemberBadges_->setContentsMargins(0, 0, 0, 0);
    meetingMemberBadges_->setSpacing(8);
    for (auto *badge : meetingMemberWidgets_) meetingMemberBadges_->addWidget(badge);
    meetingMemberBadges_->addStretch();
    meetingRosterScroll_ = new QScrollArea;
    meetingRosterScroll_->setObjectName("meetingRosterScroll");
    meetingRosterScroll_->setFrameShape(QFrame::NoFrame);
    meetingRosterScroll_->setWidgetResizable(true);
    meetingRosterScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    meetingRosterScroll_->setFixedHeight(78);
    meetingRosterScroll_->setWidget(memberStrip);
    meetingMembers->addWidget(meetingRosterScroll_, 1);
    meetingRosterStack_ = new QPushButton;
    meetingRosterStack_->setObjectName("meetingRosterStack");
    meetingRosterStack_->setFixedSize(164, 56);
    meetingRosterStack_->setIconSize(QSize(152, 44));
    meetingRosterStack_->setCursor(Qt::PointingHandCursor);
    meetingMembers->addWidget(meetingRosterStack_);
    meetingMembers->addStretch();
    meetingRosterPopup_ = new QDialog(this, Qt::Popup);
    meetingRosterPopup_->setObjectName("meetingRosterPopup");
    meetingRosterPopup_->setWindowTitle(QStringLiteral("会议成员"));
    meetingRosterPopup_->resize(460, 300);
    auto *popupLayout = new QVBoxLayout(meetingRosterPopup_);
    popupLayout->setContentsMargins(18, 16, 18, 16);
    auto *popupHeader = new QHBoxLayout;
    meetingRosterTitle_ = text({}, "meetingRosterTitle");
    popupHeader->addWidget(meetingRosterTitle_, 1);
    auto *closeRoster = new QPushButton(QStringLiteral("关闭"));
    closeRoster->setObjectName("meetingRosterClose");
    popupHeader->addWidget(closeRoster);
    popupLayout->addLayout(popupHeader);
    auto *popupScroll = new QScrollArea;
    popupScroll->setFrameShape(QFrame::NoFrame);
    popupScroll->setWidgetResizable(true);
    popupScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *popupMembers = new QWidget;
    popupMembers->setObjectName("meetingRosterPopupMembers");
    meetingRosterGrid_ = new QGridLayout(popupMembers);
    meetingRosterGrid_->setContentsMargins(0, 8, 0, 8);
    meetingRosterGrid_->setSpacing(12);
    meetingRosterGrid_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    popupScroll->setWidget(popupMembers);
    popupLayout->addWidget(popupScroll);
    connect(closeRoster, &QPushButton::clicked, meetingRosterPopup_, &QDialog::hide);
    connect(meetingRosterStack_, &QPushButton::clicked, this, [this] {
        const auto available = screen()->availableGeometry();
        const auto bounds = available.contains(frameGeometry().center()) ? available : frameGeometry();
        auto position = meetingRosterStack_->mapToGlobal(QPoint(0, -meetingRosterPopup_->height() - 8));
        position.setX(qBound(bounds.left(), position.x(), qMax(bounds.left(), bounds.right() - meetingRosterPopup_->width() + 1)));
        position.setY(qBound(bounds.top(), position.y(), qMax(bounds.top(), bounds.bottom() - meetingRosterPopup_->height() + 1)));
        meetingRosterPopup_->move(position);
        meetingRosterPopup_->show();
    });
    meetingMembers->addWidget(shareButton_, 0, Qt::AlignVCenter);
    footerColumn->addLayout(meetingMembers);
    auto *meetingControls = new QHBoxLayout;
    meetingControls->setSpacing(8);
    shareLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    meetingControls->addWidget(shareLabel_, 1);
    footerColumn->addLayout(meetingControls);
    meetingLayout->addWidget(controlsCard);
    pages->addWidget(meeting);
    auto selectMeetingDevices = [this] {
        return session_.setConferenceDevices({findChild<QComboBox *>("conferenceCamera")->currentData().toString(),
            findChild<QComboBox *>("conferenceMicrophone")->currentData().toString(),
            findChild<QComboBox *>("conferenceSpeaker")->currentData().toString()});
    };
    for (const auto *name : {"conferenceCamera", "conferenceMicrophone", "conferenceSpeaker"})
        connect(findChild<QComboBox *>(name), &QComboBox::currentIndexChanged, this,
            [selectMeetingDevices](int) { selectMeetingDevices(); });
    auto updateMuteLabels = [this, muteMic, hideCamera, muteSpeaker] {
        muteMic->setText(muteMic->isChecked() ? QStringLiteral("麦克风 · 关") : QStringLiteral("麦克风 · 开"));
        muteSpeaker->setText(muteSpeaker->isChecked() ? QStringLiteral("扬声器 · 关") : QStringLiteral("扬声器 · 开"));
        hideCamera->setText(hideCamera->isChecked() ? QStringLiteral("摄像头 · 关") : QStringLiteral("摄像头 · 开"));
        refreshMeetingParticipants();
    };
    auto mute = [this, muteMic, hideCamera, muteSpeaker, updateMuteLabels] {
        session_.setConferenceMuted(muteMic->isChecked(), hideCamera->isChecked(), muteSpeaker->isChecked());
        updateMuteLabels();
    };
    for (auto *button : {muteMic, hideCamera, muteSpeaker}) connect(button, &QPushButton::toggled, this, mute);
    connect(&session_, &zjdesk::SessionCoordinator::conferenceLocalFrame, localConference, &VideoSurface::setFrame);
    connect(&session_, &zjdesk::SessionCoordinator::conferenceRemoteFrame, remoteConference, &VideoSurface::setFrame);
    auto refreshMeeting = [=] {
        using CS = zjdesk::SessionCoordinator::ConferenceState;
        const auto state = session_.conferenceState();
        const bool mediaActive = state == CS::Connecting || state == CS::Running;
        const auto roomDetail = session_.roomType() != zjdesk::SessionCoordinator::RoomType::Meeting
            ? QStringLiteral("尚未加入会议房间") : session_.memberCount() > 0
            ? QStringLiteral("%1 人已入会").arg(session_.memberCount()) : session_.detail();
        meetingStatus->setText(roomDetail + QStringLiteral(" · ") + session_.conferenceDetail());
        for (auto *button : {muteMic, hideCamera, muteSpeaker})
        {
            button->setEnabled(mediaActive);
        }
        const QSignalBlocker blockMic(muteMic), blockCamera(hideCamera), blockSpeaker(muteSpeaker);
        muteMic->setChecked(mediaActive ? session_.conferenceMicrophoneMuted()
            : !findChild<QCheckBox *>("joinMicrophoneEnabled")->isChecked());
        hideCamera->setChecked(mediaActive ? session_.conferenceCameraHidden()
            : !findChild<QCheckBox *>("joinCameraEnabled")->isChecked());
        muteSpeaker->setChecked(mediaActive ? session_.conferenceSpeakerMuted()
            : !findChild<QCheckBox *>("joinSpeakerEnabled")->isChecked());
        updateMuteLabels();
        for (const auto *name : {"conferenceCamera", "conferenceMicrophone", "conferenceSpeaker"})
            findChild<QComboBox *>(name)->setEnabled(!mediaActive && state != CS::Calling);
        refreshDeviceControls();
        refreshMeetingParticipants();
    };
    connect(&session_, &zjdesk::SessionCoordinator::conferenceChanged, this, refreshMeeting);
    connect(&session_, &zjdesk::SessionCoordinator::conferenceCameraChanged, this, &MainWindow::refreshMeetingParticipants);
    connect(&session_, &zjdesk::SessionCoordinator::stateChanged, this, refreshMeeting);
    connect(&session_, &zjdesk::SessionCoordinator::membersChanged, this, refreshMeeting);
    connect(meetingButton, &QPushButton::clicked, pages, [=] {
        pages->setCurrentWidget(meeting);
        pageTitle->setText(QStringLiteral("视频会议"));
        pageSubtitle->setText(QStringLiteral("与房间中的其他成员进行视频和语音通话。"));
    });
    // 进入视频会议页时自动枚举一次设备，确认摄像头是否存在；通话中用现有设备，不打断采集。
    connect(meetingButton, &QPushButton::clicked, this, [this] {
        if (session_.conferenceState() != zjdesk::SessionCoordinator::ConferenceState::Idle) return;
        QTimer::singleShot(0, &session_, &zjdesk::SessionCoordinator::refreshDevices);
    });
    connect(&session_, &zjdesk::SessionCoordinator::conferenceChanged, this, [this, meetingButton] {
        if (session_.conferenceState() == zjdesk::SessionCoordinator::ConferenceState::Incoming) meetingButton->click();
    });

    auto *diagnostics = new QWidget;
    auto *diagnosticsLayout = new QVBoxLayout(diagnostics);
    diagnosticsLayout->setContentsMargins(0, 0, 0, 0);
    diagnosticsLayout->setSpacing(16);
    auto *environmentCard = card();
    auto *environmentLayout = new QFormLayout(environmentCard);
    environmentLayout->setContentsMargins(20, 20, 20, 20);
    const auto platform = zjdesk::PlatformInfo::current();
    environmentLayout->addRow(QStringLiteral("操作系统"), text(platform.system));
    environmentLayout->addRow(QStringLiteral("架构 / 会话"), text(platform.architecture + " / " + platform.desktopSession));
    environmentLayout->addRow(QStringLiteral("Qt 版本"), text(QString::fromLatin1(qVersion())));
    const auto &media = session_.mediaEnvironment();
    auto *mediaDescription = text(media.description);
    mediaDescription->setWordWrap(true);
    environmentLayout->addRow(QStringLiteral("媒体状态"), mediaDescription);
    environmentLayout->addRow(QStringLiteral("媒体版本"), text(media.version.isEmpty() ? QStringLiteral("未启用") : media.version));
    deviceLabel_ = text(QStringLiteral("正在枚举设备"));
    deviceLabel_->setWordWrap(true);
    auto *deviceScroll = new QScrollArea;
    deviceScroll->setWidgetResizable(true);
    deviceScroll->setFrameShape(QFrame::NoFrame);
    deviceScroll->setMaximumHeight(100);
    deviceScroll->setWidget(deviceLabel_);
    environmentLayout->addRow(QStringLiteral("音视频设备"), deviceScroll);
    audioLabel_ = text(session_.audioDetail());
    audioLabel_->setWordWrap(true);
    environmentLayout->addRow(QStringLiteral("音频测试"), audioLabel_);
    trafficLabel_ = text(QStringLiteral("发送 0 · 接收 0"));
    environmentLayout->addRow(QStringLiteral("信令消息"), trafficLabel_);
    environmentLayout->addRow(QStringLiteral("媒体 RTT / 码率"), text(QStringLiteral("尚未实现统计")));
    diagnosticsLayout->addWidget(environmentCard);
    auto *eventsHeader = new QHBoxLayout;
    eventsHeader->addWidget(text(QStringLiteral("会话事件"), "sectionTitle"));
    eventsHeader->addStretch();
    auto *exportButton = new QPushButton(QStringLiteral("导出事件报告"));
    eventsHeader->addWidget(exportButton);
    diagnosticsLayout->addLayout(eventsHeader);
    logView_ = new QPlainTextEdit;
    logView_->setAccessibleName(QStringLiteral("会话事件日志"));
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(zjdesk::EventLog::MaxEntries);
    diagnosticsLayout->addWidget(logView_, 1);
    pages->addWidget(diagnostics);
    contentLayout->addWidget(pages, 1);
    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(0, 0, 0, 0);
    auto *footerWidget = new QWidget;
    footerWidget->setLayout(footer);
    footer->addWidget(text(QStringLiteral("局域网双人协作"), "footer"));
    footer->addStretch();
    footer->addWidget(text(QString("ZJdesk %1").arg(zjdesk::build::Version), "footer"));
    contentLayout->addWidget(footerWidget);
    rootLayout->addWidget(content, 1);
    normalWidgets_ = {sidebar, headerWidget, connectionCard, sessionScroll, stageHeader,
        deviceActionsCard, footerWidget};
    connect(expandButton_, &QPushButton::toggled, this, &MainWindow::expandVideo);
    qApp->installEventFilter(this);

    connect(workspaceButton, &QPushButton::clicked, pages, [pages, pageTitle, pageSubtitle] {
        pages->setCurrentIndex(0);
        pageTitle->setText(QStringLiteral("协作工作台"));
        pageSubtitle->setText(QStringLiteral("连接彼此的桌面，专注眼前的协作。"));
    });
    connect(diagnosticsButton, &QPushButton::clicked, pages, [pages, diagnostics, pageTitle, pageSubtitle] {
        pages->setCurrentWidget(diagnostics);
        pageTitle->setText(QStringLiteral("运行诊断"));
        pageSubtitle->setText(QStringLiteral("查看运行环境、设备状态与会话事件。"));
    });
    connect(joinButton_, &QPushButton::clicked, this, [this] {
        using State = zjdesk::SessionCoordinator::State;
        if (session_.state() == State::Idle || session_.state() == State::Failed)
            session_.join(QUrl(serverEdit_->text().trimmed()), roomEdit_->text().trimmed(), nameEdit_->text().trimmed());
        else
            session_.leave();
    });
    connect(meetingJoinButton_, &QPushButton::clicked, this, [this, selectMeetingDevices] {
        using State = zjdesk::SessionCoordinator::State;
        if (session_.state() == State::Idle || session_.state() == State::Failed)
        {
            if (!selectMeetingDevices()) return;
            session_.join(QUrl(meetingServerEdit_->text().trimmed()), meetingRoomEdit_->text().trimmed(),
                meetingNameEdit_->text().trimmed(), zjdesk::SessionCoordinator::RoomType::Meeting);
        }
        else
            session_.leave();
    });
    connect(nameEdit_, &QLineEdit::textChanged, findChild<QLabel *>("localName"), &QLabel::setText);
    connect(&session_, &zjdesk::SessionCoordinator::stateChanged, this, &MainWindow::refreshSession);
    connect(&session_, &zjdesk::SessionCoordinator::membersChanged, this, &MainWindow::refreshSession);
    connect(&session_, &zjdesk::SessionCoordinator::trafficChanged, this, [this] {
        trafficLabel_->setText(QStringLiteral("发送 %1 · 接收 %2").arg(session_.sentMessages()).arg(session_.receivedMessages()));
    });
    connect(session_.events(), &zjdesk::EventLog::appended, logView_, &QPlainTextEdit::appendPlainText);
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportEvents);
    connect(&session_, &zjdesk::SessionCoordinator::devicesChanged, this, &MainWindow::refreshDevices);
    connect(&session_, &zjdesk::SessionCoordinator::cameraPermissionRequired, this, [this] {
        if (findChild<QMessageBox *>("cameraPermissionDialog")) return;
        auto *dialog = new QMessageBox(QMessageBox::Information, QStringLiteral("摄像头授权"),
            QStringLiteral("使用视频会议需要摄像头权限。\n\n"
                           "请在 Windows 设置中允许摄像头访问，并允许桌面应用使用摄像头。"
                           "\n\n"
                           "授权后返回本程序，摄像头列表会自动刷新。"), QMessageBox::NoButton, this);
        dialog->setObjectName("cameraPermissionDialog");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        auto *authorize = dialog->addButton(QStringLiteral("打开 Windows 摄像头设置"), QMessageBox::AcceptRole);
        dialog->addButton(QStringLiteral("暂不使用"), QMessageBox::RejectRole);
        connect(dialog, &QMessageBox::buttonClicked, this, [this, authorize](QAbstractButton *button) {
            if (button != authorize) return;
            awaitingCameraPermission_ = true;
            session_.openCameraPrivacySettings();
        });
        dialog->open();
    });
    connect(&session_, &zjdesk::SessionCoordinator::cameraPrivacySettingsFinished, this,
        [this](bool opened, const QString &detail) {
            if (opened)
            {
                session_.refreshDevices();
                return;
            }
            const auto message = QStringLiteral("无法打开 Windows 摄像头设置。\n%1\n\n"
                "请按 Win+R，输入 ms-settings:privacy-webcam 后回车；"
                "也可以前往 Windows 设置 → 隐私和安全性 → 摄像头开启权限。").arg(detail);
            deviceLabel_->setText(message);
            auto *error = new QMessageBox(QMessageBox::Warning, QStringLiteral("打开设置失败"),
                message, QMessageBox::Ok, this);
            error->setObjectName("cameraSettingsError");
            error->setAttribute(Qt::WA_DeleteOnClose);
            error->open();
        });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && awaitingCameraPermission_)
        {
            session_.refreshDevices();
            awaitingCameraPermission_ = session_.cameraPermissionStatus() != zjdesk::CameraPermission::Allowed;
        }
    });
    connect(&session_, &zjdesk::SessionCoordinator::audioChanged, this, &MainWindow::refreshDeviceControls);
    connect(&session_, &zjdesk::SessionCoordinator::shareFrame, this, [this](const QImage &image) {
        (session_.roomType() == zjdesk::SessionCoordinator::RoomType::Meeting ? meetingShareVideo_ : video_)->setFrame(image);
    });
    connect(&session_, &zjdesk::SessionCoordinator::remoteCursorChanged, video_, &VideoSurface::setRemoteCursor);
    connect(&session_, &zjdesk::SessionCoordinator::shareChanged, this, &MainWindow::refreshShare);
    connect(&session_, &zjdesk::SessionCoordinator::remoteChanged, this, &MainWindow::refreshControl);
    connect(video_, &VideoSurface::remoteInput, &session_, &zjdesk::SessionCoordinator::sendRemoteInput);
    connect(controlButton_, &QPushButton::clicked, &session_, &zjdesk::SessionCoordinator::requestControl);
    connect(allowControlButton_, &QPushButton::clicked, &session_, &zjdesk::SessionCoordinator::grantControl);
    connect(stopControlButton_, &QPushButton::clicked, &session_, &zjdesk::SessionCoordinator::stopControl);
    connect(shareButton_, &QPushButton::clicked, this, [this] {
        if (session_.screenActive())
            session_.stopScreenShare();
        else
            session_.startScreenShare();
    });
    connect(audioButton_, &QPushButton::clicked, this, [this] {
        using Preview = zjdesk::SessionCoordinator::PreviewState;
        if (session_.audioState() == Preview::Stopped || session_.audioState() == Preview::Failed)
            session_.startAudioTest();
        else
            session_.stopAudioTest();
    });
    connect(refreshDevicesButton_, &QPushButton::clicked, &session_, &zjdesk::SessionCoordinator::requestCameraPermission);
    auto *statsTimer = new QTimer(this);
    connect(statsTimer, &QTimer::timeout, this, [this] {
        auto *surface = session_.roomType() == zjdesk::SessionCoordinator::RoomType::Meeting ? meetingShareVideo_ : video_;
        session_.reportSharePresentation(surface->presentedFrames(),
            surface->isVisible() && !isMinimized() && isActiveWindow(), surface->takePresentationGapMaxMs(),
            surface->presentationStatistics());
    });
    statsTimer->start(1000);
    session_.events()->append("application", QStringLiteral("工作台已启动"));
    refreshSession();
    refreshDeviceControls();
    refreshMeeting();
    QTimer::singleShot(0, &session_, &zjdesk::SessionCoordinator::refreshDevices);
}

MainWindow::~MainWindow()
{
    // Session teardown emits frames and state changes after later-declared roster containers die.
    // Stop those UI callbacks while all MainWindow members are still alive.
    QObject::disconnect(&session_, nullptr, nullptr, nullptr);
}

void MainWindow::requestCameraPermission()
{
    session_.requestCameraPermission();
}

void MainWindow::refreshDevices()
{
    if (session_.cameraPermissionStatus() == zjdesk::CameraPermission::Allowed)
        awaitingCameraPermission_ = false;
    const auto hint = cameraHintReason(session_);
    int cameras = 0;
    for (const auto &device : session_.devices())
        if (device.kind == QLatin1String("Video/Source")) ++cameras;
    for (const auto &entry : {qMakePair("conferenceCamera", "Video/Source"),
                             qMakePair("conferenceMicrophone", "Audio/Source"),
                             qMakePair("conferenceSpeaker", "Audio/Sink")})
    {
        auto *combo = findChild<QComboBox *>(entry.first);
        const bool camera = QLatin1String(entry.first) == QLatin1String("conferenceCamera");
        if (camera && cameras == 0)
        {
            showNoDeviceHint(combo, hint);
            continue;
        }
        const auto selectedDevice = combo->currentData();
        combo->clear();
        clearNoDeviceHint(combo);
        combo->addItem(QStringLiteral("默认设备"), QString());
        for (const auto &device : session_.devices())
            if (device.kind == entry.second) combo->addItem(device.name, device.name);
        const auto deviceIndex = combo->findData(selectedDevice);
        combo->setCurrentIndex(deviceIndex < 0 ? 0 : deviceIndex);
    }
    QStringList descriptions;
    for (const auto &device : session_.devices())
    {
        const auto description = device.kind + " · " + device.name
            + (device.kind == "Video/Source" ? " · " + device.maximumMode.description() : "");
        if (!descriptions.contains(description))
            descriptions.append(description);
    }
    if (session_.cameraPermissionStatus() == zjdesk::CameraPermission::Denied)
        descriptions.prepend(QStringLiteral("摄像头尚未授权，点击“刷新设备”打开授权引导；授权后返回程序即可使用"));
    else if (session_.cameraPermissionStatus() == zjdesk::CameraPermission::Unknown)
        descriptions.prepend(QStringLiteral("无法预先确认摄像头权限；若视频会议采集失败，请检查系统权限或设备占用"));
    deviceLabel_->setText(!session_.mediaEnvironment().ready ? session_.mediaEnvironment().description
        : descriptions.isEmpty() ? QStringLiteral("没有可用设备；连接摄像头后点击“刷新设备”") : descriptions.join('\n'));
    refreshDeviceControls();
}

void MainWindow::refreshDeviceControls()
{
    using Preview = zjdesk::SessionCoordinator::PreviewState;
    const bool ready = session_.mediaEnvironment().ready;
    refreshDevicesButton_->setEnabled(ready);
    const auto audio = session_.audioState();
    audioButton_->setEnabled(ready && audio != Preview::Stopping && !session_.conferenceDevicesBusy());
    audioButton_->setText(audio == Preview::Stopped || audio == Preview::Failed
        ? QStringLiteral("测试声音") : QStringLiteral("停止声音"));
    audioLabel_->setText(session_.audioDetail());
    refreshShare();
    refreshControl();
    refreshMeetingParticipants();
}

void MainWindow::refreshMeetingParticipants()
{
    using State = zjdesk::SessionCoordinator::State;
    const bool inMeeting = session_.roomType() == zjdesk::SessionCoordinator::RoomType::Meeting
        && (session_.state() == State::WaitingForPeer || session_.state() == State::PeerAvailable);
    const int count = inMeeting ? session_.memberCount() : 0;
    const bool hasPeer = count > 1;
    const QString localName = meetingNameEdit_->text().trimmed().isEmpty()
        ? QStringLiteral("我") : meetingNameEdit_->text().trimmed();
    const QString remoteName = session_.peerName().isEmpty() ? QStringLiteral("参会成员") : session_.peerName();
    const bool hidden = session_.conferenceCameraHidden();
    const bool remoteHidden = session_.conferenceRemoteCameraHidden();
    QStringList members;
    if (count > 0) members.append(localName + QStringLiteral("（我）"));
    if (hasPeer) members.append(remoteName);
    setMeetingRoster(members);
    meetingRemoteVideo_->setVisible(hasPeer);
    findChild<QLabel *>("meetingWaiting")->setVisible(!hasPeer);
    meetingLocalVideo_->setParticipant(localName + QStringLiteral("（我）"),
        hidden ? QStringLiteral("画面已隐藏") : QStringLiteral("摄像头未开启"));
    meetingLocalVideo_->setFrameCaption(localName + QStringLiteral("（我）"));
    meetingLocalVideo_->setFrameHidden(hidden);
    meetingRemoteVideo_->setParticipant(remoteName, remoteHidden
        ? QStringLiteral("画面已隐藏") : QStringLiteral("等待摄像头画面"));
    meetingRemoteVideo_->setFrameCaption(remoteName);
    meetingRemoteVideo_->setFrameHidden(remoteHidden);
    // Keep live video above avatar tiles without reparenting OpenGL widgets.
    auto *first = hasPeer && !remoteHidden && meetingRemoteVideo_->hasFrame()
        && (hidden || !meetingLocalVideo_->hasFrame()) ? meetingRemoteVideo_ : meetingLocalVideo_;
    if (meetingParticipantLayout_->itemAt(1)->widget() != first)
    {
        meetingParticipantLayout_->removeWidget(first);
        meetingParticipantLayout_->insertWidget(1, first, 1);
    }
}

void MainWindow::setMeetingRoster(const QStringList &names)
{
    meetingMemberCount_->setText(names.isEmpty() ? QStringLiteral("尚未入会")
        : QStringLiteral("%1 人参与").arg(names.size()));
    const bool foldable = names.size() > 4;
    meetingRosterStack_->setVisible(foldable);
    if (foldable)
    {
        meetingRosterStack_->setIcon(memberStackIcon(names));
        const auto description = QStringLiteral("查看全部成员（%1 人）").arg(names.size());
        meetingRosterStack_->setAccessibleName(description);
        meetingRosterStack_->setToolTip(description);
    }
    else meetingRosterPopup_->hide();
    meetingRosterScroll_->setVisible(!names.isEmpty() && !foldable);
    meetingRosterTitle_->setText(QStringLiteral("会议成员 · %1 人").arg(names.size()));
    while (meetingMemberWidgets_.size() < names.size())
    {
        auto *badge = memberBadge("meetingMember");
        meetingMemberBadges_->insertWidget(meetingMemberWidgets_.size(), badge);
        meetingMemberWidgets_.append(badge);
    }
    for (int i = 0; i < meetingMemberWidgets_.size(); ++i)
    {
        auto *badge = meetingMemberWidgets_.at(i);
        badge->setVisible(i < names.size());
        if (i < names.size()) updateMemberBadge(badge, names.at(i));
    }
    while (meetingRosterPopupMembers_.size() < names.size())
    {
        const int index = meetingRosterPopupMembers_.size();
        auto *badge = memberBadge("meetingPopupMember");
        meetingRosterGrid_->addWidget(badge, index / 4, index % 4);
        meetingRosterPopupMembers_.append(badge);
    }
    for (int i = 0; i < meetingRosterPopupMembers_.size(); ++i)
    {
        auto *badge = meetingRosterPopupMembers_.at(i);
        badge->setVisible(i < names.size());
        if (i < names.size()) updateMemberBadge(badge, names.at(i));
    }
}

void MainWindow::refreshShare()
{
    using Preview = zjdesk::SessionCoordinator::PreviewState;
    shareButton_->setEnabled(session_.roomType() == zjdesk::SessionCoordinator::RoomType::Meeting
        && session_.mediaEnvironment().ready
        && session_.state() == zjdesk::SessionCoordinator::State::PeerAvailable
        && !session_.receivingScreen() && session_.shareState() != Preview::Stopping);
    shareButton_->setText(session_.shareState() == Preview::Stopping ? QStringLiteral("正在停止")
        : session_.receivingScreen() ? QStringLiteral("对方正在共享")
        : session_.screenActive() ? QStringLiteral("停止共享") : QStringLiteral("共享主屏幕"));
    shareLabel_->setText(session_.shareDetail());
    statusDetail_->setText(session_.screenActive()
        ? session_.receivingScreen() ? QStringLiteral("正在接收对方屏幕") : QStringLiteral("正在共享主屏幕")
        : session_.detail());
    video_->setFrameCaption(expandButton_->isChecked() ? QString() : session_.screenActive()
        ? session_.receivingScreen() ? QStringLiteral("对方的屏幕") : QStringLiteral("主屏幕 · 正在共享给对方")
        : QStringLiteral("等待远程控制画面"));
    refreshControl();
}

void MainWindow::expandVideo(bool expanded)
{
    session_.sendRemoteInput({{"kind", "release"}});
    for (auto *widget : normalWidgets_)
        widget->setVisible(!expanded);
    contentLayout_->setContentsMargins(expanded ? QMargins() : QMargins(30, 26, 30, 22));
    contentLayout_->setSpacing(expanded ? 0 : 18);
    workspaceLayout_->setSpacing(expanded ? 0 : 14);
    if (expanded)
    {
        normalWindowState_ = windowState();
        showFullScreen();
    }
    else
    {
        setWindowState(normalWindowState_);
    }
    refreshShare();
    if (session_.remoteState() == zjdesk::SessionCoordinator::RemoteState::Controlling)
        video_->setFocus();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress && expandButton_->isChecked()
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)
    {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && widget->window() == this)
        {
            expandButton_->setChecked(false);
            event->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::refreshControl()
{
    using Remote = zjdesk::SessionCoordinator::RemoteState;
    const auto state = session_.remoteState();
    controlButton_->setEnabled(session_.canRequestControl());
    allowControlButton_->setVisible(!expandButton_->isChecked() && state == Remote::Pending);
    stopControlButton_->setEnabled(state != Remote::Idle);
    stopControlButton_->setText(state == Remote::Pending ? QStringLiteral("拒绝控制") : QStringLiteral("停止控制"));
    controlLabel_->setText(session_.remoteDetail());
    video_->setControlEnabled(state == Remote::Controlling);
}

void MainWindow::refreshSession()
{
    using State = zjdesk::SessionCoordinator::State;
    const auto state = session_.state();
    const bool idle = state == State::Idle || state == State::Failed;
    serverEdit_->setEnabled(idle);
    roomEdit_->setEnabled(idle);
    nameEdit_->setEnabled(idle);
    meetingServerEdit_->setEnabled(idle);
    meetingRoomEdit_->setEnabled(idle);
    meetingNameEdit_->setEnabled(idle);
    const bool meeting = session_.roomType() == zjdesk::SessionCoordinator::RoomType::Meeting;
    joinButton_->setEnabled(idle || !meeting);
    meetingJoinButton_->setEnabled(idle || meeting);
    joinButton_->setText(idle || meeting ? QStringLiteral("加入房间")
        : state == State::Connecting || state == State::Joining ? QStringLiteral("取消连接") : QStringLiteral("离开房间"));
    meetingJoinButton_->setText(idle || !meeting ? QStringLiteral("加入会议")
        : state == State::Connecting || state == State::Joining ? QStringLiteral("取消连接") : QStringLiteral("离开会议"));
    const QString labels[] = {QStringLiteral("未加入房间"), QStringLiteral("正在连接"), QStringLiteral("正在入会"),
        QStringLiteral("等待成员"), QStringLiteral("双方已入会"), QStringLiteral("连接失败")};
    const auto label = (idle ? QString() : meeting ? QStringLiteral("会议 · ") : QStringLiteral("协作 · "))
        + labels[static_cast<int>(state)];
    if (statusLabel_->text() != label)
    {
        statusLabel_->setText(label);
        statusAnimation_->stop();
        statusAnimation_->start();
    }
    statusLabel_->setProperty("state", state == State::Failed ? "error"
        : state == State::PeerAvailable ? "connected" : state == State::Idle ? "idle" : "waiting");
    statusLabel_->style()->unpolish(statusLabel_);
    statusLabel_->style()->polish(statusLabel_);
    statusDetail_->setText(session_.detail());
    memberLabel_->setText(QStringLiteral("%1 / 2 人").arg(session_.memberCount()));
    peerLabel_->setText(session_.peerName().isEmpty() ? QStringLiteral("尚未加入") : session_.peerName());
    refreshMeetingParticipants();
    refreshShare();
    if (state == State::PeerAvailable)
        video_->setParticipant(session_.peerName(), QStringLiteral("对方已就绪，可请求远程控制"));
    else
        video_->setParticipant(QStringLiteral("等待另一位成员"),
                              state == State::Failed ? session_.detail() : QStringLiteral("双方加入同一房间后，这里将显示远端画面"));
}

void MainWindow::exportEvents()
{
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出会话事件"),
        "zjdesk-events.jsonl", "JSON Lines (*.jsonl)");
    if (path.isEmpty())
        return;
    QString error;
    if (!session_.events()->exportJsonLines(path, &error))
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
    else
        session_.events()->append("report", QStringLiteral("会话事件已导出"));
}
