#include "modpackversion.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QUrl>

namespace {

/// Keeps the cache file name usable on every platform.
QString sanitize(const QString &text)
{
    QString result;
    result.reserve(text.size());
    for (const QChar c : text) {
        if (c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('-')
            || c == QLatin1Char('_'))
            result.append(c);
        else
            result.append(QLatin1Char('_'));
    }
    return result;
}

} // namespace

QString ModpackVersion::cacheFileName() const
{
    QString suffix = QFileInfo(QUrl(downloadUrl).path()).fileName();
    if (!suffix.endsWith(QLatin1String(".mrpack"), Qt::CaseInsensitive))
        suffix = QStringLiteral("modpack.mrpack");

    // The id keeps versions apart, the URL hash does the same job for the CDN
    // manifest, which has no ids.
    QString id = sourceId;
    if (id.isEmpty()) {
        id = QString::fromLatin1(
            QCryptographicHash::hash(downloadUrl.toUtf8(), QCryptographicHash::Sha1)
                .toHex()
                .left(10));
    }
    return sanitize(id) + QLatin1Char('-') + sanitize(suffix);
}

QString ModpackVersion::channelLabel() const
{
    switch (channel) {
    case Channel::Beta:
        return QStringLiteral("Beta");
    case Channel::Alpha:
        return QStringLiteral("Alpha");
    case Channel::Release:
        break;
    }
    return QString();
}
