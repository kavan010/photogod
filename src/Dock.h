#pragma once
#include <QWidget>
#include <QPointer>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>

class QSplitter;
class QStackedLayout;

class DockManager;
class DockArea;
class DockGroup;
class DockTabBar;
class DockOverlay;
class DockGhost;
class DockFloating;

// ---------------------------------------------------------------------------
// A workspace built from three ideas, and nothing else:
//
//   panel  — one tool (Layers, Brushes, the canvas ...). Always lives in a group.
//   group  — a tab strip plus the visible panel. Drop a panel on the middle of a
//            group and it joins as a tab.
//   split  — groups tiled side by side. Drop a panel near an edge and it takes
//            that edge, splitting whatever it landed on.
//
// Anything can go anywhere: a panel dropped outside the window becomes a
// floating workspace that is itself a full dock area, so panels can be grouped
// and split inside it and the whole thing dragged back in.
// ---------------------------------------------------------------------------

// Where a dragged panel would land relative to the thing under the cursor.
enum class DockZone { None, Center, Left, Right, Top, Bottom };

// ============================== DockPanel ==============================

// One movable tool. Owns its content widget and remembers its identity so a
// layout can be written to disk and rebuilt.
class DockPanel : public QWidget
{
    Q_OBJECT
public:
    DockPanel(const QString& id, const QString& title, QWidget* content,
              QWidget* parent = nullptr);

    QString id() const { return m_id; }
    QString title() const { return m_title; }
    void setTitle(const QString& t);

    // The canvas is pinned: it cannot be dragged out, closed, or left behind.
    bool isPinned() const { return m_pinned; }
    void setPinned(bool p);

    DockGroup* group() const;

    // Where this panel sat before it was closed, so reopening puts it back
    // instead of dumping it in a default corner.
    QPointer<DockGroup> lastGroup;

signals:
    void titleChanged();

private:
    QString m_id, m_title;
    bool m_pinned = false;
};

// ============================== DockTabBar =============================

// The strip along the top of a group. Every tab is a drag handle; the strip's
// empty space moves the whole floating workspace it belongs to.
class DockTabBar : public QWidget
{
    Q_OBJECT
public:
    explicit DockTabBar(DockGroup* group);

    void refresh();
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // Caret shown while a drag hovers this strip, marking the insert slot.
    void setInsertCaret(int index);   // -1 clears
    int  insertIndexAt(const QPoint& local) const;
    QRect tabRect(int i) const;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    struct Tab { QRect rect, closeRect; };
    void rebuild();
    int tabAt(const QPoint& p) const;

    DockGroup* m_group = nullptr;
    QList<Tab> m_tabs;
    int m_hover = -1;
    int m_hoverClose = -1;
    int m_caret = -1;
    bool m_pressed = false;
    bool m_dragging = false;
    QPoint m_pressPos;
    int m_pressTab = -1;
};

// =============================== DockGroup =============================

// A tab strip plus a stack of panels. The leaf of the layout tree.
class DockGroup : public QWidget
{
    Q_OBJECT
public:
    explicit DockGroup(DockManager* mgr, QWidget* parent = nullptr);

    void addPanel(DockPanel* p, int index = -1);
    void removePanel(DockPanel* p);

    int count() const { return m_panels.size(); }
    DockPanel* panelAt(int i) const;
    const QList<DockPanel*>& panels() const { return m_panels; }
    int indexOf(DockPanel* p) const { return m_panels.indexOf(p); }

    int currentIndex() const;
    void setCurrentIndex(int i);
    DockPanel* currentPanel() const { return panelAt(currentIndex()); }

    DockManager* manager() const { return m_mgr; }
    DockArea* area() const;
    DockTabBar* tabBar() const { return m_bar; }

    // True when the only panel is the pinned canvas — then the strip is hidden,
    // because the canvas already has its own document tabs.
    bool isBareCanvas() const;
    void updateChrome();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    DockManager* m_mgr = nullptr;
    DockTabBar* m_bar = nullptr;
    QStackedLayout* m_stack = nullptr;
    QList<DockPanel*> m_panels;
};

// =============================== DockArea ==============================

// Holds one layout tree: a single group, or a splitter of groups and splitters.
// The main window has one; every floating workspace has its own.
class DockArea : public QWidget
{
    Q_OBJECT
public:
    explicit DockArea(DockManager* mgr, QWidget* parent = nullptr);

    QWidget* root() const { return m_root; }
    // Installs w as the tree. The previous root is detached, never deleted —
    // callers that are re-parenting it elsewhere depend on that.
    void setRoot(QWidget* w);

    QList<DockGroup*> groups() const;
    DockGroup* groupAt(const QPoint& globalPos) const;
    DockManager* manager() const { return m_mgr; }

    DockFloating* floatingWindow() const;   // null for the main area

private:
    DockManager* m_mgr = nullptr;
    QWidget* m_root = nullptr;
};

// ============================= DockFloating ============================

// A frameless top-level workspace. Its content is a full DockArea, so a torn
// off panel can grow into an arrangement of its own.
class DockFloating : public QWidget
{
    Q_OBJECT
public:
    explicit DockFloating(DockManager* mgr);

    DockArea* area() const { return m_area; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    bool event(QEvent*) override;

private:
    Qt::Edges edgesAt(const QPoint& local) const;

    DockManager* m_mgr = nullptr;
    DockArea* m_area = nullptr;
};

// ============================== DockOverlay ============================

// The drop preview: a translucent sheet over the area being targeted, painting
// the exact region the panel would occupy plus a one-line explanation.
class DockOverlay : public QWidget
{
    Q_OBJECT
public:
    DockOverlay();
    void showPreview(const QRect& globalRect, const QString& caption, bool tabbed);
    void hidePreview();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QRect m_local;
    QString m_caption;
    bool m_tabbed = false;
};

// =============================== DockGhost =============================

// The chip that follows the cursor during a drag.
class DockGhost : public QWidget
{
    Q_OBJECT
public:
    DockGhost();
    void showFor(const QString& title, const QPoint& globalPos);
    void moveTo(const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString m_title;
};

// ============================== DockManager ============================

class DockManager : public QObject
{
    Q_OBJECT
public:
    explicit DockManager(QWidget* parent = nullptr);
    ~DockManager() override;

    DockArea* mainArea() const { return m_mainArea; }

    // Registers a panel and puts it in the main area. `zone`/`relativeTo`
    // describe the initial placement; passing nothing appends to the right.
    DockPanel* addPanel(const QString& id, const QString& title, QWidget* content,
                        DockZone zone = DockZone::Right,
                        DockPanel* relativeTo = nullptr);

    DockPanel* panel(const QString& id) const;
    QList<DockPanel*> panels() const { return m_panels; }

    // Placement primitives. `zone == Center` tabs into the target's group.
    void dockInto(DockPanel* p, DockGroup* target, DockZone zone, int tabIndex = -1);
    void dockToAreaEdge(DockPanel* p, DockArea* area, DockZone zone);
    void floatPanel(DockPanel* p, const QPoint& globalTopLeft, const QSize& size = QSize());

    bool isOpen(DockPanel* p) const;      // in a tree somewhere (not closed)
    void closePanel(DockPanel* p);
    void openPanel(DockPanel* p);         // back where it was, or the right edge
    void togglePanel(DockPanel* p);

    void setPanelsVisible(bool on);       // hides floating workspaces with the chrome
    bool panelsVisible() const { return m_panelsVisible; }

    // Layout persistence + the built-in arrangement.
    QJsonObject saveLayout() const;
    bool restoreLayout(const QJsonObject& o);
    void captureDefaultLayout();
    void resetLayout();

    // Drag lifecycle, driven by DockTabBar.
    void beginDrag(DockPanel* p, const QPoint& globalPos);
    void updateDrag(const QPoint& globalPos);
    void endDrag(const QPoint& globalPos);
    void cancelDrag();
    bool dragging() const { return m_dragPanel != nullptr; }

    void raiseFloating(DockFloating* f);   // keeps hit-testing order sane
    void forgetFloating(DockFloating* f);

signals:
    void layoutChanged();                  // panel opened, closed, or moved

protected:
    bool eventFilter(QObject*, QEvent*) override;   // Escape aborts a drag

private:
    struct DropTarget {
        enum Kind { NoDrop, Tab, SplitGroup, SplitArea, Float } kind = Float;
        QPointer<DockGroup> group;
        QPointer<DockArea> area;
        DockZone zone = DockZone::None;
        int tabIndex = -1;
        QRect preview;      // global
        QString caption;
    };

    DropTarget resolve(const QPoint& globalPos) const;
    DockArea* areaAt(const QPoint& globalPos) const;

    void placeAtAreaEdge(DockPanel* p, DockArea* area, DockZone zone);   // no pin check
    void detach(DockPanel* p);             // pull out of its group, prune the tree
    void pruneFrom(QWidget* w);            // collapse empty groups / lone splitters
    void replaceInParent(QWidget* oldW, QWidget* newW);
    DockGroup* makeGroup(DockPanel* p);
    void splitWidget(QWidget* target, DockGroup* fresh, DockZone zone);
    void clearCaret();

    QJsonObject serializeNode(QWidget* w) const;
    QWidget* buildNode(const QJsonObject& o, DockArea* area, QList<QString>* placed);

    QWidget* m_host = nullptr;
    DockArea* m_mainArea = nullptr;
    QList<DockPanel*> m_panels;
    QList<DockFloating*> m_floats;         // front = most recently raised
    QJsonObject m_defaultLayout;
    bool m_panelsVisible = true;

    // drag state
    QPointer<DockPanel> m_dragPanel;
    QPointer<DockTabBar> m_caretBar;
    DockOverlay* m_overlay = nullptr;
    DockGhost* m_ghost = nullptr;
    DropTarget m_target;
    QSize m_dragSize;                      // size the panel had when picked up

    friend class DockTabBar;
    friend class DockGroup;
    friend class DockFloating;
};
