#include "pathsdialog.h"

#include "mcpaths.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QLabel *hint(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color: rgb(149, 149, 149)"));
    return label;
}

} // namespace

PathsDialog::PathsDialog(QWidget *parent)
    : QDialog(parent)
    , instanceEdit(new QLineEdit(this))
    , minecraftEdit(new QLineEdit(this))
    , moveCheckBox(new QCheckBox(tr("Vorhandene Installation in den neuen Ordner verschieben"), this))
    , previousInstanceDir(McPaths::instanceDir())
{
    setWindowTitle(tr("Speicherort"));
    setModal(true);
    setMinimumWidth(620);

    instanceEdit->setText(QDir::toNativeSeparators(previousInstanceDir));
    minecraftEdit->setText(QDir::toNativeSeparators(McPaths::minecraftDir()));

    const auto row = [this](QLineEdit *edit, void (PathsDialog::*browseSlot)(),
                            void (PathsDialog::*resetSlot)()) {
        auto *browse = new QPushButton(tr("Durchsuchen..."), this);
        auto *reset = new QPushButton(tr("Standard"), this);
        connect(browse, &QPushButton::clicked, this, browseSlot);
        connect(reset, &QPushButton::clicked, this, resetSlot);

        auto *layout = new QHBoxLayout;
        layout->setSpacing(8);
        layout->addWidget(edit, 1);
        layout->addWidget(browse);
        layout->addWidget(reset);
        return layout;
    };

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(10);

    layout->addWidget(hint(tr("Hier liegen die Mods, Konfigurationen und deine Welten. Wähle "
                              "einen Ordner auf einer Festplatte mit genügend freiem Platz."),
                           this));
    layout->addWidget(new QLabel(tr("Modpack-Ordner"), this));
    layout->addLayout(row(instanceEdit, &PathsDialog::browseInstanceDir,
                          &PathsDialog::resetInstanceDir));
    layout->addWidget(moveCheckBox);

    layout->addSpacing(8);
    layout->addWidget(new QLabel(tr("Minecraft-Launcher-Ordner"), this));
    layout->addLayout(row(minecraftEdit, &PathsDialog::browseMinecraftDir,
                          &PathsDialog::resetMinecraftDir));
    layout->addWidget(hint(tr("In diesem Ordner legt der offizielle Launcher seine Profile ab. "
                              "Ändere ihn nur, wenn du Minecraft selbst verschoben hast."),
                           this));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Speichern"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(buttons, &QDialogButtonBox::accepted, this, &PathsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PathsDialog::reject);

    layout->addSpacing(8);
    layout->addWidget(buttons);

    connect(instanceEdit, &QLineEdit::textChanged, this, &PathsDialog::updateMoveHint);
    updateMoveHint();
}

QString PathsDialog::instanceDir() const
{
    return QDir::fromNativeSeparators(instanceEdit->text().trimmed());
}

QString PathsDialog::minecraftDir() const
{
    return QDir::fromNativeSeparators(minecraftEdit->text().trimmed());
}

bool PathsDialog::moveExisting() const
{
    return moveCheckBox->isEnabled() && moveCheckBox->isChecked();
}

void PathsDialog::browseInstanceDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Modpack-Ordner wählen"),
                                                          instanceEdit->text());
    if (!dir.isEmpty())
        instanceEdit->setText(QDir::toNativeSeparators(dir));
}

void PathsDialog::browseMinecraftDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Minecraft-Ordner wählen"),
                                                          minecraftEdit->text());
    if (!dir.isEmpty())
        minecraftEdit->setText(QDir::toNativeSeparators(dir));
}

void PathsDialog::resetInstanceDir()
{
    instanceEdit->setText(QDir::toNativeSeparators(McPaths::defaultInstanceDir()));
}

void PathsDialog::resetMinecraftDir()
{
    minecraftEdit->setText(QDir::toNativeSeparators(McPaths::defaultMinecraftDir()));
}

void PathsDialog::updateMoveHint()
{
    // Moving is only on offer if there actually is something to move.
    const bool changed = !McPaths::isSameDir(instanceDir(), previousInstanceDir);
    const bool exists = QFileInfo(previousInstanceDir).isDir()
                        && !QDir(previousInstanceDir)
                                .isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);

    moveCheckBox->setEnabled(changed && exists);
    if (!moveCheckBox->isEnabled()) {
        moveCheckBox->setChecked(false);
        moveCheckBox->setToolTip(QString());
        return;
    }
    moveCheckBox->setChecked(true);
    moveCheckBox->setToolTip(tr("Verschiebt Welten, Einstellungen und Mods von %1 in den neuen "
                                "Ordner. Ohne diese Option bleibt die alte Installation liegen "
                                "und der neue Ordner startet leer.")
                                .arg(QDir::toNativeSeparators(previousInstanceDir)));
}

void PathsDialog::accept()
{
    QString error;
    if (!McPaths::isUsableTarget(instanceDir(), &error)) {
        QMessageBox::warning(this, tr("Modpack-Ordner"), error);
        return;
    }
    if (!McPaths::isUsableTarget(minecraftDir(), &error)) {
        QMessageBox::warning(this, tr("Minecraft-Ordner"), error);
        return;
    }
    if (McPaths::isSameDir(instanceDir(), minecraftDir())) {
        QMessageBox::warning(this, tr("Speicherort"),
                             tr("Das Modpack braucht einen eigenen Ordner, damit deine normale "
                                "Minecraft-Installation unangetastet bleibt. Wähle für das "
                                "Modpack einen anderen Ordner."));
        return;
    }
    QDialog::accept();
}
