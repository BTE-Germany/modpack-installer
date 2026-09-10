#ifndef INSTALLERCORE_H
#define INSTALLERCORE_H

#include "canceltoken.h"
#include "installmanifest.h"
#include "modpackversion.h"
#include "mrpackindex.h"

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

using ModpackVersionList = QList<ModpackVersion>;
using PackFileList = QList<PackFile>;

/**
 * Does all the actual work: fetching the version list, downloading and
 * unpacking the Modrinth modpack, installing Fabric and registering the
 * launcher profile.
 *
 * Every public slot blocks, so the object is meant to live on a worker thread.
 * Progress is reported through signals; cancellation goes through the shared
 * CancelToken, which the GUI thread may flip at any time.
 */
class InstallerCore : public QObject
{
    Q_OBJECT

public:
    explicit InstallerCore(QObject *parent = nullptr);

    /// Makes the container types usable in queued signal connections.
    static void registerMetaTypes();

    CancelTokenPtr cancelToken() const { return cancel; }

public slots:
    /// Loads the list of installable modpack versions.
    void fetchVersions();

    /**
     * Reads the optional mods of a version. Only the first few hundred kilobytes
     * of the modpack archive are transferred (or the local cache is reused), so
     * this is fast enough to run when the user opens the dialog.
     */
    void fetchOptionalMods(ModpackVersion version);

    /// Runs the whole installation. enabledOptionalMods holds project keys of
    /// the optional mods the user opted into (see PackFile::projectKey()).
    void install(ModpackVersion version, QStringList enabledOptionalMods, QString instanceDir,
                 QString minecraftDir);

    /// Moves an existing installation to a new location, so that changing the
    /// storage location keeps worlds and settings. The launcher profile is
    /// pointed at the new directory as well.
    void moveInstance(QString fromDir, QString toDir, QString minecraftDir);

signals:
    void versionsReady(ModpackVersionList versions);
    void versionsFailed(QString error);

    void optionalModsReady(PackFileList mods);
    void optionalModsFailed(QString error);

    void statusChanged(QString status, QString detail);
    /// Overall progress from 0 to 1000.
    void progressChanged(int permille);
    /// notice carries an optional hint about files that were kept aside.
    void installFinished(QString instanceDir, QString profileName, QString notice);
    void installFailed(QString error);
    void installCancelled();

    void moveFinished(QString instanceDir);
    void moveFailed(QString error);

private:
    /// A slice of the overall progress bar.
    struct Phase
    {
        int start = 0;
        int weight = 0;
    };

    struct DownloadJob
    {
        PackFile file;
        QString target;
        /// Existing file of the other enabled/disabled variant, reused if valid.
        QString reusable;
    };

    /// What to do with a file the modpack ships in its overrides.
    enum class OverrideAction { Write, Keep };

    void report(const Phase &phase, double fraction);
    void setStatus(const QString &status, const QString &detail = QString());
    bool cancelled() const { return cancel->isCancelled(); }

    /// Downloads the modpack archive into the cache, reusing a complete copy.
    QString acquireArchive(const ModpackVersion &version, const Phase &phase, QString *error);
    /// Reads modrinth.index.json without downloading the whole archive.
    MrpackIndex peekIndex(const ModpackVersion &version, QString *error);

    /**
     * Removes the files of the previous installation that the new modpack
     * version no longer contains. Only files recorded in the previous manifest
     * are considered, and only while they still are exactly as the installer
     * wrote them - everything the player added or edited stays.
     */
    bool pruneOldFiles(const QString &instanceDir, const InstallManifest &previous,
                       const QSet<QString> &keepPaths, const Phase &phase, QString *error);
    /**
     * Instances from older installer versions have no manifest, so mods of the
     * previous version cannot be told apart from mods the player added. They are
     * moved into the installer's backup folder instead of being deleted.
     */
    bool quarantineUnknownMods(const QString &instanceDir, const QSet<QString> &keepPaths,
                               QString *error);

    OverrideAction planOverride(const QString &instanceDir, const QString &relative,
                                const InstallManifest &previous);
    bool extractOverrides(const QString &archivePath, const QString &instanceDir,
                          const InstallManifest &previous, const QSet<QString> &clientOverrides,
                          InstallManifest *manifest, const Phase &phase, QString *error);
    bool downloadPackFiles(const QList<DownloadJob> &jobs, const Phase &phase, QString *error);

    /// Copies a file the player changed into <instance>/.bteg-installer/backup.
    bool backupFile(const QString &instanceDir, const QString &relative);
    /// Moves a file out of the way, keeping it inside the backup folder.
    bool setAside(const QString &instanceDir, const QString &relative);

    CancelTokenPtr cancel;
    /// Timestamped folder below .bteg-installer, created on first use.
    QString backupStamp;
    int keptAsideFiles = 0;
};

#endif // INSTALLERCORE_H
