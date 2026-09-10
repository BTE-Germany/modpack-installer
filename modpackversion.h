#ifndef MODPACKVERSION_H
#define MODPACKVERSION_H

#include <QDateTime>
#include <QMetaType>
#include <QString>

/**
 * One installable modpack version.
 *
 * Versions normally come from the Modrinth API, which also reports the release
 * channel and the hashes of the archive. The CDN manifest is only used as a
 * fallback and fills in the first four fields, so everything else is optional.
 */
struct ModpackVersion
{
    enum class Channel { Release, Beta, Alpha };

    /// Version name shown in the version box, e.g. "v1.4.1".
    QString name;
    QString minecraftVersion;
    /// Marks the version the installer preselects.
    bool latest = false;
    QString downloadUrl;

    Channel channel = Channel::Release;
    /// Modrinth version id; empty for versions from the CDN manifest.
    QString sourceId;
    QString sha1;
    QString sha512;
    qint64 fileSize = 0;
    QDateTime published;

    bool isValid() const { return !downloadUrl.isEmpty(); }

    /// File name the downloaded archive is cached under. Modrinth reuses the
    /// same file name for every version, so the version id has to be part of it.
    QString cacheFileName() const;

    /// Localised channel name, empty for a regular release.
    QString channelLabel() const;
};

Q_DECLARE_METATYPE(ModpackVersion)

#endif // MODPACKVERSION_H
