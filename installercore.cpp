#include "installercore.h"

#include "httpclient.h"
#include "launchersetup.h"
#include "mcpaths.h"
#include "ziputil.h"

#include <QCryptographicHash>
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
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace {

constexpr const char *kVersionsUrl = "https://modpack-cdn.bteger.dev/versions.json";

/// Enough for modrinth.index.json, which packwiz writes as the first entry.
constexpr qint64 kIndexPeekBytes = 512 * 1024;
constexpr int kMaxParallelDownloads = 6;

/// Progress budget of the individual installation steps (sums up to 1000).
constexpr int kDownloadWeight = 340;
constexpr int kCleanupWeight = 20;
constexpr int kExtractWeight = 190;
constexpr int kModsWeight = 400;
constexpr int kLauncherWeight = 50;

/// Directories inside the archive that are copied into the instance as-is.
/// "client-overrides" wins over "overrides", as required by the Modrinth format.
const QStringList &overridePrefixes()
{
    static const QStringList prefixes{QStringLiteral("overrides/"),
                                      QStringLiteral("client-overrides/")};
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

QString cacheFileName(const QString &url, const QString &versionId)
{
    QString name = QUrl(url).fileName();
    if (name.isEmpty())
        name = versionId + QStringLiteral(".mrpack");
    return name;
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
    const Http::Reply reply = Http::get(QString::fromLatin1(kVersionsUrl));
    if (!reply.ok) {
        emit versionsFailed(QStringLiteral("Die verfügbaren Versionen konnten nicht geladen werden "
                                           "(%1). Bitte prüfe deine Internetverbindung.")
                                .arg(reply.errorString()));
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(reply.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit versionsFailed(QStringLiteral("Die Versionsliste ist ungültig: %1")
                                .arg(parseError.errorString()));
        return;
    }

    ModpackVersionList versions;
    const QJsonArray entries = document.object().value(QStringLiteral("versions")).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        const ModpackVersion version(entry.value(QStringLiteral("name")).toString(),
                                     entry.value(QStringLiteral("mcVersion")).toString(),
                                     entry.value(QStringLiteral("latest")).toBool(),
                                     entry.value(QStringLiteral("downloadUrl")).toString());
        if (!version.getDownloadUrl().isEmpty())
            versions.append(version);
    }

    if (versions.isEmpty()) {
        emit versionsFailed(QStringLiteral("Es sind derzeit keine Modpack-Versionen verfügbar."));
        return;
    }
    emit versionsReady(versions);
}

MrpackIndex InstallerCore::peekIndex(const ModpackVersion &version, QString *error)
{
    const QString archive = QDir(McPaths::cacheDir())
                                .filePath(cacheFileName(version.getDownloadUrl(), version.getName()));

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
    const Http::Reply reply = Http::get(version.getDownloadUrl(), options);
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

    const QString archive = QDir(cacheDir).filePath(
        cacheFileName(version.getDownloadUrl(), version.getName()));

    setStatus(QStringLiteral("Modpack wird gesucht..."));
    const Http::Reply meta = Http::head(version.getDownloadUrl());
    const qint64 expectedSize = meta.ok ? meta.contentLength : -1;

    // Reuse a previous download if it is complete and still readable.
    const QFileInfo info(archive);
    if (info.isFile() && expectedSize > 0 && info.size() == expectedSize) {
        QString readError;
        if (!ZipUtil::readEntry(archive, QLatin1String(kMrpackIndexEntry), &readError).isEmpty()) {
            setStatus(QStringLiteral("Modpack wird aus dem Cache verwendet..."),
                      QFileInfo(archive).fileName());
            report(phase, 1.0);
            return archive;
        }
        QFile::remove(archive);
    }

    setStatus(QStringLiteral("Modpack wird heruntergeladen..."), version.getName());

    Http::Options options;
    options.onProgress = [this, &phase](qint64 received, qint64 total) {
        if (cancelled())
            return false;
        if (total > 0)
            report(phase, static_cast<double>(received) / static_cast<double>(total));
        return true;
    };

    const Http::Reply reply = Http::downloadToFile(version.getDownloadUrl(), archive, options);
    if (!reply.ok) {
        setError(error, reply.cancelled
                            ? QString()
                            : QStringLiteral("Das Modpack konnte nicht heruntergeladen werden (%1).")
                                  .arg(reply.errorString()));
        return QString();
    }
    report(phase, 1.0);
    return archive;
}

bool InstallerCore::removeStalePackFiles(const QString &instanceDir, const QString &archivePath,
                                         const QList<DownloadJob> &jobs, QString *error)
{
    QStringList overrideEntries;
    for (const QString &prefix : overridePrefixes())
        overrideEntries += ZipUtil::entryNames(archivePath, prefix);

    // Directories the modpack owns are replaced wholesale, "mods" is pruned
    // selectively so that unchanged mods do not have to be downloaded again.
    QSet<QString> ownedDirs;
    QSet<QString> modOverrides;
    for (const QString &entry : overrideEntries) {
        const int slash = entry.indexOf(QLatin1Char('/'));
        if (slash < 0)
            continue;
        const QString top = entry.left(slash);
        ownedDirs.insert(top);
        if (top == QStringLiteral("mods")) {
            const QString rest = entry.mid(slash + 1);
            if (!rest.contains(QLatin1Char('/')))
                modOverrides.insert(rest);
        }
    }
    ownedDirs.insert(QStringLiteral("mods"));

    QDir instance(instanceDir);
    for (const QString &name : ownedDirs) {
        if (cancelled())
            return false;
        if (name == QStringLiteral("mods"))
            continue;
        const QString path = instance.filePath(name);
        if (!QFileInfo(path).isDir())
            continue;
        setStatus(QStringLiteral("Alte Dateien werden entfernt..."), name);
        if (!QDir(path).removeRecursively()) {
            setError(error, QStringLiteral("Ordner kann nicht gelöscht werden: %1").arg(path));
            return false;
        }
    }

    const QString modsDir = instance.filePath(QStringLiteral("mods"));
    if (!QFileInfo(modsDir).isDir())
        return true;

    QSet<QString> expected = modOverrides;
    for (const DownloadJob &job : jobs) {
        const QString relative = QDir(instanceDir).relativeFilePath(job.target);
        if (relative.startsWith(QStringLiteral("mods/")))
            expected.insert(relative.mid(5));
    }

    setStatus(QStringLiteral("Alte Dateien werden entfernt..."), QStringLiteral("mods"));
    const QFileInfoList entries = QDir(modsDir).entryInfoList(QDir::Files | QDir::Dirs
                                                             | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &entry : entries) {
        if (cancelled())
            return false;
        if (entry.isDir()) {
            // Metadata directories such as mods/.index come from the overrides.
            if (!QDir(entry.absoluteFilePath()).removeRecursively()) {
                setError(error, QStringLiteral("Ordner kann nicht gelöscht werden: %1")
                                    .arg(entry.absoluteFilePath()));
                return false;
            }
            continue;
        }
        if (expected.contains(entry.fileName()))
            continue;
        if (!QFile::remove(entry.absoluteFilePath())) {
            setError(error, QStringLiteral("Datei kann nicht gelöscht werden: %1")
                                .arg(entry.absoluteFilePath()));
            return false;
        }
    }
    return true;
}

bool InstallerCore::extractOverrides(const QString &archivePath, const QString &instanceDir,
                                     const Phase &phase, QString *error)
{
    setStatus(QStringLiteral("Modpack wird entpackt..."));

    int index = 0;
    const double slice = 1.0 / overridePrefixes().size();
    for (const QString &prefix : overridePrefixes()) {
        const double offset = slice * index++;

        ZipUtil::ExtractOptions options;
        options.prefix = prefix;
        // Loose files in the instance root (servers.dat, options.txt, ...) hold
        // user data and are only written on a fresh installation.
        options.shouldOverwrite = [](const QString &relative) {
            return relative.contains(QLatin1Char('/'));
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

void InstallerCore::install(ModpackVersion version, QStringList enabledOptionalMods)
{
    const Phase downloadPhase{0, kDownloadWeight};
    const Phase cleanupPhase{kDownloadWeight, kCleanupWeight};
    const Phase extractPhase{kDownloadWeight + kCleanupWeight, kExtractWeight};
    const Phase modsPhase{kDownloadWeight + kCleanupWeight + kExtractWeight, kModsWeight};
    const Phase launcherPhase{kDownloadWeight + kCleanupWeight + kExtractWeight + kModsWeight,
                              kLauncherWeight};

    const auto abort = [this](const QString &message) {
        if (cancelled())
            emit installCancelled();
        else
            emit installFailed(message.isEmpty() ? QStringLiteral("Unbekannter Fehler") : message);
    };

    emit progressChanged(0);
    setStatus(QStringLiteral("Installation wird vorbereitet..."));

    const QString instanceDir = McPaths::bteGermanyDir();
    const QString minecraftDir = McPaths::minecraftDir();
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
    for (const PackFile &file : index.files()) {
        if (!file.clientSupported)
            continue;
        const bool isEnabled = file.optional && enabled.contains(file.projectKey());
        if (file.optional && !isEnabled)
            continue;

        DownloadJob job;
        job.file = file;
        job.target = QDir(instanceDir).filePath(file.targetPath(isEnabled));
        if (file.optional)
            job.reusable = QDir(instanceDir).filePath(variantPath(file.targetPath(isEnabled)));
        jobs.append(job);
    }

    if (cancelled()) {
        emit installCancelled();
        return;
    }

    setStatus(QStringLiteral("Alte Dateien werden entfernt..."));
    if (!removeStalePackFiles(instanceDir, archivePath, jobs, &error)) {
        abort(error);
        return;
    }
    report(cleanupPhase, 1.0);

    if (!extractOverrides(archivePath, instanceDir, extractPhase, &error)) {
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
                                    .arg(index.versionId(), index.minecraftVersion());
    setStatus(QStringLiteral("Launcher-Profil wird eingerichtet..."), profileName);
    if (!LauncherSetup::writeLauncherProfile(minecraftDir, instanceDir, versionId, profileName,
                                             &error)) {
        abort(error);
        return;
    }

    emit progressChanged(1000);
    setStatus(QStringLiteral("Fertig!"));
    emit installFinished(instanceDir, profileName);
}
