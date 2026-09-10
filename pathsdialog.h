#ifndef PATHSDIALOG_H
#define PATHSDIALOG_H

#include <QDialog>
#include <QString>

class QCheckBox;
class QLineEdit;

/**
 * Lets the user decide where the modpack and the Minecraft launcher live. Both
 * paths default to the folders the official launcher uses, so nobody has to
 * touch this dialog to get going.
 */
class PathsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PathsDialog(QWidget *parent = nullptr);

    QString instanceDir() const;
    QString minecraftDir() const;
    /// True if the existing installation should be moved to the new location.
    bool moveExisting() const;

private slots:
    void browseInstanceDir();
    void browseMinecraftDir();
    void resetInstanceDir();
    void resetMinecraftDir();
    void updateMoveHint();

private:
    void accept() override;

    QLineEdit *instanceEdit;
    QLineEdit *minecraftEdit;
    QCheckBox *moveCheckBox;
    QString previousInstanceDir;
};

#endif // PATHSDIALOG_H
