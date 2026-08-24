#include "ziputil.h"

#include "third_party/miniz/miniz.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <cstring>

namespace {

/// miniz reads the archive through this callback so that all file access stays
/// inside QFile (correct Unicode path handling on Windows).
size_t qfileRead(void *opaque, mz_uint64 offset, void *buffer, size_t bytes)
{
    auto *file = static_cast<QFile *>(opaque);
    if (!file->seek(static_cast<qint64>(offset)))
        return 0;
    const qint64 read = file->read(static_cast<char *>(buffer), static_cast<qint64>(bytes));
    return read < 0 ? 0 : static_cast<size_t>(read);
}

class Archive
{
public:
    explicit Archive(const QString &path) : file(path)
    {
        std::memset(&zip, 0, sizeof(zip));
        if (!file.open(QIODevice::ReadOnly)) {
            lastError = QStringLiteral("Archiv kann nicht geöffnet werden: %1").arg(path);
            return;
        }
        zip.m_pRead = qfileRead;
        zip.m_pIO_opaque = &file;
        if (!mz_zip_reader_init(&zip, static_cast<mz_uint64>(file.size()), 0)) {
            lastError = QStringLiteral("Archiv ist beschädigt: %1").arg(path);
            return;
        }
        opened = true;
    }

    ~Archive()
    {
        if (opened)
            mz_zip_reader_end(&zip);
    }

    Archive(const Archive &) = delete;
    Archive &operator=(const Archive &) = delete;

    bool isValid() const { return opened; }
    QString error() const { return lastError; }
    mz_zip_archive *handle() { return &zip; }
    mz_uint count() { return mz_zip_reader_get_num_files(&zip); }

    QString entryName(mz_uint index)
    {
        char name[1024];
        const mz_uint length = mz_zip_reader_get_filename(&zip, index, name, sizeof(name));
        if (length == 0)
            return QString();
        return QString::fromUtf8(name, static_cast<qsizetype>(length - 1));
    }

private:
    QFile file;
    mz_zip_archive zip{};
    bool opened = false;
    QString lastError;
};

struct WriteContext
{
    QFile *file = nullptr;
    bool failed = false;
};

size_t qfileWrite(void *opaque, mz_uint64, const void *buffer, size_t bytes)
{
    auto *context = static_cast<WriteContext *>(opaque);
    const qint64 written = context->file->write(static_cast<const char *>(buffer),
                                               static_cast<qint64>(bytes));
    if (written != static_cast<qint64>(bytes)) {
        context->failed = true;
        return 0;
    }
    return static_cast<size_t>(written);
}

void setError(QString *target, const QString &message)
{
    if (target)
        *target = message;
}

/// Rejects absolute paths and "..", which could otherwise escape the destination.
bool isSafeRelativePath(const QString &path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1Char('\\')))
        return false;
    if (path.size() > 1 && path.at(1) == QLatin1Char(':'))
        return false;
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QStringLiteral(".."))
            return false;
    }
    return true;
}

} // namespace

namespace ZipUtil {

QByteArray readEntry(const QString &archivePath, const QString &entryName, QString *error)
{
    Archive archive(archivePath);
    if (!archive.isValid()) {
        setError(error, archive.error());
        return QByteArray();
    }

    const int index = mz_zip_reader_locate_file(archive.handle(), entryName.toUtf8().constData(),
                                                nullptr, 0);
    if (index < 0) {
        setError(error, QStringLiteral("%1 wurde im Archiv nicht gefunden").arg(entryName));
        return QByteArray();
    }

    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(archive.handle(), static_cast<mz_uint>(index), &stat)) {
        setError(error, QStringLiteral("%1 kann nicht gelesen werden").arg(entryName));
        return QByteArray();
    }

    QByteArray data;
    data.resize(static_cast<qsizetype>(stat.m_uncomp_size));
    if (!mz_zip_reader_extract_to_mem(archive.handle(), static_cast<mz_uint>(index), data.data(),
                                      static_cast<size_t>(data.size()), 0)) {
        setError(error, QStringLiteral("%1 kann nicht entpackt werden").arg(entryName));
        return QByteArray();
    }
    return data;
}

QStringList entryNames(const QString &archivePath, const QString &prefix, QString *error)
{
    Archive archive(archivePath);
    if (!archive.isValid()) {
        setError(error, archive.error());
        return QStringList();
    }

    QStringList names;
    const mz_uint total = archive.count();
    for (mz_uint i = 0; i < total; ++i) {
        if (mz_zip_reader_is_file_a_directory(archive.handle(), i))
            continue;
        const QString name = archive.entryName(i);
        if (!name.startsWith(prefix))
            continue;
        const QString relative = name.mid(prefix.size());
        if (!relative.isEmpty())
            names.append(relative);
    }
    return names;
}

bool extract(const QString &archivePath, const QString &destDir, const ExtractOptions &opts,
             QString *error)
{
    Archive archive(archivePath);
    if (!archive.isValid()) {
        setError(error, archive.error());
        return false;
    }

    const mz_uint total = archive.count();
    QList<mz_uint> entries;
    qint64 totalBytes = 0;
    for (mz_uint i = 0; i < total; ++i) {
        const QString name = archive.entryName(i);
        if (!name.startsWith(opts.prefix))
            continue;
        if (mz_zip_reader_is_file_a_directory(archive.handle(), i))
            continue;

        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(archive.handle(), i, &stat))
            continue;
        entries.append(i);
        totalBytes += static_cast<qint64>(stat.m_uncomp_size);
    }

    if (opts.onProgress)
        opts.onProgress(0, totalBytes);

    QDir root(destDir);
    qint64 done = 0;
    for (const mz_uint index : entries) {
        if (opts.isCancelled && opts.isCancelled()) {
            setError(error, QStringLiteral("Abgebrochen"));
            return false;
        }

        const QString name = archive.entryName(index);
        const QString relative = name.mid(opts.prefix.size());
        if (!isSafeRelativePath(relative)) {
            setError(error, QStringLiteral("Unsicherer Pfad im Archiv: %1").arg(name));
            return false;
        }

        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(archive.handle(), index, &stat))
            continue;
        const qint64 size = static_cast<qint64>(stat.m_uncomp_size);

        const QString target = root.filePath(relative);
        if (opts.shouldOverwrite && !opts.shouldOverwrite(relative) && QFileInfo::exists(target)) {
            done += size;
            if (opts.onProgress)
                opts.onProgress(done, totalBytes);
            continue;
        }

        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            setError(error, QStringLiteral("Ordner kann nicht erstellt werden: %1")
                                .arg(QFileInfo(target).absolutePath()));
            return false;
        }

        QFile file(target);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            setError(error, QStringLiteral("Datei kann nicht geschrieben werden: %1").arg(target));
            return false;
        }

        WriteContext context;
        context.file = &file;
        const mz_bool extracted = mz_zip_reader_extract_to_callback(archive.handle(), index,
                                                                    qfileWrite, &context, 0);
        const bool flushed = file.flush();
        file.close();

        if (!extracted || context.failed || !flushed) {
            file.remove();
            setError(error,
                     context.failed
                         ? QStringLiteral("Fehler beim Schreiben von %1 (kein Speicherplatz?)").arg(target)
                         : QStringLiteral("%1 kann nicht entpackt werden").arg(name));
            return false;
        }

        done += size;
        if (opts.onProgress)
            opts.onProgress(done, totalBytes);
    }
    return true;
}

QByteArray readFirstEntryFromHead(const QByteArray &head, QString *nameOut, QString *error)
{
    constexpr int kLocalHeaderSize = 30;
    if (head.size() < kLocalHeaderSize || static_cast<quint8>(head.at(0)) != 0x50
        || static_cast<quint8>(head.at(1)) != 0x4b || static_cast<quint8>(head.at(2)) != 0x03
        || static_cast<quint8>(head.at(3)) != 0x04) {
        setError(error, QStringLiteral("Kein ZIP-Archiv"));
        return QByteArray();
    }

    const auto u16 = [&head](int offset) {
        return static_cast<quint32>(static_cast<quint8>(head.at(offset)))
               | (static_cast<quint32>(static_cast<quint8>(head.at(offset + 1))) << 8);
    };
    const auto u32 = [&head](int offset) {
        return static_cast<quint32>(static_cast<quint8>(head.at(offset)))
               | (static_cast<quint32>(static_cast<quint8>(head.at(offset + 1))) << 8)
               | (static_cast<quint32>(static_cast<quint8>(head.at(offset + 2))) << 16)
               | (static_cast<quint32>(static_cast<quint8>(head.at(offset + 3))) << 24);
    };

    const quint32 method = u16(8);
    const quint32 compressedSize = u32(18);
    const quint32 uncompressedSize = u32(22);
    const int nameLength = static_cast<int>(u16(26));
    const int extraLength = static_cast<int>(u16(28));
    const int dataOffset = kLocalHeaderSize + nameLength + extraLength;

    if (head.size() <= dataOffset) {
        setError(error, QStringLiteral("Archivkopf ist zu kurz"));
        return QByteArray();
    }
    if (nameOut)
        *nameOut = QString::fromUtf8(head.constData() + kLocalHeaderSize, nameLength);

    const int available = static_cast<int>(head.size()) - dataOffset;
    if (method == 0) {
        const int size = uncompressedSize > 0 ? static_cast<int>(uncompressedSize) : available;
        if (size > available) {
            setError(error, QStringLiteral("Archivkopf ist zu kurz"));
            return QByteArray();
        }
        return QByteArray(head.constData() + dataOffset, size);
    }
    if (method != MZ_DEFLATED) {
        setError(error, QStringLiteral("Unbekannte Kompressionsmethode %1").arg(method));
        return QByteArray();
    }

    // A truncated deflate stream simply fails to decompress, which is exactly
    // the error we want to report if the requested head was too small.
    const int streamSize = (compressedSize > 0 && static_cast<int>(compressedSize) <= available)
                               ? static_cast<int>(compressedSize)
                               : available;
    size_t outSize = 0;
    void *inflated = tinfl_decompress_mem_to_heap(head.constData() + dataOffset,
                                                  static_cast<size_t>(streamSize), &outSize, 0);
    if (!inflated) {
        setError(error, QStringLiteral("Archivkopf konnte nicht entpackt werden"));
        return QByteArray();
    }
    const QByteArray result(static_cast<const char *>(inflated), static_cast<qsizetype>(outSize));
    mz_free(inflated);
    return result;
}

} // namespace ZipUtil
