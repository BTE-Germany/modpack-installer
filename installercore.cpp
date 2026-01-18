#include "installercore.h"

InstallerCore::InstallerCore() {
    manager = new QNetworkAccessManager();
    QObject::connect(manager, &QNetworkAccessManager::finished,
                     this, [=](QNetworkReply *reply) {
                         if (reply->error()) {
                             qDebug() << reply->errorString();
                             return;
                         }

                         QString answer = reply->readAll();

                         qDebug() << answer;
                     }
                     );
}

std::vector<std::shared_ptr<ModpackVersion> >InstallerCore::getVersions()
{
    std::vector<std::shared_ptr<ModpackVersion>> versions{};

    return versions;
}
