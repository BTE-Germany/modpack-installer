#include "launchersetup.h"

#include "httpclient.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace {

constexpr const char *kProfileKey = "BTE Germany";
constexpr const char *kFabricMetaUrl = "https://meta.fabricmc.net/v2/versions/loader/%1/%2/profile/json";

/// Same JVM tuning the previous installer generation used.
constexpr const char *kJavaArgs =
    "-Xss4M -Xmx8G -XX:+DisableExplicitGC -XX:+UnlockExperimentalVMOptions -XX:+UseG1GC "
    "-XX:G1NewSizePercent=20 -XX:G1ReservePercent=20 -XX:MaxGCPauseMillis=30 "
    "-XX:G1HeapRegionSize=32M -Duser.country=US -Duser.language=en";

void setError(QString *target, const QString &message)
{
    if (target)
        *target = message;
}

QString timestamp()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ"));
}

/// The launcher accepts data URIs as profile icons, so the app logo is reused.
QString profileIcon()
{
    QFile logo(QStringLiteral(":/bte_images/assets/logo.png"));
    if (!logo.open(QIODevice::ReadOnly))
        return QStringLiteral("Furnace");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(logo.readAll().toBase64());
}

bool writeJson(const QString &path, const QJsonDocument &document, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("%1 kann nicht geschrieben werden: %2").arg(path, file.errorString()));
        return false;
    }
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        setError(error, QStringLiteral("%1 kann nicht gespeichert werden: %2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

} // namespace

namespace LauncherSetup {

QString installFabricVersion(const QString &minecraftDir, const QString &minecraftVersion,
                             const QString &loaderVersion, QString *error)
{
    const QString url = QString::fromLatin1(kFabricMetaUrl).arg(minecraftVersion, loaderVersion);
    const Http::Reply reply = Http::get(url);
    if (!reply.ok) {
        setError(error, QStringLiteral("Fabric %1 für Minecraft %2 konnte nicht geladen werden (%3).")
                            .arg(loaderVersion, minecraftVersion, reply.errorString()));
        return QString();
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(reply.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("Fabric-Profil ist ungültig: %1").arg(parseError.errorString()));
        return QString();
    }

    const QString versionId = document.object().value(QStringLiteral("id")).toString();
    if (versionId.isEmpty()) {
        setError(error, QStringLiteral("Fabric-Profil enthält keine Version."));
        return QString();
    }

    const QString versionDir = QDir(minecraftDir).filePath(QStringLiteral("versions/") + versionId);
    if (!QDir().mkpath(versionDir)) {
        setError(error, QStringLiteral("Ordner kann nicht erstellt werden: %1").arg(versionDir));
        return QString();
    }

    const QString target = QDir(versionDir).filePath(versionId + QStringLiteral(".json"));
    if (!writeJson(target, document, error))
        return QString();
    return versionId;
}

bool writeLauncherProfile(const QString &minecraftDir, const QString &instanceDir,
                          const QString &versionId, const QString &profileName, QString *error)
{
    if (!QDir().mkpath(minecraftDir)) {
        setError(error, QStringLiteral("Minecraft-Ordner kann nicht erstellt werden: %1").arg(minecraftDir));
        return false;
    }

    const QString path = QDir(minecraftDir).filePath(QStringLiteral("launcher_profiles.json"));
    QJsonObject root;

    QFile file(path);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            setError(error, QStringLiteral("launcher_profiles.json kann nicht gelesen werden: %1")
                                .arg(file.errorString()));
            return false;
        }
        const QByteArray content = file.readAll();
        file.close();

        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(content, &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            root = document.object();
            // Keep the previous state around in case something goes wrong.
            const QString backup = path + QStringLiteral(".bak");
            QFile::remove(backup);
            QFile::copy(path, backup);
        }
    }

    if (!root.contains(QStringLiteral("version")))
        root.insert(QStringLiteral("version"), 3);
    if (!root.contains(QStringLiteral("settings")))
        root.insert(QStringLiteral("settings"), QJsonObject());

    QJsonObject profiles = root.value(QStringLiteral("profiles")).toObject();
    QJsonObject profile = profiles.value(QLatin1String(kProfileKey)).toObject();

    if (!profile.contains(QStringLiteral("created")))
        profile.insert(QStringLiteral("created"), timestamp());
    profile.insert(QStringLiteral("lastUsed"), timestamp());
    profile.insert(QStringLiteral("gameDir"), QDir::toNativeSeparators(instanceDir));
    profile.insert(QStringLiteral("icon"), profileIcon());
    profile.insert(QStringLiteral("lastVersionId"), versionId);
    profile.insert(QStringLiteral("name"), profileName);
    profile.insert(QStringLiteral("type"), QStringLiteral("custom"));
    // Only set the memory arguments once so that user tweaks survive updates.
    if (!profile.contains(QStringLiteral("javaArgs")))
        profile.insert(QStringLiteral("javaArgs"), QLatin1String(kJavaArgs));

    profiles.insert(QLatin1String(kProfileKey), profile);
    root.insert(QStringLiteral("profiles"), profiles);

    return writeJson(path, QJsonDocument(root), error);
}

} // namespace LauncherSetup
