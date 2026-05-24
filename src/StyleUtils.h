#ifndef SEELIE_STYLEUTILS_H
#define SEELIE_STYLEUTILS_H

#include <QString>

class QWidget;

// Tiny helpers for working with the global Persona-5 stylesheet at
// :/styles/styles/seelie.qss. The QSS uses dynamic-property selectors
// like QPushButton[variant="secondary"], which Qt only re-evaluates
// after a style polish/unpolish — easy to forget at the call site.
// Centralizing the polish dance here keeps the mistake from spreading.
namespace StyleUtils {

/// Set a dynamic property and re-polish the widget so the global
/// stylesheet's `[variant=...]` (or any other property selector)
/// takes effect. Use this whenever you assign a property AFTER the
/// widget is constructed — Qt's QStyleSheetStyle won't pick it up
/// otherwise.
///
/// Example:
///   StyleUtils::setVariant(myButton, "secondary");
/// matches the QSS rule:
///   QPushButton[variant="secondary"] { ... }
void setVariant(QWidget *w, const char *variant);

/// Returns the absolute path to a small 8x5 black down-arrow PNG, generated
/// lazily into AppLocalDataLocation on first call. Reused by personaDialogQss
/// and by SettingsPanelWidget's combo style. Idempotent.
QString comboArrowPath();

/// Returns a QSS string that gives a QDialog (or any container) the
/// Settings panel's Persona 5 Royal look: white body, black 2px borders
/// on inputs and buttons, transparent labels with black text, white
/// groupboxes, orange (#F36F1A) accent on hover/pressed.
/// Apply via:
///   dialog->setStyleSheet(StyleUtils::personaDialogQss());
QString personaDialogQss();

/// Returns the QSS string for a Persona 5 Royal styled QPushButton: white
/// background, 2px black border, orange (#F36F1A) hover, darker (#C85A12)
/// pressed. Apply directly to each button via setStyleSheet — applying
/// it to a parent (dialog or qApp) does NOT work reliably on Windows
/// because Qt falls back to the native renderer for QPushButton states
/// when the stylesheet is inherited rather than direct.
QString personaButtonQss();

/// Returns the QSS string for a Persona 5 Royal styled QComboBox: white
/// background, 2px black border, custom down-arrow image, orange selection
/// in the popup list. Apply directly to each combo (the same direct-not-
/// cascaded constraint as personaButtonQss).
QString personaComboQss();

} // namespace StyleUtils

#endif // SEELIE_STYLEUTILS_H
