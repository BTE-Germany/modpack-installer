#include "modrinthapi.h"

#include "httpclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QUrl>

#include <algorithm>

namespace {

constexpr const char *kApiBase = "https://api.modrinth.com/v2/project/";
/// ?loaders=["fabric"] - the installer can only set up Fabric profiles.
constexpr const char *kVersionsPath = "/version?loaders=%5B%22fabric%22%5D";

constexpr const char *kMrpackSuffix = ".mrpack";

void setError(QString *target, const QString &message)
{
    if (target)
        *target = message;
}

ModpackVersion::Channel channelFor(const QString &versionType)
{
    if (versionType == QStringLiteral("beta"))
        return ModpackVersion::Channel::Beta;
    if (versionType == QStringLiteral("alpha"))
        return ModpackVersion::Channel::Alpha;
    return ModpackVersion::Channel::Release;
}

/// The modpack archive of a version, preferring the file marked as primary.
QJsonObject packFileOf(const QJsonArray &files)
{
    QJsonObject fallback;
    for (const QJsonValue &value : files) {
        const QJsonObject file = value.toObject();
        const QString name = file.value(QStringLiteral("filename")).toString();
        if (!name.endsWith(QLatin1String(kMrpackSuffix), Qt::CaseInsensitive))
            continue;
        if (file.value(QStringLiteral("primary")).toBool())
            return file;
        if (fallback.isEmpty())
            fallback = file;
    }
    return fallback;
}

/// Highest Minecraft version of an entry; the API sorts them by release date.
QString minecraftVersionOf(const QJsonArray &gameVersions)
{
    QString result;
    for (const QJsonValue &value : gameVersions) {
        const QString version = value.toString();
        if (!version.isEmpty())
            result = version;
    }
    return result;
}

QString displayNameOf(const QJsonObject &entry)
{
    QString name = entry.value(QStringLiteral("version_number")).toString();
    if (name.isEmpty())
        name = entry.value(QStringLiteral("name")).toString();
    if (!name.isEmpty() && name.at(0).isDigit())
        name.prepend(QLatin1Char('v'));
    return name;
}

ModpackVersion parseVersion(const QJsonObject &entry)
{
    ModpackVersion version;

    // Drafts, scheduled and archived versions are not meant to be installed,
    // and an unlisted one was hidden on purpose.
    const QString status = entry.value(QStringLiteral("status")).toString();
    if (!status.isEmpty() && status != QStringLiteral("listed"))
        return version;

    const QJsonObject file = packFileOf(entry.value(QStringLiteral("files")).toArray());
    if (file.isEmpty())
        return version;

    version.name = displayNameOf(entry);
    version.minecraftVersion = minecraftVersionOf(entry.value(QStringLiteral("game_versions")).toArray());
    version.channel = channelFor(entry.value(QStringLiteral("version_type")).toString());
    version.sourceId = entry.value(QStringLiteral("id")).toString();
    version.published = QDateTime::fromString(entry.value(QStringLiteral("date_published")).toString(),
                                              Qt::ISODateWithMs);

    version.downloadUrl = file.value(QStringLiteral("url")).toString();
    version.fileSize = static_cast<qint64>(file.value(QStringLiteral("size")).toDouble());
    const QJsonObject hashes = file.value(QStringLiteral("hashes")).toObject();
    version.sha1 = hashes.value(QStringLiteral("sha1")).toString();
    version.sha512 = hashes.value(QStringLiteral("sha512")).toString();

    if (version.name.isEmpty() || version.minecraftVersion.isEmpty())
        return ModpackVersion();
    return version;
}

} // namespace

namespace Modrinth {

QList<ModpackVersion> fetchVersions(const QString &project, QString *error)
{
    const QString url = QString::fromLatin1(kApiBase) + QString::fromUtf8(QUrl::toPercentEncoding(project))
                        + QString::fromLatin1(kVersionsPath);
    const Http::Reply reply = Http::get(url);
    if (!reply.ok) {
        setError(error, QStringLiteral("Die Modpack-Versionen konnten nicht von Modrinth geladen "
                                       "werden (%1).")
                            .arg(reply.errorString()));
        return {};
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(reply.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        setError(error, QStringLiteral("Die Antwort von Modrinth ist ungültig: %1")
                            .arg(parseError.errorString()));
        return {};
    }

    QList<ModpackVersion> versions;
    QSet<QString> seenIds;
    const QJsonArray entries = document.array();
    for (const QJsonValue &value : entries) {
        const ModpackVersion version = parseVersion(value.toObject());
        if (!version.isValid())
            continue;
        if (!version.sourceId.isEmpty() && seenIds.contains(version.sourceId))
            continue;
        seenIds.insert(version.sourceId);
        versions.append(version);
    }

    if (versions.isEmpty()) {
        setError(error, QStringLiteral("Für dieses Modpack ist auf Modrinth keine installierbare "
                                       "Version veröffentlicht."));
        return {};
    }

    // Newest first, so the preselected entry is the most recent release.
    std::stable_sort(versions.begin(), versions.end(),
                     [](const ModpackVersion &a, const ModpackVersion &b) {
                         return a.published > b.published;
                     });

    // Pre-releases are only offered if there is nothing stable to install.
    const bool hasRelease = std::any_of(versions.cbegin(), versions.cend(),
                                        [](const ModpackVersion &version) {
                                            return version.channel
                                                   == ModpackVersion::Channel::Release;
                                        });
    if (hasRelease) {
        const auto isPreRelease = [](const ModpackVersion &version) {
            return version.channel != ModpackVersion::Channel::Release;
        };
        versions.erase(std::remove_if(versions.begin(), versions.end(), isPreRelease),
                       versions.end());
    }

    versions.first().latest = true;
    return versions;
}

} // namespace Modrinth
