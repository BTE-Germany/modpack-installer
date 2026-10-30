#include <QCoreApplication>

class McPathsTr
{
    Q_DECLARE_TR_FUNCTIONS(McPaths)
};

#include "mcpaths.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

#if defined(Q_OS_WIN)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

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
#if defined(Q_OS_LINUX)
    const QString flatpak = QDir(QDir::homePath())
                                .filePath(QStringLiteral(".var/app/com.mojang.Minecraft/.minecraft"));
    if (QFileInfo(flatpak).isDir())
        return QDir::cleanPath(flatpak);
#endif
    return defaultDataDir(QStringLiteral("minecraft"));
}

QString defaultInstanceDir()
{
#if defined(Q_OS_LINUX)
    const QString flatpak = QDir(QDir::homePath())
                                .filePath(QStringLiteral(".var/app/com.mojang.Minecraft/.minecraft"));
    if (QFileInfo(flatpak).isDir()) {
        // The Flatpak launcher only discovers this game directory reliably
        // when it is located inside its sandboxed .minecraft directory.
        return QDir::cleanPath(QDir(flatpak).filePath(QStringLiteral(".btegermany")));
    }
#endif
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
        return fail(McPathsTr::tr("Bitte wähle einen Ordner aus."));
    if (!QDir::isAbsolutePath(target))
        return fail(McPathsTr::tr("Bitte gib einen vollständigen Pfad an, zum Beispiel %1.")
                        .arg(QDir::toNativeSeparators(defaultInstanceDir())));

    const QFileInfo info(target);
    if (info.exists() && !info.isDir())
        return fail(McPathsTr::tr("%1 ist eine Datei, kein Ordner.")
                        .arg(QDir::toNativeSeparators(target)));
    if (info.exists() && !info.isWritable())
        return fail(McPathsTr::tr("In %1 darf nicht geschrieben werden. Wähle einen anderen Ordner.")
                        .arg(QDir::toNativeSeparators(target)));

    if (!info.exists()) {
        // Walk up until an existing directory shows up - that one has to be
        // writable so that the missing folders can be created.
        QDir parent = QFileInfo(target).dir();
        while (!parent.exists() && !parent.isRoot() && parent.cdUp()) {
        }
        if (!parent.exists() || !QFileInfo(parent.absolutePath()).isWritable())
            return fail(McPathsTr::tr("%1 kann nicht angelegt werden. Wähle einen anderen Ordner.")
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

namespace {

/// Starts a process that outlives this installer (new session / detached group).
bool startDetached(const QString &program, const QStringList &arguments = {})
{
    if (program.isEmpty())
        return false;

    QProcess process;
#if defined(Q_OS_LINUX)
    // setsid gives the child its own session so it is not killed when the
    // installer process group disappears.
    QStringList args{program};
    args.append(arguments);
    process.setProgram(QStringLiteral("setsid"));
    process.setArguments(args);
#else
    process.setProgram(program);
    process.setArguments(arguments);
#endif
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
#if defined(Q_OS_WIN)
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS;
        args->inheritHandles = false;
    });
#endif
    return process.startDetached();
}

bool isExecutableFile(const QString &path)
{
    const QFileInfo info(path);
    return info.isFile()
#ifdef Q_OS_UNIX
           && info.isExecutable()
#endif
        ;
}

bool startFirstExisting(const QStringList &candidates)
{
    for (const QString &path : candidates) {
        if (!isExecutableFile(path))
            continue;
        if (startDetached(path))
            return true;
    }
    return false;
}

#if defined(Q_OS_LINUX)
bool flatpakMinecraftInstalled()
{
    QProcess process;
    process.start(QStringLiteral("flatpak"),
                  {QStringLiteral("info"), QStringLiteral("com.mojang.Minecraft")});
    if (!process.waitForFinished(3000)) {
        process.kill();
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool startFlatpakMinecraft()
{
    if (!flatpakMinecraftInstalled())
        return false;
    return startDetached(QStringLiteral("flatpak"),
                         {QStringLiteral("run"), QStringLiteral("com.mojang.Minecraft")});
}
#endif

} // namespace

bool startMinecraftLauncher(QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

#if defined(Q_OS_WIN)
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString localAppData = env.value(QStringLiteral("LOCALAPPDATA"));
    const QString programFilesX86 = env.value(QStringLiteral("ProgramFiles(x86)"));
    const QString programFiles = env.value(QStringLiteral("ProgramFiles"));

    QStringList candidates;
    const auto add = [&candidates](const QString &base, const QString &relative) {
        if (!base.isEmpty())
            candidates.append(QDir(base).filePath(relative));
    };
    add(localAppData, QStringLiteral("Programs/Minecraft Launcher/MinecraftLauncher.exe"));
    add(programFilesX86, QStringLiteral("Minecraft Launcher/MinecraftLauncher.exe"));
    add(programFiles, QStringLiteral("Minecraft Launcher/MinecraftLauncher.exe"));
    candidates.append(QStandardPaths::findExecutable(QStringLiteral("MinecraftLauncher")));

    if (startFirstExisting(candidates))
        return true;

    // Microsoft Store / Xbox install of the Java launcher.
    constexpr const char *kStoreFamily = "Microsoft.4297127D64EC6_8wekyb3d8bbwe";
    const QString storePackage =
        QDir(localAppData).filePath(QStringLiteral("Packages/") + QLatin1String(kStoreFamily));
    if (QFileInfo(storePackage).isDir()) {
        const QString appId =
            QStringLiteral("shell:AppsFolder\\%1!Minecraft").arg(QLatin1String(kStoreFamily));
        if (startDetached(QStringLiteral("explorer.exe"), {appId}))
            return true;
    }

#elif defined(Q_OS_MACOS)
    const QString app = QStringLiteral("/Applications/Minecraft.app");
    if (QFileInfo(app).isDir()
        && startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-a"), app}))
        return true;
    if (startDetached(QStringLiteral("/usr/bin/open"),
                      {QStringLiteral("-a"), QStringLiteral("Minecraft")}))
        return true;

#else
    // Prefer Flatpak when the configured launcher directory lives in its sandbox,
    // or when the Flatpak is installed at all.
    const QString flatpakData = QDir(QDir::homePath())
                                    .filePath(QStringLiteral(".var/app/com.mojang.Minecraft"));
    if (isInside(minecraftDir(), flatpakData) && startFlatpakMinecraft())
        return true;

    QStringList candidates;
    for (const QString &name : {QStringLiteral("minecraft-launcher"),
                                QStringLiteral("minecraft-launcher-cmd")}) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty())
            candidates.append(found);
    }
    candidates << QStringLiteral("/usr/bin/minecraft-launcher")
               << QStringLiteral("/usr/local/bin/minecraft-launcher")
               << QStringLiteral("/opt/minecraft-launcher/minecraft-launcher")
               << QStringLiteral("/snap/bin/minecraft-launcher");

    if (startFirstExisting(candidates))
        return true;

    if (startFlatpakMinecraft())
        return true;

    // Desktop entry shipped by the official .deb / some distro packages.
    for (const QString &appsDir :
         QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        if (!QFileInfo(QDir(appsDir).filePath(QStringLiteral("minecraft-launcher.desktop"))).isFile())
            continue;
        if (startDetached(QStringLiteral("gtk-launch"), {QStringLiteral("minecraft-launcher")}))
            return true;
        break;
    }
#endif

    return fail(McPathsTr::tr("Der Minecraft Launcher wurde nicht gefunden. Starte ihn manuell "
                              "und wähle das installierte Profil aus."));
}

} // namespace McPaths
