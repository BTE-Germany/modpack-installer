#include "optionalmodsdialog.h"

#include "httpclient.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

OptionalModsDialog::OptionalModsDialog(const QList<PackFile> &mods, const QSet<QString> &enabledKeys,
                                       QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Optionale Mods"));
    setModal(true);
    resize(520, 420);

    auto *intro = new QLabel(tr("Diese Mods gehören zum Modpack, sind aber standardmäßig "
                                "deaktiviert. Nicht ausgewählte Mods werden nicht "
                                "heruntergeladen."),
                             this);
    intro->setWordWrap(true);

    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setSpacing(10);

    for (const PackFile &mod : mods) {
        auto *checkBox = new QCheckBox(mod.displayName(), content);
        checkBox->setChecked(enabledKeys.contains(mod.projectKey()));

        auto *size = new QLabel(Http::formatBytes(mod.size), content);
        size->setStyleSheet(QStringLiteral("color: rgb(149, 149, 149)"));
        size->setIndent(checkBox->fontMetrics().horizontalAdvance(QStringLiteral("MM")));

        contentLayout->addWidget(checkBox);
        contentLayout->addWidget(size);
        entries.append({mod, checkBox});
    }

    if (mods.isEmpty()) {
        contentLayout->addWidget(new QLabel(tr("Dieses Modpack enthält keine optionalen Mods."),
                                            content));
    }
    contentLayout->addStretch(1);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidget(content);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Speichern"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(14);
    layout->addWidget(intro);
    layout->addWidget(scrollArea, 1);
    layout->addWidget(buttons);
}

QSet<QString> OptionalModsDialog::enabledKeys() const
{
    QSet<QString> keys;
    for (const auto &entry : entries) {
        if (entry.second->isChecked())
            keys.insert(entry.first.projectKey());
    }
    return keys;
}
