#include "bteginstaller.h"
#include "installercore.h"

#include <QApplication>
#include <QByteArray>

int main(int argc, char *argv[])
{
#if defined(Q_OS_LINUX)
    // Some Linux distributions expose Wayland but do not ship Qt's Wayland
    // platform plugin. Prefer the widely available XCB backend in that case.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")
        && qEnvironmentVariableIsSet("WAYLAND_DISPLAY"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("xcb"));
#endif
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
