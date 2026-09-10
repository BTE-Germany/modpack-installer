#include "bteginstaller.h"
#include "./ui_bteginstaller.h"

#include "mcpaths.h"
#include "optionalmodsdialog.h"
#include "pathsdialog.h"
#include "progressdialog.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QThread>
#include <QUrl>

namespace {

constexpr const char *kLicensesUrl = "https://buildthe.earth/installer-licenses";
constexpr const char *kLegalUrl = "https://bte-germany.de/legal";
constexpr const char *kPrivacyUrl = "https://bte-germany.de/privacy";
constexpr const char *kOptionalModsSetting = "optionalMods";

} // namespace

BTEGInstaller::BTEGInstaller(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::BTEGInstaller)
    , workerThread(new QThread(this))
    , core(new InstallerCore)
{
    ui->setupUi(this);
    QFontDatabase::addApplicationFont(":/bte_fonts/outfit.tff");
    QFont outfit("Outfit");
    ui->mainWidget->setFont(outfit);

    QMenu *menu = new QMenu(this);
    menu->addAction(ui->actionLizenzen);
    menu->addAction(ui->actionAboutQt);
    menu->addAction(ui->actionLegal);
    menu->addAction(ui->actionPrivacy);
    ui->moreButton->setMenu(menu);

    loadOptionalModSelection();

    // All network and file work happens on the worker thread so that the
    // window stays responsive during an installation.
    core->moveToThread(workerThread);
    connect(workerThread, &QThread::finished, core, &QObject::deleteLater);

    connect(core, &InstallerCore::versionsReady, this, &BTEGInstaller::onVersionsReady);
    connect(core, &InstallerCore::versionsFailed, this, &BTEGInstaller::onVersionsFailed);
    connect(core, &InstallerCore::optionalModsReady, this, &BTEGInstaller::onOptionalModsReady);
    connect(core, &InstallerCore::optionalModsFailed, this, &BTEGInstaller::onOptionalModsFailed);
    connect(core, &InstallerCore::installFinished, this, &BTEGInstaller::onInstallFinished);
    connect(core, &InstallerCore::installFailed, this, &BTEGInstaller::onInstallFailed);
    connect(core, &InstallerCore::installCancelled, this, &BTEGInstaller::onInstallCancelled);
    connect(core, &InstallerCore::moveFinished, this, &BTEGInstaller::onMoveFinished);
    connect(core, &InstallerCore::moveFailed, this, &BTEGInstaller::onMoveFailed);

    connect(ui->installButton, &QPushButton::clicked, this, &BTEGInstaller::startInstall);
    connect(ui->optionsButton, &QPushButton::clicked, this, &BTEGInstaller::openOptionalMods);
    connect(ui->pathButton, &QPushButton::clicked, this, &BTEGInstaller::openPathSettings);

    updatePathLabel();

    workerThread->start();

    ui->versions->addItem(tr("Versionen werden geladen..."));
    setBusy(true);

    InstallerCore *worker = core;
    QMetaObject::invokeMethod(core, [worker] { worker->fetchVersions(); });
}

BTEGInstaller::~BTEGInstaller()
{
    core->cancelToken()->cancel();
    workerThread->quit();
    workerThread->wait();
    delete ui;
}

void BTEGInstaller::closeEvent(QCloseEvent *event)
{
    if (!installing && !moving) {
        event->accept();
        return;
    }

    const auto answer = QMessageBox::question(this, tr("Wirklich abbrechen?"),
                                              moving
                                                  ? tr("Der Ordner wird noch verschoben. Wirklich "
                                                       "abbrechen?")
                                                  : tr("Die Installation läuft noch. Wirklich "
                                                       "abbrechen?"),
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    core->cancelToken()->cancel();
    event->accept();
}

void BTEGInstaller::setBusy(bool busy)
{
    ui->versions->setEnabled(!busy && !versions.isEmpty());
    ui->installButton->setEnabled(!busy && !versions.isEmpty());
    ui->optionsButton->setEnabled(!busy && !versions.isEmpty());
    // The location can be changed before any version has been loaded.
    ui->pathButton->setEnabled(!installing && !moving && !loadingOptionalMods);
}

void BTEGInstaller::updatePathLabel()
{
    const QString path = QDir::toNativeSeparators(McPaths::instanceDir());
    ui->pathLabel->setToolTip(tr("Modpack-Ordner: %1\nMinecraft-Ordner: %2")
                                  .arg(path, QDir::toNativeSeparators(McPaths::minecraftDir())));
    // The path is often longer than the window, so only the tail is shown.
    const int available = qMax(200, ui->pathLabel->maximumWidth() - 20);
    ui->pathLabel->setText(tr("Speicherort: %1")
                               .arg(ui->pathLabel->fontMetrics().elidedText(path, Qt::ElideMiddle,
                                                                           available)));
}

bool BTEGInstaller::hasSelectedVersion() const
{
    const int index = ui->versions->currentIndex();
    return index >= 0 && index < versions.size();
}

ModpackVersion BTEGInstaller::selectedVersion() const
{
    return versions.at(ui->versions->currentIndex());
}

void BTEGInstaller::loadOptionalModSelection()
{
    const QSettings settings;
    const QStringList keys = settings.value(QLatin1String(kOptionalModsSetting)).toStringList();
    enabledOptionalMods = QSet<QString>(keys.begin(), keys.end());
}

void BTEGInstaller::saveOptionalModSelection() const
{
    QSettings settings;
    settings.setValue(QLatin1String(kOptionalModsSetting),
                      QStringList(enabledOptionalMods.begin(), enabledOptionalMods.end()));
}

void BTEGInstaller::onVersionsReady(ModpackVersionList loaded)
{
    versions = loaded;
    ui->versions->clear();

    int latest = 0;
    for (int i = 0; i < versions.size(); ++i) {
        const ModpackVersion &version = versions.at(i);
        QString item = QStringLiteral("%1, Minecraft %2").arg(version.name, version.minecraftVersion);
        if (!version.channelLabel().isEmpty())
            item.append(QStringLiteral(" (%1)").arg(version.channelLabel()));
        if (version.latest) {
            item.append(tr(" (aktuell)"));
            if (latest == 0)
                latest = i;
        }
        ui->versions->addItem(item);
    }
    ui->versions->setCurrentIndex(latest);
    setBusy(false);
}

void BTEGInstaller::onVersionsFailed(const QString &error)
{
    ui->versions->clear();
    ui->versions->addItem(tr("Keine Versionen verfügbar"));
    setBusy(true);
    QMessageBox::critical(this, tr("Fehler"), error);
}

void BTEGInstaller::openOptionalMods()
{
    if (loadingOptionalMods || !hasSelectedVersion())
        return;

    loadingOptionalMods = true;
    ui->optionsButton->setEnabled(false);
    ui->optionsButton->setText(tr("Lade..."));
    InstallerCore *worker = core;
    const ModpackVersion version = selectedVersion();
    QMetaObject::invokeMethod(core, [worker, version] { worker->fetchOptionalMods(version); });
}

void BTEGInstaller::onOptionalModsReady(PackFileList mods)
{
    loadingOptionalMods = false;
    ui->optionsButton->setText(tr("Optionale Mods..."));
    ui->optionsButton->setEnabled(!installing);

    OptionalModsDialog dialog(mods, enabledOptionalMods, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // Only the keys of this modpack are updated so that choices for mods of
    // other versions survive.
    for (const PackFile &mod : mods)
        enabledOptionalMods.remove(mod.projectKey());
    enabledOptionalMods.unite(dialog.enabledKeys());
    saveOptionalModSelection();
}

void BTEGInstaller::onOptionalModsFailed(const QString &error)
{
    loadingOptionalMods = false;
    ui->optionsButton->setText(tr("Optionale Mods..."));
    ui->optionsButton->setEnabled(!installing);
    QMessageBox::warning(this, tr("Optionale Mods"),
                         tr("Die optionalen Mods konnten nicht geladen werden.\n\n%1").arg(error));
}

void BTEGInstaller::startInstall()
{
    if (installing || !hasSelectedVersion())
        return;

    installing = true;
    setBusy(true);
    // Reset before queueing the work: a cancellation requested while the worker
    // has not picked up the call yet has to survive.
    core->cancelToken()->reset();

    showProgressDialog(tr("Modpack wird installiert"));

    InstallerCore *worker = core;
    const ModpackVersion version = selectedVersion();
    const QStringList optional(enabledOptionalMods.begin(), enabledOptionalMods.end());
    const QString instanceDir = McPaths::instanceDir();
    const QString minecraftDir = McPaths::minecraftDir();
    QMetaObject::invokeMethod(core, [worker, version, optional, instanceDir, minecraftDir] {
        worker->install(version, optional, instanceDir, minecraftDir);
    });

    progressDialog->exec();
}

void BTEGInstaller::openPathSettings()
{
    if (installing || moving)
        return;

    PathsDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString oldInstanceDir = McPaths::instanceDir();
    const bool move = dialog.moveExisting()
                      && !McPaths::isSameDir(oldInstanceDir, dialog.instanceDir());

    McPaths::setInstanceDir(dialog.instanceDir());
    McPaths::setMinecraftDir(dialog.minecraftDir());
    updatePathLabel();

    if (!move) {
        // Without moving, the new folder starts out empty - say so instead of
        // letting the next launch look like the installation vanished.
        if (!McPaths::isSameDir(oldInstanceDir, McPaths::instanceDir())
            && QDir(oldInstanceDir).exists()) {
            QMessageBox::information(this, tr("Speicherort geändert"),
                                     tr("Das Modpack wird ab jetzt in %1 installiert. Die alte "
                                        "Installation bleibt in %2 liegen.\n\nKlicke auf "
                                        "\"Modpack installieren/updaten\", um das Modpack im "
                                        "neuen Ordner einzurichten.")
                                         .arg(QDir::toNativeSeparators(McPaths::instanceDir()),
                                              QDir::toNativeSeparators(oldInstanceDir)));
        }
        return;
    }

    moving = true;
    setBusy(true);
    core->cancelToken()->reset();
    showProgressDialog(tr("Ordner wird verschoben"));

    InstallerCore *worker = core;
    const QString target = McPaths::instanceDir();
    const QString minecraftDir = McPaths::minecraftDir();
    QMetaObject::invokeMethod(core, [worker, oldInstanceDir, target, minecraftDir] {
        worker->moveInstance(oldInstanceDir, target, minecraftDir);
    });

    progressDialog->exec();
}

void BTEGInstaller::showProgressDialog(const QString &title)
{
    progressDialog = new ProgressDialog(this);
    progressDialog->setWindowTitle(title);
    connect(core, &InstallerCore::statusChanged, progressDialog, &ProgressDialog::setStatus);
    connect(core, &InstallerCore::progressChanged, progressDialog, &ProgressDialog::setProgress);
    connect(progressDialog, &ProgressDialog::cancelRequested, this,
            [this] { core->cancelToken()->cancel(); });
}

void BTEGInstaller::closeProgressDialog()
{
    installing = false;
    moving = false;
    setBusy(false);
    if (!progressDialog)
        return;
    progressDialog->accept();
    progressDialog->deleteLater();
    progressDialog = nullptr;
}

void BTEGInstaller::onInstallFinished(const QString &instanceDir, const QString &profileName,
                                      const QString &notice)
{
    closeProgressDialog();

    QString informative = tr("Starte den Minecraft Launcher und wähle das Profil \"%1\" aus.")
                              .arg(profileName);
    if (!notice.isEmpty())
        informative.append(QStringLiteral("\n\n") + notice);

    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(tr("Fertig"));
    box.setText(tr("Das Modpack wurde installiert."));
    box.setInformativeText(informative);
    QPushButton *openFolder = box.addButton(tr("Ordner öffnen"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Ok);
    box.setDefaultButton(QMessageBox::Ok);
    box.exec();

    if (box.clickedButton() == openFolder)
        QDesktopServices::openUrl(QUrl::fromLocalFile(instanceDir));
}

void BTEGInstaller::onInstallFailed(const QString &error)
{
    closeProgressDialog();
    QMessageBox::critical(this, tr("Installation fehlgeschlagen"),
                          tr("Die Installation konnte nicht abgeschlossen werden.\n\n%1").arg(error));
}

void BTEGInstaller::onInstallCancelled()
{
    closeProgressDialog();
    QMessageBox::information(this, tr("Abgebrochen"),
                             tr("Die Installation wurde abgebrochen. Das Modpack ist "
                                "möglicherweise unvollständig - starte die Installation "
                                "einfach erneut."));
}

void BTEGInstaller::onMoveFinished(const QString &instanceDir)
{
    closeProgressDialog();
    updatePathLabel();
    QMessageBox::information(this, tr("Speicherort geändert"),
                             tr("Das Modpack liegt jetzt in %1.")
                                 .arg(QDir::toNativeSeparators(instanceDir)));
}

void BTEGInstaller::onMoveFailed(const QString &error)
{
    closeProgressDialog();
    updatePathLabel();
    QMessageBox::warning(this, tr("Speicherort"),
                         tr("Der Ordner konnte nicht verschoben werden.\n\n%1").arg(error));
}

void BTEGInstaller::on_actionLizenzen_triggered()
{
    QDesktopServices::openUrl(QUrl(QLatin1String(kLicensesUrl)));
}

void BTEGInstaller::on_actionAboutQt_triggered()
{
    QMessageBox::aboutQt(this, "Über Qt");
}

void BTEGInstaller::on_actionLegal_triggered()
{
    QDesktopServices::openUrl(QUrl(QLatin1String(kLegalUrl)));
}

void BTEGInstaller::on_actionPrivacy_triggered()
{
    QDesktopServices::openUrl(QUrl(QLatin1String(kPrivacyUrl)));
}
