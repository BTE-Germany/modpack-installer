#ifndef MODRINTHAPI_H
#define MODRINTHAPI_H

#include "modpackversion.h"

#include <QList>
#include <QString>

/**
 * Thin client for the parts of the Modrinth API the installer needs. The
 * modpack versions are read straight from the project, so a new release only
 * has to be published on Modrinth and shows up in the installer automatically.
 */
namespace Modrinth {

/// Project the installer offers; accepts the slug as well as the project id.
inline constexpr const char *kModpackProject = "bte-germany-modpack";

/**
 * Loads the installable versions of a project, newest first. Only versions
 * with a Fabric .mrpack file are returned; pre-releases are left out as long as
 * there is at least one regular release.
 *
 * Returns an empty list and sets error on failure.
 */
QList<ModpackVersion> fetchVersions(const QString &project, QString *error = nullptr);

} // namespace Modrinth

#endif // MODRINTHAPI_H
