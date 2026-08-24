#ifndef ZIPUTIL_H
#define ZIPUTIL_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>

/**
 * Minimal ZIP reading helpers built on miniz. Archives are read through QFile,
 * so paths with non-ASCII characters work on every platform.
 */
namespace ZipUtil {

using ProgressFn = std::function<void(qint64 done, qint64 total)>;
using CancelFn = std::function<bool()>;

struct ExtractOptions
{
    /// Only entries starting with this prefix are extracted; the prefix itself
    /// is stripped from the resulting path.
    QString prefix;
    /// Asked before an already existing file is replaced. Without a callback
    /// every file is overwritten.
    std::function<bool(const QString &relativePath)> shouldOverwrite;
    ProgressFn onProgress;
    CancelFn isCancelled;
};

/// Reads a single entry completely into memory.
QByteArray readEntry(const QString &archivePath, const QString &entryName, QString *error = nullptr);

/// Relative paths of all files (directories excluded) below prefix.
QStringList entryNames(const QString &archivePath, const QString &prefix, QString *error = nullptr);

/// Extracts everything below opts.prefix into destDir.
bool extract(const QString &archivePath, const QString &destDir, const ExtractOptions &opts,
             QString *error = nullptr);

/**
 * Inflates the first entry of a ZIP file from a partial (head) download. Used to
 * read modrinth.index.json without transferring the whole modpack archive.
 */
QByteArray readFirstEntryFromHead(const QByteArray &head, QString *nameOut = nullptr,
                                  QString *error = nullptr);

} // namespace ZipUtil

#endif // ZIPUTIL_H
