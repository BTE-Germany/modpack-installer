#ifndef MCPATHS_H
#define MCPATHS_H

#include <QString>

/**
 * Resolves the Minecraft directories, mirroring the layout the official
 * launcher uses. Both the modpack directory and the launcher directory can be
 * moved somewhere else by the user; the choice is stored in QSettings and read
 * back here.
 */
namespace McPaths {

/// Platform default of a Minecraft-style directory, e.g. "minecraft" ->
/// %APPDATA%/.minecraft (or the platform equivalent).
QString defaultDataDir(const QString &name);

/// Default location of the vanilla launcher directory (contains versions/ and
/// launcher_profiles.json).
QString defaultMinecraftDir();
/// Default location the modpack is installed to. For the Minecraft Flatpak it
/// is inside .minecraft; otherwise it remains the traditional ~/.btegermany.
QString defaultInstanceDir();

/// Directory the modpack is installed into (mods, configs, worlds, ...).
QString instanceDir();
/// Launcher directory the version manifest and the profile are written to.
QString minecraftDir();

/// Stores a location; an empty or default path falls back to the default again.
void setInstanceDir(const QString &path);
void setMinecraftDir(const QString &path);

bool usesDefaultInstanceDir();
bool usesDefaultMinecraftDir();

/**
 * Checks whether a directory can be used as a target: the path has to be
 * absolute and either be a writable directory or have a parent the installer
 * may create it in. Returns a ready to show message on failure.
 */
bool isUsableTarget(const QString &path, QString *error = nullptr);

/// True if child is inside parent (or the same directory).
bool isInside(const QString &child, const QString &parent);
/// True if both paths point at the same directory.
bool isSameDir(const QString &a, const QString &b);

/// Directory used to cache downloaded modpack archives between runs
QString cacheDir();

/**
 * Starts the official Minecraft launcher when it can be found.
 *
 * Covers the common desktop installs (Windows paths and Store app, macOS
 * Minecraft.app, Linux PATH / Flatpak). Returns false with a ready-to-show
 * message when nothing usable is available.
 */
bool startMinecraftLauncher(QString *error = nullptr);

} // namespace McPaths

#endif // MCPATHS_H
