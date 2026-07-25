#pragma once
#include "Common.h"
#include "Document.h"
#include <QMainWindow>
#include <QUndoGroup>
#include <QHash>
#include <QList>
#include <QSet>
#include <QByteArray>
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
class QActionGroup;
class QDockWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    Canvas* currentCanvas() const;
    Document* currentDoc() const;

    void newDocument(const QSize& size = QSize(), const QColor& bg = Qt::white);
    void openPath(const QString& path);

    void showHome();       // switch central view to the start screen
    void showEditor();     // switch central view to the document tabs

    void showCommandPalette(const QString& query = QString());

    // Tabify two panels by title. Used by --tabshot to verify a tabbed group
    // renders a single header.
    void tabifyPanelsForTest(const QString& a, const QString& b);

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
    void resetPanelLayout();       // restore the stock panel arrangement
    void hideNativeDockTabs();     // collapse Qt's tab bar; our strip replaces it
    void savePanelLayout();        // persist dock positions between sessions
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
    ToolType m_tool = ToolType::Move;

    QStackedWidget* m_central = nullptr;   // [0] = home, [1] = tabs
    HomePage* m_home = nullptr;
    void updateChromeForView();            // enable/disable docks+toolbars for home vs editor
    QTabWidget* m_tabs;
    QUndoGroup m_undoGroup;
    LayersPanel* m_layers;
    ColorPanel* m_color;
    PropertiesPanel* m_props;
    BrushesPanel* m_brushes;
    AdjustmentsPanel* m_adjust;
    HistoryPanel* m_history = nullptr;
    QDockWidget* m_dockProps = nullptr;
    QDockWidget* m_dockLayers = nullptr;
    QList<QDockWidget*> m_docks;    // every panel, in default stacking order
    QByteArray m_defaultLayout;     // saveState() of the stock arrangement
    QSet<QDockWidget*> m_userClosed;     // panels the user closed on purpose
    bool m_switchingView = false;        // suppresses m_userClosed bookkeeping
    bool m_shuttingDown = false;         // set in ~MainWindow; see updateTabTitle
    CommandPalette* m_palette = nullptr;

    QStackedWidget* m_optStack;
    QHash<int, QAction*> m_toolActions;   // key: int(ToolType)
    QSlider* m_brushSizeSlider = nullptr;
    QList<std::function<void()>> m_optionSync;   // refresh option widgets from m_ts

    QLabel *m_statusPos, *m_statusZoom, *m_statusSize;
};
