#pragma once
#include "Common.h"
#include "Document.h"
#include <QMainWindow>
#include <QUndoGroup>
#include <QHash>
#include <functional>

class Canvas;
class LayersPanel;
class ColorPanel;
class PropertiesPanel;
class BrushesPanel;
class AdjustmentsPanel;
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

    Canvas* currentCanvas() const;
    Document* currentDoc() const;

    void newDocument(const QSize& size = QSize(), const QColor& bg = Qt::white);
    void openPath(const QString& path);

protected:
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    void buildToolbar();
    void buildOptionsBar();
    void buildMenus();
    void buildDocks();

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

    QTabWidget* m_tabs;
    QUndoGroup m_undoGroup;
    LayersPanel* m_layers;
    ColorPanel* m_color;
    PropertiesPanel* m_props;
    BrushesPanel* m_brushes;
    AdjustmentsPanel* m_adjust;
    QDockWidget* m_dockProps = nullptr;

    QStackedWidget* m_optStack;
    QHash<int, QAction*> m_toolActions;   // key: int(ToolType)
    QSlider* m_brushSizeSlider = nullptr;
    QList<std::function<void()>> m_optionSync;   // refresh option widgets from m_ts

    QLabel *m_statusPos, *m_statusZoom, *m_statusSize;
};
