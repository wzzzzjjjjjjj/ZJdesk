#include "ipv4_address_edit.h"
#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <algorithm>

Ipv4AddressEdit::Ipv4AddressEdit(QWidget *parent) : QFrame(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 5, 10, 5);
    layout->setSpacing(3);
    auto fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    fixedFont.setPointSizeF(font().pointSizeF());
    for (int index = 0; index < 4; ++index)
    {
        if (index)
        {
            auto *dot = new QLabel(".", this);
            dot->setObjectName("ipv4Separator");
            dot->setAlignment(Qt::AlignCenter);
            layout->addWidget(dot);
        }
        auto *octet = new QLineEdit(this);
        octets_[index] = octet;
        octet->setObjectName(QString("ipv4Octet%1").arg(index));
        octet->setAccessibleName(QStringLiteral("IPv4 第 %1 段").arg(index + 1));
        octet->setFont(fixedFont);
        octet->setAlignment(Qt::AlignCenter);
        octet->setMaxLength(3);
        octet->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{0,3}"), octet));
        octet->setFixedWidth(octet->fontMetrics().horizontalAdvance("000") + 22);
        octet->setStyleSheet("QLineEdit { border: none; border-bottom: 1px solid #8b99a8; border-radius: 0;"
            " background: transparent; padding: 0px 3px 3px; min-height: 20px; }"
            "QLineEdit:focus { border-bottom: 1px solid #1f8577; }");
        octet->installEventFilter(this);
        layout->addWidget(octet);
        connect(octet, &QLineEdit::textChanged, this, [this] { emit textChanged(); });
        connect(octet, &QLineEdit::textEdited, this, [this, octet, index] {
            wholeAddressSelected_ = false;
            if (octet->text().size() == 3 && octet->cursorPosition() == 3 && index < 3)
            {
                octets_[index + 1]->setFocus();
                octets_[index + 1]->selectAll();
            }
        });
        connect(octet, &QLineEdit::returnPressed, this, &Ipv4AddressEdit::returnPressed);
    }
    auto *clearButton = new QToolButton(this);
    clearButton->setObjectName("ipv4Clear");
    clearButton->setIcon(style()->standardIcon(QStyle::SP_LineEditClearButton));
    clearButton->setToolTip(QStringLiteral("清空地址"));
    clearButton->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(clearButton);
    connect(clearButton, &QToolButton::clicked, this, [this] { clear(); octets_[0]->setFocus(); });
    history_ = new QMenu(this);
    connect(history_, &QMenu::aboutToShow, this, &Ipv4AddressEdit::historyRequested);
    auto *historyButton = new QToolButton(this);
    historyButton->setObjectName("ipv4History");
    historyButton->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    historyButton->setToolTip(QStringLiteral("最近连接地址"));
    historyButton->setFocusPolicy(Qt::NoFocus);
    historyButton->setMenu(history_);
    historyButton->setPopupMode(QToolButton::InstantPopup);
    layout->addWidget(historyButton);
    setFocusProxy(octets_[0]);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setToolTip(QStringLiteral("每段输入三位后自动跳转；Tab / Shift+Tab 切换，也可粘贴完整 IPv4 地址"));
}

QString Ipv4AddressEdit::address() const
{
    QStringList parts;
    bool empty = true;
    for (auto *octet : octets_)
    {
        const auto value = octet->text();
        empty = empty && value.isEmpty();
        parts.append(value.isEmpty() ? QString() : QString::number(value.toUInt()));
    }
    return empty ? QString() : parts.join('.');
}

void Ipv4AddressEdit::setText(const QString &value)
{
    if (!value.isEmpty() && !QRegularExpression("^[0-9]{0,3}(\\.[0-9]{0,3}){3}$").match(value).hasMatch()) return;
    const auto previous = address();
    const auto parts = value.split('.');
    for (int index = 0; index < 4; ++index)
    {
        const QSignalBlocker blocker(octets_[index]);
        octets_[index]->setText(parts.value(index));
    }
    wholeAddressSelected_ = false;
    if (address() != previous) emit textChanged();
}

void Ipv4AddressEdit::clear()
{
    setText({});
}

void Ipv4AddressEdit::selectAll()
{
    for (auto *octet : octets_) octet->selectAll();
    wholeAddressSelected_ = true;
}

void Ipv4AddressEdit::setSuggestions(const QStringList &addresses)
{
    history_->clear();
    for (const auto &value : addresses)
    {
        const auto scope = QHostAddress(value).isInSubnet(QHostAddress("100.64.0.0"), 10)
            ? QStringLiteral("Tailscale") : QStringLiteral("同局域网");
        auto *action = history_->addAction(value + " · " + scope);
        action->setData(value);
        connect(action, &QAction::triggered, this, [this, value] { setText(value); });
    }
    if (addresses.isEmpty()) history_->addAction(QStringLiteral("暂无最近连接地址"))->setEnabled(false);
}

bool Ipv4AddressEdit::eventFilter(QObject *watched, QEvent *event)
{
    auto *octet = qobject_cast<QLineEdit *>(watched);
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress)
        wholeAddressSelected_ = false;
    if (event->type() != QEvent::KeyPress) return QFrame::eventFilter(watched, event);
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Control || key->key() == Qt::Key_Shift
        || key->key() == Qt::Key_Alt || key->key() == Qt::Key_Meta)
        return false;
    const auto iterator = std::find(octets_.begin(), octets_.end(), octet);
    const int index = int(iterator - octets_.begin());
    if (key->key() == Qt::Key_F4 || (key->key() == Qt::Key_Down && key->modifiers().testFlag(Qt::AltModifier)))
    {
        history_->popup(mapToGlobal(QPoint(0, height())));
        return true;
    }
    if (key->matches(QKeySequence::SelectAll)) { selectAll(); return true; }
    if (wholeAddressSelected_ && (key->matches(QKeySequence::Copy) || key->matches(QKeySequence::Cut)))
    {
        QApplication::clipboard()->setText(address());
        if (key->matches(QKeySequence::Cut)) clear();
        return true;
    }
    if (wholeAddressSelected_ && (key->key() == Qt::Key_Backspace || key->key() == Qt::Key_Delete))
    {
        clear();
        return true;
    }
    if (key->matches(QKeySequence::Paste))
    {
        const auto value = QApplication::clipboard()->text().trimmed();
        if (value.contains('.')) setText(value);
        else if (QRegularExpression("^[0-9]{1,12}$").match(value).hasMatch())
        {
            if (wholeAddressSelected_)
            {
                clear();
                octets_[0]->setFocus();
            }
            // Use the same validation and segment advancement as individual keystrokes.
            for (const auto digit : value)
            {
                QKeyEvent input(QEvent::KeyPress, digit.unicode(), Qt::NoModifier, QString(digit));
                QApplication::sendEvent(focusWidget(), &input);
            }
        }
        return true;
    }
    if (wholeAddressSelected_ && !key->text().isEmpty() && key->text().at(0).isDigit())
    {
        clear();
        octets_[0]->setFocus();
        QApplication::sendEvent(octets_[0], event);
        return true;
    }
    const bool backward = key->key() == Qt::Key_Backtab
        || (key->key() == Qt::Key_Tab && key->modifiers().testFlag(Qt::ShiftModifier));
    if (key->key() == Qt::Key_Tab || backward || key->key() == Qt::Key_Space
        || key->key() == Qt::Key_Period || key->key() == Qt::Key_Comma)
    {
        const int destination = index + (backward ? -1 : 1);
        if (destination < 0 || destination >= 4) return false;
        octets_[destination]->setFocus(backward ? Qt::BacktabFocusReason : Qt::TabFocusReason);
        octets_[destination]->selectAll();
        return true;
    }
    wholeAddressSelected_ = false;
    return QFrame::eventFilter(watched, event);
}
