#pragma once

#include <QApplication>
#include <QPalette>
#include <QColor>
#include <QProxyStyle>
#include <QString>
#include <QToolBar>

namespace meda {

/// Toolbar metrics: a wider icon↔text gap and item margin than the
/// platform default — reads calmer and more deliberate.
class ToolBarStyle : public QProxyStyle {
public:
    int pixelMetric(PixelMetric m, const QStyleOption* o,
                    const QWidget* w) const override
    {
        switch (m) {
        case PM_ToolBarItemSpacing:    return 8;   // icon ↔ label gap
        case PM_ToolBarItemMargin:     return 5;   // padding inside item
        case PM_ToolBarSeparatorExtent: return 16;
        default: return QProxyStyle::pixelMetric(m, o, w);
        }
    }
};

/// Flight-deck cockpit theme (DESIGN.md): dark instrument panel, cyan
/// nav accent for selection/focus, amber for armed states. Apply once,
/// after QApplication construction.
inline void applyTheme(QApplication& app)
{
    const QColor panel(0x13, 0x15, 0x19);        // soft charcoal
    const QColor instrument(0x1A, 0x1E, 0x24);   // pane interiors
    const QColor bezel(0x2E, 0x35, 0x40);        // hairlines
    const QColor label(0x8E, 0x99, 0xA6);        // secondary text
    const QColor data(0xDC, 0xE2, 0xE9);         // primary text
    const QColor nav(0x4D, 0xA3, 0xE8);          // professional blue
    const QColor amber(0xE8, 0xA3, 0x3D);        // armed states

    app.setStyle("Fusion");
    QPalette pal;
    pal.setColor(QPalette::Window, panel);
    pal.setColor(QPalette::WindowText, data);
    pal.setColor(QPalette::Base, instrument);
    pal.setColor(QPalette::AlternateBase, panel);
    pal.setColor(QPalette::ToolTipBase, instrument);
    pal.setColor(QPalette::ToolTipText, data);
    pal.setColor(QPalette::Text, data);
    pal.setColor(QPalette::Button, instrument);
    pal.setColor(QPalette::ButtonText, data);
    pal.setColor(QPalette::BrightText, nav);
    pal.setColor(QPalette::Highlight, nav);
    pal.setColor(QPalette::HighlightedText, panel);
    pal.setColor(QPalette::Link, nav);
    pal.setColor(QPalette::Disabled, QPalette::Text, label.darker(160));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, label.darker(160));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, label.darker(160));
    app.setPalette(pal);

    app.setStyleSheet(QString(R"(
        * { outline: none; }
        QMainWindow, QDialog { background: %1; }
        QMainWindow::separator {
            background: %3; width: 2px; height: 2px;
        }
        QMainWindow::separator:hover { background: %4; }

        /* Menus & menu bar */
        QMenuBar {
            background: %1; color: %5; border: none;
            padding: 2px 4px;
        }
        QMenuBar::item { padding: 4px 10px; background: transparent; }
        QMenuBar::item:selected { background: %3; }
        QMenuBar::item:pressed { background: %3; }
        QMenu {
            background: %2; color: %5;
            border: 1px solid %3; padding: 4px 0px;
        }
        QMenu::item { padding: 5px 28px 5px 24px; }
        QMenu::item:selected {
            background: %3;
            border-left: 2px solid %6;
        }
        QMenu::item:disabled { color: %7; }
        QMenu::separator {
            height: 1px; background: %3; margin: 4px 8px;
        }
        QMenu::indicator { width: 12px; height: 12px; }

        /* Toolbar — annunciator row */
        QToolBar {
            background: %1; border: none;
            border-bottom: 1px solid %3;
            spacing: 1px; padding: 4px 10px 5px;
        }
        QToolBar::separator {
            background: %3; width: 1px; margin: 8px 10px;
        }
        QToolBar QLabel {
            color: %7; font-size: 11px;
            padding: 0 4px 0 10px;
        }
        QToolButton {
            background: transparent; color: #B7BFC7;
            border: none;
            border-bottom: 2px solid transparent;
            padding: 6px 11px 4px;
            font-size: 12px;
        }
        QToolButton:hover {
            color: %5; background: %2;
        }
        QToolButton:checked {
            color: %8;
            border-bottom: 2px solid %8;
        }
        QToolButton:pressed { background: %2; }
        QToolButton:disabled { color: %7; opacity: 0.4; }
        QToolBar QSpinBox {
            background: %2; border: 1px solid %3;
            border-radius: 3px;
            color: %5; padding: 3px 6px; font-size: 11px;
        }
        QToolBar QSpinBox:focus { border-color: %6; }
        QToolBar QSpinBox::up-button, QToolBar QSpinBox::down-button {
            width: 0; border: none;
        }

        /* Dock — instrument side panel */
        QDockWidget {
            color: %7;
            titlebar-close-icon: none;
            titlebar-normal-icon: none;
        }
        QDockWidget::title {
            background: %1; color: %7;
            padding: 6px 10px 6px 10px;
            text-transform: uppercase;
            letter-spacing: 1px;
            border-bottom: 1px solid %3;
            font-size: 10px;
        }
        QDockWidget::close-button, QDockWidget::float-button {
            background: transparent; border: none; padding: 2px;
        }

        /* Library tree */
        QTreeWidget {
            background: %1; color: %5; border: none;
            alternate-background-color: %2;
            show-decoration-selected: 0;
        }
        QTreeWidget::item { padding: 4px 6px; }
        QTreeWidget::item:selected {
            background: %3; border-left: 2px solid %6; color: %5;
        }
        QTreeWidget::item:hover { background: %2; }

        /* Status bar — annunciator strip */
        QStatusBar {
            background: %1; color: %7;
            border-top: 1px solid %3;
            font-size: 11px;
        }
        QStatusBar::item { border: none; }
        QStatusBar QLabel { color: %7; padding: 0 6px; }

        /* Progress tape */
        QProgressBar {
            border: 1px solid %3; background: %2;
            color: %5; text-align: center;
            font-size: 10px;
        }
        QProgressBar::chunk { background: #2ECC71; }

        /* Thin groove scrollbars */
        QScrollBar:vertical {
            background: %1; width: 8px; margin: 0;
        }
        QScrollBar::handle:vertical {
            background: %3; min-height: 24px;
        }
        QScrollBar::handle:vertical:hover { background: %4; }
        QScrollBar:horizontal {
            background: %1; height: 8px; margin: 0;
        }
        QScrollBar::handle:horizontal {
            background: %3; min-width: 24px;
        }
        QScrollBar::handle:horizontal:hover { background: %4; }
        QScrollBar::add-line, QScrollBar::sub-line,
        QScrollBar::add-page, QScrollBar::sub-page { height: 0; width: 0; }

        /* Spins & line edits — instrument readouts */
        QSpinBox, QDoubleSpinBox, QLineEdit, QComboBox {
            background: %2; color: %5;
            border: 1px solid %3; padding: 2px 6px;
            selection-background-color: %6;
            selection-color: %1;
        }
        QSpinBox:focus, QLineEdit:focus, QComboBox:focus {
            border-color: %6;
        }
        QComboBox QAbstractItemView {
            background: %2; color: %5; border: 1px solid %3;
            selection-background-color: %3;
        }

        /* Dialogs */
        QMessageBox QLabel { color: %5; }
        QPushButton {
            background: %2; color: %5;
            border: 1px solid %3; padding: 6px 18px;
        }
        QPushButton:hover { border-color: %4; }
        QPushButton:pressed, QPushButton:default {
            border-color: %6;
        }
        QPushButton:disabled { color: %7; }

        QToolTip {
            background: %2; color: %5; border: 1px solid %3;
            padding: 4px 8px;
        }
    )")
        .arg(panel.name())      // %1
        .arg(instrument.name()) // %2
        .arg(bezel.name())      // %3
        .arg(QColor(0x3D,0x47,0x4F).name()) // %4 bezel-lit
        .arg(data.name())       // %5
        .arg(nav.name())        // %6
        .arg(label.name())      // %7
        .arg(amber.name()));    // %8
}

} // namespace meda
