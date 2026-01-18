#ifndef BTEGINSTALLER_H
#define BTEGINSTALLER_H

#include <QMainWindow>

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

private slots:
    void on_actionLizenzen_triggered();

    void on_actionAboutQt_triggered();

    void on_actionLegal_triggered();

    void on_actionPrivacy_triggered();

private:
    Ui::BTEGInstaller *ui;
};
#endif // BTEGINSTALLER_H
