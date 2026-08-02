#pragma once
#include "Common.h"
#include "Document.h"
#include <QMainWindow>
#include <QUndoGroup>
#include <QHash>
#include <QList>
#include <QJsonObject>
#include <functional>

class Canvas;
class CommandPalette;
struct PaletteCommand;
class HomePage;
class LayersPanel;
class ColorPanel;
class PropertiesPanel;
class BrushesPanel;
class AdjustmentsPanel;
class HistoryPanel;
class QTabWidget;
class QStackedWidget;
class QLabel;
class QSlider;
class QToolBar;
class QActionGroup;
class QMenu;
class DockManager;
class DockPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    Canvas* currentCanvas() const;
    Document* currentDoc() const;

    void newDocument(const QSize& size = QSize(), const QColor& bg = Qt::white);
    // quiet: fail silently instead of putting a dialog in the way — used at
    // startup, where a stale path should just leave you on the home screen.
    void openPath(const QString& path, bool quiet = false);

    void showHome();       // switch central view to the start screen
    void showEditor();     // switch central view to the document tabs

    void showCommandPalette(const QString& query = QString());

    // Group two panels by id. Used by --tabshot to verify a tabbed group
    // renders a single header.
    void tabifyPanelsForTest(const QString& a, const QString& b);

    DockManager* dockManager() const { return m_dock; }   // --docktest drives this

    // Look up a palette command by exact title and run it. Returns false if no
    // such command exists. Used by --modeltest to drive the palette headlessly.
    bool runPaletteCommand(const QString& title);

protected:
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    void buildToolbar();
    void buildOptionsBar();
    void buildMenus();
    void buildDocks();
    void resetPanelLayout();          // restore the stock panel arrangement
    void applyDefaultProportions();   // first-run splitter sizes, once laid out
    void savePanelLayout();           // persist the workspace between sessions
    void revealPanel(DockPanel* p);   // open it and bring its tab forward
    void refreshWindowMenu();         // panel ticks, rebuilt on drop-down
    void toggleFocusMode();           // stash the workspace away and back
    void buildCommandPalette();

    // Universal search: rebuilt on every open so layers stay current.
    QList<PaletteCommand> collectCommands();
    void revealLayer(int index);

    void addDocument(Document* doc);
    void setTool(ToolType t);
    int pageForTool(ToolType t) const;
    bool closeTab(int index);           // returns false if cancelled
    bool saveDoc(Document* doc, bool saveAs);
    void exportDoc(Document* doc);
    void applyDirectFilter(FilterType t);
    void openFilterDialog(FilterType t);
    void updateTabTitle(Document* doc);

    // edit / select helpers
    void doCopy();
    void doPaste();
    void placeImageAsLayer(const QImage& img, const QString& name);

    ToolSettings m_ts;
    ToolType m_tool = ToolType::Brush;   // see ToolSettings — you start painting

    QStackedWidget* m_central = nullptr;   // [0] = home, [1] = the dock workspace
    HomePage* m_home = nullptr;
    void updateChromeForView();            // swap panels+toolbars for home vs editor
    QTabWidget* m_tabs;
    QUndoGroup m_undoGroup;
    LayersPanel* m_layers;
    ColorPanel* m_color;
    PropertiesPanel* m_props;
    BrushesPanel* m_brushes;
    AdjustmentsPanel* m_adjust;
    HistoryPanel* m_history = nullptr;
    DockManager* m_dock = nullptr;       // the whole panel workspace
    DockPanel* m_panelProps = nullptr;
    DockPanel* m_panelLayers = nullptr;
    QMenu* m_windowMenu = nullptr;       // rebuilt whenever the workspace changes
    QJsonObject m_zenLayout;             // the workspace focus mode put aside
    bool m_zen = false;
    bool m_shuttingDown = false;         // set in ~MainWindow; see updateTabTitle
    CommandPalette* m_palette = nullptr;

    QStackedWidget* m_optStack;
    QToolBar* m_optionsBar = nullptr;     // always on; its contents swap per tool
    QHash<int, QAction*> m_toolActions;   // key: int(ToolType)
    QSlider* m_brushSizeSlider = nullptr;
    QList<std::function<void()>> m_optionSync;   // refresh option widgets from m_ts

    void buildStatusBar();
    QLabel *m_statusPos, *m_statusZoom, *m_statusSize;
};
