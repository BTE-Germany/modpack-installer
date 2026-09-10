#include "mcpaths.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>

namespace {

constexpr const char *kInstanceDirSetting = "paths/instanceDir";
constexpr const char *kMinecraftDirSetting = "paths/minecraftDir";

QString cleaned(const QString &path)
{
    if (path.trimmed().isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
}

QString configured(const char *key)
{
    const QSettings settings;
    const QString path = cleaned(settings.value(QLatin1String(key)).toString());
    // A relative path would be resolved against the working directory, which is
    // never what the user meant.
    if (path.isEmpty() || !QDir::isAbsolutePath(path))
        return QString();
    return path;
}

void store(const char *key, const QString &path, const QString &defaultPath)
{
    QSettings settings;
    const QString value = cleaned(path);
    if (value.isEmpty() || value == cleaned(defaultPath))
        settings.remove(QLatin1String(key));
    else
        settings.setValue(QLatin1String(key), value);
}

} // namespace

namespace McPaths {

QString defaultDataDir(const QString &name)
{
#ifdef Q_OS_WIN
    const QString appData = QProcessEnvironment::systemEnvironment().value("APPDATA");
    if (!appData.isEmpty())
        return QDir::cleanPath(QDir(appData).filePath("." + name));
    return QDir::cleanPath(QDir(QDir::homePath()).filePath("." + name));
#elif defined(Q_OS_MACOS)
    return QDir::cleanPath(QDir(QDir::homePath()).filePath("Library/Application Support/" + name));
#else
    return QDir::cleanPath(QDir(QDir::homePath()).filePath("." + name));
#endif
}

QString defaultMinecraftDir()
{
    return defaultDataDir(QStringLiteral("minecraft"));
}

QString defaultInstanceDir()
{
    return defaultDataDir(QStringLiteral("btegermany"));
}

QString instanceDir()
{
    const QString path = configured(kInstanceDirSetting);
    return path.isEmpty() ? defaultInstanceDir() : path;
}

QString minecraftDir()
{
    const QString path = configured(kMinecraftDirSetting);
    return path.isEmpty() ? defaultMinecraftDir() : path;
}

void setInstanceDir(const QString &path)
{
    store(kInstanceDirSetting, path, defaultInstanceDir());
}

void setMinecraftDir(const QString &path)
{
    store(kMinecraftDirSetting, path, defaultMinecraftDir());
}

bool usesDefaultInstanceDir()
{
    return configured(kInstanceDirSetting).isEmpty();
}

bool usesDefaultMinecraftDir()
{
    return configured(kMinecraftDirSetting).isEmpty();
}

bool isUsableTarget(const QString &path, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    const QString target = cleaned(path);
    if (target.isEmpty())
        return fail(QStringLiteral("Bitte wähle einen Ordner aus."));
    if (!QDir::isAbsolutePath(target))
        return fail(QStringLiteral("Bitte gib einen vollständigen Pfad an, zum Beispiel %1.")
                        .arg(QDir::toNativeSeparators(defaultInstanceDir())));

    const QFileInfo info(target);
    if (info.exists() && !info.isDir())
        return fail(QStringLiteral("%1 ist eine Datei, kein Ordner.")
                        .arg(QDir::toNativeSeparators(target)));
    if (info.exists() && !info.isWritable())
        return fail(QStringLiteral("In %1 darf nicht geschrieben werden. Wähle einen anderen "
                                   "Ordner.")
                        .arg(QDir::toNativeSeparators(target)));

    if (!info.exists()) {
        // Walk up until an existing directory shows up - that one has to be
        // writable so that the missing folders can be created.
        QDir parent = QFileInfo(target).dir();
        while (!parent.exists() && !parent.isRoot() && parent.cdUp()) {
        }
        if (!parent.exists() || !QFileInfo(parent.absolutePath()).isWritable())
            return fail(QStringLiteral("%1 kann nicht angelegt werden. Wähle einen anderen "
                                       "Ordner.")
                            .arg(QDir::toNativeSeparators(target)));
    }
    return true;
}

bool isInside(const QString &child, const QString &parent)
{
    const QString a = cleaned(child);
    const QString b = cleaned(parent);
    if (a.isEmpty() || b.isEmpty())
        return false;
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    if (a.compare(b, cs) == 0)
        return true;
    return a.startsWith(b.endsWith(QLatin1Char('/')) ? b : b + QLatin1Char('/'), cs);
}

bool isSameDir(const QString &a, const QString &b)
{
    const QString first = cleaned(a);
    const QString second = cleaned(b);
    if (first.isEmpty() || second.isEmpty())
        return false;
#ifdef Q_OS_WIN
    return first.compare(second, Qt::CaseInsensitive) == 0;
#else
    return first == second;
#endif
}

QString cacheDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        base = QDir(QDir::tempPath()).filePath(QStringLiteral("bteginstaller"));
    return QDir(base).filePath(QStringLiteral("modpacks"));
}

} // namespace McPaths
