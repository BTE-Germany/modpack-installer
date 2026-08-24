#ifndef LAUNCHERSETUP_H
#define LAUNCHERSETUP_H

#include <QString>

/**
 * Integrates the modpack with the official Minecraft launcher: installs the
 * Fabric version manifest and registers a launcher profile that points at the
 * modpack directory.
 */
namespace LauncherSetup {

/**
 * Fetches the Fabric launcher profile from meta.fabricmc.net and writes it to
 * <minecraftDir>/versions/<id>/<id>.json. The libraries themselves are resolved
 * by the launcher, so nothing else has to be downloaded here.
 *
 * Returns the version id (e.g. "fabric-loader-0.18.4-1.21.10") or an empty
 * string on failure.
 */
QString installFabricVersion(const QString &minecraftDir, const QString &minecraftVersion,
                             const QString &loaderVersion, QString *error = nullptr);

/**
 * Creates or updates the "BTE Germany" entry in launcher_profiles.json. All
 * other profiles and settings are preserved, and a backup of the previous file
 * is kept next to it.
 */
bool writeLauncherProfile(const QString &minecraftDir, const QString &instanceDir,
                          const QString &versionId, const QString &profileName,
                          QString *error = nullptr);

} // namespace LauncherSetup

#endif // LAUNCHERSETUP_H
