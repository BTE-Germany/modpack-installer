#ifndef INSTALLMANIFEST_H
#define INSTALLMANIFEST_H

#include <QHash>
#include <QString>
#include <QStringList>

/// One file the installer wrote into the instance directory.
struct InstalledFile
{
    /// Path relative to the instance directory, always with "/" separators.
    QString path;
    QString sha1;
    qint64 size = 0;
    /// True for files that came out of the modpack archive (overrides), false
    /// for the mods downloaded from the URLs in modrinth.index.json.
    bool fromOverrides = false;

    bool isValid() const { return !path.isEmpty(); }
};

/**
 * Record of what the installer put into an instance directory.
 *
 * The manifest is what separates the modpack from everything the player owns:
 * on an update only files listed here are refreshed or removed, so worlds,
 * screenshots, key bindings, self-added mods and every setting the player (or a
 * mod such as FancyMenu) wrote stay untouched.
 */
class InstallManifest
{
public:
    /// Directory the installer keeps its own bookkeeping in.
    static QString stateDir(const QString &instanceDir);
    static QString filePath(const QString &instanceDir);

    /// Reads the manifest of a previous run; returns an empty manifest if the
    /// instance was installed by an older version of the installer.
    static InstallManifest load(const QString &instanceDir);
    bool save(const QString &instanceDir, QString *error = nullptr) const;

    bool isEmpty() const { return entries.isEmpty(); }
    bool contains(const QString &path) const { return entries.contains(path); }
    InstalledFile entry(const QString &path) const { return entries.value(path); }
    QStringList paths() const { return entries.keys(); }

    void add(const InstalledFile &file);

    QString packName;
    QString packVersion;
    /// Modrinth version id of the installed modpack, if known.
    QString sourceId;
    QString minecraftVersion;
    QString loaderVersion;
    QString installedAt;

private:
    QHash<QString, InstalledFile> entries;
};

#endif // INSTALLMANIFEST_H
