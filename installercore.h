#ifndef INSTALLERCORE_H
#define INSTALLERCORE_H

#include "canceltoken.h"
#include "modpackversion.h"
#include "mrpackindex.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

using ModpackVersionList = QList<ModpackVersion>;
using PackFileList = QList<PackFile>;

/**
 * Does all the actual work: fetching the version manifest, downloading and
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
    void install(ModpackVersion version, QStringList enabledOptionalMods);

signals:
    void versionsReady(ModpackVersionList versions);
    void versionsFailed(QString error);

    void optionalModsReady(PackFileList mods);
    void optionalModsFailed(QString error);

    void statusChanged(QString status, QString detail);
    /// Overall progress from 0 to 1000.
    void progressChanged(int permille);
    void installFinished(QString instanceDir, QString profileName);
    void installFailed(QString error);
    void installCancelled();

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

    void report(const Phase &phase, double fraction);
    void setStatus(const QString &status, const QString &detail = QString());
    bool cancelled() const { return cancel->isCancelled(); }

    /// Downloads the modpack archive into the cache, reusing a complete copy.
    QString acquireArchive(const ModpackVersion &version, const Phase &phase, QString *error);
    /// Reads modrinth.index.json without downloading the whole archive.
    MrpackIndex peekIndex(const ModpackVersion &version, QString *error);

    bool removeStalePackFiles(const QString &instanceDir, const QString &archivePath,
                              const QList<DownloadJob> &jobs, QString *error);
    bool extractOverrides(const QString &archivePath, const QString &instanceDir,
                          const Phase &phase, QString *error);
    bool downloadPackFiles(const QList<DownloadJob> &jobs, const Phase &phase, QString *error);

    CancelTokenPtr cancel;
};

#endif // INSTALLERCORE_H
