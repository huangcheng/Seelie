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

/// Returns a QSS string that gives a QDialog (or any container) the
/// Settings panel's retro look: white body, black 2px borders on inputs
/// and buttons, transparent labels with black text, white groupboxes.
/// Apply via:
///   dialog->setStyleSheet(StyleUtils::retroDialogQss());
QString retroDialogQss();

} // namespace StyleUtils

#endif // SEELIE_STYLEUTILS_H
