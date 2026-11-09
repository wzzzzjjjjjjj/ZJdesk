#include "main_window.h"
#include "platform/platform_info.h"
#include "video_surface.h"
#include "annotation_layer.h"
#include "zjdesk/build_info.h"
#include <QClipboard>
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
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QTimer>
#include <QApplication>
#include <QAction>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QUuid>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

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

// 单行省略的单行文本：昵称过长时以“…”收尾，而不是撑高卡片或硬裁到半个字。
class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel(const QString &value, QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setTextFormat(Qt::PlainText);
        setText(value);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumHeight(fontMetrics().height() + 2);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter,
            fontMetrics().elidedText(QLabel::text(), Qt::ElideRight, contentsRect().width()));
    }
};

QLabel *elided(const QString &value, const char *name = nullptr)
{
    auto *label = new ElidedLabel(value);
    if (name)
        label->setObjectName(QString::fromLatin1(name));
    return label;
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

QIcon remotePeerIcon(const QString &name)
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#d9efeb"));
    painter.drawEllipse(pixmap.rect());
    auto font = painter.font();
    font.setPixelSize(28);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor("#237c77"));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, name.trimmed().left(1).toUpper());
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
    Q_UNUSED(roomId) // Legacy launch argument; remote connections now use peer codes.
    setWindowTitle(QStringLiteral("ZJdesk · 跨平台协作工作台"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/zjdesk.png")));
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
    auto *brandMark = text({}, "brandMark");
    brandMark->setAlignment(Qt::AlignCenter);
    brandMark->setFixedSize(34, 34);
    brandMark->setPixmap(windowIcon().pixmap(QSize(34, 34), devicePixelRatioF()));
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
    // 状态与当前账号合成一组：长昵称只在自己的输入框里省略，不再把页头挤到窗口边缘。
    auto *headerActions = new QWidget;
    headerActions->setObjectName("headerActions");
    auto *actionsLayout = new QHBoxLayout(headerActions);
    actionsLayout->setContentsMargins(12, 0, 0, 0);
    actionsLayout->setSpacing(18);
    statusLabel_ = text(QStringLiteral("未连接"), "statusBadge");
    auto *statusEffect = new QGraphicsOpacityEffect(statusLabel_);
    statusEffect->setOpacity(1.0);
    statusLabel_->setGraphicsEffect(statusEffect);
    statusAnimation_ = new QPropertyAnimation(statusEffect, "opacity", this);
    statusAnimation_->setDuration(180);
    statusAnimation_->setStartValue(0.55);
    statusAnimation_->setEndValue(1.0);
    actionsLayout->addWidget(statusLabel_, 0, Qt::AlignVCenter);
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
    meetingNameEdit_->setFixedWidth(132);
    meetingNameEdit_->setPlaceholderText(QStringLiteral("你的昵称"));
    meetingNameEdit_->setAccessibleName(QStringLiteral("你的昵称"));
    profileLayout->addWidget(profileAvatar);
    profileLayout->addWidget(meetingNameEdit_);
    actionsLayout->addWidget(profile, 0, Qt::AlignVCenter);
    header->addWidget(headerActions, 0, Qt::AlignVCenter);
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
    // 本机校验码：小标题压低层级、数字放大、旁边一次点击即可复制。
    auto *identityHeader = new QWidget;
    identityHeader->setObjectName("identityHeader");
    auto *identityLayout = new QHBoxLayout(identityHeader);
    identityLayout->setContentsMargins(2, 0, 2, 0);
    identityLayout->setSpacing(14);
    auto *codeColumn = new QVBoxLayout;
    codeColumn->setContentsMargins(0, 0, 0, 0);
    codeColumn->setSpacing(1);
    codeColumn->addWidget(text(QStringLiteral("本机校验码"), "codeCaption"));
    remoteCodeLabel_ = text(QStringLiteral("未在线"), "localRemoteCode");
    remoteCodeLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    codeColumn->addWidget(remoteCodeLabel_);
    identityLayout->addLayout(codeColumn);
    identityLayout->addStretch();
    copyCodeButton_ = new QPushButton(QStringLiteral("复制"));
    copyCodeButton_->setObjectName("copyCodeButton");
    copyCodeButton_->setMinimumWidth(82);
    copyCodeButton_->setToolTip(QStringLiteral("复制本机校验码到剪贴板"));
    copyCodeButton_->setEnabled(false);
    identityLayout->addWidget(copyCodeButton_, 0, Qt::AlignVCenter);
    workspaceLayout->addWidget(identityHeader);
    connect(copyCodeButton_, &QPushButton::clicked, this, [this] {
        const auto code = session_.remoteCode();
        if (code.isEmpty())
            return;
        QApplication::clipboard()->setText(code);
        auto *button = copyCodeButton_;
        button->setText(QStringLiteral("已复制"));
        QTimer::singleShot(1500, button, [button] { button->setText(QStringLiteral("复制")); });
    });
    auto *connectionLayout = new QHBoxLayout(connectionCard);
    connectionLayout_ = connectionLayout;
    connectionLayout->setContentsMargins(20, 16, 20, 16);
    connectionLayout->setSpacing(14);
    // 服务地址由对端 IP 验证自动设置，界面上不再单独提供输入框；
    // 控件保留为隐藏状态，用于会话启动、最近连接历史与会议页同步。
    serverEdit_ = new QLineEdit(serverUrl, connectionCard);
    serverEdit_->setObjectName("serverAddress");
    serverEdit_->setReadOnly(true);
    serverEdit_->setPlaceholderText(QStringLiteral("验证对端 IP 后自动设置"));
    serverEdit_->setToolTip(QStringLiteral("服务地址由对端 IP 验证自动设置；修改对端 IP 后请重新验证。"));
    serverEdit_->setVisible(false);
    roomEdit_ = new QLineEdit;
    roomEdit_->setObjectName("remoteCode");
    roomEdit_->setPlaceholderText(QStringLiteral("输入对方 9 位校验码"));
    roomEdit_->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{9}"), roomEdit_));
    roomEdit_->setToolTip(QStringLiteral("两端连接同一服务，输入对方显示的 9 位数字"));
    roomEdit_->setMaxLength(9);
    remoteHistory_ = new QComboBox;
    remoteHistory_->setObjectName("recentRemoteCodes");
    remoteHistory_->setEditable(true);
    remoteHistory_->setLineEdit(roomEdit_);
    remoteHistory_->setCompleter(nullptr);
    remoteHistory_->setInsertPolicy(QComboBox::NoInsert);
    remoteHistory_->setIconSize(QSize(28, 28));
    remoteHistory_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    remoteHistory_->setAccessibleName(QStringLiteral("对方校验码与最近连接"));
    remoteHistory_->setToolTip(QStringLiteral("输入对方校验码，或选择最近连接的电脑后点击连接"));
    auto *recentAction = roomEdit_->addAction(style()->standardIcon(QStyle::SP_ArrowDown), QLineEdit::TrailingPosition);
    recentAction->setToolTip(QStringLiteral("最近连接"));
    connect(recentAction, &QAction::triggered, remoteHistory_, &QComboBox::showPopup);
    refreshRemoteHistory();
    connect(remoteHistory_, &QComboBox::activated, this, [this](int index) {
        roomEdit_->setText(remoteHistory_->itemData(index).toString());
    });
    // 对方校验码列：启动验证时会被对端 IP 面板替换。
    networkColumn_ = new QWidget(connectionCard);
    networkLayout_ = new QVBoxLayout(networkColumn_);
    networkLayout_->setContentsMargins(0, 0, 0, 0);
    networkLayout_->setSpacing(6);
    auto *remoteCodeFieldLabel = text(QStringLiteral("对方校验码"), "fieldLabel");
    remoteCodeFieldLabel->setObjectName("remoteCodeFieldLabel");
    remoteCodeFieldLabel->setBuddy(roomEdit_);
    roomEdit_->setAccessibleName(QStringLiteral("对方校验码"));
    networkLayout_->addWidget(remoteCodeFieldLabel);
    networkLayout_->addWidget(remoteHistory_);
    connectionLayout->addWidget(networkColumn_, 1);
    joinButton_ = new QPushButton(QStringLiteral("连接服务"));
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
    memberHeader->addWidget(text(QStringLiteral("连接设备"), "sectionTitle"));
    memberLabel_ = text(QStringLiteral("0 / 2 人"), "memberCount");
    memberHeader->addStretch();
    memberHeader->addWidget(memberLabel_, 0, Qt::AlignVCenter);
    sessionLayout->addLayout(memberHeader);
    // 成员行改成上下堆叠：角色在上、名字在下，行高稳定，长昵称只在本行内省略。
    const auto memberRow = [](const QString &role, QLabel *value) {
        auto *row = new QWidget;
        row->setObjectName("memberRow");
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 4, 0, 4);
        rowLayout->setSpacing(1);
        rowLayout->addWidget(text(role, "roleLabel"));
        rowLayout->addWidget(value);
        return row;
    };
    sessionLayout->addWidget(memberRow(QStringLiteral("本机"), elided(name, "localName")));
    peerLabel_ = elided(QStringLiteral("尚未连接"), "peerName");
    sessionLayout->addWidget(memberRow(QStringLiteral("对方"), peerLabel_));
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
    shareButton_->setToolTip(QStringLiteral("将整个主屏幕及鼠标指针发送给已连接的对方"));
    shareLabel_ = text(session_.shareDetail(), "shareStatus");
    shareLabel_->setWordWrap(true);
    sessionLayout->addWidget(text(QStringLiteral("远程协助"), "sectionTitle"));
    controlButton_ = new QPushButton(QStringLiteral("请求控制对方电脑"));
    controlButton_->setObjectName("controlButton");
    allowControlButton_ = new QPushButton(QStringLiteral("允许控制"));
    allowControlButton_->setObjectName("allowControlButton");
    stopControlButton_ = new QPushButton(QStringLiteral("结束控制"));
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
    settingsLayout->setContentsMargins(24, 22, 24, 20);
    settingsLayout->setSpacing(16);
    auto *settingsHeader = new QVBoxLayout;
    settingsHeader->setSpacing(3);
    settingsHeader->addWidget(text(QStringLiteral("设置"), "settingsTitle"));
    settingsHeader->addWidget(text(QStringLiteral("远程控制安全策略与默认音视频设备，修改即时保存到本机。"), "settingsSubtitle"));
    settingsLayout->addLayout(settingsHeader);
    auto *remoteCard = new QFrame;
    remoteCard->setObjectName("settingsForm");
    auto *settingsBodyLayout = new QVBoxLayout(remoteCard);
    settingsBodyLayout->setContentsMargins(20, 18, 20, 18);
    settingsBodyLayout->setSpacing(10);
    settingsBodyLayout->addWidget(text(QStringLiteral("远程控制"), "sectionTitle"));
    auto *requireApproval = new QCheckBox(QStringLiteral("需要二次验证（连接后手动点击允许）"));
    requireApproval->setObjectName("requireRemoteApproval");
    QSettings remotePreferences(QSettings::IniFormat, QSettings::UserScope, "ZJdesk", "remote-control");
    requireApproval->setChecked(remotePreferences.value("requireApproval", true).toBool());
    session_.setRequiresRemoteApproval(requireApproval->isChecked());
    requireApproval->setToolTip(QStringLiteral("关闭后，匹配并连接本机的对端可直接控制本机并复制文件。IP 地址不提供身份认证；下次请求生效。"));
    connect(requireApproval, &QCheckBox::toggled, this, [this](bool enabled) {
        session_.setRequiresRemoteApproval(enabled);
        QSettings preferences(QSettings::IniFormat, QSettings::UserScope, "ZJdesk", "remote-control");
        preferences.setValue("requireApproval", enabled);
    });
    settingsBodyLayout->addWidget(requireApproval);
    auto *codePolicy = new QComboBox;
    codePolicy->setObjectName("remoteCodePolicy");
    codePolicy->addItem(QStringLiteral("每次打开变化"), "startup");
    codePolicy->addItem(QStringLiteral("3天变一次"), "three_days");
    codePolicy->addItem(QStringLiteral("从不变"), "never");
    codePolicy->setToolTip(QStringLiteral("下次上线生效；3天按连续72小时计算，连接期间到期会延后至断开后更换"));
    auto identity = remotePreferences.value("identity").toString();
    if (!zjdesk::protocol::isRemoteIdentityValid(identity))
    {
        identity = QUuid::createUuid().toString(QUuid::Id128);
        remotePreferences.setValue("identity", identity);
    }
    codePolicy->setCurrentIndex(qMax(0, codePolicy->findData(remotePreferences.value("codePolicy", "startup"))));
    auto *codePolicyError = text(QStringLiteral("校验码设置保存失败，请检查本机配置目录的读写权限"), "remoteCodePolicyError");
    codePolicyError->setWordWrap(true);
    codePolicyError->hide();
    remotePreferences.sync();
    if (remotePreferences.status() != QSettings::NoError)
    {
        codePolicy->setCurrentIndex(0);
        remotePreferences.setValue("codePolicy", "startup");
        codePolicyError->show();
    }
    session_.setRemoteCodePolicy(codePolicy->currentData().toString(), identity);
    connect(codePolicy, &QComboBox::currentIndexChanged, this, [this, codePolicy, codePolicyError, identity] {
        QSettings preferences(QSettings::IniFormat, QSettings::UserScope, "ZJdesk", "remote-control");
        preferences.setValue("identity", identity);
        preferences.setValue("codePolicy", codePolicy->currentData());
        preferences.sync();
        if (preferences.status() != QSettings::NoError)
        {
            preferences.setValue("codePolicy", session_.remoteCodePolicy());
            const QSignalBlocker blocker(codePolicy);
            codePolicy->setCurrentIndex(codePolicy->findData(session_.remoteCodePolicy()));
            codePolicyError->show();
            return;
        }
        session_.setRemoteCodePolicy(codePolicy->currentData().toString(), identity);
        codePolicyError->hide();
    });
    auto *codePolicyForm = new QFormLayout;
    codePolicyForm->setHorizontalSpacing(16);
    codePolicyForm->setVerticalSpacing(8);
    codePolicyForm->addRow(QStringLiteral("校验码变化"), codePolicy);
    codePolicyForm->labelForField(codePolicy)->setObjectName("remoteCodePolicyLabel");
    settingsBodyLayout->addLayout(codePolicyForm);
    settingsBodyLayout->addWidget(codePolicyError);
    settingsLayout->addWidget(remoteCard);
    auto *deviceSection = new QVBoxLayout;
    settingsLayout->addLayout(deviceSection);
    deviceSection->setSpacing(10);
    deviceSection->addWidget(text(QStringLiteral("音视频设备"), "sectionTitle"));
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
    deviceSection->addWidget(meetingDevices);
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
    auto *deviceNotice = text(QStringLiteral("默认使用默认设备，可选择其他设备并设置入会后的开启状态。默认开关自动保存，下次连接生效；不改变当前通话。通话中更换设备请先离会。建议佩戴耳机。"), "muted");
    deviceNotice->setWordWrap(true);
    deviceSection->addWidget(deviceActionsCard);
    deviceSection->addWidget(deviceNotice);
    settingsLayout->addStretch(1);
    auto *closeSettings = new QPushButton(QStringLiteral("完成"));
    closeSettings->setObjectName("settingsDone");
    settingsLayout->addWidget(closeSettings, 0, Qt::AlignRight);
    connect(closeSettings, &QPushButton::clicked, meetingSettings, &QDialog::hide);
    meetingSettings->ensurePolished();
    // Keep the styled device rows from being compressed below their full content height.
    meetingSettings->setMinimumHeight(meetingSettings->sizeHint().height());
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
    meetingRemoteVideos_.append(remoteConference);
    for (int index = 2; index <= 3; ++index)
    {
        auto *surface = new VideoSurface;
        surface->setObjectName(QString("conferenceRemoteVideo%1").arg(index));
        surface->setParticipantTile(true);
        meetingRemoteVideos_.append(surface);
    }
    auto *participants = card();
    participants->setObjectName("meetingParticipants");
    participants->setMinimumWidth(190);
    participants->setMaximumWidth(300);
    meetingParticipantLayout_ = new QVBoxLayout(participants);
    meetingParticipantLayout_->setContentsMargins(10, 12, 10, 12);
    meetingParticipantLayout_->setSpacing(10);
    meetingParticipantLayout_->addWidget(text(QStringLiteral("参会画面"), "sectionTitle"));
    for (auto *surface : QList<VideoSurface *>{localConference} + meetingRemoteVideos_)
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
    desktopAnnotations_ = new AnnotationLayer(nullptr, true);
    desktopAnnotationTools_ = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
        | Qt::WindowDoesNotAcceptFocus);
    desktopAnnotationTools_->setObjectName("desktopAnnotationTools");
    desktopAnnotationTools_->setAttribute(Qt::WA_ShowWithoutActivating);
    auto *desktopToolsLayout = new QHBoxLayout(desktopAnnotationTools_);
    desktopToolsLayout->setContentsMargins(4, 4, 4, 4);
    desktopToolsLayout->setSpacing(4);
    const QStringList desktopTools{QStringLiteral("浏览"), QStringLiteral("画笔"), QStringLiteral("圆圈"), QStringLiteral("箭头")};
    for (int i = 0; i < desktopTools.size(); ++i)
    {
        auto *button = new QPushButton(desktopTools.at(i), desktopAnnotationTools_);
        button->setObjectName(QStringLiteral("desktopAnnotationTool%1").arg(i));
        button->setCheckable(true);
        desktopToolsLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, i] {
            if (annotationTool_->currentIndex() == i) refreshAnnotations();
            else annotationTool_->setCurrentIndex(i);
        });
    }
    desktopAnnotationTools_->setStyleSheet(QStringLiteral(
        "QWidget#desktopAnnotationTools { background: #253646; border-radius: 6px; }"
        "QPushButton { color: white; background: #394e61; border: 0; border-radius: 4px; padding: 5px 10px; }"
        "QPushButton:checked { background: #2474e5; }"));
    desktopAnnotationTools_->adjustSize();
#ifdef Q_OS_WIN
    SetWindowDisplayAffinity(reinterpret_cast<HWND>(desktopAnnotationTools_->winId()), 0x00000011);
#endif
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
    tileLayout->addWidget(tileHeader);
    tileLayout->addWidget(meetingShareVideo_, 1);
    views->addWidget(tile, 4);
    views->addWidget(participants, 1);
    meetingLayout->addLayout(views, 1);
    auto *controlsCard = card();
    controlsCard->setObjectName("meetingFooter");
    auto *footerColumn = new QVBoxLayout(controlsCard);
    footerColumn->setContentsMargins(18, 10, 18, 12);
    footerColumn->setSpacing(4);
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
    meetingMembers->addStretch();    meetingRosterPopup_ = new QDialog(this, Qt::Popup);
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
    footerColumn->addLayout(meetingMembers);
    auto *meetingControls = new QHBoxLayout;
    meetingControls->setSpacing(12);
    shareLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    shareLabel_->setMinimumWidth(80);
    meetingControls->addWidget(shareLabel_, 1);
    // 共享状态与共享按钮同一行，底部不再留一条只放说明的空白。
    meetingControls->addWidget(shareButton_, 0, Qt::AlignVCenter);
    annotationTool_ = new QComboBox;
    annotationTool_->setObjectName("annotationTool");
    annotationTool_->addItem(QStringLiteral("浏览"), QString());
    annotationTool_->addItem(QStringLiteral("画笔"), "pen");
    annotationTool_->addItem(QStringLiteral("圆圈"), "circle");
    annotationTool_->addItem(QStringLiteral("箭头"), "arrow");
    annotationTool_->addItem(QStringLiteral("远控"), "control");
    annotationTool_->setToolTip(QStringLiteral("选择工具后，在共享画面上按住鼠标左键拖动"));
    clearAnnotations_ = new QPushButton(QStringLiteral("清除我的"));
    clearAnnotations_->setObjectName("clearMyAnnotations");
    clearAllAnnotations_ = new QPushButton(QStringLiteral("清除全部"));
    clearAllAnnotations_->setObjectName("clearAllAnnotations");
    tileTitle->addWidget(annotationTool_);
    tileTitle->addWidget(clearAnnotations_);
    tileTitle->addWidget(clearAllAnnotations_);
    connect(annotationTool_, &QComboBox::currentIndexChanged, this, [this] {
        const auto tool = annotationTool_->currentData().toString();
        if (!tool.isEmpty() && tool != "control" && !session_.canDrawAnnotation())
            session_.requestAnnotationPermission();
        refreshAnnotations();
    });
    connect(clearAnnotations_, &QPushButton::clicked, this, [this] { session_.clearAnnotations(); });
    connect(clearAllAnnotations_, &QPushButton::clicked, this, [this] { session_.clearAnnotations(true); });
    connect(meetingShareVideo_->annotationLayer(), &AnnotationLayer::strokeReady,
        &session_, &zjdesk::SessionCoordinator::submitAnnotation);
    connect(desktopAnnotations_, &AnnotationLayer::strokeReady, &session_, &zjdesk::SessionCoordinator::submitAnnotation);
    connect(desktopAnnotations_, &AnnotationLayer::drawingExitRequested, this, [this] { annotationTool_->setCurrentIndex(0); });
    connect(meetingShareVideo_, &VideoSurface::frameAvailabilityChanged, this, &MainWindow::refreshAnnotations);
    connect(&session_, &zjdesk::SessionCoordinator::annotationsChanged, this, &MainWindow::refreshAnnotations);
    connect(&session_, &zjdesk::SessionCoordinator::annotationPermissionResult, this, [this](bool approved) {
        if (!approved)
        {
            const QSignalBlocker blocker(annotationTool_);
            annotationTool_->setCurrentIndex(0);
            shareLabel_->setText(session_.annotationDetail());
        }
    });
    connect(&session_, &zjdesk::SessionCoordinator::annotationPermissionRequested, this,
        [this](const QString &peerId, const QString &participantName, const QString &streamId) {
            auto *dialog = new QMessageBox(QMessageBox::Question, QStringLiteral("画笔使用申请"),
                QStringLiteral("%1 请求在共享画面上使用画笔，是否同意？").arg(participantName),
                QMessageBox::Yes | QMessageBox::No, this);
            dialog->setObjectName("annotationPermissionDialog");
            dialog->setProperty("annotationPeerId", peerId);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            connect(dialog, &QMessageBox::finished, this, [this, dialog, peerId, streamId](int result) {
                if (dialog->property("annotationCancelled").toBool()) return;
                session_.respondAnnotationPermission(peerId, streamId, result == QMessageBox::Yes);
            });
            dialog->open();
        });
    const auto discardStaleAnnotationRequests = [this] {
        for (auto *dialog : findChildren<QMessageBox *>("annotationPermissionDialog"))
        {
            if (session_.ownsSharedScreen() && session_.conferencePeers().contains(dialog->property("annotationPeerId").toString()))
                continue;
            dialog->setProperty("annotationCancelled", true);
            dialog->reject();
        }
    };
    connect(&session_, &zjdesk::SessionCoordinator::membersChanged, this, discardStaleAnnotationRequests);
    connect(&session_, &zjdesk::SessionCoordinator::annotationsChanged, this, discardStaleAnnotationRequests);
    connect(&session_, &zjdesk::SessionCoordinator::annotationsCleared, this, [this](const QString &author) {
        if (author.isEmpty() || author == session_.annotationAuthor().value("authorId").toString())
        {
            meetingShareVideo_->annotationLayer()->cancelDraft();
            desktopAnnotations_->cancelDraft();
        }
    });
    auto *meetingControlRow = new QHBoxLayout;
    meetingControlLabel_ = new QLabel;
    meetingControlLabel_->setWordWrap(true);
    meetingControlLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    meetingRequestControl_ = new QPushButton(QStringLiteral("申请控制"));
    meetingRequestControl_->setObjectName("meetingRequestControl");
    meetingControlMember_ = new QComboBox;
    meetingControlMember_->setObjectName("meetingControlMember");
    meetingControlMember_->setToolTip(QStringLiteral("选择可控制当前共享桌面的会议成员"));
    meetingGrantControl_ = new QPushButton(QStringLiteral("交给所选成员"));
    meetingGrantControl_->setObjectName("meetingGrantControl");
    meetingStopControl_ = new QPushButton(QStringLiteral("收回控制"));
    meetingStopControl_->setObjectName("meetingStopControl");
    meetingControlRow->addWidget(meetingControlLabel_, 1);
    meetingControlRow->addWidget(meetingRequestControl_);
    meetingControlRow->addWidget(meetingControlMember_);
    meetingControlRow->addWidget(meetingGrantControl_);
    meetingControlRow->addWidget(meetingStopControl_);
    connect(meetingRequestControl_, &QPushButton::clicked, &session_, &zjdesk::SessionCoordinator::requestControl);
    connect(meetingGrantControl_, &QPushButton::clicked, this, [this] {
        session_.grantMeetingControl(meetingControlMember_->currentData().toString());
    });
    connect(meetingStopControl_, &QPushButton::clicked, &session_, &zjdesk::SessionCoordinator::stopControl);
    connect(meetingControlMember_, &QComboBox::currentIndexChanged, this, [this] {
        meetingGrantControl_->setEnabled(session_.canGrantMeetingControl(meetingControlMember_->currentData().toString()));
    });
    connect(meetingShareVideo_, &VideoSurface::remoteInput, &session_, &zjdesk::SessionCoordinator::sendRemoteInput);
    connect(&session_, &zjdesk::SessionCoordinator::remoteCursorChanged, meetingShareVideo_, &VideoSurface::setRemoteCursor);
    footerColumn->addLayout(meetingControls);
    footerColumn->addLayout(meetingControlRow);
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
    connect(&session_, &zjdesk::SessionCoordinator::conferencePeerFrame, this,
        [this](const QString &id, const QImage &image) {
            for (auto *surface : meetingRemoteVideos_)
                if (surface->property("peerId").toString() == id) surface->setFrame(image);
        });
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
    deviceLabel_ = text(QStringLiteral("正在枚举设备"), "deviceList");
    deviceLabel_->setWordWrap(true);
    deviceLabel_->setContentsMargins(12, 10, 12, 10);
    auto *deviceScroll = new QScrollArea;
    deviceScroll->setObjectName("deviceScroll");
    deviceScroll->setWidgetResizable(true);
    deviceScroll->setFrameShape(QFrame::NoFrame);
    deviceScroll->setMinimumHeight(104);
    deviceScroll->setMaximumHeight(132);
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
    logView_->setObjectName("eventLog");
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
    footer->addWidget(text(QStringLiteral("局域网协作"), "footer"));
    footer->addStretch();
    footer->addWidget(text(QString("ZJdesk %1").arg(zjdesk::build::Version), "footer"));
    contentLayout->addWidget(footerWidget);
    rootLayout->addWidget(content, 1);
    normalWidgets_ = {sidebar, headerWidget, identityHeader, remoteCodeLabel_, connectionCard, sessionScroll, stageHeader,
        deviceActionsCard, footerWidget};
    connect(expandButton_, &QPushButton::toggled, this, &MainWindow::expandVideo);
    for (auto *surface : QList<VideoSurface *>{meetingLocalVideo_, meetingShareVideo_} + meetingRemoteVideos_)
    {
        surface->setExpandable(true);
        connect(surface, &VideoSurface::expandRequested, this, [this, surface] {
            if (expandedMeetingVideo_ == surface) restoreMeetingVideo();
            else expandMeetingVideo(surface);
        });
        connect(surface, &VideoSurface::frameAvailabilityChanged, this, [this, surface] {
            if (expandedMeetingVideo_ == surface && !surface->hasFrame()) restoreMeetingVideo();
        });
    }
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
        if (networkPeer_ && !networkSetup_.verified())
        {
            verifyStartupPeer();
            return;
        }
        if (session_.state() == State::Idle || session_.state() == State::Failed)
            startRemoteAccess();
        else if (session_.remoteAccess() && session_.state() == State::WaitingForPeer)
        {
            if (!networkPeer_) session_.connectRemote(roomEdit_->text().trimmed());
        }
        else if (session_.remoteAccess() && session_.state() == State::PeerAvailable)
            session_.disconnectRemote();
        else
            session_.leave();
    });
    connect(roomEdit_, &QLineEdit::textChanged, this, &MainWindow::refreshSession);
    connect(serverEdit_, &QLineEdit::textChanged, this, &MainWindow::refreshRemoteHistory);
    connect(&session_, &zjdesk::SessionCoordinator::remotePeerConnected, this, &MainWindow::rememberRemotePeer);
    connect(serverEdit_, &QLineEdit::textEdited, this, [this] {
        if (session_.remoteAccess() && session_.state() == zjdesk::SessionCoordinator::State::WaitingForPeer)
            session_.leave();
    });
    connect(meetingJoinButton_, &QPushButton::clicked, this, [this, selectMeetingDevices] {
        if (networkPeer_ && !networkSetup_.verified()) return;
        using State = zjdesk::SessionCoordinator::State;
        if (session_.state() == State::Idle || session_.state() == State::Failed
            || (session_.remoteAccess() && session_.state() == State::WaitingForPeer))
        {
            if (!selectMeetingDevices()) return;
            if (session_.remoteAccess()) session_.leave();
            session_.join(QUrl(meetingServerEdit_->text().trimmed()), meetingRoomEdit_->text().trimmed(),
                meetingNameEdit_->text().trimmed(), zjdesk::SessionCoordinator::RoomType::Meeting);
        }
        else
        {
            session_.leave();
            startRemoteAccess();
        }
    });
    connect(meetingNameEdit_, &QLineEdit::textChanged, findChild<QLabel *>("localName"), &QLabel::setText);
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
    connect(refreshDevicesButton_, &QPushButton::clicked, this, &MainWindow::requestCameraPermission);
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
    qApp->removeEventFilter(this);
    // Session teardown emits frames and state changes after later-declared roster containers die.
    // Stop those UI callbacks while all MainWindow members are still alive.
    QObject::disconnect(&session_, nullptr, nullptr, nullptr);
    delete desktopAnnotationTools_;
    delete desktopAnnotations_;
}

void MainWindow::requestCameraPermission()
{
    if (networkPeer_ && !networkSetup_.verified()) return;
    session_.requestCameraPermission();
}

void MainWindow::enableNetworkSetup()
{
    if (networkPeer_) return;
    remoteCodeLabel_->hide();
    normalWidgets_.removeAll(remoteCodeLabel_);
    // 启动校验模式不需要本机校验码，连同小标题与复制按钮一起收起，避免留下空行。
    if (auto *identityHeader = findChild<QWidget *>("identityHeader"))
    {
        identityHeader->hide();
        normalWidgets_.removeAll(identityHeader);
    }
    remoteHistory_->hide();
    findChild<QLabel *>("remoteCodeFieldLabel")->hide();
    findChild<QComboBox *>("remoteCodePolicy")->hide();
    findChild<QWidget *>("remoteCodePolicyLabel")->hide();
    findChild<QLabel *>("remoteCodePolicyError")->hide();
    session_.setRemoteCodePolicy("startup", {});
#ifdef Q_OS_LINUX
    networkSetup_.startLocalServer(QUrl(serverEdit_->text()).port(8765));
#endif
    // IP 验证与发起远控分为两个独立操作。
    if (connectionLayout_ && networkColumn_)
    {
        connectionLayout_->removeWidget(networkColumn_);
        networkColumn_->hide();
    }
    auto *panel = new QFrame;
    panel->setObjectName("startupNetworkPanel");
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
#ifdef Q_OS_WIN
    layout->addWidget(text(QStringLiteral("Linux IP"), "networkLabel"));
    networkPeerKey_ = QStringLiteral("linuxAddress");
#else
    layout->addWidget(text(QStringLiteral("Windows IP"), "networkLabel"));
    networkPeerKey_ = QStringLiteral("windowsAddress");
#endif
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "ZJdesk", "network");
    networkPeer_ = new QLineEdit(settings.value("networkSetup/" + networkPeerKey_).toString());
    networkPeer_->setObjectName("networkPeerAddress");
    networkPeer_->setPlaceholderText(QStringLiteral("填写对端 IP"));
    networkPeer_->setAccessibleName(QStringLiteral("对端 IP 地址"));
    networkPeer_->setClearButtonEnabled(true);
    auto *inputRow = new QHBoxLayout;
    inputRow->setSpacing(10);
    inputRow->addWidget(networkPeer_, 1);
    if (joinButton_)
    {
        joinButton_->setText(QStringLiteral("连接服务"));
        inputRow->addWidget(joinButton_);
    }
    remoteConnectButton_ = new QPushButton(QStringLiteral("远程控制"));
    remoteConnectButton_->setObjectName("remoteConnectButton");
    remoteConnectButton_->setToolTip(QStringLiteral("连接服务后，点击此按钮请求控制对端电脑"));
    inputRow->addWidget(remoteConnectButton_);
    connect(remoteConnectButton_, &QPushButton::clicked, this, [this] {
        if (networkSetup_.verified()) session_.connectRemoteIp();
    });
    layout->addLayout(inputRow);
#ifdef Q_OS_WIN
    auto *help = text(QStringLiteral("在对端 Linux 电脑运行 sh tools/ip-info.sh，填写输出的地址"), "networkHelp");
    help->setToolTip(QStringLiteral(
        "同局域网填「同局域网」地址；跨网或经 Tailscale 连接填「不同局域网」地址。脚本只输出这两个地址，本机地址自动选择。"));
#else
    auto *help = text(QStringLiteral("在对端 Windows 电脑运行 tools\\ip-info.cmd，填写输出的地址"), "networkHelp");
    help->setToolTip(QStringLiteral(
        "同局域网填「同局域网」地址；跨网或经 Tailscale 连接填「不同局域网」地址。脚本只输出这两个地址，本机地址自动选择。"));
#endif
    help->setWordWrap(true);
    help->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(help);
    networkStatus_ = text({}, "networkStatus");
    networkStatus_->setWordWrap(true);
    layout->addWidget(networkStatus_);
    if (connectionLayout_) connectionLayout_->addWidget(panel, 1);
    connect(networkPeer_, &QLineEdit::textChanged, this, &MainWindow::invalidateNetworkSetup);
    connect(&networkSetup_, &zjdesk::NetworkSetup::progress, this, [this](const QString &message) {
        if (!networkStatus_) return;
        networkStatus_->setText(QStringLiteral("正在验证…"));
        networkStatus_->setToolTip(message);
    });
    connect(&networkSetup_, &zjdesk::NetworkSetup::finished, this,
        [this](bool success, const QString &message) {
            if (!networkPeer_) return;
            networkVerifying_ = false;
            if (networkStatus_)
            {
                networkStatus_->setText(success ? QStringLiteral("验证通过") : QStringLiteral("验证失败，悬停查看原因"));
                networkStatus_->setToolTip(message);
            }
            networkPeer_->setProperty("networkInvalid", !success);
            networkPeer_->setStyleSheet(success ? QString() : QStringLiteral("QLineEdit { border: 2px solid #dc3545; background: #fff1f2; }"));
            if (success)
            {
                const auto configuration = networkSetup_.configuration();
                serverEdit_->setText(configuration.serverUrl.toString());
                QSettings saved(QSettings::IniFormat, QSettings::UserScope, "ZJdesk", "network");
                saved.setValue("networkSetup/" + networkPeerKey_, configuration.peerAddress());
                if (networkStatus_)
                    networkStatus_->setToolTip(QStringLiteral("本机 %1 · 对端 %2").arg(configuration.localAddress(), configuration.peerAddress()));
                startRemoteAccess();
                requestCameraPermission();
            }
            refreshSession();
        });
    connect(networkPeer_, &QLineEdit::returnPressed, this, &MainWindow::verifyStartupPeer);
    invalidateNetworkSetup();
}

void MainWindow::verifyStartupPeer()
{
    if (!networkPeer_) return;
    networkSetup_.cancel();
    session_.leave();
    networkVerifying_ = true;
    networkPeer_->setStyleSheet(QString());
    networkPeer_->setProperty("networkInvalid", false);
    if (networkStatus_)
    {
        networkStatus_->setText(QStringLiteral("正在验证…"));
        networkStatus_->setToolTip(QStringLiteral("正在检查本机网卡、对端 ping 和共用服务"));
    }
    refreshSession();
    networkSetup_.verifyPeer(networkPeer_->text().trimmed(), QUrl(serverEdit_->text()));
}

void MainWindow::invalidateNetworkSetup()
{
    if (!networkPeer_) return;
    networkSetup_.cancel();
    session_.leave();
    networkVerifying_ = false;
    const bool empty = networkPeer_->text().trimmed().isEmpty();
    networkPeer_->setStyleSheet(empty ? QStringLiteral("QLineEdit { border: 2px solid #dc3545; background: #fff1f2; }") : QString());
    networkPeer_->setProperty("networkInvalid", empty);
    if (networkStatus_)
    {
        networkStatus_->setText(empty ? QStringLiteral("请填写对端 IP") : QStringLiteral("未验证"));
        networkStatus_->setToolTip(QStringLiteral("请先启动对端并验证 IP，验证后可使用远控和会议。"));
    }
    refreshSession();
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
    if (!inMeeting && expandedMeetingVideo_) restoreMeetingVideo();
    const int count = inMeeting ? session_.memberCount() : 0;
    const bool hasPeer = count > 1;
    const QString localName = meetingNameEdit_->text().trimmed().isEmpty()
        ? QStringLiteral("我") : meetingNameEdit_->text().trimmed();
    const QString remoteName = session_.peerName().isEmpty() ? QStringLiteral("参会成员") : session_.peerName();
    const bool hidden = session_.conferenceCameraHidden();
    const bool remoteHidden = session_.conferenceRemoteCameraHidden();
    meetingParticipantLayout_->setContentsMargins(10, count > 2 ? 8 : 12, 10, count > 2 ? 8 : 12);
    meetingParticipantLayout_->setSpacing(count > 2 ? 6 : 10);
    meetingLocalVideo_->setMinimumHeight(count > 2 ? 60 : 110);
    QStringList members;
    if (count > 0) members.append(localName + QStringLiteral("（我）"));
    auto peers = inMeeting ? session_.conferencePeers() : QMap<QString, QString>{};
    QStringList ids = peers.keys();
    if (ids.removeOne(session_.peerId())) ids.prepend(session_.peerId());
    for (const auto &id : ids) members.append(peers.value(id));
    setMeetingRoster(members);
    for (int index = 0; index < meetingRemoteVideos_.size(); ++index)
    {
        auto *surface = meetingRemoteVideos_.at(index);
        const QString id = inMeeting && index < ids.size() ? ids.at(index) : QString{};
        if (surface->property("peerId").toString() != id)
        {
            surface->setProperty("peerId", id);
            if (expandedMeetingVideo_ == surface) restoreMeetingVideo();
            surface->setFrame({});
        }
        surface->setVisible(!id.isEmpty());
        surface->setMinimumHeight(count > 2 ? 60 : 110);
        const bool peerHidden = session_.conferencePeerCameraHidden(id);
        surface->setParticipant(peers.value(id), peerHidden
            ? QStringLiteral("画面已隐藏") : QStringLiteral("等待摄像头画面"));
        surface->setFrameCaption(peers.value(id));
        surface->setFrameHidden(peerHidden);
    }
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
    refreshExpandedMeetingVideo();
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
    refreshCaptureExclusion();
    using Preview = zjdesk::SessionCoordinator::PreviewState;
    shareButton_->setEnabled(session_.roomType() == zjdesk::SessionCoordinator::RoomType::Meeting
        && session_.mediaEnvironment().ready
        && session_.state() == zjdesk::SessionCoordinator::State::PeerAvailable
        && !session_.receivingScreen() && session_.shareState() != Preview::Stopping
        && (session_.shareState() != Preview::Starting || session_.screenActive()));
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
    refreshAnnotations();
}

void MainWindow::refreshCaptureExclusion()
{
#ifdef Q_OS_WIN
    const auto handle = reinterpret_cast<HWND>(effectiveWinId());
    if (!handle) return;
    // Exclude the entire local window so its live preview cannot feed back into desktop capture.
    // WDA_EXCLUDEFROMCAPTURE shows the desktop underneath instead of a black fullscreen window.
    const DWORD desired = session_.isDesktopShare() ? 0x00000011 : WDA_NONE;
    DWORD current = WDA_NONE;
    if (GetWindowDisplayAffinity(handle, &current) && current == desired) return;
    if (!SetWindowDisplayAffinity(handle, desired))
        qWarning("Unable to update desktop capture exclusion (Win32 error %lu)", GetLastError());
#endif
}

void MainWindow::refreshAnnotations()
{
    if (!annotationTool_) return;
    const bool active = session_.canAnnotate();
    if (!active && annotationTool_->currentIndex() != 0)
    {
        const QSignalBlocker blocker(annotationTool_);
        annotationTool_->setCurrentIndex(0);
    }
    annotationTool_->setEnabled(active && meetingShareVideo_->hasFrame());
    annotationTool_->setToolTip(session_.annotationRequestPending() ? QStringLiteral("等待共享者同意使用画笔")
        : session_.annotationDetail());
    if (active && (session_.annotationRequestPending() || session_.canDrawAnnotation()))
        shareLabel_->setText(session_.annotationRequestPending() ? QStringLiteral("等待共享者同意使用画笔")
            : session_.shareDetail());
    clearAnnotations_->setEnabled(active);
    clearAllAnnotations_->setVisible(session_.ownsSharedScreen());
    auto *layer = meetingShareVideo_->annotationLayer();
    layer->setAnnotationsInVideo(session_.annotationsInVideo());
    layer->setAnnotations(session_.annotations(), session_.annotationAuthor());
    layer->setTool(annotationTool_->isEnabled() && session_.canDrawAnnotation()
        ? annotationTool_->currentData().toString() : QString());
    if (!session_.canDrawAnnotation()) layer->cancelDraft();
    meetingShareVideo_->setControlEnabled(session_.roomType() == zjdesk::SessionCoordinator::RoomType::Meeting
        && session_.remoteState() == zjdesk::SessionCoordinator::RemoteState::Controlling
        && annotationTool_->currentData().toString() == "control");
    shareLabel_->setToolTip(session_.annotationDetail());
    desktopAnnotations_->setAnnotations(session_.annotations(), session_.annotationAuthor());
    const bool desktopDrawing = session_.isDesktopShare() && session_.canDrawAnnotation()
        && annotationTool_->isEnabled() && !annotationTool_->currentData().toString().isEmpty();
    desktopAnnotations_->setTool(desktopDrawing ? annotationTool_->currentData().toString() : QString());
    const bool desktopSharing = session_.isDesktopShare();
    const bool desktop = desktopSharing && (desktopDrawing || !session_.annotations().isEmpty());
    if (desktopSharing)
    {
        auto geometry = QGuiApplication::primaryScreen()->geometry();
#ifndef Q_OS_WIN
        // ximagesrc captures the X root window, including all attached monitors.
        for (auto *screen : QGuiApplication::screens()) geometry = geometry.united(screen->geometry());
#endif
        desktopAnnotations_->setGeometry(geometry);
        desktopAnnotationTools_->move(geometry.right() - desktopAnnotationTools_->width() - 11, geometry.top() + 12);
    }
    desktopAnnotations_->setVisible(desktop);
    desktopAnnotationTools_->setEnabled(annotationTool_->isEnabled());
    for (int i = 0; i < 4; ++i)
        desktopAnnotationTools_->findChild<QPushButton *>(QStringLiteral("desktopAnnotationTool%1").arg(i))
            ->setChecked(annotationTool_->currentIndex() == i);
    desktopAnnotationTools_->setVisible(desktopSharing);
    if (desktopSharing) desktopAnnotationTools_->raise();
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
        normalWindowGeometry_ = geometry();
        showFullScreen();
    }
    else
    {
        setWindowState(normalWindowState_);
        if (!isMaximized() && !isFullScreen()) setGeometry(normalWindowGeometry_);
    }
    refreshShare();
    if (session_.remoteState() == zjdesk::SessionCoordinator::RemoteState::Controlling)
        video_->setFocus();
}

void MainWindow::expandMeetingVideo(VideoSurface *surface)
{
    if (!surface->isVisible()) return;
    if (expandedMeetingVideo_) restoreMeetingVideo();
    expandedMeetingVideo_ = surface;
    normalWindowState_ = windowState();
    normalWindowGeometry_ = geometry();
    // Keep the OpenGL widget in its original parent and layout.
    for (QWidget *widget = surface; widget; widget = widget->parentWidget())
    {
        auto *layout = widget->layout();
        videoLayoutStates_.append({widget, widget->minimumSize(), widget->maximumSize(),
            layout ? layout->contentsMargins() : QMargins(), layout ? layout->spacing() : 0});
        if (widget == centralWidget()) break;
        for (auto *sibling : widget->parentWidget()->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
            if (sibling != widget)
                videoSiblingVisibility_.append({sibling, !sibling->isHidden()});
    }
    surface->setExpanded(true);
    for (int index = 0; index < meetingParticipantLayout_->count(); ++index)
    {
        videoParticipantStretches_.append(meetingParticipantLayout_->stretch(index));
        meetingParticipantLayout_->setStretch(index, meetingParticipantLayout_->itemAt(index)->widget() == surface ? 1 : 0);
    }
    refreshExpandedMeetingVideo();
    showFullScreen();
    surface->setFocus();
}

void MainWindow::refreshExpandedMeetingVideo()
{
    if (!expandedMeetingVideo_) return;
    for (const auto &state : videoLayoutStates_)
    {
        state.widget->setMinimumSize(0, 0);
        state.widget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        if (auto *layout = state.widget->layout())
        {
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);
        }
    }
    // Hidden camera tiles must not reserve their normal minimum height.
    for (auto *surface : QList<VideoSurface *>{meetingLocalVideo_} + meetingRemoteVideos_)
        surface->setMinimumHeight(0);
    for (const auto &sibling : videoSiblingVisibility_)
        if (expandedMeetingVideo_ != meetingShareVideo_ || sibling.first->objectName() != "stageHeader")
            sibling.first->hide();
}

void MainWindow::restoreMeetingVideo()
{
    if (!expandedMeetingVideo_) return;
    expandedMeetingVideo_->setExpanded(false);
    expandedMeetingVideo_ = nullptr;
    for (const auto &state : videoLayoutStates_)
    {
        state.widget->setMinimumSize(state.minimum);
        state.widget->setMaximumSize(state.maximum);
        if (auto *layout = state.widget->layout())
        {
            layout->setContentsMargins(state.margins);
            layout->setSpacing(state.spacing);
        }
    }
    for (const auto &sibling : videoSiblingVisibility_) sibling.first->setVisible(sibling.second);
    videoLayoutStates_.clear();
    videoSiblingVisibility_.clear();
    for (int index = 0; index < videoParticipantStretches_.size(); ++index)
        meetingParticipantLayout_->setStretch(index, videoParticipantStretches_.at(index));
    videoParticipantStretches_.clear();
    setWindowState(normalWindowState_);
    if (!isMaximized() && !isFullScreen()) setGeometry(normalWindowGeometry_);
    refreshMeetingParticipants();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this && (event->type() == QEvent::WinIdChange || event->type() == QEvent::Show
        || event->type() == QEvent::WindowStateChange))
        refreshCaptureExclusion();
    if (event->type() == QEvent::KeyPress && (expandButton_->isChecked() || expandedMeetingVideo_)
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)
    {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && widget->window() == this)
        {
            if (expandedMeetingVideo_) restoreMeetingVideo();
            else expandButton_->setChecked(false);
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
    if (remoteConnectButton_)
    {
        const bool verified = networkSetup_.verified();
        remoteConnectButton_->setVisible(verified);
        remoteConnectButton_->setEnabled(verified && session_.remoteAccess()
            && session_.state() == zjdesk::SessionCoordinator::State::WaitingForPeer && !session_.remoteConnecting());
    }
    allowControlButton_->setVisible(!expandButton_->isChecked() && state == Remote::Pending);
    stopControlButton_->setEnabled(state != Remote::Idle);
    stopControlButton_->setText(state == Remote::Pending ? QStringLiteral("拒绝控制") : QStringLiteral("结束控制"));
    controlLabel_->setText(session_.remoteDetail());
    const bool meeting = session_.roomType() == zjdesk::SessionCoordinator::RoomType::Meeting;
    video_->setControlEnabled(!meeting && state == Remote::Controlling);
    const bool owner = meeting && session_.ownsSharedScreen();
    const bool controlling = meeting && state == Remote::Controlling;
    if (controlling && !meetingWasControlling_) annotationTool_->setCurrentIndex(annotationTool_->findData("control"));
    if (!controlling && annotationTool_->currentData().toString() == "control") annotationTool_->setCurrentIndex(0);
    meetingWasControlling_ = controlling;
    meetingControlLabel_->setVisible(meeting && session_.screenActive());
    meetingControlLabel_->setText(session_.remoteDetail());
    meetingRequestControl_->setVisible(meeting && session_.receivingScreen());
    meetingRequestControl_->setEnabled(session_.canRequestControl());
    meetingControlMember_->setVisible(owner);
    meetingGrantControl_->setVisible(owner);
    const auto selected = meetingControlMember_->currentData().toString();
    meetingControlMember_->clear();
    const auto peers = session_.conferencePeers();
    for (auto it = peers.cbegin(); it != peers.cend(); ++it) meetingControlMember_->addItem(it.value(), it.key());
    const auto next = owner && state == Remote::Pending ? session_.meetingControlPeerId() : selected;
    const int selectedIndex = meetingControlMember_->findData(next);
    if (selectedIndex >= 0) meetingControlMember_->setCurrentIndex(selectedIndex);
    meetingGrantControl_->setEnabled(session_.canGrantMeetingControl(meetingControlMember_->currentData().toString()));
    meetingStopControl_->setVisible(meeting && state != Remote::Idle);
    meetingStopControl_->setText(state == Remote::Pending ? QStringLiteral("拒绝控制")
        : owner ? QStringLiteral("收回控制") : state == Remote::Requesting ? QStringLiteral("取消申请") : QStringLiteral("释放控制"));
    refreshAnnotations();
}

void MainWindow::refreshSession()
{
    using State = zjdesk::SessionCoordinator::State;
    const auto state = session_.state();
    const bool idle = state == State::Idle || state == State::Failed;
    const bool remote = session_.remoteAccess();
    const bool ready = remote && state == State::WaitingForPeer;
    const bool startup = networkPeer_ != nullptr;
    remoteCodeLabel_->setText(session_.remoteCode().isEmpty() ? QStringLiteral("未在线") : session_.remoteCode());
    if (copyCodeButton_)
        copyCodeButton_->setEnabled(!session_.remoteCode().isEmpty());
    serverEdit_->setEnabled(idle || ready);
    remoteHistory_->setEnabled(idle || ready);
    if (remote && state == State::PeerAvailable && !roomEdit_->text().isEmpty())
    {
        const QSignalBlocker blocker(roomEdit_);
        remoteHistory_->setCurrentIndex(-1);
        roomEdit_->clear();
    }
    meetingServerEdit_->setEnabled(idle);
    meetingRoomEdit_->setEnabled(idle || ready);
    meetingNameEdit_->setEnabled(idle);
    const bool meeting = session_.roomType() == zjdesk::SessionCoordinator::RoomType::Meeting;
    const bool networkReady = !startup || networkSetup_.verified();
    bool joinEnabled = networkReady && (idle || (!meeting && !session_.remoteConnecting() && (!ready || startup || (roomEdit_->hasAcceptableInput()
        && roomEdit_->text() != session_.remoteCode()))));
    if (startup && idle)
        joinEnabled = !networkPeer_->text().trimmed().isEmpty() && !networkVerifying_;
    if (startup && ready) joinEnabled = false;
    joinButton_->setEnabled(joinEnabled);
    meetingJoinButton_->setEnabled(networkReady && (idle || meeting || ready));
    joinButton_->setText(idle ? (startup ? QStringLiteral("连接服务") : QStringLiteral("获取校验码"))
        : ready ? (startup ? QStringLiteral("服务已连接") : QStringLiteral("连接对方"))
        : state == State::Connecting || state == State::Joining ? QStringLiteral("取消连接") : QStringLiteral("断开连接"));
    meetingJoinButton_->setText(idle || !meeting ? QStringLiteral("加入会议")
        : state == State::Connecting || state == State::Joining ? QStringLiteral("取消连接") : QStringLiteral("离开会议"));
    const QString labels[] = {QStringLiteral("未连接"), QStringLiteral("正在连接"), QStringLiteral("正在连接"),
        remote ? QStringLiteral("远控已在线") : QStringLiteral("等待成员"), meeting ? QStringLiteral("成员已入会") : QStringLiteral("已连接对方"), QStringLiteral("连接失败")};
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
    memberLabel_->setText(QStringLiteral("%1 / %2 人").arg(session_.memberCount()).arg(meeting ? 4 : 2));
    peerLabel_->setText(session_.peerName().isEmpty() ? QStringLiteral("尚未连接") : session_.peerName());
    refreshMeetingParticipants();
    refreshShare();
    if (state == State::PeerAvailable)
        video_->setParticipant(session_.peerName(), QStringLiteral("对方已就绪，可请求远程控制"));
    else
        video_->setParticipant(QStringLiteral("等待远程连接"),
                              state == State::Failed ? session_.detail() : networkPeer_ ? QStringLiteral("验证 IP 后连接对方，这里将显示远端画面") : QStringLiteral("输入对方校验码连接后，这里将显示远端画面"));
}

void MainWindow::startRemoteAccess()
{
    if (networkPeer_ && !networkSetup_.verified()) return;
    // 昵称只在页头填写；留空时沿用会话显示名，避免校验失败。
    const QString nickname = meetingNameEdit_->text().trimmed().isEmpty()
        ? QStringLiteral("我") : meetingNameEdit_->text().trimmed();
    session_.startRemoteAccess(QUrl(serverEdit_->text().trimmed()), nickname,
        networkPeer_ ? networkSetup_.configuration().peerAddress() : QString());
}

void MainWindow::refreshRemoteHistory()
{
    const auto entered = roomEdit_->text();
    const QSignalBlocker comboBlocker(remoteHistory_);
    const QSignalBlocker editBlocker(roomEdit_);
    remoteHistory_->clear();
    const auto server = QUrl(serverEdit_->text().trimmed()).toString();
    QSettings preferences(QSettings::IniFormat, QSettings::UserScope, "ZJdesk", "remote-control");
    for (const auto &value : preferences.value("recentConnections").toList())
    {
        const auto entry = value.toMap();
        const auto code = entry.value("code").toString();
        if (entry.value("server").toString() != server || !zjdesk::protocol::isRemoteCodeValid(code)) continue;
        const auto name = entry.value("name").toString();
        remoteHistory_->addItem(remotePeerIcon(name), code + "  " + name, code);
    }
    remoteHistory_->setCurrentIndex(-1);
    roomEdit_->setText(entered);
}

void MainWindow::rememberRemotePeer(const QString &code, const QString &name)
{
    if (networkPeer_) return;
    const auto server = QUrl(serverEdit_->text().trimmed()).toString();
    QSettings preferences(QSettings::IniFormat, QSettings::UserScope, "ZJdesk", "remote-control");
    auto entries = preferences.value("recentConnections").toList();
    for (qsizetype i = entries.size(); i-- > 0;)
    {
        const auto entry = entries.at(i).toMap();
        if (entry.value("server").toString() == server && entry.value("code").toString() == code)
            entries.removeAt(i);
    }
    entries.prepend(QVariantMap{{"server", server}, {"code", code}, {"name", name}});
    while (entries.size() > 10) entries.removeLast();
    preferences.setValue("recentConnections", entries);
    refreshRemoteHistory();
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
