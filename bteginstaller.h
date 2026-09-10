#ifndef BTEGINSTALLER_H
#define BTEGINSTALLER_H

#include "installercore.h"

#include <QMainWindow>
#include <QSet>

class ProgressDialog;
class QThread;

QT_BEGIN_NAMESPACE
namespace Ui {
class BTEGInstaller;
}
QT_END_NAMESPACE

class BTEGInstaller : public QMainWindow
{
    Q_OBJECT

public:
    BTEGInstaller(QWidget *parent = nullptr);
    ~BTEGInstaller();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_actionLizenzen_triggered();
    void on_actionAboutQt_triggered();
    void on_actionLegal_triggered();
    void on_actionPrivacy_triggered();

    void onVersionsReady(ModpackVersionList loaded);
    void onVersionsFailed(const QString &error);
    void onOptionalModsReady(PackFileList mods);
    void onOptionalModsFailed(const QString &error);
    void onInstallFinished(const QString &instanceDir, const QString &profileName,
                           const QString &notice);
    void onInstallFailed(const QString &error);
    void onInstallCancelled();
    void onMoveFinished(const QString &instanceDir);
    void onMoveFailed(const QString &error);

    void startInstall();
    void openOptionalMods();
    void openPathSettings();

private:
    void setBusy(bool busy);
    void showProgressDialog(const QString &title);
    void closeProgressDialog();
    bool hasSelectedVersion() const;
    ModpackVersion selectedVersion() const;
    void loadOptionalModSelection();
    void saveOptionalModSelection() const;
    void updatePathLabel();

    Ui::BTEGInstaller *ui;
    QThread *workerThread;
    InstallerCore *core;
    ModpackVersionList versions;
    /// Project keys of the optional mods the user opted into.
    QSet<QString> enabledOptionalMods;
    ProgressDialog *progressDialog = nullptr;
    bool installing = false;
    bool loadingOptionalMods = false;
    bool moving = false;
};
#endif // BTEGINSTALLER_H
