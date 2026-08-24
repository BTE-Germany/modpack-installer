#include "bteginstaller.h"
#include "./ui_bteginstaller.h"

#include "optionalmodsdialog.h"
#include "progressdialog.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QFontDatabase>
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

    connect(ui->installButton, &QPushButton::clicked, this, &BTEGInstaller::startInstall);
    connect(ui->optionsButton, &QPushButton::clicked, this, &BTEGInstaller::openOptionalMods);

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
    if (!installing) {
        event->accept();
        return;
    }

    const auto answer = QMessageBox::question(this, tr("Installation abbrechen?"),
                                              tr("Die Installation läuft noch. Wirklich abbrechen?"),
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
        QString item = QString("%1, Minecraft %2").arg(version.getName(), version.getMinecraftVersion());
        if (version.getIsLatest()) {
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

    progressDialog = new ProgressDialog(this);
    connect(core, &InstallerCore::statusChanged, progressDialog, &ProgressDialog::setStatus);
    connect(core, &InstallerCore::progressChanged, progressDialog, &ProgressDialog::setProgress);
    connect(progressDialog, &ProgressDialog::cancelRequested, this,
            [this] { core->cancelToken()->cancel(); });

    InstallerCore *worker = core;
    const ModpackVersion version = selectedVersion();
    const QStringList optional(enabledOptionalMods.begin(), enabledOptionalMods.end());
    QMetaObject::invokeMethod(core, [worker, version, optional] { worker->install(version, optional); });

    progressDialog->exec();
}

void BTEGInstaller::closeProgressDialog()
{
    installing = false;
    setBusy(false);
    if (!progressDialog)
        return;
    progressDialog->accept();
    progressDialog->deleteLater();
    progressDialog = nullptr;
}

void BTEGInstaller::onInstallFinished(const QString &instanceDir, const QString &profileName)
{
    closeProgressDialog();

    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(tr("Fertig"));
    box.setText(tr("Das Modpack wurde installiert."));
    box.setInformativeText(tr("Starte den Minecraft Launcher und wähle das Profil "
                              "\"%1\" aus.")
                               .arg(profileName));
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
