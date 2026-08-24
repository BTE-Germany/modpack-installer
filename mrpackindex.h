#ifndef MRPACKINDEX_H
#define MRPACKINDEX_H

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

/// Name of the index file inside a Modrinth modpack archive.
inline constexpr const char *kMrpackIndexEntry = "modrinth.index.json";

/**
 * A single file referenced by modrinth.index.json. Files are not part of the
 * archive - they are downloaded from the URLs listed here.
 */
struct PackFile
{
    /// Path relative to the instance directory, e.g. "mods/sodium.jar".
    /// Optional mods use packwiz's ".disabled" suffix so that the game ignores
    /// them until the user opts in.
    QString path;
    QString sha1;
    QString sha512;
    qint64 size = 0;
    QStringList downloads;
    bool optional = false;
    bool clientSupported = true;

    /// File name without the ".disabled" marker and without the ".jar" suffix.
    QString displayName() const;
    /// Stable identity of the mod across modpack versions, taken from the
    /// Modrinth project id in the download URL. Used to remember the user's
    /// choice for optional mods.
    QString projectKey() const;
    /// Target path; enabling an optional mod means dropping the ".disabled".
    QString targetPath(bool enabled) const;
    bool isValid() const { return !path.isEmpty() && !downloads.isEmpty(); }
};

/**
 * Parsed modrinth.index.json (format version 1).
 */
class MrpackIndex
{
public:
    static MrpackIndex parse(const QByteArray &json, QString *error = nullptr);

    bool isValid() const { return valid; }

    QString name() const { return packName; }
    QString versionId() const { return packVersion; }
    QString minecraftVersion() const { return mcVersion; }
    QString loaderId() const { return loader; }
    QString loaderVersion() const { return loaderVer; }

    const QList<PackFile> &files() const { return packFiles; }
    QList<PackFile> optionalFiles() const;

private:
    bool valid = false;
    QString packName;
    QString packVersion;
    QString mcVersion;
    QString loader;
    QString loaderVer;
    QList<PackFile> packFiles;
};

Q_DECLARE_METATYPE(PackFile)

#endif // MRPACKINDEX_H
