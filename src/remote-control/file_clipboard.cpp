#include "file_clipboard.h"
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QUuid>

namespace zjdesk
{
namespace
{
constexpr int ChunkBytes = 16384, MaxEntries = 4096;
QClipboard *clipboard()
{
    return qobject_cast<QGuiApplication *>(QCoreApplication::instance()) ? QGuiApplication::clipboard() : nullptr;
}
bool validPath(const QString &path)
{
    if (path.isEmpty() || path.size() > 1024 || path.contains('\\')) return false;
    static const QRegularExpression forbidden(QStringLiteral("[<>:\"|?*\\x{0000}-\\x{001f}]"));
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\\.|$)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto parts = path.split('/');
    if (parts.size() > 64) return false;
    for (const auto &part : parts)
        if (part.isEmpty() || part == "." || part == ".." || part.size() > 255
            || part.endsWith('.') || part.endsWith(' ') || part.contains(forbidden) || part.contains(reserved)) return false;
    return true;
}
}

FileClipboard::FileClipboard(QObject *parent) : QObject(parent)
{
    timeout_.setSingleShot(true);
    timeout_.setInterval(30000);
    connect(&timeout_, &QTimer::timeout, this, [this] { fail(QStringLiteral("文件传输超时，请重新复制")); });
    if (auto *cb = clipboard()) connect(cb, &QClipboard::dataChanged, this, &FileClipboard::clipboardChanged);
}

QList<QUrl> FileClipboard::copiedFiles(const QMimeData *mime)
{
    if (!mime || mime->hasFormat("application/x-zjdesk-file-clipboard")) return {};
    if (mime->hasUrls()) return mime->urls();
    const auto lines = mime->data("x-special/gnome-copied-files").split('\n');
    QList<QUrl> urls;
    if (!lines.isEmpty() && (lines.first() == "copy" || lines.first() == "cut"))
        for (int i = 1; i < lines.size(); ++i)
            if (!lines[i].trimmed().isEmpty()) urls.append(QUrl::fromEncoded(lines[i].trimmed()));
    return urls;
}

void FileClipboard::start(bool sender, quint64 epoch)
{
    if (active() && sender_ == sender && epoch_ == epoch) return;
    stop();
    sender_ = sender;
    epoch_ = epoch;
    if (active() && sender_ && clipboard()) offerFiles(copiedFiles(clipboard()->mimeData()));
}

void FileClipboard::stop()
{
    resetTransfer();
    epoch_ = 0;
    setStatus({});
}

void FileClipboard::resetTransfer()
{
    timeout_.stop();
    file_.close();
    pending_.reset();
    entries_.clear(); paths_.clear(); directories_.clear(); roots_.clear();
    id_.clear(); currentPath_.clear();
    index_ = count_ = 0;
    offset_ = currentSize_ = 0;
}

void FileClipboard::setStatus(const QString &text)
{
    if (status_ == text) return;
    status_ = text;
    emit statusChanged();
}

void FileClipboard::send(QJsonObject message)
{
    message.insert("epoch", QString::number(epoch_));
    message.insert("id", id_);
    emit sendMessage(message);
}

void FileClipboard::fail(const QString &reason)
{
    if (!id_.isEmpty()) send({{"action", "cancel"}});
    resetTransfer();
    setStatus(reason);
}

void FileClipboard::clipboardChanged()
{
    if (!active() || settingClipboard_) return;
    if (sender_) offerFiles(copiedFiles(clipboard()->mimeData()));
    else if (!id_.isEmpty()) fail(QStringLiteral("本机剪贴板已改变，文件传输取消"));
}

bool FileClipboard::appendEntry(const QString &source, const QString &path)
{
    const QFileInfo info(source);
    const auto key = path.toCaseFolded();
    if (entries_.size() >= MaxEntries || !validPath(path) || paths_.contains(key)
        || info.isSymLink() || !info.isReadable() || (!info.isFile() && !info.isDir())) return false;
    paths_.insert(key);
    entries_.append({source, path, info.isDir() ? 0 : info.size(), info.lastModified().toMSecsSinceEpoch(), info.isDir()});
    if (info.isDir())
    {
        QDirIterator children(source, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        while (children.hasNext())
        {
            const auto child = children.next();
            if (!appendEntry(child, path + '/' + children.fileName())) return false;
        }
    }
    return true;
}

void FileClipboard::offerFiles(const QList<QUrl> &urls)
{
    if (!active() || !sender_) return;
    if (!id_.isEmpty()) send({{"action", "cancel"}});
    resetTransfer();
    setStatus({});
    if (urls.isEmpty()) return;
    for (const auto &url : urls)
        if (!url.isLocalFile() || !appendEntry(url.toLocalFile(), QFileInfo(url.toLocalFile()).fileName()))
        {
            fail(QStringLiteral("无法复制：仅支持可读普通文件/文件夹，最多 4096 项，名称须兼容 Windows（不支持符号链接）"));
            return;
        }
    id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    count_ = int(entries_.size());
    timeout_.start();
    setStatus(QStringLiteral("正在发送文件到对方…"));
    send({{"action", "offer"}, {"count", count_}});
}

void FileClipboard::receive(const QJsonObject &message)
{
    if (!active() || message.value("epoch").toString() != QString::number(epoch_)) return;
    const auto action = message.value("action").toString();
    const auto id = message.value("id").toString();
    if (!sender_ && action == "offer")
    {
        const int count = message.value("count").toInt();
        if (id.isEmpty() || id.size() > 64 || count < 1 || count > MaxEntries) return;
        resetTransfer();
        id_ = id; count_ = count;
        pending_ = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/zjdesk-files-XXXXXX");
        if (!pending_->isValid()) { fail(QStringLiteral("无法创建文件剪贴板缓存")); return; }
        requestNext();
        return;
    }
    if (id_.isEmpty() || id != id_) return;
    if (action == "cancel") { resetTransfer(); setStatus(QStringLiteral("文件传输已取消，请重新复制")); }
    else if (sender_ && action == "read") serve(message);
    else if (!sender_ && action == "entry") accept(message);
    else if (sender_ && action == "done" && index_ == count_)
    {
        resetTransfer();
        setStatus(QStringLiteral("文件已到对方，可在文件管理器粘贴"));
    }
}

void FileClipboard::serve(const QJsonObject &message)
{
    bool ok = false;
    const auto offset = message.value("offset").toString().toLongLong(&ok);
    if (!ok || message.value("index").toInt(-1) != index_ || offset != offset_ || index_ >= count_)
    { fail(QStringLiteral("文件传输序号无效")); return; }
    const auto &entry = entries_[index_];
    QByteArray data;
    if (!entry.directory)
    {
        const QFileInfo info(entry.source);
        if (info.isSymLink() || info.size() != entry.size || info.lastModified().toMSecsSinceEpoch() != entry.modified)
        { fail(QStringLiteral("源文件已改变，请重新复制")); return; }
        file_.setFileName(entry.source);
        if (!file_.open(QIODevice::ReadOnly) || !file_.seek(offset_))
        { fail(QStringLiteral("无法读取源文件")); return; }
        data = file_.read(qMin<qint64>(ChunkBytes, entry.size - offset_));
        if (data.size() != qMin<qint64>(ChunkBytes, entry.size - offset_))
        { fail(QStringLiteral("读取源文件失败")); return; }
        file_.close();
    }
    const bool eof = offset_ + data.size() == entry.size;
    QJsonObject response{{"action", "entry"}, {"index", index_}, {"path", entry.path},
        {"directory", entry.directory}, {"size", QString::number(entry.size)}, {"offset", QString::number(offset_)},
        {"data", QString::fromLatin1(data.toBase64())}, {"eof", eof}};
    if (eof) { ++index_; offset_ = 0; }
    else offset_ += data.size();
    timeout_.start();
    send(response);
}

void FileClipboard::requestNext()
{
    timeout_.start();
    setStatus(QStringLiteral("文件传输 %1/%2，当前文件 %3 KiB；完成后可粘贴").arg(index_ + 1).arg(count_).arg(offset_ / 1024));
    send({{"action", "read"}, {"index", index_}, {"offset", QString::number(offset_)}});
}

void FileClipboard::accept(const QJsonObject &message)
{
    bool sizeOk = false, offsetOk = false;
    const auto size = message.value("size").toString().toLongLong(&sizeOk);
    const auto offset = message.value("offset").toString().toLongLong(&offsetOk);
    const auto path = message.value("path").toString();
    const bool directory = message.value("directory").toBool();
    const auto encoded = message.value("data").toString().toLatin1();
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    const auto &data = decoded.decoded;
    const bool eof = message.value("eof").toBool();
    if (!pending_ || !sizeOk || size < 0 || !offsetOk || offset != offset_
        || message.value("index").toInt(-1) != index_ || !validPath(path)
        || encoded.size() > 4 * ((ChunkBytes + 2) / 3) || !decoded || data.size() > ChunkBytes
        || offset > size || data.size() > size - offset || eof != (data.size() == size - offset)
        || (!eof && data.isEmpty()) || (directory && (size != 0 || !eof)))
    { fail(QStringLiteral("收到无效的文件数据")); return; }
    if (offset_ == 0)
    {
        const auto key = path.toCaseFolded();
        const int slash = path.lastIndexOf('/');
        if (paths_.contains(key) || (slash >= 0 && !directories_.contains(key.left(slash))))
        { fail(QStringLiteral("收到重复文件名或无效目录")); return; }
        paths_.insert(key);
        currentPath_ = path; currentSize_ = size;
        const auto destination = pending_->filePath(path);
        if (directory)
        {
            if (!QDir().mkdir(destination)) { fail(QStringLiteral("无法创建目标文件夹")); return; }
            directories_.insert(key);
        }
        else
        {
            file_.setFileName(destination);
            if (!file_.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            { fail(QStringLiteral("无法创建目标文件")); return; }
        }
        if (slash < 0) roots_.append(QUrl::fromLocalFile(destination));
    }
    else if (path != currentPath_ || size != currentSize_ || directory)
    { fail(QStringLiteral("文件内容与目录不一致")); return; }
    if (!directory && file_.write(data) != data.size())
    { fail(QStringLiteral("写入文件失败，请检查磁盘空间")); return; }
    offset_ += data.size();
    if (eof)
    {
        if (!directory && !file_.flush()) { fail(QStringLiteral("保存文件失败")); return; }
        file_.close(); ++index_; offset_ = 0;
    }
    if (index_ < count_) { requestNext(); return; }
    timeout_.stop();
    const auto urls = roots_;
    if (auto *cb = clipboard())
    {
        auto *mime = new QMimeData;
        mime->setUrls(urls); // Qt exposes local files as CF_HDROP on Windows.
        QByteArray gnome = "copy\n";
        for (const auto &url : urls) gnome += url.toEncoded() + '\n';
        mime->setData("x-special/gnome-copied-files", gnome);
        mime->setData("application/x-zjdesk-file-clipboard", id_.toUtf8());
        settingClipboard_ = true;
        cb->setMimeData(mime);
        settingClipboard_ = false;
        if (!cb->ownsClipboard())
        {
            fail(QStringLiteral("文件已收到，但本机剪贴板被占用，请重新复制"));
            return;
        }
    }
    completed_.push_back(std::move(pending_));
    send({{"action", "done"}});
    resetTransfer();
    setStatus(QStringLiteral("%1 个文件/文件夹已就绪，可在本机文件管理器按 Ctrl+V").arg(urls.size()));
    emit filesReady(urls);
}
} // namespace zjdesk
