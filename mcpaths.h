#ifndef MCPATHS_H
#define MCPATHS_H

#include <QString>

/**
 * Resolves the well known Minecraft directories in a platform specific way,
 * mirroring the layout the official launcher uses.
 */
namespace McPaths {

/// Directory of a Minecraft-style instance, e.g. "btegermany" -> %APPDATA%/.btegermany
QString instanceDir(const QString &name);

/// The vanilla launcher directory (contains versions/ and launcher_profiles.json)
QString minecraftDir();

/// The modpack is installed side by side with the vanilla directory
QString bteGermanyDir();

/// Directory used to cache downloaded modpack archives between runs
QString cacheDir();

} // namespace McPaths

#endif // MCPATHS_H
