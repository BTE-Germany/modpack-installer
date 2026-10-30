#include "i18n.h"

#include <QApplication>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace {

QTranslator *appTranslator = nullptr;
QString currentLanguage = QStringLiteral("de");

QString resolveLanguage(const QString &preference)
{
    if (preference == QLatin1String(I18n::kGerman))
        return QStringLiteral("de");
    if (preference == QLatin1String(I18n::kEnglish))
        return QStringLiteral("en");

    const QLocale system = QLocale::system();
    if (system.language() == QLocale::English)
        return QStringLiteral("en");
    return QStringLiteral("de");
}

bool loadLanguage(const QString &language)
{
    if (!appTranslator)
        return false;

    QApplication::removeTranslator(appTranslator);
    currentLanguage = QStringLiteral("de");

    // German is the source language baked into the binaries - no .qm needed.
    if (language == QLatin1String("de"))
        return true;

    if (appTranslator->load(QStringLiteral(":/i18n/bteginstaller_%1.qm").arg(language))) {
        QApplication::installTranslator(appTranslator);
        currentLanguage = language;
        return true;
    }
    return false;
}

} // namespace

namespace I18n {

void initialize(QApplication *app)
{
    if (!app || appTranslator)
        return;

    appTranslator = new QTranslator(app);
    loadLanguage(resolveLanguage(preferredLanguage()));
}

QString preferredLanguage()
{
    const QSettings settings;
    const QString value = settings.value(QLatin1String(kLanguageSetting),
                                         QLatin1String(kSystemLanguage))
                              .toString();
    if (value == QLatin1String(kGerman) || value == QLatin1String(kEnglish)
        || value == QLatin1String(kSystemLanguage))
        return value;
    return QLatin1String(kSystemLanguage);
}

bool setPreferredLanguage(const QString &language)
{
    QString preference = language;
    if (preference != QLatin1String(kGerman) && preference != QLatin1String(kEnglish)
        && preference != QLatin1String(kSystemLanguage))
        preference = QLatin1String(kSystemLanguage);

    QSettings settings;
    settings.setValue(QLatin1String(kLanguageSetting), preference);

    const QString resolved = resolveLanguage(preference);
    if (resolved == currentLanguage)
        return false;
    loadLanguage(resolved);
    return true;
}

QString activeLanguage()
{
    return currentLanguage;
}

} // namespace I18n
