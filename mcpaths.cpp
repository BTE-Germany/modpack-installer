#include "mcpaths.h"

#include <QDir>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace McPaths {

QString instanceDir(const QString &name)
{
#ifdef Q_OS_WIN
    const QString appData = QProcessEnvironment::systemEnvironment().value("APPDATA");
    if (!appData.isEmpty())
        return QDir(appData).filePath("." + name);
    return QDir(QDir::homePath()).filePath("." + name);
#elif defined(Q_OS_MACOS)
    return QDir(QDir::homePath()).filePath("Library/Application Support/" + name);
#else
    return QDir(QDir::homePath()).filePath("." + name);
#endif
}

QString minecraftDir()
{
    return instanceDir("minecraft");
}

QString bteGermanyDir()
{
    return instanceDir("btegermany");
}

QString cacheDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        base = QDir(QDir::tempPath()).filePath("bteginstaller");
    return QDir(base).filePath("modpacks");
}

} // namespace McPaths
