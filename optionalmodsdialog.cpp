#include "optionalmodsdialog.h"

#include "httpclient.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

OptionalModsDialog::OptionalModsDialog(const QList<PackFile> &mods, const QSet<QString> &enabledKeys,
                                       QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Optionale Mods & Shader"));
    setModal(true);
    resize(520, 420);

    auto *intro = new QLabel(tr("Diese Mods und Shader gehören zum Modpack, sind aber standardmäßig "
                                "deaktiviert. Nicht ausgewählte Einträge werden nicht "
                                "heruntergeladen."),
                             this);
    intro->setWordWrap(true);

    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setSpacing(10);

    auto *modsLayout = new QVBoxLayout;
    auto *shadersLayout = new QVBoxLayout;
    QList<PackFile> shaders;
    QList<PackFile> regularMods;
    for (const PackFile &mod : mods) {
        (mod.isShader() ? shaders : regularMods).append(mod);
    }
    addSection(tr("Optionale Mods"), regularMods, content, modsLayout, enabledKeys);
    addSection(tr("Optionale Shader"), shaders, content, shadersLayout, enabledKeys);

    auto *sections = new QVBoxLayout;
    sections->addLayout(modsLayout);
    sections->addLayout(shadersLayout);
    contentLayout->addLayout(sections);
    if (mods.isEmpty())
        contentLayout->addWidget(new QLabel(tr("Dieses Modpack enthält keine optionalen Mods "
                                               "oder Shader."), content));
    contentLayout->addStretch(1);

    toggleButton = new QPushButton(this);
    connect(toggleButton, &QPushButton::clicked, this, [this] {
        bool allSelected = !entries.isEmpty();
        for (const auto &entry : entries)
            allSelected = allSelected && entry.second->isChecked();
        for (const auto &entry : entries)
            entry.second->setChecked(!allSelected);
        updateToggleButton();
    });
    updateToggleButton();

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
    layout->addWidget(toggleButton);
    layout->addWidget(scrollArea, 1);
    layout->addWidget(buttons);

    for (const auto &entry : entries) {
        connect(entry.second, &QCheckBox::toggled, this, &OptionalModsDialog::updateToggleButton);
    }
}

void OptionalModsDialog::addSection(const QString &title, const QList<PackFile> &mods,
                                    QWidget *content, QVBoxLayout *layout,
                                    const QSet<QString> &enabledKeys)
{
    if (mods.isEmpty())
        return;
    auto *heading = new QLabel(QStringLiteral("<b>%1</b>").arg(title), this);
    layout->addWidget(heading);
    for (const PackFile &mod : mods) {
        auto *checkBox = new QCheckBox(mod.displayName(), content);
        checkBox->setChecked(enabledKeys.contains(mod.projectKey()));

        QString details = mod.description;
        if (!details.isEmpty())
            details += QStringLiteral(" · ");
        details += Http::formatBytes(mod.size);
        auto *size = new QLabel(details, content);
        size->setWordWrap(true);
        size->setStyleSheet(QStringLiteral("color: rgb(149, 149, 149)"));
        size->setIndent(checkBox->fontMetrics().horizontalAdvance(QStringLiteral("MM")));

        layout->addWidget(checkBox);
        layout->addWidget(size);
        entries.append({mod, checkBox});
    }
}

void OptionalModsDialog::updateToggleButton()
{
    if (!toggleButton)
        return;

    const bool allSelected = !entries.isEmpty()
                             && std::all_of(entries.cbegin(), entries.cend(),
                                            [](const auto &entry) {
                                                return entry.second->isChecked();
                                            });
    toggleButton->setText(allSelected ? tr("Alle abwählen") : tr("Alle auswählen"));
    toggleButton->setEnabled(!entries.isEmpty());
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
