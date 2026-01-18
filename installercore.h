#ifndef INSTALLERCORE_H
#define INSTALLERCORE_H

#include "modpackversion.h"
#include <QtNetwork/QNetworkAccessManager>
class InstallerCore
{
public:
    InstallerCore();
    std::vector<std::shared_ptr<ModpackVersion>> getVersions();

private:
    QNetworkAccessManager networkManager;
};

#endif // INSTALLERCORE_H
