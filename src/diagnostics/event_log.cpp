#include "event_log.h"
#include "zjdesk/build_info.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSysInfo>

namespace zjdesk
{
void EventLog::append(const QString &category, const QString &message)
{
    const auto time = QDateTime::currentDateTimeUtc();
    if (entries_.size() == MaxEntries)
        entries_.removeFirst();
    entries_.append({time, category, message});
    emit appended(QString("%1  [%2]  %3").arg(time.toLocalTime().toString("HH:mm:ss"),
                                           category, message));
}

bool EventLog::exportJsonLines(const QString &path, QString *error) const
{
    QSaveFile file(path);
    const auto fail = [&] {
        if (error)
            *error = file.errorString();
        return false;
    };
    if (!file.open(QIODevice::WriteOnly))
        return fail();
    const auto write = [&file](const QJsonObject &object) {
        const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
        return file.write(bytes) == bytes.size();
    };
    if (!write({{"schemaVersion", 1}, {"kind", "environment"},
                {"application", QString("ZJdesk %1").arg(zjdesk::build::Version)},
                {"qt", qVersion()}, {"os", QSysInfo::prettyProductName()},
                {"architecture", QSysInfo::buildCpuArchitecture()}}))
        return fail();
    for (const auto &entry : entries_)
        if (!write({{"timestamp", entry.timestamp.toString(Qt::ISODateWithMs)},
                    {"category", entry.category}, {"message", entry.message}}))
            return fail();
    return file.commit() || fail();
}
} // namespace zjdesk
