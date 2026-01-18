#ifndef MODPACKVERSION_H
#define MODPACKVERSION_H
#include <QString>
class ModpackVersion
{
public:
    ModpackVersion(const QString &name, const QString &minecraftVersion, bool isLatest, const QString &downloadUrl);

    QString getName() const;
    QString getMinecraftVersion() const;
    bool getIsLatest() const;
    QString getDownloadUrl() const;

private:
    QString name;
    QString minecraftVersion;
    bool isLatest;
    QString downloadUrl;

};

#endif // MODPACKVERSION_H
