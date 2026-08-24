#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <QByteArray>
#include <QString>
#include <functional>

/**
 * Thin, blocking HTTP layer on top of cpr. Every call is meant to be made from
 * a worker thread. All file I/O goes through QFile so that installation paths
 * containing non-ASCII characters keep working on Windows.
 */
namespace Http {

/// Called with the number of received and expected bytes. Return false to abort.
using ProgressFn = std::function<bool(qint64 received, qint64 total)>;

struct Options
{
    ProgressFn onProgress;
    qint64 rangeFrom = -1;
    qint64 rangeTo = -1;
    /// Number of additional attempts for network level failures.
    int retries = 2;
};

struct Reply
{
    bool ok = false;
    bool cancelled = false;
    long status = 0;
    QString error;
    QByteArray body;
    qint64 contentLength = -1;

    QString errorString() const;
};

Reply get(const QString &url, const Options &opts = Options());
Reply head(const QString &url);

/**
 * Streams a URL into destPath. The data is written to a temporary file and only
 * moved into place once the transfer completed successfully.
 */
Reply downloadToFile(const QString &url, const QString &destPath, const Options &opts = Options());

/// Human readable byte count, e.g. "12,4 MB"
QString formatBytes(qint64 bytes);

} // namespace Http

#endif // HTTPCLIENT_H
