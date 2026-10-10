#include "main_window.h"
#include "platform/platform_info.h"
#include "video_surface.h"
#include "zjdesk/build_info.h"
#include <QFileDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QStyle>
#include <QStackedWidget>
#include <QScrollArea>
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
} // namespace

MainWindow::MainWindow(const QString &serverUrl, const QString &roomId, const QString &name,
                       QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("ZJdesk · 跨平台协作工作台"));
    resize(1200, 780);
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
    nav->setContentsMargins(20, 30, 20, 24);
    nav->setSpacing(10);
    nav->addWidget(text("ZJdesk", "brand"));
    nav->addWidget(text(QStringLiteral("让协作跨越系统"), "sidebarSubtitle"));
    nav->addSpacing(30);
    auto *workspaceButton = new QPushButton(QStringLiteral("协作空间"));
    auto *diagnosticsButton = new QPushButton(QStringLiteral("运行诊断"));
    for (auto *button : {workspaceButton, diagnosticsButton})
    {
        button->setObjectName("navButton");
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setMinimumHeight(44);
        nav->addWidget(button);
    }
    workspaceButton->setChecked(true);
    nav->addStretch();
    nav->addWidget(text("WINDOWS / LINUX", "sidebarSubtitle"));
    nav->addWidget(text(QStringLiteral("屏幕共享  %1").arg(zjdesk::build::Version), "sidebarSubtitle"));
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
    roomEdit_->setPlaceholderText(QStringLiteral("输入房间号"));
    nameEdit_->setPlaceholderText(QStringLiteral("输入你的昵称"));
    roomEdit_->setMaxLength(32);
    nameEdit_->setMaxLength(40);
    const QList<QPair<QString, QLineEdit *>> fields = {
        {QStringLiteral("信令服务"), serverEdit_}, {QStringLiteral("房间号"), roomEdit_},
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
    remoteLayout_ = remoteLayout;
    remoteLayout->setContentsMargins(16, 8, 12, 8);
    remoteLayout->addWidget(text(QStringLiteral("共享画面"), "stageTitle"));
    remoteLayout->addStretch();
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
    peerLabel_ = text(QStringLiteral("尚未加入"));
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

    auto *previewCard = card();
    auto *previewColumn = new QVBoxLayout(previewCard);
    previewColumn->setContentsMargins(18, 12, 18, 12);
    previewColumn->setSpacing(8);
    auto *previewLayout = new QHBoxLayout;
    previewLayout->setSpacing(10);
    previewLayout->addWidget(text(QStringLiteral("本地预览"), "sectionTitle"));
    sourceCombo_ = new QComboBox;
    sourceCombo_->setObjectName("previewSource");
    sourceCombo_->setMinimumWidth(165);
    sourceCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    sourceCombo_->setMinimumContentsLength(12);
    sourceCombo_->setAccessibleName(QStringLiteral("预览视频源"));
    sourceCombo_->addItem(QStringLiteral("视频测试源"), "test");
    previewLayout->addWidget(sourceCombo_, 1);
    previewButton_ = new QPushButton(QStringLiteral("开始预览"));
    previewButton_->setObjectName("previewButton");
    previewLayout->addWidget(previewButton_);
    audioButton_ = new QPushButton(QStringLiteral("测试声音"));
    audioButton_->setObjectName("audioTestButton");
    previewLayout->addWidget(audioButton_);
    refreshDevicesButton_ = new QPushButton(QStringLiteral("刷新设备"));
    previewLayout->addWidget(refreshDevicesButton_);
    previewColumn->addLayout(previewLayout);
    workspaceLayout->addWidget(previewCard);
    previewLabel_ = text(session_.previewDetail(), "muted");
    previewLabel_->setObjectName("previewStatus");
    previewLabel_->setWordWrap(true);
    previewColumn->addWidget(previewLabel_);

    sessionLayout->addWidget(text(QStringLiteral("屏幕共享"), "sectionTitle"));
    shareButton_ = new QPushButton(QStringLiteral("共享主屏幕"));
    shareButton_->setObjectName("shareButton");
    shareButton_->setToolTip(QStringLiteral("将整个主屏幕及鼠标指针发送给房间中的另一位成员"));
    sessionLayout->addWidget(shareButton_);
    shareLabel_ = text(session_.shareDetail(), "muted");
    shareLabel_->setObjectName("shareStatus");
    shareLabel_->setWordWrap(true);
    sessionLayout->addWidget(shareLabel_);
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
    auto *notice = text(QStringLiteral("远程控制需对方本地允许。\n摄像头预览仅在本机显示。"), "muted");
    notice->setWordWrap(true);
    sessionLayout->addWidget(notice);
    pages->addWidget(workspace);

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
    previewStatsLabel_ = text(QStringLiteral("采集 0 帧 · 丢弃 0 帧 · 待显示 0 帧"));
    environmentLayout->addRow(QStringLiteral("本地预览帧"), previewStatsLabel_);
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
        previewCard, footerWidget};
    connect(expandButton_, &QPushButton::toggled, this, &MainWindow::expandVideo);
    qApp->installEventFilter(this);

    connect(workspaceButton, &QPushButton::clicked, pages, [pages, pageTitle, pageSubtitle] {
        pages->setCurrentIndex(0);
        pageTitle->setText(QStringLiteral("协作工作台"));
        pageSubtitle->setText(QStringLiteral("连接彼此的桌面，专注眼前的协作。"));
    });
    connect(diagnosticsButton, &QPushButton::clicked, pages, [pages, pageTitle, pageSubtitle] {
        pages->setCurrentIndex(1);
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
    connect(nameEdit_, &QLineEdit::textChanged, findChild<QLabel *>("localName"), &QLabel::setText);
    connect(&session_, &zjdesk::SessionCoordinator::stateChanged, this, &MainWindow::refreshSession);
    connect(&session_, &zjdesk::SessionCoordinator::membersChanged, this, &MainWindow::refreshSession);
    connect(&session_, &zjdesk::SessionCoordinator::trafficChanged, this, [this] {
        trafficLabel_->setText(QStringLiteral("发送 %1 · 接收 %2").arg(session_.sentMessages()).arg(session_.receivedMessages()));
    });
    connect(session_.events(), &zjdesk::EventLog::appended, logView_, &QPlainTextEdit::appendPlainText);
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportEvents);
    connect(&session_, &zjdesk::SessionCoordinator::devicesChanged, this, &MainWindow::refreshDevices);
    connect(&session_, &zjdesk::SessionCoordinator::previewChanged, this, &MainWindow::refreshPreview);
    connect(&session_, &zjdesk::SessionCoordinator::audioChanged, this, &MainWindow::refreshPreview);
    connect(&session_, &zjdesk::SessionCoordinator::previewFrame, this, [this](const QImage &image) {
        if (!session_.screenActive())
            video_->setFrame(image);
    });
    connect(&session_, &zjdesk::SessionCoordinator::shareFrame, video_, &VideoSurface::setFrame);
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
    connect(previewButton_, &QPushButton::clicked, this, [this] {
        using Preview = zjdesk::SessionCoordinator::PreviewState;
        if (session_.previewState() == Preview::Stopped || session_.previewState() == Preview::Failed)
            session_.startPreview(sourceCombo_->currentData().toString());
        else
            session_.stopPreview();
    });
    connect(audioButton_, &QPushButton::clicked, this, [this] {
        using Preview = zjdesk::SessionCoordinator::PreviewState;
        if (session_.audioState() == Preview::Stopped || session_.audioState() == Preview::Failed)
            session_.startAudioTest();
        else
            session_.stopAudioTest();
    });
    connect(refreshDevicesButton_, &QPushButton::clicked, &session_, &zjdesk::SessionCoordinator::refreshDevices);
    auto *statsTimer = new QTimer(this);
    connect(statsTimer, &QTimer::timeout, this, [this] {
        session_.reportSharePresentation(video_->presentedFrames(),
            video_->isVisible() && !isMinimized() && isActiveWindow(), video_->takePresentationGapMaxMs());
        const auto stats = session_.previewStats();
        previewStatsLabel_->setText(QStringLiteral("采集 %1 帧 · 丢弃 %2 帧 · 待显示 %3 帧")
            .arg(stats.capturedFrames).arg(stats.droppedFrames).arg(stats.pendingFrames));
    });
    statsTimer->start(1000);
    session_.events()->append("application", QStringLiteral("工作台已启动"));
    refreshSession();
    refreshPreview();
    QTimer::singleShot(0, &session_, &zjdesk::SessionCoordinator::refreshDevices);
}

void MainWindow::refreshDevices()
{
    const auto selected = sourceCombo_->currentData().toString();
    sourceCombo_->clear();
    sourceCombo_->addItem(QStringLiteral("视频测试源"), "test");
    QStringList descriptions;
    for (const auto &device : session_.devices())
    {
        if (device.kind.contains("Video/Source"))
            sourceCombo_->addItem(device.name + " · " + device.maximumMode.description(), device.id);
        const auto description = device.kind + " · " + device.name
            + (device.kind == "Video/Source" ? " · " + device.maximumMode.description() : "");
        if (!descriptions.contains(description))
            descriptions.append(description);
    }
    const auto index = sourceCombo_->findData(selected);
    sourceCombo_->setCurrentIndex(index < 0 ? 0 : index);
    deviceLabel_->setText(!session_.mediaEnvironment().ready ? session_.mediaEnvironment().description
        : descriptions.isEmpty() ? QStringLiteral("没有可用设备；测试源仍可使用") : descriptions.join('\n'));
}

void MainWindow::refreshPreview()
{
    using Preview = zjdesk::SessionCoordinator::PreviewState;
    const auto state = session_.previewState();
    const bool idle = state == Preview::Stopped || state == Preview::Failed;
    const bool ready = session_.mediaEnvironment().ready;
    previewButton_->setEnabled(ready && state != Preview::Stopping);
    previewButton_->setText(idle ? QStringLiteral("开始预览") : QStringLiteral("停止预览"));
    sourceCombo_->setEnabled(ready && idle);
    refreshDevicesButton_->setEnabled(ready && idle);
    previewLabel_->setText(ready ? session_.previewDetail() : session_.mediaEnvironment().description);
    if (idle && !session_.screenActive())
        video_->setFrame({});
    const auto audio = session_.audioState();
    audioButton_->setEnabled(ready && audio != Preview::Stopping);
    audioButton_->setText(audio == Preview::Stopped || audio == Preview::Failed
        ? QStringLiteral("测试声音") : QStringLiteral("停止声音"));
    audioLabel_->setText(session_.audioDetail());
    refreshShare();
    refreshControl();
}

void MainWindow::refreshShare()
{
    using Preview = zjdesk::SessionCoordinator::PreviewState;
    shareButton_->setEnabled(session_.mediaEnvironment().ready
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
        : QStringLiteral("本地预览 · 尚未发送给对方"));
    refreshControl();
}

void MainWindow::expandVideo(bool expanded)
{
    session_.sendRemoteInput({{"kind", "release"}});
    for (auto *widget : normalWidgets_)
        widget->setVisible(!expanded);
    expandButton_->setText(expanded ? QStringLiteral("退出放大 (Esc)") : QStringLiteral("放大画面"));
    contentLayout_->setContentsMargins(expanded ? QMargins() : QMargins(30, 26, 30, 22));
    contentLayout_->setSpacing(expanded ? 0 : 18);
    workspaceLayout_->setSpacing(expanded ? 0 : 14);
    if (expanded)
    {
        normalWindowState_ = windowState();
        remoteLayout_->removeWidget(expandButton_);
        expandButton_->setParent(video_);
        expandButton_->adjustSize();
        expandButton_->show();
        expandButton_->raise();
        showFullScreen();
    }
    else
    {
        expandButton_->setParent(remoteLayout_->parentWidget());
        remoteLayout_->addWidget(expandButton_);
        expandButton_->show();
        setWindowState(normalWindowState_);
    }
    refreshShare();
    if (session_.remoteState() == zjdesk::SessionCoordinator::RemoteState::Controlling)
        video_->setFocus();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == video_ && event->type() == QEvent::Resize && expandButton_->isChecked())
        expandButton_->move(qMax(0, video_->width() - expandButton_->width() - 12), 12);
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
    joinButton_->setText(idle ? QStringLiteral("加入房间")
        : state == State::Connecting || state == State::Joining ? QStringLiteral("取消连接") : QStringLiteral("离开房间"));
    const QString labels[] = {QStringLiteral("未加入房间"), QStringLiteral("正在连接"), QStringLiteral("正在入会"),
        QStringLiteral("等待成员"), QStringLiteral("双方已入会"), QStringLiteral("连接失败")};
    const auto label = labels[static_cast<int>(state)];
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
    refreshShare();
    if (state == State::PeerAvailable)
        video_->setParticipant(session_.peerName(), QStringLiteral("对方已就绪，可共享屏幕或请求远程控制"));
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
