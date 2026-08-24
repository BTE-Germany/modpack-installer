#include "httpclient.h"

#include <cpr/cpr.h>

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>
#include <QThread>

#include <chrono>
#include <cstdint>

namespace {

constexpr const char *kUserAgent = "BTEGermanyModpackInstaller/3.0 (+https://bte-germany.de)";
constexpr int kConnectTimeoutMs = 20000;
/// Abort a transfer that moved less than 1 byte/s for a minute - a dead
/// connection would otherwise block the installation forever.
constexpr std::int32_t kLowSpeedLimit = 1;
constexpr std::chrono::seconds kLowSpeedTime{60};

void configure(cpr::Session &session, const QString &url, const Http::Options &opts)
{
    session.SetUrl(cpr::Url{url.toStdString()});
    session.SetUserAgent(cpr::UserAgent{kUserAgent});
    session.SetConnectTimeout(cpr::ConnectTimeout{kConnectTimeoutMs});
    session.SetLowSpeed(cpr::LowSpeed{kLowSpeedLimit, kLowSpeedTime});
    session.SetRedirect(cpr::Redirect{3L, true, true, cpr::PostRedirectFlags::POST_ALL});
    session.SetAcceptEncoding(cpr::AcceptEncoding{{cpr::AcceptEncodingMethods::deflate,
                                                  cpr::AcceptEncodingMethods::gzip}});
    if (opts.rangeFrom >= 0)
        session.SetRange(cpr::Range{opts.rangeFrom, opts.rangeTo});
}

bool isSuccess(long status, bool ranged)
{
    return status == 200 || (ranged && status == 206);
}

/// Network errors are worth retrying, a 404 is not.
bool isRetryable(const cpr::Response &response)
{
    if (response.error.code != cpr::ErrorCode::OK)
        return true;
    return response.status_code == 0 || response.status_code == 408 || response.status_code == 429
           || response.status_code >= 500;
}

QString describe(const cpr::Response &response)
{
    if (response.error.code != cpr::ErrorCode::OK && !response.error.message.empty())
        return QString::fromStdString(response.error.message);
    if (response.status_code == 0)
        return QStringLiteral("Keine Verbindung zum Server");
    return QStringLiteral("HTTP %1").arg(response.status_code);
}

} // namespace

namespace Http {

QString Reply::errorString() const
{
    if (ok)
        return QString();
    if (cancelled)
        return QStringLiteral("Abgebrochen");
    return error.isEmpty() ? QStringLiteral("Unbekannter Fehler") : error;
}

Reply get(const QString &url, const Options &opts)
{
    Reply reply;
    const bool ranged = opts.rangeFrom >= 0;

    for (int attempt = 0; attempt <= opts.retries; ++attempt) {
        bool aborted = false;
        cpr::Session session;
        configure(session, url, opts);
        if (opts.onProgress) {
            session.SetProgressCallback(cpr::ProgressCallback{
                [&opts, &aborted](cpr::cpr_pf_arg_t total, cpr::cpr_pf_arg_t now, cpr::cpr_pf_arg_t,
                                  cpr::cpr_pf_arg_t, intptr_t) {
                    if (opts.onProgress(static_cast<qint64>(now), static_cast<qint64>(total)))
                        return true;
                    aborted = true;
                    return false;
                }});
        }

        const cpr::Response response = session.Get();
        if (aborted) {
            reply.cancelled = true;
            reply.error = QStringLiteral("Abgebrochen");
            return reply;
        }

        if (isSuccess(response.status_code, ranged)) {
            reply.ok = true;
            reply.status = response.status_code;
            reply.body = QByteArray(response.text.data(), static_cast<qsizetype>(response.text.size()));
            reply.contentLength = static_cast<qint64>(response.downloaded_bytes);
            return reply;
        }

        reply.status = response.status_code;
        reply.error = describe(response);
        if (attempt == opts.retries || !isRetryable(response))
            break;
        QThread::msleep(500 * (attempt + 1));
    }
    return reply;
}

Reply head(const QString &url)
{
    Reply reply;
    cpr::Session session;
    configure(session, url, Options());
    const cpr::Response response = session.Head();
    reply.status = response.status_code;
    if (response.status_code == 200) {
        reply.ok = true;
        const auto it = response.header.find("Content-Length");
        if (it != response.header.end())
            reply.contentLength = QString::fromStdString(it->second).toLongLong();
    } else {
        reply.error = describe(response);
    }
    return reply;
}

Reply downloadToFile(const QString &url, const QString &destPath, const Options &opts)
{
    Reply reply;
    QDir().mkpath(QFileInfo(destPath).absolutePath());

    for (int attempt = 0; attempt <= opts.retries; ++attempt) {
        QSaveFile file(destPath);
        if (!file.open(QIODevice::WriteOnly)) {
            reply.error = QStringLiteral("Kann Datei nicht schreiben: %1").arg(destPath);
            return reply;
        }

        bool aborted = false;
        bool writeFailed = false;
        qint64 written = 0;

        cpr::Session session;
        configure(session, url, opts);
        session.SetProgressCallback(cpr::ProgressCallback{
            [&opts, &aborted](cpr::cpr_pf_arg_t total, cpr::cpr_pf_arg_t now, cpr::cpr_pf_arg_t,
                              cpr::cpr_pf_arg_t, intptr_t) {
                if (!opts.onProgress)
                    return true;
                if (opts.onProgress(static_cast<qint64>(now), static_cast<qint64>(total)))
                    return true;
                aborted = true;
                return false;
            }});

        const cpr::Response response = session.Download(cpr::WriteCallback{
            [&file, &written, &writeFailed](std::string_view data, intptr_t) {
                const qint64 n = file.write(data.data(), static_cast<qint64>(data.size()));
                if (n != static_cast<qint64>(data.size())) {
                    writeFailed = true;
                    return false;
                }
                written += n;
                return true;
            }});

        if (aborted) {
            file.cancelWriting();
            reply.cancelled = true;
            reply.error = QStringLiteral("Abgebrochen");
            return reply;
        }
        if (writeFailed) {
            file.cancelWriting();
            reply.error = QStringLiteral("Fehler beim Schreiben von %1 (kein Speicherplatz?)").arg(destPath);
            return reply;
        }

        if (isSuccess(response.status_code, opts.rangeFrom >= 0)) {
            if (!file.commit()) {
                reply.error = QStringLiteral("Konnte %1 nicht speichern: %2").arg(destPath, file.errorString());
                return reply;
            }
            reply.ok = true;
            reply.status = response.status_code;
            reply.contentLength = written;
            return reply;
        }

        file.cancelWriting();
        reply.status = response.status_code;
        reply.error = describe(response);
        if (attempt == opts.retries || !isRetryable(response))
            break;
        QThread::msleep(500 * (attempt + 1));
    }
    return reply;
}

QString formatBytes(qint64 bytes)
{
    return QLocale::system().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

} // namespace Http
