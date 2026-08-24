#include "mrpackindex.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

namespace {

constexpr const char *kDisabledSuffix = ".disabled";

/// Mod loaders the Modrinth format may declare, in the order we prefer them.
const QStringList &loaderKeys()
{
    static const QStringList keys{QStringLiteral("fabric-loader"), QStringLiteral("quilt-loader"),
                                  QStringLiteral("neoforge"), QStringLiteral("forge")};
    return keys;
}

} // namespace

QString PackFile::displayName() const
{
    QString name = QFileInfo(path).fileName();
    if (name.endsWith(QLatin1String(kDisabledSuffix), Qt::CaseInsensitive))
        name.chop(static_cast<int>(qstrlen(kDisabledSuffix)));
    if (name.endsWith(QLatin1String(".jar"), Qt::CaseInsensitive))
        name.chop(4);
    return name;
}

QString PackFile::projectKey() const
{
    // https://cdn.modrinth.com/data/<projectId>/versions/<versionId>/<file>
    for (const QString &url : downloads) {
        const QStringList parts = QUrl(url).path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        const int marker = parts.indexOf(QStringLiteral("data"));
        if (marker >= 0 && marker + 1 < parts.size())
            return parts.at(marker + 1);
    }
    return displayName();
}

QString PackFile::targetPath(bool enabled) const
{
    if (!enabled || !path.endsWith(QLatin1String(kDisabledSuffix), Qt::CaseInsensitive))
        return path;
    QString target = path;
    target.chop(static_cast<int>(qstrlen(kDisabledSuffix)));
    return target;
}

MrpackIndex MrpackIndex::parse(const QByteArray &json, QString *error)
{
    MrpackIndex index;

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("modrinth.index.json ist ungültig: %1").arg(parseError.errorString());
        return index;
    }

    const QJsonObject root = document.object();
    index.packName = root.value(QStringLiteral("name")).toString();
    index.packVersion = root.value(QStringLiteral("versionId")).toString();

    const QJsonObject dependencies = root.value(QStringLiteral("dependencies")).toObject();
    index.mcVersion = dependencies.value(QStringLiteral("minecraft")).toString();
    for (const QString &key : loaderKeys()) {
        const QString value = dependencies.value(key).toString();
        if (!value.isEmpty()) {
            index.loader = key;
            index.loaderVer = value;
            break;
        }
    }

    if (index.mcVersion.isEmpty()) {
        if (error)
            *error = QStringLiteral("Im Modpack ist keine Minecraft-Version angegeben.");
        return index;
    }
    if (index.loader != QStringLiteral("fabric-loader")) {
        if (error) {
            *error = index.loader.isEmpty()
                         ? QStringLiteral("Im Modpack ist kein Mod-Loader angegeben.")
                         : QStringLiteral("Der Mod-Loader \"%1\" wird von diesem Installer nicht "
                                          "unterstützt.")
                               .arg(index.loader);
        }
        return index;
    }

    const QJsonArray files = root.value(QStringLiteral("files")).toArray();
    index.packFiles.reserve(files.size());
    for (const QJsonValue &value : files) {
        const QJsonObject entry = value.toObject();

        PackFile file;
        file.path = entry.value(QStringLiteral("path")).toString();
        file.size = static_cast<qint64>(entry.value(QStringLiteral("fileSize")).toDouble());

        const QJsonObject hashes = entry.value(QStringLiteral("hashes")).toObject();
        file.sha1 = hashes.value(QStringLiteral("sha1")).toString();
        file.sha512 = hashes.value(QStringLiteral("sha512")).toString();

        for (const QJsonValue &url : entry.value(QStringLiteral("downloads")).toArray()) {
            const QString text = url.toString();
            if (!text.isEmpty())
                file.downloads.append(text);
        }

        const QString clientEnv = entry.value(QStringLiteral("env"))
                                      .toObject()
                                      .value(QStringLiteral("client"))
                                      .toString(QStringLiteral("required"));
        file.clientSupported = clientEnv != QStringLiteral("unsupported");
        file.optional = clientEnv == QStringLiteral("optional");

        if (file.isValid())
            index.packFiles.append(file);
    }

    index.valid = true;
    return index;
}

QList<PackFile> MrpackIndex::optionalFiles() const
{
    QList<PackFile> result;
    for (const PackFile &file : packFiles) {
        if (file.clientSupported && file.optional)
            result.append(file);
    }
    return result;
}
