#include "bteginstaller.h"
#include "./ui_bteginstaller.h"
#include <QDesktopServices>
#include <QMessageBox>
#include <QFontDatabase>
#include <QMenu>

BTEGInstaller::BTEGInstaller(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::BTEGInstaller)
{
    ui->setupUi(this);
    QFontDatabase::addApplicationFont(":/bte_fonts/outfit.tff");
    QFont outfit("Outfit");
    ui->mainWidget->setFont(outfit);

    QMenu *menu = new QMenu();
    menu->addAction(ui->actionLizenzen);
    menu->addAction(ui->actionAboutQt);
    menu->addAction(ui->actionLegal);
    menu->addAction(ui->actionPrivacy);

    ui->moreButton->setMenu(menu);

}

BTEGInstaller::~BTEGInstaller()
{
    delete ui;
}

void BTEGInstaller::on_actionLizenzen_triggered()
{
    QDesktopServices::openUrl(QUrl("https://example.com"));
}


void BTEGInstaller::on_actionAboutQt_triggered()
{
    QMessageBox::aboutQt(this, "Über Qt");

}


void BTEGInstaller::on_actionLegal_triggered()
{
    QDesktopServices::openUrl(QUrl("https://bte-germany.de/legal"));
}


void BTEGInstaller::on_actionPrivacy_triggered()
{
    QDesktopServices::openUrl(QUrl("https://bte-germany.de/privacy"));
}

