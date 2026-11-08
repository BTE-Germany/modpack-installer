#include "bteginstaller.h"
#include "./ui_bteginstaller.h"

#include "i18n.h"
#include "mcpaths.h"
#include "modrinthapi.h"
#include "optionalmodsdialog.h"
#include "pathsdialog.h"
#include "progressdialog.h"
#include "ziputil.h"

#include <QActionGroup>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QThread>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr const char *kLegalUrl = "https://bte-germany.de/legal";
constexpr const char *kPrivacyUrl = "https://bte-germany.de/privacy";
constexpr const char *kRepositoryUrl = "https://github.com/BTE-Germany/modpack-installer";
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

    auto *languageMenu = new QMenu(this);
    auto *languageGroup = new QActionGroup(this);
    languageGroup->setExclusive(true);
    for (QAction *action : {ui->actionLanguageSystem, ui->actionLanguageGerman,
                            ui->actionLanguageEnglish}) {
        languageGroup->addAction(action);
        languageMenu->addAction(action);
        connect(action, &QAction::triggered, this, &BTEGInstaller::changeLanguage);
    }
    ui->languageButton->setMenu(languageMenu);
    updateLanguageActions();

    connect(ui->githubButton, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QLatin1String(kRepositoryUrl)));
    });

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

void BTEGInstaller::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QMainWindow::changeEvent(event);
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
    updateVersionLabels();

    int latest = 0;
    for (int i = 0; i < versions.size(); ++i) {
        if (versions.at(i).latest) {
            latest = i;
            break;
        }
    }
    ui->versions->setCurrentIndex(latest);
    setBusy(false);
}

void BTEGInstaller::updateVersionLabels()
{
    const int selected = ui->versions->currentIndex();
    ui->versions->clear();

    for (const ModpackVersion &version : versions) {
        QString item = QStringLiteral("%1, Minecraft %2").arg(version.name, version.minecraftVersion);
        if (!version.channelLabel().isEmpty())
            item.append(QStringLiteral(" (%1)").arg(version.channelLabel()));
        if (version.latest)
            item.append(tr(" (aktuell)"));
        ui->versions->addItem(item);
    }
    if (selected >= 0 && selected < versions.size())
        ui->versions->setCurrentIndex(selected);
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
    if (loadingOptionalMods || installing || moving || !hasSelectedVersion())
        return;

    loadingOptionalMods = true;
    setBusy(true);
    ui->optionsButton->setText(tr("Lade..."));
    core->cancelToken()->reset();
    showProgressDialog(tr("Optionale Mods & Shader"));
    InstallerCore *worker = core;
    const ModpackVersion version = selectedVersion();
    QMetaObject::invokeMethod(core, [worker, version] { worker->fetchOptionalMods(version); });
    progressDialog->exec();
}

void BTEGInstaller::onOptionalModsReady(PackFileList mods)
{
    closeProgressDialog();
    loadingOptionalMods = false;
    ui->optionsButton->setText(tr("Optionale Mods && Shader"));
    setBusy(false);

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
    closeProgressDialog();
    loadingOptionalMods = false;
    ui->optionsButton->setText(tr("Optionale Mods && Shader"));
    setBusy(false);
    if (!error.isEmpty())
        QMessageBox::warning(this, tr("Optionale Mods & Shader"),
                             tr("Die optionalen Mods und Shader konnten nicht geladen werden.\n\n%1")
                                 .arg(error));
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
    connect(&dialog, &PathsDialog::settingsReset, this, &BTEGInstaller::onSettingsReset);
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

void BTEGInstaller::onSettingsReset()
{
    enabledOptionalMods.clear();
    updatePathLabel();
}

void BTEGInstaller::changeLanguage()
{
    QString language = QLatin1String(I18n::kSystemLanguage);
    if (ui->actionLanguageGerman->isChecked())
        language = QLatin1String(I18n::kGerman);
    else if (ui->actionLanguageEnglish->isChecked())
        language = QLatin1String(I18n::kEnglish);

    if (!I18n::setPreferredLanguage(language)) {
        updateLanguageActions();
        return;
    }
    retranslate();
}

void BTEGInstaller::updateLanguageActions()
{
    const QString preferred = I18n::preferredLanguage();
    ui->actionLanguageSystem->setChecked(preferred == QLatin1String(I18n::kSystemLanguage));
    ui->actionLanguageGerman->setChecked(preferred == QLatin1String(I18n::kGerman));
    ui->actionLanguageEnglish->setChecked(preferred == QLatin1String(I18n::kEnglish));
}

void BTEGInstaller::retranslate()
{
    ui->retranslateUi(this);
    updateLanguageActions();
    updatePathLabel();
    if (!versions.isEmpty())
        updateVersionLabels();
    ui->optionsButton->setText(loadingOptionalMods ? tr("Lade...") : tr("Optionale Mods && Shader"));
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

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Fertig"));
    dialog.setModal(true);
    dialog.setMinimumWidth(420);
    dialog.setMinimumHeight(200);

    auto *icon = new QLabel(&dialog);
    icon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(32, 32));

    auto *title = new QLabel(tr("Das Modpack wurde installiert."), &dialog);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto *body = new QLabel(informative, &dialog);
    body->setWordWrap(true);

    auto *textColumn = new QVBoxLayout;
    textColumn->setSpacing(6);
    textColumn->addWidget(title);
    textColumn->addWidget(body);

    auto *header = new QHBoxLayout;
    header->setSpacing(12);
    header->addWidget(icon, 0, Qt::AlignTop);
    header->addLayout(textColumn, 1);

    auto *startLauncher = new QPushButton(tr("Launcher starten"), &dialog);
    auto *openFolder = new QPushButton(tr("Ordner öffnen"), &dialog);
    auto *ok = new QPushButton(tr("OK"), &dialog);
    ok->setDefault(true);
    connect(startLauncher, &QPushButton::clicked, &dialog, [this, &dialog] {
        QString error;
        if (McPaths::startMinecraftLauncher(&error))
            return;
        QMessageBox::warning(&dialog, tr("Launcher starten"),
                             tr("%1\n\nDu kannst den Launcher auch manuell öffnen und das "
                                "installierte Profil auswählen.")
                                 .arg(error));
    });
    connect(openFolder, &QPushButton::clicked, &dialog, [instanceDir] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(instanceDir));
    });
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(startLauncher);
    buttons->addWidget(openFolder);
    buttons->addWidget(ok);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(16);
    layout->addLayout(header);
    layout->addLayout(buttons);

    dialog.exec();
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
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Lizenzen"));
    dialog.resize(680, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *browser = new QTextBrowser(&dialog);
    browser->setOpenExternalLinks(true);

    QString html = QStringLiteral("<h2>%1</h2><p>%2</p>")
                       .arg(tr("Installer und Bibliotheken").toHtmlEscaped(),
                            tr("Für den Installer ist im Repository derzeit keine eigene Lizenz "
                               "angegeben. Die folgenden Komponenten haben eigene Lizenzen.")
                                .toHtmlEscaped());
    html += QStringLiteral("<p><a href=\"%1\">%2</a></p>")
                .arg(QLatin1String(kRepositoryUrl), tr("Installer-Quellcode").toHtmlEscaped());
    html += QStringLiteral(
        "<ul>"
        "<li><a href=\"https://doc.qt.io/qt-6/licensing.html\">Qt</a>: LGPL-3.0 / GPL-3.0 / commercial</li>"
        "<li><a href=\"https://github.com/libcpr/cpr/blob/master/LICENSE\">cpr</a>: MIT</li>"
        "<li><a href=\"https://curl.se/docs/copyright.html\">libcurl</a>: curl license</li>"
        "<li><a href=\"https://github.com/richgel999/miniz/blob/master/LICENSE\">miniz</a>: MIT</li>"
        "<li><a href=\"https://www.zlib.net/zlib_license.html\">zlib</a>: zlib license</li>"
        "<li><a href=\"https://github.com/zlib-ng/zlib-ng/blob/develop/LICENSE.md\">zlib-ng</a>: zlib license</li>"
        "<li><a href=\"https://www.openssl.org/source/license.html\">OpenSSL</a>: Apache-2.0 (3.x)</li>"
        "<li><a href=\"https://github.com/Outfitio/Outfit-Fonts/blob/main/OFL.txt\">Outfit</a>: OFL-1.1</li>"
        "</ul>");
    html += QStringLiteral("<h2>%1</h2><p>%2</p><p><a href=\"https://modrinth.com/modpack/%3\">Modrinth</a></p>")
                .arg(tr("Modpack und Mods").toHtmlEscaped(),
                     tr("Mods und Shader haben eigene Lizenzen. Die Lizenzangaben findest du "
                        "auf den jeweiligen Modrinth-Projektseiten. Nach dem Laden des Modpacks "
                        "werden die enthaltenen Projekte hier verlinkt.")
                         .toHtmlEscaped(),
                     QLatin1String(Modrinth::kModpackProject));
    if (hasSelectedVersion()) {
        const QString archive = QDir(McPaths::cacheDir()).filePath(selectedVersion().cacheFileName());
        const MrpackIndex index = MrpackIndex::parse(
            ZipUtil::readEntry(archive, QLatin1String(kMrpackIndexEntry)));
        QSet<QString> seen;
        html += QStringLiteral("<ul>");
        for (const PackFile &file : index.files()) {
            if (!file.clientSupported)
                continue;
            // Only construct project links for actual Modrinth CDN URLs.
            for (const QString &download : file.downloads) {
                const QUrl url(download);
                const QStringList parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
                if (url.host() != QStringLiteral("cdn.modrinth.com")
                    || parts.size() < 2 || parts.first() != QStringLiteral("data"))
                    continue;
                const QString key = parts.at(1);
                if (!seen.contains(key)) {
                    seen.insert(key);
                    html += QStringLiteral("<li><a href=\"https://modrinth.com/project/%1\">%2</a></li>")
                                .arg(QString::fromLatin1(QUrl::toPercentEncoding(key)),
                                     file.displayName().toHtmlEscaped());
                }
                break;
            }
        }
        html += QStringLiteral("</ul>");
    }
    QFile notices(QStringLiteral(":/licenses/third-party-notices.txt"));
    if (notices.open(QIODevice::ReadOnly))
        html += QStringLiteral("<pre>%1</pre>")
                    .arg(QString::fromUtf8(notices.readAll()).toHtmlEscaped());
    browser->setHtml(html);
    layout->addWidget(browser);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void BTEGInstaller::on_actionAboutQt_triggered()
{
    QMessageBox::aboutQt(this, tr("Über Qt"));
}

void BTEGInstaller::on_actionLegal_triggered()
{
    QDesktopServices::openUrl(QUrl(QLatin1String(kLegalUrl)));
}

void BTEGInstaller::on_actionPrivacy_triggered()
{
    QDesktopServices::openUrl(QUrl(QLatin1String(kPrivacyUrl)));
}
