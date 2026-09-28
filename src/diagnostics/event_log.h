#pragma once

#include <QDateTime>
#include <QObject>
#include <QVector>

namespace zjdesk
{
struct DiagnosticEvent
{
    QDateTime timestamp;
    QString category;
    QString message;
};

class EventLog final : public QObject
{
    Q_OBJECT
public:
    static constexpr int MaxEntries = 300;
    explicit EventLog(QObject *parent = nullptr) : QObject(parent) {}
    void append(const QString &category, const QString &message);
    const QVector<DiagnosticEvent> &entries() const { return entries_; }
    bool exportJsonLines(const QString &path, QString *error) const;

signals:
    void appended(const QString &displayText);

private:
    QVector<DiagnosticEvent> entries_;
};
} // namespace zjdesk
