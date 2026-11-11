#pragma once

#include <QFrame>
#include <array>

class QLineEdit;
class QMenu;

class Ipv4AddressEdit final : public QFrame
{
    Q_OBJECT
public:
    explicit Ipv4AddressEdit(QWidget *parent = nullptr);
    QString address() const;
    void setText(const QString &address);
    void clear();
    void selectAll();
    void setSuggestions(const QStringList &addresses);

signals:
    void textChanged();
    void returnPressed();
    void historyRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    std::array<QLineEdit *, 4> octets_{};
    QMenu *history_ = nullptr;
    bool wholeAddressSelected_ = false;
};
