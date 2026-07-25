#include "DockTitleBar.h"

#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QFontMetrics>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QToolTip>

// ---- palette (matches the app stylesheet in main.cpp) ----
namespace {
const QColor kStripBg     ("#1c1c1e");
const QColor kTabActiveBg ("#242427");
const QColor kTabHoverBg  ("#232328");
const QColor kTextActive  ("#f0f0f4");
const QColor kTextIdle    ("#8a8a92");
const QColor kAccent      ("#4f7cff");   // the "blue" of the drop hints
const QColor kDivider     ("#2a2a2e");

constexpr int kStripH   = 26;
constexpr int kTabPadX  = 12;
constexpr int kCloseW   = 16;
// A drag only starts once the pointer clears this many pixels, so a plain
// click-to-raise never turns into an accidental tear-off.
constexpr int kDragSlop = 6;
}

// ============================ DropIndicator ============================

DropIndicator::DropIndicator(QWidget* parent)
    : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
}

void DropIndicator::showAs(Shape s, const QRect& globalRect)
{
    if (s == None || globalRect.isEmpty()) { hideHint(); return; }
    m_shape = s;
    setGeometry(globalRect);
    show();
    raise();
    update();
}

void DropIndicator::hideHint()
{
    m_shape = None;
    hide();
}

void DropIndicator::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);

    if (m_shape == Bar) {
        // Solid accent line: "this panel becomes its own row here".
        p.fillRect(rect(), kAccent);
    } else if (m_shape == Box) {
        // Outline + wash: "this panel joins that group as a tab".
        QColor fill = kAccent;
        fill.setAlpha(48);
        p.fillRect(r, fill);
        p.setPen(QPen(kAccent, 2));
        p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
    }
}

// ============================ DockTitleBar ============================

// One shared indicator for the whole app — only one drag can be live at a time.
static DropIndicator* indicator()
{
    static QPointer<DropIndicator> inst;
    if (!inst) inst = new DropIndicator;
    return inst;
}

DockTitleBar::DockTitleBar(QDockWidget* dock)
    : QWidget(dock), m_dock(dock)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);

    connect(dock, &QDockWidget::windowTitleChanged, this, [this] { refresh(); });
    // Floating/re-docking changes who shares this strip.
    connect(dock, &QDockWidget::topLevelChanged, this, [this] { refresh(); });

    rebuild();
}

QSize DockTitleBar::sizeHint() const
{
    // Width must cover the tabs, otherwise the strip is squeezed and titles
    // elide to "Col…" even when the panel below is plenty wide.
    int w = 0;
    for (const Tab& t : m_tabs) w += t.rect.width();
    return QSize(w, kStripH);
}

QSize DockTitleBar::minimumSizeHint() const { return QSize(0, kStripH); }

QMainWindow* DockTitleBar::mainWindow() const
{
    return m_dock ? qobject_cast<QMainWindow*>(m_dock->parentWidget()) : nullptr;
}

// Every dock tabbed together with ours, in tab order, ours included.
QList<QDockWidget*> DockTitleBar::groupDocks() const
{
    QList<QDockWidget*> out;
    if (!m_dock) return out;
    QMainWindow* mw = mainWindow();
    if (!mw || m_dock->isFloating()) { out << m_dock; return out; }

    // tabifiedDockWidgets() omits the dock itself, so splice it back in at the
    // position matching the on-screen tab order.
    QList<QDockWidget*> siblings = mw->tabifiedDockWidgets(m_dock);
    siblings.removeAll(nullptr);
    if (siblings.isEmpty()) { out << m_dock; return out; }

    out = siblings;
    out << m_dock;
    // Order by on-screen position so tabs don't jump around between rebuilds.
    std::sort(out.begin(), out.end(), [](QDockWidget* a, QDockWidget* b) {
        QPoint pa = a->mapToGlobal(QPoint(0, 0));
        QPoint pb = b->mapToGlobal(QPoint(0, 0));
        if (pa.y() != pb.y()) return pa.y() < pb.y();
        return pa.x() < pb.x();
    });
    return out;
}

void DockTitleBar::rebuild()
{
    m_tabs.clear();
    const QList<QDockWidget*> docks = groupDocks();
    QFontMetrics fm(font());

    int x = 0;
    for (QDockWidget* d : docks) {
        if (!d || !d->isVisible()) continue;
        Tab t;
        t.dock = d;
        int w = fm.horizontalAdvance(d->windowTitle()) + kTabPadX * 2;
        if (d == m_dock) w += kCloseW;          // × only on the active tab
        t.rect = QRect(x, 0, w, kStripH);
        if (d == m_dock)
            t.closeRect = QRect(x + w - kCloseW - 6, (kStripH - kCloseW) / 2,
                                kCloseW, kCloseW);
        m_tabs << t;
        x += w;
    }
    update();
}

void DockTitleBar::refresh()
{
    rebuild();
    updateGeometry();
}

int DockTitleBar::tabAt(const QPoint& p) const
{
    for (int i = 0; i < m_tabs.size(); ++i)
        if (m_tabs[i].rect.contains(p)) return i;
    return -1;
}

void DockTitleBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), kStripBg);

    // Divider under the strip separates the header from the panel body, and
    // between stacked panels reads as the horizontal rule between windows.
    p.setPen(kDivider);
    p.drawLine(0, height() - 1, width(), height() - 1);

    QFontMetrics fm(font());
    for (int i = 0; i < m_tabs.size(); ++i) {
        const Tab& t = m_tabs[i];
        if (!t.dock) continue;
        const bool active = (t.dock == m_dock);

        if (active) {
            p.fillRect(t.rect, kTabActiveBg);
            // Accent underline marks the visible panel, like a tab anchor.
            p.fillRect(QRect(t.rect.left(), t.rect.bottom() - 1, t.rect.width(), 2),
                       kAccent);
        } else if (i == m_hover) {
            p.fillRect(t.rect, kTabHoverBg);
        }

        p.setPen(active ? kTextActive : kTextIdle);
        // Right inset mirrors rebuild()'s width: padding both sides, plus the ×
        // slot on the active tab. That leaves the text area exactly equal to the
        // string's advance, and elidedText() trims at >=, so give it a pixel of
        // slack — otherwise every title renders as "Adjustmen…".
        QRect textRect = t.rect.adjusted(kTabPadX, 0,
                                         active ? -(kTabPadX + kCloseW) : -kTabPadX, 0);
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(t.dock->windowTitle(), Qt::ElideRight, textRect.width() + 1));

        if (active && !t.closeRect.isNull()) {
            p.setPen(i == m_hoverClose ? kTextActive : kTextIdle);
            p.drawText(t.closeRect, Qt::AlignCenter, "×");
        }
    }
}

bool DockTitleBar::event(QEvent* e)
{
    // Rebuild lazily when the group changes underneath us (Qt gives no signal
    // for tabify), so a strip never shows stale tabs. FontChange matters too:
    // tab widths are measured from the font, and the stylesheet's font-size
    // lands after construction — stale metrics would elide the labels.
    if (e->type() == QEvent::Show || e->type() == QEvent::ParentChange
        || e->type() == QEvent::FontChange || e->type() == QEvent::StyleChange)
        rebuild();
    return QWidget::event(e);
}

void DockTitleBar::leaveEvent(QEvent*)
{
    if (m_hover != -1 || m_hoverClose != -1) {
        m_hover = m_hoverClose = -1;
        update();
    }
}

void DockTitleBar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) { QWidget::mousePressEvent(e); return; }

    const QPoint pos = e->position().toPoint();
    const int i = tabAt(pos);
    if (i < 0) { e->ignore(); return; }   // dead space in the strip is inert

    // × on the active tab closes the panel.
    if (m_tabs[i].dock == m_dock && m_tabs[i].closeRect.contains(pos)) {
        m_dock->close();
        e->accept();
        return;
    }

    // Clicking another tab in the group brings it forward.
    if (m_tabs[i].dock && m_tabs[i].dock != m_dock) {
        m_tabs[i].dock->raise();
        m_tabs[i].dock->show();
    }

    m_pressed = true;
    m_pressPos = pos;
    m_dragDock = m_tabs[i].dock;
    e->accept();
}

void DockTitleBar::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint pos = e->position().toPoint();

    if (!m_pressed) {
        // Hover feedback only.
        const int i = tabAt(pos);
        int hc = (i >= 0 && m_tabs[i].dock == m_dock
                  && m_tabs[i].closeRect.contains(pos)) ? i : -1;
        if (i != m_hover || hc != m_hoverClose) {
            m_hover = i;
            m_hoverClose = hc;
            setCursor(i >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
            update();
        }
        QWidget::mouseMoveEvent(e);
        return;
    }

    if (!m_dragging
        && (pos - m_pressPos).manhattanLength() < kDragSlop) return;   // still a click

    if (!m_dragging) {
        m_dragging = true;
        setCursor(Qt::ClosedHandCursor);
    }
    updateDropHint(e->globalPosition().toPoint());
    e->accept();
}

void DockTitleBar::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) { QWidget::mouseReleaseEvent(e); return; }

    const bool wasDragging = m_dragging;
    const QPoint global = e->globalPosition().toPoint();

    m_pressed = false;
    m_dragging = false;
    // Always restore the cursor: a drag that ended anywhere — including over
    // another widget or off-screen — must not leave a stale grab shape behind.
    unsetCursor();
    setCursor(tabAt(e->position().toPoint()) >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
    indicator()->hideHint();

    if (wasDragging) commitDrop(global);

    m_dragDock = nullptr;
    m_dropDock = nullptr;
    e->accept();
}

void DockTitleBar::mouseDoubleClickEvent(QMouseEvent* e)
{
    const int i = tabAt(e->position().toPoint());
    if (e->button() == Qt::LeftButton && i >= 0 && m_tabs[i].dock
        && (m_tabs[i].dock->features() & QDockWidget::DockWidgetFloatable)) {
        QDockWidget* d = m_tabs[i].dock;
        d->setFloating(!d->isFloating());
        refresh();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

// Find the strip under the cursor (if any) and the dock that owns it.
DockTitleBar* DockTitleBar::stripAt(const QPoint& globalPos, QDockWidget** owner) const
{
    QMainWindow* mw = mainWindow();
    if (!mw) return nullptr;
    for (QDockWidget* d : mw->findChildren<QDockWidget*>()) {
        if (!d->isVisible()) continue;
        auto* strip = qobject_cast<DockTitleBar*>(d->titleBarWidget());
        if (!strip || !strip->isVisible()) continue;
        QRect g(strip->mapToGlobal(QPoint(0, 0)), strip->size());
        if (g.contains(globalPos)) {
            if (owner) *owner = d;
            return strip;
        }
    }
    return nullptr;
}

void DockTitleBar::updateDropHint(const QPoint& globalPos)
{
    QMainWindow* mw = mainWindow();
    if (!mw || !m_dragDock) return;

    m_dropDock = nullptr;
    m_dropAsTab = false;
    m_dropBelow = false;

    // --- grouping zone: over a tab strip -> merge as a tab (blue box) ---
    QDockWidget* owner = nullptr;
    if (DockTitleBar* strip = stripAt(globalPos, &owner)) {
        if (owner && owner != m_dragDock) {
            m_dropDock = owner;
            m_dropAsTab = true;
            QRect g(strip->mapToGlobal(QPoint(0, 0)), strip->size());
            indicator()->showAs(DropIndicator::Box, g);
            return;
        }
    }

    // --- docking zone: over a panel body -> its own row (thin blue bar) ---
    for (QDockWidget* d : mw->findChildren<QDockWidget*>()) {
        if (!d->isVisible() || d == m_dragDock || d->isFloating()) continue;
        QRect g(d->mapToGlobal(QPoint(0, 0)), d->size());
        if (!g.contains(globalPos)) continue;

        // Top half inserts above, bottom half below.
        const bool below = globalPos.y() > g.center().y();
        m_dropDock = d;
        m_dropAsTab = false;
        m_dropBelow = below;

        const int kBar = 3;
        QRect bar(g.left(), below ? g.bottom() - kBar + 1 : g.top(), g.width(), kBar);
        indicator()->showAs(DropIndicator::Bar, bar);
        return;
    }

    // Nothing under the cursor: releasing here floats the panel.
    indicator()->hideHint();
}

void DockTitleBar::commitDrop(const QPoint& globalPos)
{
    QMainWindow* mw = mainWindow();
    QDockWidget* moving = m_dragDock;
    if (!mw || !moving) return;

    // Dropped over nothing -> float it where the cursor is.
    if (!m_dropDock) {
        if (moving->features() & QDockWidget::DockWidgetFloatable) {
            moving->setFloating(true);
            moving->move(globalPos - QPoint(60, 10));
        }
        refresh();
        return;
    }

    QDockWidget* target = m_dropDock;
    if (target == moving) return;

    if (m_dropAsTab) {
        // Merge into the target's group and bring the moved panel forward.
        if (moving->isFloating()) moving->setFloating(false);
        mw->tabifyDockWidget(target, moving);
        moving->show();
        moving->raise();
    } else {
        // Own row above or below the target. splitDockWidget puts `moving`
        // second, so for an "above" drop split from the moving panel instead.
        if (moving->isFloating()) moving->setFloating(false);
        if (m_dropBelow) {
            mw->splitDockWidget(target, moving, Qt::Vertical);
        } else {
            mw->splitDockWidget(target, moving, Qt::Vertical);
            // Swap the two so `moving` ends up on top of `target`.
            QList<QDockWidget*> pair{moving, target};
            mw->resizeDocks(pair, QList<int>{1, 1}, Qt::Vertical);
            mw->splitDockWidget(moving, target, Qt::Vertical);
        }
        moving->show();
    }

    // Qt may have just created a native tab bar for a new group; collapse it so
    // the group shows only our strip. Deferred: the tab bar appears during the
    // layout pass that follows this call, not inside it.
    QTimer::singleShot(0, mw, [mw] {
        for (QTabBar* tb : mw->findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly)) {
            tb->setFixedHeight(0);
            tb->hide();
        }
        // Every strip in the window may have gained or lost tabs.
        for (QDockWidget* d : mw->findChildren<QDockWidget*>())
            if (auto* s = qobject_cast<DockTitleBar*>(d->titleBarWidget())) s->refresh();
    });
}
