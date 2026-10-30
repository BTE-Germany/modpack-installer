#ifndef I18N_H
#define I18N_H

#include <QString>

class QApplication;
class QTranslator;

/**
 * Loads and switches UI translations. German is the source language of the
 * strings in the code; English is provided as a compiled .qm resource.
 */
namespace I18n {

constexpr const char *kLanguageSetting = "language";
constexpr const char *kSystemLanguage = "system";
constexpr const char *kGerman = "de";
constexpr const char *kEnglish = "en";

/// Installs translators on the application. Call once at startup.
void initialize(QApplication *app);

/// Current preference: "system", "de" or "en".
QString preferredLanguage();

/// Applies a language preference and persists it. Returns true when the
/// active translation changed.
bool setPreferredLanguage(const QString &language);

/// Locale code currently driving the UI ("de" or "en").
QString activeLanguage();

} // namespace I18n

#endif // I18N_H
