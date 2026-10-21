#ifndef OPTIONALMODSDIALOG_H
#define OPTIONALMODSDIALOG_H

#include "mrpackindex.h"

#include <QDialog>
#include <QList>
#include <QSet>

class QCheckBox;
class QVBoxLayout;

/**
 * Lets the user opt into the mods the modpack ships as optional. Everything is
 * off by default, exactly like the modpack itself declares it.
 */
class OptionalModsDialog : public QDialog
{
    Q_OBJECT

public:
    OptionalModsDialog(const QList<PackFile> &mods, const QSet<QString> &enabledKeys,
                       QWidget *parent = nullptr);

    /// Project keys of the mods the user enabled.
    QSet<QString> enabledKeys() const;

private:
    void addSection(const QString &title, const QList<PackFile> &mods, QWidget *content,
                    QVBoxLayout *layout, const QSet<QString> &enabledKeys);

    QList<QPair<PackFile, QCheckBox *>> entries;
};

#endif // OPTIONALMODSDIALOG_H
