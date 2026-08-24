#include "bteginstaller.h"
#include "installercore.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setOrganizationName("BuildTheEarth Germany");
    QApplication::setOrganizationDomain("bte-germany.de");
    QApplication::setApplicationName("BTEG Modpack Installer");
#ifdef BTEG_VERSION_STRING
    QApplication::setApplicationVersion(BTEG_VERSION_STRING);
#endif

    InstallerCore::registerMetaTypes();

    BTEGInstaller w;
    w.show();
    return a.exec();
}
