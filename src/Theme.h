#pragma once
#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QStringList>

// ============================================================================
// PhotoGod design system
//
// One rule decides everything here: the image is the main character. The
// chrome is a calm, near-neutral graphite so it never tints how you read a
// photograph, and it recedes in tone as it gets closer to the canvas.
//
//   void  ──────────  the darkest surface, only the canvas surround
//   shell ──────────  window chrome (menu bar, rails, status)
//   panel ──────────  panel bodies
//   raised ─────────  inputs, cards, active states
//
// Type is a single normal-weight grotesque. Nothing above weight 500 —
// hierarchy comes from size and tone, not from thickness.
// ============================================================================

namespace Theme {

// ---- surfaces ----
inline constexpr const char* Void    = "#0E0E10";   // canvas surround
inline constexpr const char* Shell   = "#151517";   // window chrome
inline constexpr const char* Panel   = "#1A1A1D";   // panel bodies
inline constexpr const char* Raised  = "#202024";   // inputs, cards
inline constexpr const char* Hover   = "#26262B";   // hover
inline constexpr const char* Active  = "#2C2C32";   // pressed / checked

// ---- hairlines (used sparingly — space separates, not borders) ----
inline constexpr const char* Line    = "#222226";
inline constexpr const char* LineSoft= "#1D1D21";

// ---- text ----
inline constexpr const char* Text    = "#E8E8EB";   // primary
inline constexpr const char* Text2   = "#A0A0A8";   // secondary
inline constexpr const char* Text3   = "#6C6C75";   // tertiary / meta
inline constexpr const char* Text4   = "#4A4A52";   // disabled

// ---- accent: a calm, low-chroma blue that stays honest over photographs ----
inline constexpr const char* Accent     = "#7E9CD8";
inline constexpr const char* AccentDim  = "#5C7CB8";
inline constexpr const char* AccentWash = "#2A3446";   // selected row / checked fill
inline constexpr const char* Amber      = "#D6A45F";   // unsaved / attention, used once

// ---- geometry ----
inline constexpr int RadiusSm = 5;
inline constexpr int RadiusMd = 7;
inline constexpr int RadiusLg = 10;

// The UI font stack. Prefers modern neutral grotesques, falls back through
// whatever a given Linux desktop actually ships.
inline QStringList uiFontFamilies()
{
    return {"Inter", "Adwaita Sans", "SF Pro Text", "Segoe UI Variable Text",
            "Cantarell", "Noto Sans", "DejaVu Sans"};
}

// Tabular figures for readouts (zoom, coordinates, sizes) so numbers stop
// jittering as they change.
inline QStringList monoFontFamilies()
{
    return {"JetBrains Mono", "Adwaita Mono", "SF Mono", "Cascadia Mono",
            "Noto Sans Mono", "DejaVu Sans Mono"};
}

inline QFont uiFont(int px = 13, int weight = QFont::Normal)
{
    QFont f;
    f.setFamilies(uiFontFamilies());
    f.setPixelSize(px);
    f.setWeight(QFont::Weight(weight));
    return f;
}

inline QFont monoFont(int px = 11)
{
    QFont f;
    f.setFamilies(monoFontFamilies());
    f.setPixelSize(px);
    return f;
}

inline QColor color(const char* hex, int alpha = 255)
{
    QColor c(hex);
    c.setAlpha(alpha);
    return c;
}

// The whole application skin. Kept in one place so the app reads as one
// surface rather than a pile of independently styled widgets.
inline QString styleSheet()
{
    return QString(R"(
        /* ---------------------------------------------------------- shell */
        QMainWindow, QMainWindow > QWidget { background: %SHELL%; }
        QMainWindow::separator { background: %SHELL%; width: 6px; height: 6px; }
        QMainWindow::separator:hover { background: %LINE%; }

        /* menu bar — a thin strip of quiet words, no frame beneath it */
        QMenuBar { background: %SHELL%; border: none; padding: 3px 6px; }
        QMenuBar::item { background: transparent; padding: 5px 10px; margin: 0;
                         border-radius: %R_SM%px; color: %TEXT2%; }
        QMenuBar::item:selected { background: %HOVER%; color: %TEXT%; }
        QMenuBar::item:pressed  { background: %ACTIVE%; color: %TEXT%; }

        QMenu { background: %RAISED%; border: 1px solid %LINE%;
                border-radius: %R_MD%px; padding: 4px; }
        QMenu::item { padding: 5px 20px 5px 14px; border-radius: %R_SM%px; color: %TEXT%; }
        QMenu::item:selected { background: %WASH%; color: %TEXT%; }
        QMenu::item:disabled { color: %TEXT4%; }
        QMenu::separator { height: 1px; background: %LINE%; margin: 4px 8px; }

        /* ------------------------------------------------------- toolbars */
        QToolBar { background: %SHELL%; border: none; padding: 3px; spacing: 0px; }
        /* a hairline between groups of tools — present, never loud. Inset far
           enough that it reads as a tick mark, not a rule across the rail. */
        QToolBar::separator { background: %ACTIVE%; height: 1px; width: 1px;
                              margin: 5px 8px; }

        /* Buttons you pick with never grow a slab. All state lives in the icon:
           idle grey, white on hover, accent while active — cross-faded by
           IconGlow. The surface behind them stays exactly as it was. */
        QToolButton { background: transparent; border: none;
                      border-radius: %R_SM%px; padding: 3px; color: %TEXT2%; }
        QToolButton:hover   { background: transparent; color: %TEXT%; }
        QToolButton:pressed { background: transparent; }
        QToolButton:checked { background: transparent; color: %ACCENT%; }

        /* the left tool rail — lean, one icon wide, no wasted gutter */
        #toolsBar { padding: 3px 2px; spacing: 0px; }
        #toolsBar QToolButton { min-width: 26px; min-height: 26px; padding: 4px; }

        #optionsBar { padding: 4px 10px; spacing: 10px;
                      border-bottom: 1px solid %LINESOFT%; }
        #optionsBar QLabel { color: %TEXT3%; }
        /* the view toggles parked at its right edge read as words, so their
           state is carried by tone, the same way the tool icons carry theirs */
        #optionsBar QToolButton { color: %TEXT3%; padding: 3px 9px; }
        #optionsBar QToolButton:hover { color: %TEXT%; }
        #optionsBar QToolButton:checked { color: %ACCENT%; }

        /* ----------------------------------------------------- status bar */
        QStatusBar { background: %SHELL%; border: none; color: %TEXT3%; padding: 0 8px; }
        QStatusBar::item { border: none; }
        QStatusBar QLabel { color: %TEXT3%; }

        /* --------------------------------------------------------- panels */
        /* Panel groups paint their own surface and tab strip (see Dock.cpp);
           all this layer owns is the seam between them. Invisible until you
           reach for it, then it lights up as the thing you are about to drag. */
        QSplitter#dockSplit::handle { background: %SHELL%; }
        QSplitter#dockSplit::handle:hover { background: %ACCENTDIM%; }
        QSplitter#dockSplit::handle:pressed { background: %ACCENT%; }

        /* document tabs across the top of the canvas — they vanish entirely
           when only one document is open */
        #docTabs::pane { border: none; background: %VOID%; }
        #docTabs QTabBar::tab { background: transparent; color: %TEXT3%;
                                padding: 8px 18px; border: none; }
        #docTabs QTabBar::tab:selected { background: %VOID%; color: %TEXT%; }
        #docTabs QTabBar::tab:hover:!selected { color: %TEXT2%; }

        /* panel-internal tab bars */
        QTabBar::tab { background: transparent; color: %TEXT3%; padding: 7px 14px;
                       border: none; margin-right: 2px; }
        QTabBar::tab:selected { color: %TEXT%; }
        QTabBar::tab:hover { color: %TEXT2%; }
        QTabWidget::pane { border: none; }

        /* --------------------------------------------------------- inputs */
        QLineEdit, QSpinBox, QComboBox, QFontComboBox, QDoubleSpinBox {
            background: %RAISED%; border: 1px solid transparent;
            border-radius: %R_MD%px; padding: 5px 9px; color: %TEXT%;
            selection-background-color: %ACCENTDIM%; selection-color: #ffffff;
        }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus,
        QFontComboBox:focus, QDoubleSpinBox:focus { border-color: %ACCENTDIM%; }
        QComboBox::drop-down, QFontComboBox::drop-down { border: none; width: 18px; }
        QComboBox QAbstractItemView { background: %RAISED%; border: 1px solid %LINE%;
                                      border-radius: %R_MD%px; padding: 4px;
                                      selection-background-color: %WASH%; outline: none; }
        QSpinBox::up-button, QSpinBox::down-button,
        QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 14px; border: none; }

        /* --------------------------------------------------------- lists */
        QListWidget, QUndoView { background: transparent; border: none; outline: none; }
        QListWidget::item { border-radius: %R_MD%px; padding: 2px; color: %TEXT%; }
        QListWidget::item:selected { background: %WASH%; }
        QListWidget::item:hover:!selected { background: %HOVER%; }

        /* ------------------------------------------------------- buttons */
        QPushButton { background: %RAISED%; border: none; border-radius: %R_MD%px;
                      padding: 7px 14px; color: %TEXT2%; }
        QPushButton:hover   { background: %HOVER%; color: %TEXT%; }
        QPushButton:pressed { background: %ACTIVE%; }
        QPushButton:disabled { color: %TEXT4%; background: %LINESOFT%; }
        QPushButton:default { color: %TEXT%; }

        /* Fusion draws a perfectly good checkmark; styling the indicator box
           would replace it with a blank blue square. */
        QCheckBox { color: %TEXT2%; spacing: 8px; }
        QLabel { color: %TEXT2%; background: transparent; }

        /* Sliders: a hairline track and a round handle. The handle's height
           comes from groove + margins, so 4 + 2*4 = 12 must match its width
           for the border-radius to read as a circle rather than a lozenge. */
        QSlider::groove:horizontal { height: 4px; background: %ACTIVE%; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: %ACCENTDIM%; border-radius: 2px; }
        QSlider::handle:horizontal { background: %TEXT%; width: 12px;
                                     margin: -4px 0; border-radius: 6px; }
        QSlider::handle:horizontal:hover { background: #ffffff; }

        /* ----------------------------------------------------- scrollbars */
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: %ACTIVE%; border-radius: 5px; min-height: 40px; }
        QScrollBar::handle:vertical:hover { background: #3A3A42; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
        QScrollBar::handle:horizontal { background: %ACTIVE%; border-radius: 5px; min-width: 40px; }
        QScrollBar::handle:horizontal:hover { background: #3A3A42; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

        /* Tooltips: a small rounded card, not a paragraph in a box. The corners
           only actually round because main.cpp makes the tip window
           translucent — Qt draws them square otherwise. */
        QToolTip { background: %RAISED%; color: %TEXT%; border: 1px solid %LINE%;
                   border-radius: %R_MD%px; padding: 4px 8px; font-size: 12px; }

        /* --------------------------------------------------------- dialogs */
        QDialog { background: %SHELL%; }
        QGroupBox { border: none; margin-top: 16px; color: %TEXT3%; }
        QGroupBox::title { subcontrol-origin: margin; left: 0; padding: 0 0 6px 0; }
    )")
        .replace("%VOID%",      Void)
        .replace("%SHELL%",     Shell)
        .replace("%PANEL%",     Panel)
        .replace("%RAISED%",    Raised)
        .replace("%HOVER%",     Hover)
        .replace("%ACTIVE%",    Active)
        .replace("%LINESOFT%",  LineSoft)
        .replace("%LINE%",      Line)
        .replace("%TEXT2%",     Text2)
        .replace("%TEXT3%",     Text3)
        .replace("%TEXT4%",     Text4)
        .replace("%TEXT%",      Text)
        .replace("%ACCENTDIM%", AccentDim)
        .replace("%ACCENT%",    Accent)
        .replace("%WASH%",      AccentWash)
        .replace("%R_SM%",      QString::number(RadiusSm))
        .replace("%R_MD%",      QString::number(RadiusMd))
        .replace("%R_LG%",      QString::number(RadiusLg));
}

} // namespace Theme
