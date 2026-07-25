#pragma once
#include <QWidget>
#include <QPointer>

class QDockWidget;
class QMainWindow;

// Photoshop-style drop hint drawn over the main window while a panel is being
// dragged. Two shapes, matching Photoshop's two drop-zone kinds:
//   Bar  — a thin line between panels: the panel docks as its own row.
//   Box  — an outline around a tab strip: the panel merges in as a tab.
// Frameless tool-tip window so it floats above the docks without stealing input.
class DropIndicator : public QWidget
{
    Q_OBJECT
public:
    enum Shape { None, Bar, Box };

    explicit DropIndicator(QWidget* parent = nullptr);
    void showAs(Shape s, const QRect& globalRect);
    void hideHint();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Shape m_shape = None;
};

// The tab strip that heads every dock panel. This *is* the panel's handle —
// there is no second title bar above it. Dragging a tab (and only a tab) tears
// that panel out; the rest of the strip is inert, exactly like Photoshop.
class DockTitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit DockTitleBar(QDockWidget* dock);

    // Docks tabbed together share one strip: each rebuilds its own tabs from
    // the group so every panel in the group draws the same row.
    void refresh();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    bool event(QEvent*) override;

private:
    struct Tab {
        QPointer<QDockWidget> dock;
        QRect rect;        // tab body
        QRect closeRect;   // × hit box, only on the active tab
    };

    void rebuild();                       // recompute m_tabs from the group
    QList<QDockWidget*> groupDocks() const;
    int tabAt(const QPoint& p) const;
    QMainWindow* mainWindow() const;

    // Drag handling
    void updateDropHint(const QPoint& globalPos);
    void commitDrop(const QPoint& globalPos);
    DockTitleBar* stripAt(const QPoint& globalPos, QDockWidget** owner) const;

    QDockWidget* m_dock = nullptr;
    QList<Tab> m_tabs;
    int m_hover = -1;
    int m_hoverClose = -1;
    bool m_pressed = false;
    bool m_dragging = false;
    QPoint m_pressPos;          // local
    QPointer<QDockWidget> m_dragDock;

    // Resolved drop target for the in-flight drag.
    QPointer<QDockWidget> m_dropDock;
    bool m_dropAsTab = false;   // true = merge as tab, false = dock as row
    bool m_dropBelow = false;   // for Bar drops: insert after the target
};
