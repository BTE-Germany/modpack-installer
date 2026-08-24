#ifndef MODPACKVERSION_H
#define MODPACKVERSION_H
#include <QMetaType>
#include <QString>

class ModpackVersion
{
public:
    ModpackVersion() = default;
    ModpackVersion(const QString &name, const QString &minecraftVersion, bool isLatest, const QString &downloadUrl);

    QString getName() const;
    QString getMinecraftVersion() const;
    bool getIsLatest() const;
    QString getDownloadUrl() const;

private:
    QString name;
    QString minecraftVersion;
    bool isLatest = false;
    QString downloadUrl;

};

Q_DECLARE_METATYPE(ModpackVersion)

#endif // MODPACKVERSION_H
