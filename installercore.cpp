#include "installercore.h"

#include "httpclient.h"
#include "launchersetup.h"
#include "mcpaths.h"
#include "modrinthapi.h"
#include "ziputil.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace {

/// Used when the Modrinth API cannot be reached.
constexpr const char *kFallbackVersionsUrl = "https://modpack-cdn.bteger.dev/versions.json";

/// Enough for modrinth.index.json, which packwiz writes as the first entry.
constexpr qint64 kIndexPeekBytes = 512 * 1024;
constexpr int kMaxParallelDownloads = 6;
/// Files above this size are pack assets rather than settings and are not
/// copied into the backup folder before they are refreshed.
constexpr qint64 kMaxBackupBytes = 8 * 1024 * 1024;

/// Progress budget of the individual installation steps (sums up to 1000).
constexpr int kDownloadWeight = 300;
constexpr int kCleanupWeight = 40;
constexpr int kExtractWeight = 180;
constexpr int kModsWeight = 380;
constexpr int kLauncherWeight = 50;
constexpr int kFinishWeight = 50;

/// Directories inside the archive that are copied into the instance as-is.
/// "client-overrides" wins over "overrides", as required by the Modrinth format.
const QStringList &overridePrefixes()
{
    static const QStringList prefixes{QStringLiteral("overrides/"),
                                      QStringLiteral("client-overrides/")};
    return prefixes;
}

/**
 * Directories that belong to the player alone. The installer never writes into
 * them and never deletes anything below them, even if a modpack version were to
 * ship files there.
 */
const QStringList &protectedPrefixes()
{
    static const QStringList prefixes{QStringLiteral("saves/"),
                                      QStringLiteral("screenshots/"),
                                      QStringLiteral("logs/"),
                                      QStringLiteral("crash-reports/"),
                                      QStringLiteral("backups/"),
                                      QStringLiteral("schematics/"),
                                      QStringLiteral("journeymap/"),
                                      QStringLiteral("XaeroWaypoints/"),
                                      QStringLiteral("XaeroWorldMap/"),
                                      QStringLiteral(".bteg-installer/")};
    return prefixes;
}

void setError(QString *target, const QString &message)
{
    if (target)
        *target = message;
}

QString hashFile(const QString &path, QCryptographicHash::Algorithm algorithm)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(algorithm);
    if (!hash.addData(&file))
        return QString();
    return QString::fromLatin1(hash.result().toHex());
}

/// Checks a file against the hashes from modrinth.index.json.
bool matchesPackFile(const QString &path, const PackFile &file)
{
    const QFileInfo info(path);
    if (!info.isFile())
        return false;
    if (file.size > 0 && info.size() != file.size)
        return false;
    if (!file.sha1.isEmpty())
        return hashFile(path, QCryptographicHash::Sha1).compare(file.sha1, Qt::CaseInsensitive) == 0;
    if (!file.sha512.isEmpty())
        return hashFile(path, QCryptographicHash::Sha512).compare(file.sha512, Qt::CaseInsensitive) == 0;
    return true;
}

/// The other spelling of an optional mod, i.e. with or without ".disabled".
QString variantPath(const QString &path)
{
    const QLatin1String suffix(".disabled");
    if (path.endsWith(suffix, Qt::CaseInsensitive)) {
        QString stripped = path;
        stripped.chop(suffix.size());
        return stripped;
    }
    return path + suffix;
}

/// Loose files in the instance root (options.txt, servers.dat, ...) hold the
/// player's own settings.
bool isRootLevel(const QString &relative)
{
    return !relative.contains(QLatin1Char('/'));
}

bool isProtected(const QString &relative)
{
    for (const QString &prefix : protectedPrefixes()) {
        if (relative.startsWith(prefix, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

/// A mod directly in mods/ - the game loads these, so an outdated one has to go.
bool isModJar(const QString &relative)
{
    if (!relative.startsWith(QStringLiteral("mods/")))
        return false;
    const QString name = relative.mid(5);
    if (name.contains(QLatin1Char('/')))
        return false;
    return name.endsWith(QLatin1String(".jar"), Qt::CaseInsensitive)
           || name.endsWith(QLatin1String(".jar.disabled"), Qt::CaseInsensitive);
}

/// Deletes directories that were left empty, up to (but excluding) instanceDir.
void pruneEmptyDirs(const QString &instanceDir, const QString &relative)
{
    QDir dir(QFileInfo(QDir(instanceDir).filePath(relative)).absolutePath());
    const QDir root(instanceDir);
    while (dir.exists() && dir.absolutePath() != root.absolutePath()
           && McPaths::isInside(dir.absolutePath(), root.absolutePath())) {
        if (!dir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
            return;
        const QString name = dir.dirName();
        if (!dir.cdUp())
            return;
        if (!dir.rmdir(name))
            return;
    }
}

/// All files below dir, as paths relative to it.
QStringList collectFiles(const QString &dir, qint64 *totalBytes = nullptr)
{
    QStringList files;
    qint64 bytes = 0;
    const QDir root(dir);
    QStringList pending{QString()};
    while (!pending.isEmpty()) {
        const QString relativeDir = pending.takeLast();
        const QString absolute = relativeDir.isEmpty() ? dir : root.filePath(relativeDir);
        const QFileInfoList entries = QDir(absolute).entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &entry : entries) {
            const QString relative = relativeDir.isEmpty()
                                         ? entry.fileName()
                                         : relativeDir + QLatin1Char('/') + entry.fileName();
            if (entry.isDir() && !entry.isSymLink()) {
                pending.append(relative);
                continue;
            }
            files.append(relative);
            bytes += entry.size();
        }
    }
    if (totalBytes)
        *totalBytes = bytes;
    return files;
}

} // namespace

InstallerCore::InstallerCore(QObject *parent)
    : QObject(parent)
    , cancel(std::make_shared<CancelToken>())
{
}

void InstallerCore::registerMetaTypes()
{
    qRegisterMetaType<ModpackVersion>("ModpackVersion");
    qRegisterMetaType<ModpackVersionList>("ModpackVersionList");
    qRegisterMetaType<PackFile>("PackFile");
    qRegisterMetaType<PackFileList>("PackFileList");
}

void InstallerCore::report(const Phase &phase, double fraction)
{
    const double clamped = qBound(0.0, fraction, 1.0);
    emit progressChanged(phase.start + static_cast<int>(clamped * phase.weight));
}

void InstallerCore::setStatus(const QString &status, const QString &detail)
{
    emit statusChanged(status, detail);
}

void InstallerCore::fetchVersions()
{
    // The Modrinth project is the source of truth: publishing a version there
    // is all it takes for the installer to offer it.
    QString apiError;
    ModpackVersionList versions = Modrinth::fetchVersions(QLatin1String(Modrinth::kModpackProject),
                                                          &apiError);
    if (!versions.isEmpty()) {
        emit versionsReady(versions);
        return;
    }

    // Modrinth being unreachable must not stop an installation, so the CDN
    // manifest is still read as a fallback.
    const Http::Reply reply = Http::get(QString::fromLatin1(kFallbackVersionsUrl));
    if (!reply.ok) {
        emit versionsFailed(QStringLiteral("%1 Bitte prüfe deine Internetverbindung.")
                                .arg(apiError));
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(reply.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit versionsFailed(apiError);
        return;
    }

    const QJsonArray entries = document.object().value(QStringLiteral("versions")).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        ModpackVersion version;
        version.name = entry.value(QStringLiteral("name")).toString();
        version.minecraftVersion = entry.value(QStringLiteral("mcVersion")).toString();
        version.latest = entry.value(QStringLiteral("latest")).toBool();
        version.downloadUrl = entry.value(QStringLiteral("downloadUrl")).toString();
        version.sha1 = entry.value(QStringLiteral("sha1")).toString();
        version.fileSize = static_cast<qint64>(entry.value(QStringLiteral("size")).toDouble());
        if (version.isValid())
            versions.append(version);
    }

    if (versions.isEmpty()) {
        emit versionsFailed(apiError.isEmpty()
                                ? QStringLiteral("Es sind derzeit keine Modpack-Versionen "
                                                 "verfügbar.")
                                : apiError);
        return;
    }
    emit versionsReady(versions);
}

MrpackIndex InstallerCore::peekIndex(const ModpackVersion &version, QString *error)
{
    const QString archive = QDir(McPaths::cacheDir()).filePath(version.cacheFileName());

    // A complete archive from an earlier run is the cheapest source.
    if (QFileInfo::exists(archive)) {
        QString readError;
        const QByteArray json = ZipUtil::readEntry(archive, QLatin1String(kMrpackIndexEntry), &readError);
        if (!json.isEmpty()) {
            const MrpackIndex index = MrpackIndex::parse(json, error);
            if (index.isValid())
                return index;
        }
    }

    Http::Options options;
    options.rangeFrom = 0;
    options.rangeTo = kIndexPeekBytes - 1;
    const Http::Reply reply = Http::get(version.downloadUrl, options);
    if (!reply.ok) {
        setError(error, QStringLiteral("Das Modpack konnte nicht gelesen werden (%1).")
                            .arg(reply.errorString()));
        return MrpackIndex();
    }

    QString entryName;
    QString zipError;
    const QByteArray json = ZipUtil::readFirstEntryFromHead(reply.body, &entryName, &zipError);
    if (json.isEmpty() || entryName != QLatin1String(kMrpackIndexEntry)) {
        setError(error, QStringLiteral("Die Modpack-Informationen konnten nicht vorab gelesen "
                                       "werden (%1).")
                            .arg(zipError.isEmpty() ? QStringLiteral("unerwartetes Archivformat")
                                                    : zipError));
        return MrpackIndex();
    }
    return MrpackIndex::parse(json, error);
}

void InstallerCore::fetchOptionalMods(ModpackVersion version)
{
    QString error;
    const MrpackIndex index = peekIndex(version, &error);
    if (!index.isValid()) {
        emit optionalModsFailed(error);
        return;
    }
    emit optionalModsReady(index.optionalFiles());
}

QString InstallerCore::acquireArchive(const ModpackVersion &version, const Phase &phase,
                                      QString *error)
{
    const QString cacheDir = McPaths::cacheDir();
    if (!QDir().mkpath(cacheDir)) {
        setError(error, QStringLiteral("Cache-Ordner kann nicht erstellt werden: %1").arg(cacheDir));
        return QString();
    }

    const QString archive = QDir(cacheDir).filePath(version.cacheFileName());
    const bool hashKnown = !version.sha1.isEmpty() || !version.sha512.isEmpty();

    // Reuse a previous download if it is complete and still readable.
    const QFileInfo info(archive);
    if (info.isFile()) {
        setStatus(QStringLiteral("Modpack wird gesucht..."), info.fileName());
        bool usable = false;
        if (hashKnown) {
            usable = !version.sha1.isEmpty()
                         ? hashFile(archive, QCryptographicHash::Sha1)
                                   .compare(version.sha1, Qt::CaseInsensitive)
                               == 0
                         : hashFile(archive, QCryptographicHash::Sha512)
                                   .compare(version.sha512, Qt::CaseInsensitive)
                               == 0;
        } else {
            const Http::Reply meta = Http::head(version.downloadUrl);
            usable = meta.ok && meta.contentLength > 0 && info.size() == meta.contentLength;
        }
        if (usable) {
            QString readError;
            if (!ZipUtil::readEntry(archive, QLatin1String(kMrpackIndexEntry), &readError).isEmpty()) {
                setStatus(QStringLiteral("Modpack wird aus dem Cache verwendet..."), info.fileName());
                report(phase, 1.0);
                return archive;
            }
        }
        QFile::remove(archive);
    }

    if (cancelled())
        return QString();

    setStatus(QStringLiteral("Modpack wird heruntergeladen..."), version.name);

    Http::Options options;
    options.onProgress = [this, &phase](qint64 received, qint64 total) {
        if (cancelled())
            return false;
        if (total > 0)
            report(phase, static_cast<double>(received) / static_cast<double>(total));
        return true;
    };

    const Http::Reply reply = Http::downloadToFile(version.downloadUrl, archive, options);
    if (!reply.ok) {
        setError(error, reply.cancelled
                            ? QString()
                            : QStringLiteral("Das Modpack konnte nicht heruntergeladen werden (%1).")
                                  .arg(reply.errorString()));
        return QString();
    }

    // A truncated or corrupted archive would fail much later, with a far more
    // confusing message.
    if (hashKnown) {
        setStatus(QStringLiteral("Modpack wird geprüft..."), version.name);
        const bool valid = !version.sha1.isEmpty()
                               ? hashFile(archive, QCryptographicHash::Sha1)
                                         .compare(version.sha1, Qt::CaseInsensitive)
                                     == 0
                               : hashFile(archive, QCryptographicHash::Sha512)
                                         .compare(version.sha512, Qt::CaseInsensitive)
                                     == 0;
        if (!valid) {
            QFile::remove(archive);
            setError(error, QStringLiteral("Das Modpack wurde fehlerhaft übertragen. Bitte "
                                           "versuche es erneut."));
            return QString();
        }
    }

    report(phase, 1.0);
    return archive;
}

bool InstallerCore::backupFile(const QString &instanceDir, const QString &relative)
{
    const QFileInfo info(QDir(instanceDir).filePath(relative));
    if (!info.isFile() || info.size() > kMaxBackupBytes)
        return false;

    if (backupStamp.isEmpty())
        backupStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));

    const QString target = QDir(InstallManifest::stateDir(instanceDir))
                               .filePath(QStringLiteral("backup/") + backupStamp
                                         + QLatin1Char('/') + relative);
    if (!QDir().mkpath(QFileInfo(target).absolutePath()))
        return false;
    QFile::remove(target);
    return QFile::copy(info.absoluteFilePath(), target);
}

bool InstallerCore::setAside(const QString &instanceDir, const QString &relative)
{
    const QString source = QDir(instanceDir).filePath(relative);
    if (backupStamp.isEmpty())
        backupStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));

    const QString target = QDir(InstallManifest::stateDir(instanceDir))
                               .filePath(QStringLiteral("removed/") + backupStamp
                                         + QLatin1Char('/') + relative);
    if (!QDir().mkpath(QFileInfo(target).absolutePath()))
        return false;
    QFile::remove(target);
    if (!QFile::rename(source, target))
        return false;
    ++keptAsideFiles;
    return true;
}

bool InstallerCore::pruneOldFiles(const QString &instanceDir, const InstallManifest &previous,
                                  const QSet<QString> &keepPaths, const Phase &phase,
                                  QString *error)
{
    const QStringList paths = previous.paths();
    int done = 0;
    for (const QString &relative : paths) {
        if (cancelled())
            return false;
        ++done;
        report(phase, paths.isEmpty() ? 1.0 : static_cast<double>(done) / paths.size());

        if (keepPaths.contains(relative) || isProtected(relative))
            continue;
        // An optional mod that was toggled keeps its file under the other name;
        // the download step renames it instead of fetching it again.
        if (keepPaths.contains(variantPath(relative)))
            continue;

        const QString target = QDir(instanceDir).filePath(relative);
        if (!QFileInfo::exists(target))
            continue;

        const InstalledFile entry = previous.entry(relative);
        const bool untouched = !entry.sha1.isEmpty()
                               && hashFile(target, QCryptographicHash::Sha1)
                                          .compare(entry.sha1, Qt::CaseInsensitive)
                                      == 0;

        if (!untouched) {
            // The player edited this file. Mods still have to go - the game
            // would load a mod that is no longer part of the modpack - but they
            // are only moved aside, everything else simply stays.
            if (!isModJar(relative))
                continue;
            setStatus(QStringLiteral("Alte Dateien werden entfernt..."), QFileInfo(target).fileName());
            if (!setAside(instanceDir, relative)) {
                setError(error, QStringLiteral("Datei kann nicht verschoben werden: %1").arg(target));
                return false;
            }
            pruneEmptyDirs(instanceDir, relative);
            continue;
        }

        setStatus(QStringLiteral("Alte Dateien werden entfernt..."), QFileInfo(target).fileName());
        if (!QFile::remove(target)) {
            setError(error, QStringLiteral("Datei kann nicht gelöscht werden: %1").arg(target));
            return false;
        }
        pruneEmptyDirs(instanceDir, relative);
    }
    report(phase, 1.0);
    return true;
}

bool InstallerCore::quarantineUnknownMods(const QString &instanceDir,
                                          const QSet<QString> &keepPaths, QString *error)
{
    const QString modsDir = QDir(instanceDir).filePath(QStringLiteral("mods"));
    if (!QFileInfo(modsDir).isDir())
        return true;

    const QFileInfoList entries = QDir(modsDir).entryInfoList(QDir::Files | QDir::Hidden);
    for (const QFileInfo &entry : entries) {
        if (cancelled())
            return false;
        const QString relative = QStringLiteral("mods/") + entry.fileName();
        if (!isModJar(relative) || keepPaths.contains(relative))
            continue;

        setStatus(QStringLiteral("Alte Mods werden beiseite gelegt..."), entry.fileName());
        if (!setAside(instanceDir, relative)) {
            setError(error, QStringLiteral("Datei kann nicht verschoben werden: %1")
                                .arg(entry.absoluteFilePath()));
            return false;
        }
    }
    return true;
}

InstallerCore::OverrideAction InstallerCore::planOverride(const QString &instanceDir,
                                                          const QString &relative,
                                                          const InstallManifest &previous)
{
    if (isProtected(relative))
        return OverrideAction::Keep;

    const QString target = QDir(instanceDir).filePath(relative);
    if (!QFileInfo::exists(target))
        return OverrideAction::Write;

    const InstalledFile entry = previous.entry(relative);
    const bool untouched = !entry.sha1.isEmpty()
                           && hashFile(target, QCryptographicHash::Sha1)
                                      .compare(entry.sha1, Qt::CaseInsensitive)
                                  == 0;

    // Files the player owns are never replaced. In the instance root those are
    // the settings files (options.txt, servers.dat, ...), which the modpack only
    // seeds on a fresh installation.
    if (isRootLevel(relative))
        return untouched ? OverrideAction::Write : OverrideAction::Keep;

    // Everything else belongs to the modpack and is brought back to the state
    // the pack expects - a half updated config directory is what leaves mods
    // such as FancyMenu in a broken state. A copy of the player's version is
    // kept so nothing is lost for good.
    if (!untouched)
        backupFile(instanceDir, relative);
    return OverrideAction::Write;
}

bool InstallerCore::extractOverrides(const QString &archivePath, const QString &instanceDir,
                                     const InstallManifest &previous,
                                     const QSet<QString> &clientOverrides,
                                     InstallManifest *manifest, const Phase &phase, QString *error)
{
    setStatus(QStringLiteral("Modpack wird entpackt..."));

    int index = 0;
    const double slice = 1.0 / overridePrefixes().size();
    for (const QString &prefix : overridePrefixes()) {
        const double offset = slice * index++;
        const bool isGenericPrefix = prefix == QStringLiteral("overrides/");

        ZipUtil::ExtractOptions options;
        options.prefix = prefix;
        options.shouldExtract = [&](const QString &relative) {
            // client-overrides/ replaces the same path in overrides/, so the
            // generic entry must not be written first.
            if (isGenericPrefix && clientOverrides.contains(relative))
                return false;
            return planOverride(instanceDir, relative, previous) == OverrideAction::Write;
        };
        options.onFileWritten = [&](const QString &relative) {
            InstalledFile file;
            file.path = relative;
            file.fromOverrides = true;
            const QString target = QDir(instanceDir).filePath(relative);
            file.size = QFileInfo(target).size();
            file.sha1 = hashFile(target, QCryptographicHash::Sha1);
            manifest->add(file);
        };
        options.isCancelled = [this] { return cancelled(); };
        options.onProgress = [this, &phase, offset, slice](qint64 done, qint64 total) {
            if (total > 0)
                report(phase, offset + slice * (static_cast<double>(done) / static_cast<double>(total)));
        };

        QString extractError;
        if (!ZipUtil::extract(archivePath, instanceDir, options, &extractError)) {
            if (cancelled())
                return false;
            setError(error, extractError);
            return false;
        }
    }
    report(phase, 1.0);
    return true;
}

bool InstallerCore::downloadPackFiles(const QList<DownloadJob> &jobs, const Phase &phase,
                                      QString *error)
{
    if (jobs.isEmpty()) {
        report(phase, 1.0);
        return true;
    }

    qint64 totalBytes = 0;
    for (const DownloadJob &job : jobs)
        totalBytes += qMax<qint64>(job.file.size, 1);

    const int workerCount = std::max(1, std::min({kMaxParallelDownloads,
                                                  QThread::idealThreadCount(),
                                                  static_cast<int>(jobs.size())}));

    std::atomic<int> nextJob{0};
    std::atomic<int> finishedJobs{0};
    std::atomic<int> runningWorkers{workerCount};
    std::atomic<qint64> finishedBytes{0};
    std::vector<std::atomic<qint64>> inFlight(static_cast<size_t>(workerCount));
    for (auto &value : inFlight)
        value.store(0);

    QMutex mutex;
    QString firstError;
    QString currentFile;

    const auto fail = [&mutex, &firstError](const QString &message) {
        QMutexLocker locker(&mutex);
        if (firstError.isEmpty())
            firstError = message;
    };
    const auto hasFailed = [&mutex, &firstError] {
        QMutexLocker locker(&mutex);
        return !firstError.isEmpty();
    };

    const auto worker = [&](int slot) {
        for (;;) {
            const qsizetype index = nextJob.fetch_add(1);
            if (index >= jobs.size() || cancelled() || hasFailed())
                break;

            const DownloadJob &job = jobs.at(index);
            {
                QMutexLocker locker(&mutex);
                currentFile = QFileInfo(job.target).fileName();
            }

            if (!QDir().mkpath(QFileInfo(job.target).absolutePath())) {
                fail(QStringLiteral("Ordner kann nicht erstellt werden: %1")
                         .arg(QFileInfo(job.target).absolutePath()));
                break;
            }

            bool present = matchesPackFile(job.target, job.file);
            // The same mod may already be on disk under its other name after the
            // user toggled it - a rename saves the whole download.
            if (!present && !job.reusable.isEmpty() && matchesPackFile(job.reusable, job.file)) {
                QFile::remove(job.target);
                present = QFile::rename(job.reusable, job.target);
            }

            if (!present) {
                Http::Options options;
                options.onProgress = [&, slot](qint64 received, qint64) {
                    if (cancelled() || hasFailed())
                        return false;
                    inFlight[static_cast<size_t>(slot)].store(received);
                    return true;
                };

                Http::Reply reply;
                for (const QString &url : job.file.downloads) {
                    reply = Http::downloadToFile(url, job.target, options);
                    if (reply.ok || reply.cancelled)
                        break;
                }
                inFlight[static_cast<size_t>(slot)].store(0);

                if (reply.cancelled || cancelled())
                    break;
                if (!reply.ok) {
                    fail(QStringLiteral("%1 konnte nicht heruntergeladen werden (%2).")
                             .arg(QFileInfo(job.target).fileName(), reply.errorString()));
                    break;
                }
                if (!matchesPackFile(job.target, job.file)) {
                    QFile::remove(job.target);
                    fail(QStringLiteral("%1 wurde fehlerhaft übertragen. Bitte versuche es erneut.")
                             .arg(QFileInfo(job.target).fileName()));
                    break;
                }
            }

            // The variant of a toggled optional mod would otherwise stay behind
            // and be loaded by the game.
            if (!job.reusable.isEmpty() && QFileInfo::exists(job.reusable))
                QFile::remove(job.reusable);

            finishedBytes.fetch_add(qMax<qint64>(job.file.size, 1));
            finishedJobs.fetch_add(1);
        }
        runningWorkers.fetch_sub(1);
    };

    std::vector<std::thread> pool;
    pool.reserve(static_cast<size_t>(workerCount));
    for (int slot = 0; slot < workerCount; ++slot)
        pool.emplace_back(worker, slot);

    // The coordinator only reports progress so that all signals stay on this thread.
    while (runningWorkers.load() > 0) {
        qint64 done = finishedBytes.load();
        for (auto &value : inFlight)
            done += value.load();
        report(phase, static_cast<double>(done) / static_cast<double>(totalBytes));

        QMutexLocker locker(&mutex);
        const QString detail = currentFile;
        locker.unlock();
        setStatus(QStringLiteral("Mods werden heruntergeladen... (%1/%2)")
                      .arg(finishedJobs.load())
                      .arg(jobs.size()),
                  detail);
        QThread::msleep(120);
    }
    for (std::thread &thread : pool)
        thread.join();

    if (cancelled())
        return false;
    if (!firstError.isEmpty()) {
        setError(error, firstError);
        return false;
    }
    report(phase, 1.0);
    return true;
}

void InstallerCore::install(ModpackVersion version, QStringList enabledOptionalMods,
                            QString instanceDir, QString minecraftDir)
{
    const Phase downloadPhase{0, kDownloadWeight};
    const Phase cleanupPhase{kDownloadWeight, kCleanupWeight};
    const Phase extractPhase{kDownloadWeight + kCleanupWeight, kExtractWeight};
    const Phase modsPhase{kDownloadWeight + kCleanupWeight + kExtractWeight, kModsWeight};
    const Phase launcherPhase{kDownloadWeight + kCleanupWeight + kExtractWeight + kModsWeight,
                              kLauncherWeight};
    const Phase finishPhase{1000 - kFinishWeight, kFinishWeight};

    // What the previous run left behind, and what this run writes.
    InstallManifest previous;
    InstallManifest manifest;

    const auto abort = [&](const QString &message) {
        // A cancelled or failed update leaves the instance somewhere between the
        // two versions. Recording that state keeps the next run from mistaking
        // the files it wrote itself for the player's own.
        if (!manifest.isEmpty()) {
            for (const QString &path : previous.paths()) {
                if (!manifest.contains(path)
                    && QFileInfo::exists(QDir(instanceDir).filePath(path)))
                    manifest.add(previous.entry(path));
            }
            manifest.save(instanceDir);
        }
        if (cancelled())
            emit installCancelled();
        else
            emit installFailed(message.isEmpty() ? QStringLiteral("Unbekannter Fehler") : message);
    };

    backupStamp.clear();
    keptAsideFiles = 0;

    emit progressChanged(0);
    setStatus(QStringLiteral("Installation wird vorbereitet..."));

    if (instanceDir.isEmpty())
        instanceDir = McPaths::instanceDir();
    if (minecraftDir.isEmpty())
        minecraftDir = McPaths::minecraftDir();
    if (!QDir().mkpath(instanceDir)) {
        abort(QStringLiteral("Der Modpack-Ordner konnte nicht erstellt werden: %1").arg(instanceDir));
        return;
    }

    QString error;
    const QString archivePath = acquireArchive(version, downloadPhase, &error);
    if (archivePath.isEmpty()) {
        abort(error);
        return;
    }

    const QByteArray indexJson = ZipUtil::readEntry(archivePath, QLatin1String(kMrpackIndexEntry),
                                                    &error);
    if (indexJson.isEmpty()) {
        abort(QStringLiteral("Das Modpack-Archiv ist unvollständig (%1).").arg(error));
        return;
    }
    const MrpackIndex index = MrpackIndex::parse(indexJson, &error);
    if (!index.isValid()) {
        abort(error);
        return;
    }

    // Build the download list: everything required plus the opted-in extras.
    const QSet<QString> enabled(enabledOptionalMods.begin(), enabledOptionalMods.end());
    QList<DownloadJob> jobs;
    QSet<QString> keepPaths;
    for (const PackFile &file : index.files()) {
        if (!file.clientSupported)
            continue;
        const bool isEnabled = file.optional && enabled.contains(file.projectKey());
        if (file.optional && !isEnabled)
            continue;

        DownloadJob job;
        job.file = file;
        const QString relative = file.targetPath(isEnabled);
        job.target = QDir(instanceDir).filePath(relative);
        if (file.optional)
            job.reusable = QDir(instanceDir).filePath(variantPath(relative));
        jobs.append(job);
        keepPaths.insert(relative);
    }

    // Everything the archive ships as overrides, with client-overrides winning.
    const QSet<QString> clientOverrides = [&] {
        const QStringList names = ZipUtil::entryNames(archivePath,
                                                      QStringLiteral("client-overrides/"));
        return QSet<QString>(names.begin(), names.end());
    }();
    for (const QString &prefix : overridePrefixes()) {
        const QStringList names = ZipUtil::entryNames(archivePath, prefix);
        for (const QString &name : names)
            keepPaths.insert(name);
    }

    if (cancelled()) {
        emit installCancelled();
        return;
    }

    previous = InstallManifest::load(instanceDir);

    setStatus(QStringLiteral("Alte Dateien werden entfernt..."));
    if (!pruneOldFiles(instanceDir, previous, keepPaths, cleanupPhase, &error)) {
        abort(error);
        return;
    }
    if (previous.isEmpty() && !quarantineUnknownMods(instanceDir, keepPaths, &error)) {
        abort(error);
        return;
    }
    report(cleanupPhase, 1.0);

    manifest.packName = index.name();
    manifest.packVersion = index.versionId().isEmpty() ? version.name : index.versionId();
    manifest.sourceId = version.sourceId;
    manifest.minecraftVersion = index.minecraftVersion();
    manifest.loaderVersion = index.loaderVersion();

    if (!extractOverrides(archivePath, instanceDir, previous, clientOverrides, &manifest,
                          extractPhase, &error)) {
        abort(error);
        return;
    }

    if (!downloadPackFiles(jobs, modsPhase, &error)) {
        abort(error);
        return;
    }

    // Shaders are not part of the pack, but Iris expects the folder to exist.
    QDir().mkpath(QDir(instanceDir).filePath(QStringLiteral("shaderpacks")));

    setStatus(QStringLiteral("Fabric wird installiert..."),
              QStringLiteral("Fabric %1").arg(index.loaderVersion()));
    const QString versionId = LauncherSetup::installFabricVersion(minecraftDir,
                                                                  index.minecraftVersion(),
                                                                  index.loaderVersion(), &error);
    if (versionId.isEmpty()) {
        abort(error);
        return;
    }
    report(launcherPhase, 0.6);

    const QString profileName = QStringLiteral("BTE Germany v%1, Minecraft %2")
                                    .arg(manifest.packVersion, index.minecraftVersion());
    setStatus(QStringLiteral("Launcher-Profil wird eingerichtet..."), profileName);
    if (!LauncherSetup::writeLauncherProfile(minecraftDir, instanceDir, versionId, profileName,
                                             &error)) {
        abort(error);
        return;
    }
    report(launcherPhase, 1.0);

    // The manifest is what lets the next update tell the modpack's files apart
    // from the player's own, so it is written last, once everything is in place.
    setStatus(QStringLiteral("Installation wird abgeschlossen..."));
    for (const DownloadJob &job : jobs) {
        InstalledFile file;
        file.path = QDir(instanceDir).relativeFilePath(job.target);
        file.size = job.file.size > 0 ? job.file.size : QFileInfo(job.target).size();
        file.sha1 = job.file.sha1.isEmpty() ? hashFile(job.target, QCryptographicHash::Sha1)
                                            : job.file.sha1.toLower();
        manifest.add(file);
    }
    if (!manifest.save(instanceDir, &error)) {
        abort(error);
        return;
    }
    report(finishPhase, 1.0);

    QString notice;
    if (keptAsideFiles > 0) {
        notice = QStringLiteral("%1 Mod(s) gehören nicht mehr zum Modpack und liegen jetzt in "
                                "%2.")
                     .arg(keptAsideFiles)
                     .arg(QDir::toNativeSeparators(
                         QDir(InstallManifest::stateDir(instanceDir))
                             .filePath(QStringLiteral("removed/") + backupStamp)));
    }

    emit progressChanged(1000);
    setStatus(QStringLiteral("Fertig!"));
    emit installFinished(instanceDir, profileName, notice);
}

void InstallerCore::moveInstance(QString fromDir, QString toDir, QString minecraftDir)
{
    emit progressChanged(0);
    setStatus(QStringLiteral("Ordner wird verschoben..."), QDir::toNativeSeparators(toDir));

    const QFileInfo source(fromDir);
    if (!source.isDir()) {
        emit moveFailed(QStringLiteral("%1 existiert nicht mehr.")
                            .arg(QDir::toNativeSeparators(fromDir)));
        return;
    }
    if (McPaths::isInside(toDir, fromDir)) {
        emit moveFailed(QStringLiteral("Der neue Ordner darf nicht im alten Ordner liegen."));
        return;
    }
    if (QFileInfo::exists(toDir)
        && !QDir(toDir).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
        emit moveFailed(QStringLiteral("%1 ist nicht leer. Wähle einen leeren oder noch nicht "
                                       "vorhandenen Ordner.")
                            .arg(QDir::toNativeSeparators(toDir)));
        return;
    }

    // Within the same file system this is instant.
    QDir().mkpath(QFileInfo(toDir).absolutePath());
    if (QFileInfo::exists(toDir))
        QDir().rmdir(toDir);
    if (QDir().rename(fromDir, toDir)) {
        LauncherSetup::retargetLauncherProfile(minecraftDir, toDir);
        emit progressChanged(1000);
        emit moveFinished(toDir);
        return;
    }

    qint64 totalBytes = 0;
    const QStringList files = collectFiles(fromDir, &totalBytes);
    if (!QDir().mkpath(toDir)) {
        emit moveFailed(QStringLiteral("%1 kann nicht erstellt werden.")
                            .arg(QDir::toNativeSeparators(toDir)));
        return;
    }

    const Phase phase{0, 1000};
    qint64 copied = 0;
    for (const QString &relative : files) {
        if (cancelled()) {
            // Nothing has been removed yet, so dropping the half written copy
            // leaves the installation exactly where it was.
            QDir(toDir).removeRecursively();
            emit moveFailed(QStringLiteral("Das Verschieben wurde abgebrochen. Die Installation "
                                           "liegt weiterhin in %1.")
                                .arg(QDir::toNativeSeparators(fromDir)));
            return;
        }

        const QString source = QDir(fromDir).filePath(relative);
        const QString target = QDir(toDir).filePath(relative);
        setStatus(QStringLiteral("Ordner wird verschoben..."), relative);

        if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(source, target)) {
            QDir(toDir).removeRecursively();
            emit moveFailed(QStringLiteral("%1 konnte nicht kopiert werden. Die Installation liegt "
                                           "weiterhin in %2.")
                                .arg(relative, QDir::toNativeSeparators(fromDir)));
            return;
        }
        copied += QFileInfo(target).size();
        if (totalBytes > 0)
            report(phase, static_cast<double>(copied) / static_cast<double>(totalBytes));
    }

    // The copy is complete, so the installation is usable even if the old
    // folder cannot be cleaned up.
    QDir(fromDir).removeRecursively();
    LauncherSetup::retargetLauncherProfile(minecraftDir, toDir);
    emit progressChanged(1000);
    emit moveFinished(toDir);
}
