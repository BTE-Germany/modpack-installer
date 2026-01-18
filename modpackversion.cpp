#include "modpackversion.h"


ModpackVersion::ModpackVersion(const QString &name, const QString &minecraftVersion, bool isLatest, const QString &downloadUrl) : name(name),
    minecraftVersion(minecraftVersion),
    isLatest(isLatest),
    downloadUrl(downloadUrl)
{}

QString ModpackVersion::getName() const
{
    return name;
}

QString ModpackVersion::getMinecraftVersion() const
{
    return minecraftVersion;
}

bool ModpackVersion::getIsLatest() const
{
    return isLatest;
}

QString ModpackVersion::getDownloadUrl() const
{
    return downloadUrl;
}
