#include "installmanifest.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace {

constexpr const char *kStateDirName = ".bteg-installer";
constexpr const char *kManifestName = "manifest.json";
/// Bumped whenever the layout below changes incompatibly.
constexpr int kFormatVersion = 1;

void setError(QString *target, const QString &message)
{
    if (target)
        *target = message;
}

} // namespace

QString InstallManifest::stateDir(const QString &instanceDir)
{
    return QDir(instanceDir).filePath(QLatin1String(kStateDirName));
}

QString InstallManifest::filePath(const QString &instanceDir)
{
    return QDir(stateDir(instanceDir)).filePath(QLatin1String(kManifestName));
}

InstallManifest InstallManifest::load(const QString &instanceDir)
{
    InstallManifest manifest;

    QFile file(filePath(instanceDir));
    if (!file.open(QIODevice::ReadOnly))
        return manifest;

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return manifest;

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("formatVersion")).toInt() > kFormatVersion)
        return manifest;

    manifest.packName = root.value(QStringLiteral("packName")).toString();
    manifest.packVersion = root.value(QStringLiteral("packVersion")).toString();
    manifest.sourceId = root.value(QStringLiteral("sourceId")).toString();
    manifest.minecraftVersion = root.value(QStringLiteral("minecraftVersion")).toString();
    manifest.loaderVersion = root.value(QStringLiteral("loaderVersion")).toString();
    manifest.installedAt = root.value(QStringLiteral("installedAt")).toString();

    for (const QJsonValue &value : root.value(QStringLiteral("files")).toArray()) {
        const QJsonObject entry = value.toObject();
        InstalledFile file;
        file.path = entry.value(QStringLiteral("path")).toString();
        file.sha1 = entry.value(QStringLiteral("sha1")).toString();
        file.size = static_cast<qint64>(entry.value(QStringLiteral("size")).toDouble());
        file.fromOverrides = entry.value(QStringLiteral("overrides")).toBool();
        manifest.add(file);
    }
    return manifest;
}

bool InstallManifest::save(const QString &instanceDir, QString *error) const
{
    const QString dir = stateDir(instanceDir);
    if (!QDir().mkpath(dir)) {
        setError(error, QStringLiteral("Ordner kann nicht erstellt werden: %1").arg(dir));
        return false;
    }

    QJsonArray files;
    // Sorted, so that consecutive installations produce comparable files.
    QStringList sorted = entries.keys();
    sorted.sort();
    for (const QString &path : sorted) {
        const InstalledFile &file = entries.value(path);
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), file.path);
        if (!file.sha1.isEmpty())
            entry.insert(QStringLiteral("sha1"), file.sha1);
        entry.insert(QStringLiteral("size"), static_cast<double>(file.size));
        if (file.fromOverrides)
            entry.insert(QStringLiteral("overrides"), true);
        files.append(entry);
    }

    QJsonObject root;
    root.insert(QStringLiteral("formatVersion"), kFormatVersion);
    root.insert(QStringLiteral("packName"), packName);
    root.insert(QStringLiteral("packVersion"), packVersion);
    root.insert(QStringLiteral("sourceId"), sourceId);
    root.insert(QStringLiteral("minecraftVersion"), minecraftVersion);
    root.insert(QStringLiteral("loaderVersion"), loaderVersion);
    root.insert(QStringLiteral("installedAt"),
                installedAt.isEmpty()
                    ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate)
                    : installedAt);
    root.insert(QStringLiteral("files"), files);

    const QString path = filePath(instanceDir);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("%1 kann nicht geschrieben werden: %2")
                            .arg(path, file.errorString()));
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        setError(error, QStringLiteral("%1 kann nicht gespeichert werden: %2")
                            .arg(path, file.errorString()));
        return false;
    }
    return true;
}

void InstallManifest::add(const InstalledFile &file)
{
    if (file.isValid())
        entries.insert(file.path, file);
}
