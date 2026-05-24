#include "StyleUtils.h"

#include <QStyle>
#include <QWidget>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QPainter>
#include <QPolygon>

namespace StyleUtils {

void setVariant(QWidget *w, const char *variant)
{
    if (!w) return;
    w->setProperty("variant", variant);
    // QStyleSheetStyle evaluates rules at polish time. A property assigned
    // after construction (or after the widget is first shown) doesn't trigger
    // re-evaluation on its own — we have to nudge it explicitly. The
    // unpolish/polish pair is the canonical Qt idiom for this.
    if (w->style()) {
        w->style()->unpolish(w);
        w->style()->polish(w);
    }
}

QString comboArrowPath()
{
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(cacheDir);
    const QString arrowPath = cacheDir + QStringLiteral("/combo_arrow.png");
    if (!QFile::exists(arrowPath)) {
        QPixmap arrow(8, 5);
        arrow.fill(Qt::transparent);
        QPainter p(&arrow);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::black);
        p.setPen(Qt::NoPen);
        QPolygon tri;
        tri << QPoint(0, 0) << QPoint(8, 0) << QPoint(4, 5);
        p.drawPolygon(tri);
        p.end();
        arrow.save(arrowPath);
    }
    return arrowPath;
}

QString personaDialogQss()
{
    // Mirrors the Settings panel's Persona 5 Royal visual language:
    // white backgrounds, 2px black borders on interactive widgets,
    // #2C2C2E text, orange (#F36F1A) hover/selection accents.
    const QString arrowPath = QString(comboArrowPath()).replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QStringLiteral(R"(
        QDialog {
            background: white;
        }
        QGroupBox {
            background: white;
            border: 1px solid black;
            border-radius: 3px;
            margin-top: 1.4ex;
            padding-top: 4px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 8px;
            padding: 0 2px;
            color: black;
            background: white;
        }
        QLabel {
            background: transparent;
            color: #2C2C2E;
        }
        QLineEdit {
            background: white;
            border: 2px solid black;
            border-radius: 3px;
            padding: 2px 6px;
            color: #2C2C2E;
        }
        QComboBox {
            background: white;
            border: 2px solid black;
            border-radius: 3px;
            padding: 2px 6px;
            color: #2C2C2E;
            min-width: 70px;
        }
        QComboBox::drop-down {
            background: white;
            border-left: 2px solid black;
            width: 18px;
            subcontrol-origin: padding;
            subcontrol-position: center right;
        }
        QComboBox::down-arrow {
            image: url(%1);
            width: 8px;
            height: 5px;
        }
        QComboBox QAbstractItemView {
            background: white;
            color: #2C2C2E;
            border: 2px solid black;
            border-radius: 4px;
            selection-background-color: #F36F1A;
            selection-color: white;
            outline: none;
        }
        QComboBox QAbstractItemView::item {
            color: #2C2C2E;
            padding: 3px 6px;
        }
        QComboBox QAbstractItemView::item:selected {
            background: #F36F1A;
            color: white;
        }
        QPushButton {
            background: white;
            border: 2px solid black;
            border-radius: 3px;
            padding: 4px 8px;
            color: #2C2C2E;
            min-width: 60px;
        }
        QPushButton:hover {
            background-color: #F36F1A;
            border: 2px solid black;
            border-radius: 3px;
            color: white;
            padding: 4px 8px;
            min-width: 60px;
        }
        QPushButton:pressed {
            background-color: #C95A14;
            border: 2px solid black;
            border-radius: 3px;
            color: white;
            padding: 4px 8px;
            min-width: 60px;
        }
        QCheckBox {
            color: #2C2C2E;
            background: transparent;
        }
        QCheckBox::indicator {
            width: 12px;
            height: 12px;
            background: white;
            border: 2px solid black;
            border-radius: 3px;
        }
        QCheckBox::indicator:checked {
            background: #F36F1A;
            border: 1px solid #F36F1A;
        }
        QListWidget {
            background: white;
            border: 2px solid black;
            color: #2C2C2E;
        }
        QListWidget::item:selected {
            background: #F36F1A;
            color: white;
        }
    )").arg(arrowPath);
}

} // namespace StyleUtils
