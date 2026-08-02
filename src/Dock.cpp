#include "Dock.h"
#include "Theme.h"

#include <QApplication>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSplitter>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QWindow>

// ---- palette: all tones come from the one design system in Theme.h ----
namespace {
const QColor kStripBg    = Theme::color(Theme::Shell);   // strip sits under the body
const QColor kGroupBg    = Theme::color(Theme::Panel);
const QColor kTabActive  = Theme::color(Theme::Panel);   // open tab merges into it
const QColor kTabHover   = Theme::color(Theme::Hover);
const QColor kTextActive = Theme::color(Theme::Text);
const QColor kTextIdle   = Theme::color(Theme::Text3);
const QColor kAccent     = Theme::color(Theme::Accent);
const QColor kDivider    = Theme::color(Theme::Line);
const QColor kFloatBg    = Theme::color(Theme::Shell);
const QColor kFloatEdge  = Theme::color(Theme::Line);

constexpr int kStripH    = 30;
constexpr int kTabPadX   = 11;
constexpr int kCloseW    = 16;
constexpr int kDragSlop  = 6;      // pixels before a click becomes a drag
constexpr int kEdgeBand  = 26;     // outer band of an area = dock to its edge
constexpr int kFloatPad  = 4;      // grab frame around a floating workspace

QRect globalRectOf(const QWidget* w)
{
    if (!w || !w->isVisible()) return QRect();
    return QRect(w->mapToGlobal(QPoint(0, 0)), w->size());
}

QSplitter* makeSplitter(Qt::Orientation o)
{
    auto* s = new QSplitter(o);
    s->setObjectName("dockSplit");
    s->setChildrenCollapsible(false);
    s->setHandleWidth(4);
    return s;
}

bool isHorizontal(DockZone z) { return z == DockZone::Left || z == DockZone::Right; }
bool isBefore(DockZone z)     { return z == DockZone::Left || z == DockZone::Top; }

QString zoneWord(DockZone z)
{
    switch (z) {
    case DockZone::Left:   return "left";
    case DockZone::Right:  return "right";
    case DockZone::Top:    return "top";
    case DockZone::Bottom: return "bottom";
    default:               return "here";
    }
}

// The slice `zone` would carve out of `r`.
QRect sliceOf(const QRect& r, DockZone z, int maxExtent = 360)
{
    if (z == DockZone::Center || z == DockZone::None) return r;
    if (isHorizontal(z)) {
        int w = qBound(80, r.width() / 3, maxExtent);
        return z == DockZone::Left ? QRect(r.left(), r.top(), w, r.height())
                                   : QRect(r.right() - w + 1, r.top(), w, r.height());
    }
    int h = qBound(60, r.height() / 3, maxExtent);
    return z == DockZone::Top ? QRect(r.left(), r.top(), r.width(), h)
                              : QRect(r.left(), r.bottom() - h + 1, r.width(), h);
}
}   // namespace

// ============================== DockPanel ==============================

DockPanel::DockPanel(const QString& id, const QString& title, QWidget* content,
                     QWidget* parent)
    : QWidget(parent), m_id(id), m_title(title)
{
    setObjectName("dockPanel_" + id);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    if (content) lay->addWidget(content);
}

void DockPanel::setTitle(const QString& t)
{
    if (t == m_title) return;
    m_title = t;
    emit titleChanged();
}

void DockPanel::setPinned(bool p)
{
    if (m_pinned == p) return;
    m_pinned = p;
    // Pinning is what makes a lone canvas hide its strip, and it is usually set
    // just after the panel is docked — so the group has to be told again.
    if (DockGroup* g = group()) g->updateChrome();
}

DockGroup* DockPanel::group() const
{
    // The panel sits in the group's stacked layout, so the group is the parent.
    return qobject_cast<DockGroup*>(parentWidget());
}

// ============================== DockTabBar =============================

DockTabBar::DockTabBar(DockGroup* group)
    : QWidget(group), m_group(group)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setFixedHeight(kStripH);
}

QSize DockTabBar::sizeHint() const
{
    int w = 0;
    for (const Tab& t : m_tabs) w += t.rect.width();
    return QSize(w, kStripH);
}

QSize DockTabBar::minimumSizeHint() const { return QSize(0, kStripH); }

void DockTabBar::rebuild()
{
    m_tabs.clear();
    if (!m_group) return;
    QFontMetrics fm(font());
    int x = 0;
    for (DockPanel* p : m_group->panels()) {
        Tab t;
        // The × slot is always reserved so a tab never reflows on hover.
        int w = fm.horizontalAdvance(p->title()) + kTabPadX * 2 + kCloseW;
        t.rect = QRect(x, 0, w, kStripH);
        t.closeRect = QRect(x + w - kCloseW - 5, (kStripH - kCloseW) / 2, kCloseW, kCloseW);
        m_tabs << t;
        x += w;
    }
}

void DockTabBar::refresh()
{
    rebuild();
    updateGeometry();
    update();
}

QRect DockTabBar::tabRect(int i) const
{
    return (i >= 0 && i < m_tabs.size()) ? m_tabs[i].rect : QRect();
}

int DockTabBar::tabAt(const QPoint& p) const
{
    for (int i = 0; i < m_tabs.size(); ++i)
        if (m_tabs[i].rect.contains(p)) return i;
    return -1;
}

// Which slot a drop on this strip would land in: the gap nearest the cursor.
int DockTabBar::insertIndexAt(const QPoint& local) const
{
    for (int i = 0; i < m_tabs.size(); ++i)
        if (local.x() < m_tabs[i].rect.center().x()) return i;
    return m_tabs.size();
}

void DockTabBar::setInsertCaret(int index)
{
    if (m_caret == index) return;
    m_caret = index;
    update();
}

void DockTabBar::paintEvent(QPaintEvent*)
{
    if (m_tabs.size() != (m_group ? m_group->count() : 0)) rebuild();

    QPainter p(this);
    p.fillRect(rect(), kStripBg);
    p.setPen(kDivider);
    p.drawLine(0, height() - 1, width(), height() - 1);

    const int cur = m_group ? m_group->currentIndex() : -1;
    QFontMetrics fm(font());

    for (int i = 0; i < m_tabs.size(); ++i) {
        DockPanel* panel = m_group->panelAt(i);
        if (!panel) continue;
        const Tab& t = m_tabs[i];
        const bool active = (i == cur);

        if (active) {
            p.fillRect(t.rect, kTabActive);
            p.fillRect(QRect(t.rect.left(), t.rect.bottom() - 1, t.rect.width(), 2), kAccent);
        } else if (i == m_hover) {
            p.fillRect(t.rect, kTabHover);
        }

        p.setPen(active ? kTextActive : kTextIdle);
        QRect textRect = t.rect.adjusted(kTabPadX, 0, -(kTabPadX + kCloseW), 0);
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(panel->title(), Qt::ElideRight, textRect.width() + 1));

        // × appears on the tab you are pointing at, and on the open one.
        if (!panel->isPinned() && (active || i == m_hover)) {
            p.setPen(i == m_hoverClose ? kTextActive : kTextIdle);
            p.drawText(t.closeRect, Qt::AlignCenter, "×");
        }
    }

    if (m_caret >= 0) {
        int x = m_caret < m_tabs.size() ? m_tabs[m_caret].rect.left()
                                        : (m_tabs.isEmpty() ? 0 : m_tabs.last().rect.right());
        p.fillRect(QRect(x - 1, 2, 2, height() - 5), kAccent);
    }
}

void DockTabBar::leaveEvent(QEvent*)
{
    if (m_hover != -1 || m_hoverClose != -1) {
        m_hover = m_hoverClose = -1;
        update();
    }
}

void DockTabBar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) { QWidget::mousePressEvent(e); return; }
    const QPoint pos = e->position().toPoint();
    const int i = tabAt(pos);

    if (i < 0) {
        // Empty strip: in a floating workspace this is the window's grab bar.
        if (m_group && m_group->area() && m_group->area()->floatingWindow()) {
            if (QWindow* wh = window()->windowHandle()) { wh->startSystemMove(); e->accept(); return; }
        }
        e->ignore();
        return;
    }

    DockPanel* panel = m_group->panelAt(i);
    if (!panel) return;

    if (!panel->isPinned() && m_tabs[i].closeRect.contains(pos)) {
        m_group->manager()->closePanel(panel);   // may delete this strip
        e->accept();
        return;
    }

    m_group->setCurrentIndex(i);
    m_pressed = true;
    m_pressTab = i;
    m_pressPos = pos;
    update();
    e->accept();
}

void DockTabBar::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint pos = e->position().toPoint();
    DockManager* mgr = m_group ? m_group->manager() : nullptr;

    if (!m_pressed) {
        const int i = tabAt(pos);
        DockPanel* panel = (i >= 0 && m_group) ? m_group->panelAt(i) : nullptr;
        const int hc = (panel && !panel->isPinned() && m_tabs[i].closeRect.contains(pos)) ? i : -1;
        if (i != m_hover || hc != m_hoverClose) {
            m_hover = i;
            m_hoverClose = hc;
            setCursor(i >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
            update();
        }
        QWidget::mouseMoveEvent(e);
        return;
    }

    if (!mgr) return;

    if (m_dragging) {
        mgr->updateDrag(e->globalPosition().toPoint());
        e->accept();
        return;
    }
    if ((pos - m_pressPos).manhattanLength() < kDragSlop) return;

    DockPanel* panel = m_group->panelAt(m_pressTab);
    if (!panel || panel->isPinned()) { m_pressed = false; return; }
    m_dragging = true;
    setCursor(Qt::ClosedHandCursor);
    mgr->beginDrag(panel, e->globalPosition().toPoint());
    e->accept();
}

void DockTabBar::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) { QWidget::mouseReleaseEvent(e); return; }

    const bool wasDragging = m_dragging;
    const QPoint global = e->globalPosition().toPoint();
    DockManager* mgr = m_group ? m_group->manager() : nullptr;

    m_pressed = false;
    m_dragging = false;
    m_pressTab = -1;
    unsetCursor();

    if (wasDragging && mgr) {
        mgr->endDrag(global);   // may reparent or delete this strip
        return;
    }
    e->accept();
}

void DockTabBar::mouseDoubleClickEvent(QMouseEvent* e)
{
    const int i = tabAt(e->position().toPoint());
    DockPanel* panel = (i >= 0 && m_group) ? m_group->panelAt(i) : nullptr;
    if (e->button() != Qt::LeftButton || !panel || panel->isPinned()) {
        QWidget::mouseDoubleClickEvent(e);
        return;
    }
    // Double-click is the no-drag shortcut: pop out, or snap back in.
    DockManager* mgr = m_group->manager();
    if (m_group->area() && m_group->area()->floatingWindow())
        mgr->dockToAreaEdge(panel, mgr->mainArea(), DockZone::Right);
    else
        mgr->floatPanel(panel, e->globalPosition().toPoint() - QPoint(60, 14), size());
    e->accept();
}

// =============================== DockGroup =============================

DockGroup::DockGroup(DockManager* mgr, QWidget* parent)
    : QWidget(parent), m_mgr(mgr)
{
    setObjectName("dockGroup");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    m_bar = new DockTabBar(this);
    lay->addWidget(m_bar);
    m_stack = new QStackedLayout;
    m_stack->setContentsMargins(0, 0, 0, 0);
    lay->addLayout(m_stack, 1);
}

DockArea* DockGroup::area() const
{
    for (QWidget* w = parentWidget(); w; w = w->parentWidget())
        if (auto* a = qobject_cast<DockArea*>(w)) return a;
    return nullptr;
}

DockPanel* DockGroup::panelAt(int i) const
{
    return (i >= 0 && i < m_panels.size()) ? m_panels[i] : nullptr;
}

int DockGroup::currentIndex() const { return m_stack->currentIndex(); }

void DockGroup::setCurrentIndex(int i)
{
    if (i < 0 || i >= m_panels.size()) return;
    m_stack->setCurrentIndex(i);
    m_bar->update();
}

void DockGroup::addPanel(DockPanel* p, int index)
{
    if (!p) return;
    if (index < 0 || index > m_panels.size()) index = m_panels.size();
    m_panels.insert(index, p);
    m_stack->insertWidget(index, p);
    p->lastGroup = this;
    p->show();
    connect(p, &DockPanel::titleChanged, this, [this] { m_bar->refresh(); });
    m_stack->setCurrentIndex(index);
    updateChrome();
    m_bar->refresh();
}

void DockGroup::removePanel(DockPanel* p)
{
    const int i = m_panels.indexOf(p);
    if (i < 0) return;
    m_panels.removeAt(i);
    m_stack->removeWidget(p);
    disconnect(p, &DockPanel::titleChanged, this, nullptr);
    p->lastGroup = this;
    p->hide();
    p->setParent(nullptr);
    updateChrome();
    m_bar->refresh();
}

bool DockGroup::isBareCanvas() const
{
    return m_panels.size() == 1 && m_panels.first()->isPinned();
}

void DockGroup::updateChrome()
{
    // The canvas alone needs no strip — it already carries the document tabs.
    m_bar->setVisible(!isBareCanvas() && !m_panels.isEmpty());
}

void DockGroup::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), kGroupBg);
}

// =============================== DockArea ==============================

DockArea::DockArea(DockManager* mgr, QWidget* parent)
    : QWidget(parent), m_mgr(mgr)
{
    setObjectName("dockArea");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
}

void DockArea::setRoot(QWidget* w)
{
    if (m_root == w) return;
    auto* lay = static_cast<QVBoxLayout*>(layout());
    if (m_root && m_root != w) {
        lay->removeWidget(m_root);
        m_root->hide();
        m_root->setParent(nullptr);
    }
    m_root = w;
    if (w) {
        lay->addWidget(w);
        w->show();
    }
}

QList<DockGroup*> DockArea::groups() const
{
    return findChildren<DockGroup*>();
}

DockGroup* DockArea::groupAt(const QPoint& globalPos) const
{
    for (DockGroup* g : groups())
        if (globalRectOf(g).contains(globalPos)) return g;
    return nullptr;
}

DockFloating* DockArea::floatingWindow() const
{
    return qobject_cast<DockFloating*>(const_cast<DockArea*>(this)->window());
}

// ============================= DockFloating ============================

DockFloating::DockFloating(DockManager* mgr)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint), m_mgr(mgr)
{
    setObjectName("dockFloating");
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMouseTracking(true);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(kFloatPad, kFloatPad, kFloatPad, kFloatPad);
    lay->setSpacing(0);
    m_area = new DockArea(mgr, this);
    lay->addWidget(m_area);
}

void DockFloating::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), kFloatBg);
    p.setPen(kFloatEdge);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

// The padding around the area doubles as the resize frame.
Qt::Edges DockFloating::edgesAt(const QPoint& local) const
{
    Qt::Edges e;
    const int m = kFloatPad + 2;
    if (local.x() <= m) e |= Qt::LeftEdge;
    if (local.x() >= width() - m) e |= Qt::RightEdge;
    if (local.y() <= m) e |= Qt::TopEdge;
    if (local.y() >= height() - m) e |= Qt::BottomEdge;
    return e;
}

void DockFloating::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        Qt::Edges edges = edgesAt(e->position().toPoint());
        if (edges && windowHandle()) {
            windowHandle()->startSystemResize(edges);
            e->accept();
            return;
        }
    }
    QWidget::mousePressEvent(e);
}

void DockFloating::mouseMoveEvent(QMouseEvent* e)
{
    Qt::Edges edges = edgesAt(e->position().toPoint());
    Qt::CursorShape shape = Qt::ArrowCursor;
    if ((edges & Qt::LeftEdge && edges & Qt::TopEdge)
        || (edges & Qt::RightEdge && edges & Qt::BottomEdge)) shape = Qt::SizeFDiagCursor;
    else if ((edges & Qt::RightEdge && edges & Qt::TopEdge)
             || (edges & Qt::LeftEdge && edges & Qt::BottomEdge)) shape = Qt::SizeBDiagCursor;
    else if (edges & (Qt::LeftEdge | Qt::RightEdge)) shape = Qt::SizeHorCursor;
    else if (edges & (Qt::TopEdge | Qt::BottomEdge)) shape = Qt::SizeVerCursor;
    setCursor(shape);
    QWidget::mouseMoveEvent(e);
}

void DockFloating::leaveEvent(QEvent* e)
{
    unsetCursor();
    QWidget::leaveEvent(e);
}

bool DockFloating::event(QEvent* e)
{
    if (e->type() == QEvent::WindowActivate && m_mgr) m_mgr->raiseFloating(this);
    if (e->type() == QEvent::Close && m_mgr) {
        // Closing the workspace closes everything parked in it.
        const QList<DockGroup*> gs = m_area->groups();
        for (DockGroup* g : gs) {
            const QList<DockPanel*> ps = g->panels();
            for (DockPanel* p : ps) m_mgr->closePanel(p);
        }
    }
    return QWidget::event(e);
}

// ============================== DockOverlay ============================

DockOverlay::DockOverlay()
    : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
}

void DockOverlay::showPreview(const QRect& globalRect, const QString& caption, bool tabbed)
{
    if (globalRect.isEmpty()) { hidePreview(); return; }
    m_caption = caption;
    m_tabbed = tabbed;
    setGeometry(globalRect);
    show();
    raise();
    update();
}

void DockOverlay::hidePreview() { hide(); }

void DockOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = rect().adjusted(1, 1, -1, -1);

    QColor fill = kAccent;
    fill.setAlpha(m_tabbed ? 60 : 44);
    QPainterPath path;
    path.addRoundedRect(r, Theme::RadiusMd, Theme::RadiusMd);
    p.fillPath(path, fill);
    p.setPen(QPen(kAccent, 2));
    p.drawPath(path);

    if (m_caption.isEmpty()) return;
    QFontMetrics fm(font());
    const QString text = fm.elidedText(m_caption, Qt::ElideRight, int(r.width()) - 24);
    QRect chip(0, 0, fm.horizontalAdvance(text) + 20, fm.height() + 10);
    chip.moveCenter(rect().center());
    QPainterPath chipPath;
    chipPath.addRoundedRect(chip, chip.height() / 2.0, chip.height() / 2.0);
    p.fillPath(chipPath, Theme::color(Theme::Shell, 235));
    p.setPen(kTextActive);
    p.drawText(chip, Qt::AlignCenter, text);
}

// =============================== DockGhost =============================

DockGhost::DockGhost()
    : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
}

void DockGhost::showFor(const QString& title, const QPoint& globalPos)
{
    m_title = title;
    QFontMetrics fm(font());
    resize(fm.horizontalAdvance(title) + 26, kStripH);
    moveTo(globalPos);
    show();
    raise();
    update();
}

void DockGhost::moveTo(const QPoint& globalPos) { move(globalPos + QPoint(14, 14)); }

void DockGhost::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, Theme::RadiusMd, Theme::RadiusMd);
    p.fillPath(path, Theme::color(Theme::Raised, 238));
    p.setPen(QPen(kAccent, 1.5));
    p.drawPath(path);
    p.setPen(kTextActive);
    p.drawText(rect(), Qt::AlignCenter, m_title);
}

// ============================== DockManager ============================

DockManager::DockManager(QWidget* parent)
    : QObject(parent), m_host(parent)
{
    m_mainArea = new DockArea(this, parent);
    m_overlay = new DockOverlay;
    m_ghost = new DockGhost;
}

DockManager::~DockManager()
{
    delete m_overlay;
    delete m_ghost;
    for (DockFloating* f : m_floats) delete f;
}

DockPanel* DockManager::panel(const QString& id) const
{
    for (DockPanel* p : m_panels)
        if (p->id() == id) return p;
    return nullptr;
}

DockPanel* DockManager::addPanel(const QString& id, const QString& title,
                                 QWidget* content, DockZone zone, DockPanel* relativeTo)
{
    auto* p = new DockPanel(id, title, content);
    m_panels << p;

    if (relativeTo && relativeTo->group())
        dockInto(p, relativeTo->group(), zone);
    else
        placeAtAreaEdge(p, m_mainArea, zone == DockZone::None ? DockZone::Right : zone);
    emit layoutChanged();
    return p;
}

DockGroup* DockManager::makeGroup(DockPanel* p)
{
    // Parented to the host so it never blinks up as its own top-level window in
    // the moment between creation and being placed in the tree. Deliberately
    // not hidden: an explicit hide() survives reparenting into a splitter, and
    // a hidden child of a QSplitter is allotted zero pixels forever after.
    auto* g = new DockGroup(this, m_host);
    g->addPanel(p);
    return g;
}

bool DockManager::isOpen(DockPanel* p) const { return p && p->group() != nullptr; }

// ---- tree surgery ----

void DockManager::replaceInParent(QWidget* oldW, QWidget* newW)
{
    QWidget* par = oldW->parentWidget();
    if (auto* sp = qobject_cast<QSplitter*>(par)) {
        const int idx = sp->indexOf(oldW);
        const QList<int> sizes = sp->sizes();
        sp->replaceWidget(idx, newW);
        sp->setSizes(sizes);
    } else if (auto* ar = qobject_cast<DockArea*>(par)) {
        ar->setRoot(newW);
    }
}

// Puts `fresh` next to `target` on the given side, reusing the parent splitter
// when its orientation already matches so the tree stays as flat as possible.
void DockManager::splitWidget(QWidget* target, DockGroup* fresh, DockZone zone)
{
    const Qt::Orientation o = isHorizontal(zone) ? Qt::Horizontal : Qt::Vertical;
    const bool before = isBefore(zone);
    const QSize prev = target->size();
    const int extent = (o == Qt::Horizontal) ? prev.width() : prev.height();
    const int give = qBound(140, extent / 3, 380);

    if (auto* sp = qobject_cast<QSplitter*>(target->parentWidget());
        sp && sp->orientation() == o) {
        const int idx = sp->indexOf(target);
        QList<int> sizes = sp->sizes();
        const int at = before ? idx : idx + 1;
        sp->insertWidget(at, fresh);
        fresh->show();
        if (idx < sizes.size()) {
            // The newcomer takes its share out of the panel it was dropped on.
            const int keep = qMax(80, sizes[idx] - give);
            const int taken = qMax(80, sizes[idx] - keep);
            sizes[idx] = keep;
            sizes.insert(at, taken);
            sp->setSizes(sizes);
        }
        return;
    }

    QSplitter* s = makeSplitter(o);
    QWidget* par = target->parentWidget();
    auto* parSplit = qobject_cast<QSplitter*>(par);
    auto* parArea  = qobject_cast<DockArea*>(par);
    int idx = -1;
    QList<int> parentSizes;
    if (parSplit) {
        idx = parSplit->indexOf(target);
        parentSizes = parSplit->sizes();
    }

    // Detach the target *through its owner*: an area that still believes it
    // holds this widget would pull it straight back out of the new splitter.
    if (parArea) parArea->setRoot(nullptr);
    else { target->hide(); target->setParent(nullptr); }

    if (before) { s->addWidget(fresh); s->addWidget(target); }
    else        { s->addWidget(target); s->addWidget(fresh); }
    target->show();
    fresh->show();

    if (parSplit) {
        parSplit->insertWidget(idx, s);
        if (!parentSizes.isEmpty()) parSplit->setSizes(parentSizes);
    } else if (parArea) {
        parArea->setRoot(s);
    } else {
        // Detached target (shouldn't happen) — keep the tree alive anyway.
        m_mainArea->setRoot(s);
    }
    s->setSizes(before ? QList<int>{give, qMax(80, extent - give)}
                       : QList<int>{qMax(80, extent - give), give});
}

// Collapses whatever the removal of a panel left behind, walking up the tree.
void DockManager::pruneFrom(QWidget* w)
{
    while (w) {
        if (auto* g = qobject_cast<DockGroup*>(w)) {
            if (g->count() > 0) return;
            QWidget* par = g->parentWidget();
            if (auto* ar = qobject_cast<DockArea*>(par)) ar->setRoot(nullptr);
            else { g->hide(); g->setParent(nullptr); }
            g->deleteLater();
            w = par;
            continue;
        }
        if (auto* sp = qobject_cast<QSplitter*>(w)) {
            if (sp->count() >= 2) return;
            QWidget* par = sp->parentWidget();
            if (sp->count() == 1) {
                // A splitter with one child is just that child.
                QWidget* child = sp->widget(0);
                child->hide();
                child->setParent(nullptr);
                replaceInParent(sp, child);
                child->show();
                sp->deleteLater();
                return;
            }
            if (auto* ar = qobject_cast<DockArea*>(par)) ar->setRoot(nullptr);
            else { sp->hide(); sp->setParent(nullptr); }
            sp->deleteLater();
            w = par;
            continue;
        }
        if (auto* ar = qobject_cast<DockArea*>(w)) {
            if (DockFloating* f = ar->floatingWindow()) {
                forgetFloating(f);
                f->hide();
                f->deleteLater();
            }
            return;
        }
        return;
    }
}

void DockManager::detach(DockPanel* p)
{
    DockGroup* g = p ? p->group() : nullptr;
    if (!g) return;
    g->removePanel(p);
    if (g->count() == 0) pruneFrom(g);
}

void DockManager::dockInto(DockPanel* p, DockGroup* target, DockZone zone, int tabIndex)
{
    if (!p || !target || p->isPinned()) return;
    DockGroup* old = p->group();

    if (zone == DockZone::Center) {
        if (old == target) {
            // Reordering inside the same strip.
            const int cur = target->indexOf(p);
            int at = tabIndex < 0 ? target->count() - 1 : tabIndex;
            if (at > cur) --at;
            if (at == cur) return;
            target->removePanel(p);
            target->addPanel(p, at);
        } else {
            detach(p);
            target->addPanel(p, tabIndex);
        }
        target->setCurrentIndex(target->indexOf(p));
    } else {
        if (old == target && target->count() == 1) return;   // no-op split
        detach(p);
        splitWidget(target, makeGroup(p), zone);
    }
    emit layoutChanged();
}

// The unguarded version: used when the manager itself is placing a panel, so
// it also works for the pinned canvas, which the user is not allowed to move.
void DockManager::placeAtAreaEdge(DockPanel* p, DockArea* area, DockZone zone)
{
    if (!p || !area) return;
    detach(p);
    DockGroup* fresh = makeGroup(p);
    if (QWidget* root = area->root()) splitWidget(root, fresh, zone);
    else area->setRoot(fresh);
}

void DockManager::dockToAreaEdge(DockPanel* p, DockArea* area, DockZone zone)
{
    if (!p || p->isPinned()) return;
    placeAtAreaEdge(p, area, zone);
    emit layoutChanged();
}

void DockManager::floatPanel(DockPanel* p, const QPoint& globalTopLeft, const QSize& size)
{
    if (!p || p->isPinned()) return;
    detach(p);
    auto* f = new DockFloating(this);
    f->area()->setRoot(makeGroup(p));
    QSize s = size.isValid() && size.width() > 120 ? size : QSize(320, 420);
    f->resize(s + QSize(kFloatPad * 2, kFloatPad * 2 + kStripH));
    f->move(globalTopLeft);
    m_floats.prepend(f);
    f->show();
    f->raise();
    emit layoutChanged();
}

void DockManager::raiseFloating(DockFloating* f)
{
    if (m_floats.removeAll(f)) m_floats.prepend(f);
}

void DockManager::forgetFloating(DockFloating* f) { m_floats.removeAll(f); }

// ---- open / close ----

void DockManager::closePanel(DockPanel* p)
{
    if (!p || p->isPinned() || !isOpen(p)) return;
    detach(p);
    emit layoutChanged();
}

void DockManager::openPanel(DockPanel* p)
{
    if (!p || isOpen(p)) return;
    if (DockGroup* g = p->lastGroup.data()) {
        g->addPanel(p);
        g->setCurrentIndex(g->indexOf(p));
        emit layoutChanged();
        return;
    }
    dockToAreaEdge(p, m_mainArea, DockZone::Right);
}

void DockManager::togglePanel(DockPanel* p)
{
    if (!p) return;
    if (isOpen(p)) closePanel(p);
    else openPanel(p);
}

void DockManager::setPanelsVisible(bool on)
{
    m_panelsVisible = on;
    for (DockFloating* f : m_floats) f->setVisible(on);
}

// ---- drag ----

DockArea* DockManager::areaAt(const QPoint& globalPos) const
{
    for (DockFloating* f : m_floats)
        if (f->isVisible() && globalRectOf(f->area()).contains(globalPos)) return f->area();
    if (m_mainArea->isVisible() && globalRectOf(m_mainArea).contains(globalPos))
        return m_mainArea;
    return nullptr;
}

DockManager::DropTarget DockManager::resolve(const QPoint& gp) const
{
    DropTarget t;
    DockArea* area = areaAt(gp);
    if (!area) {
        t.kind = DropTarget::Float;
        return t;
    }
    t.area = area;

    // 1. Over a tab strip: land in that strip, at the slot under the cursor.
    if (DockGroup* g = area->groupAt(gp)) {
        DockTabBar* bar = g->tabBar();
        if (bar->isVisible() && globalRectOf(bar).contains(gp)) {
            if (g == m_dragPanel->group() && g->count() == 1) { t.kind = DropTarget::NoDrop; return t; }
            t.kind = DropTarget::Tab;
            t.group = g;
            t.tabIndex = bar->insertIndexAt(bar->mapFromGlobal(gp));
            t.preview = globalRectOf(g);
            t.caption = "Add tab to this group";
            return t;
        }
    }

    // 2. Outer band of the whole area: take that full edge.
    const QRect ag = globalRectOf(area);
    const int band = qMin(kEdgeBand, qMin(ag.width(), ag.height()) / 6);
    DockZone edge = DockZone::None;
    int best = band + 1;
    const int dl = gp.x() - ag.left(), dr = ag.right() - gp.x();
    const int dt = gp.y() - ag.top(), db = ag.bottom() - gp.y();
    if (dl >= 0 && dl < best) { best = dl; edge = DockZone::Left; }
    if (dr >= 0 && dr < best) { best = dr; edge = DockZone::Right; }
    if (dt >= 0 && dt < best) { best = dt; edge = DockZone::Top; }
    if (db >= 0 && db < best) { best = db; edge = DockZone::Bottom; }
    if (edge != DockZone::None) {
        t.kind = DropTarget::SplitArea;
        t.zone = edge;
        t.preview = sliceOf(ag, edge);
        t.caption = "Dock to the " + zoneWord(edge) + " edge";
        return t;
    }

    // 3. Inside a group: the middle joins as a tab, the outer thirds split that
    //    side. The canvas is the exception — it can never be tabbed over, so
    //    every point on it maps to whichever of its four sides is nearest.
    DockGroup* g = area->groupAt(gp);
    if (!g) { t.kind = DropTarget::NoDrop; return t; }
    const QRect gr = globalRectOf(g);
    const bool sidesOnly = g->isBareCanvas();
    const double bw = sidesOnly ? gr.width() / 2.0
                                : qBound(24.0, gr.width() * 0.3, 170.0);
    const double bh = sidesOnly ? gr.height() / 2.0
                                : qBound(24.0, gr.height() * 0.3, 170.0);
    struct Cand { DockZone z; double d; } cands[4] = {
        { DockZone::Left,   (gp.x() - gr.left())   / bw },
        { DockZone::Right,  (gr.right() - gp.x())  / bw },
        { DockZone::Top,    (gp.y() - gr.top())    / bh },
        { DockZone::Bottom, (gr.bottom() - gp.y()) / bh },
    };
    DockZone zone = DockZone::Center;
    double m = sidesOnly ? 1e9 : 1.0;   // sidesOnly: always resolves to a side
    for (const Cand& c : cands)
        if (c.d < m) { m = c.d; zone = c.z; }

    const bool alone = (g == m_dragPanel->group() && g->count() == 1);
    if (alone) { t.kind = DropTarget::NoDrop; return t; }

    if (zone == DockZone::Center) {
        t.kind = DropTarget::Tab;
        t.group = g;
        t.tabIndex = -1;
        t.preview = gr;
        t.caption = "Tab with " + (g->currentPanel() ? g->currentPanel()->title()
                                                     : QString("this group"));
        return t;
    }
    t.kind = DropTarget::SplitGroup;
    t.group = g;
    t.zone = zone;
    t.preview = sliceOf(gr, zone);
    t.caption = "Split " + zoneWord(zone);
    return t;
}

void DockManager::clearCaret()
{
    if (m_caretBar) m_caretBar->setInsertCaret(-1);
    m_caretBar = nullptr;
}

void DockManager::beginDrag(DockPanel* p, const QPoint& globalPos)
{
    if (!p || p->isPinned()) return;
    m_dragPanel = p;
    m_dragSize = p->size();
    m_ghost->showFor(p->title(), globalPos);
    qApp->installEventFilter(this);
    updateDrag(globalPos);
}

void DockManager::updateDrag(const QPoint& globalPos)
{
    if (!m_dragPanel) return;
    m_ghost->moveTo(globalPos);
    m_target = resolve(globalPos);
    clearCaret();

    if (m_target.kind == DropTarget::Tab && m_target.group) {
        DockTabBar* bar = m_target.group->tabBar();
        if (bar->isVisible() && m_target.tabIndex >= 0) {
            bar->setInsertCaret(m_target.tabIndex);
            m_caretBar = bar;
        }
    }

    if (m_target.kind == DropTarget::Float || m_target.kind == DropTarget::NoDrop)
        m_overlay->hidePreview();
    else
        m_overlay->showPreview(m_target.preview, m_target.caption,
                               m_target.kind == DropTarget::Tab);
}

void DockManager::endDrag(const QPoint& globalPos)
{
    if (!m_dragPanel) return;
    updateDrag(globalPos);

    DockPanel* p = m_dragPanel;
    DropTarget t = m_target;
    cancelDrag();

    switch (t.kind) {
    case DropTarget::Tab:
        if (t.group) dockInto(p, t.group, DockZone::Center, t.tabIndex);
        break;
    case DropTarget::SplitGroup:
        if (t.group) dockInto(p, t.group, t.zone);
        break;
    case DropTarget::SplitArea:
        if (t.area) dockToAreaEdge(p, t.area, t.zone);
        break;
    case DropTarget::Float:
        floatPanel(p, globalPos - QPoint(50, 14), m_dragSize);
        break;
    case DropTarget::NoDrop:
        break;
    }
}

void DockManager::cancelDrag()
{
    clearCaret();
    m_overlay->hidePreview();
    m_ghost->hide();
    m_dragPanel = nullptr;
    m_target = DropTarget();
    qApp->removeEventFilter(this);
}

bool DockManager::eventFilter(QObject* obj, QEvent* e)
{
    if (m_dragPanel && e->type() == QEvent::KeyPress
        && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
        cancelDrag();
        return true;
    }
    return QObject::eventFilter(obj, e);
}

// ---- layout persistence ----

QJsonObject DockManager::serializeNode(QWidget* w) const
{
    QJsonObject o;
    if (auto* g = qobject_cast<DockGroup*>(w)) {
        o["type"] = "group";
        o["current"] = g->currentIndex();
        QJsonArray ids;
        for (DockPanel* p : g->panels()) ids.append(p->id());
        o["panels"] = ids;
        return o;
    }
    if (auto* sp = qobject_cast<QSplitter*>(w)) {
        o["type"] = "split";
        o["orient"] = sp->orientation() == Qt::Horizontal ? "h" : "v";
        QJsonArray sizes, kids;
        for (int s : sp->sizes()) sizes.append(s);
        for (int i = 0; i < sp->count(); ++i) {
            QJsonObject child = serializeNode(sp->widget(i));
            if (!child.isEmpty()) kids.append(child);
        }
        o["sizes"] = sizes;
        o["children"] = kids;
        return o;
    }
    return o;
}

QJsonObject DockManager::saveLayout() const
{
    QJsonObject o;
    o["version"] = 1;
    if (m_mainArea->root()) o["main"] = serializeNode(m_mainArea->root());

    QJsonArray floats;
    for (DockFloating* f : m_floats) {
        if (!f->area()->root()) continue;
        QJsonObject fo;
        const QRect g = f->geometry();
        fo["geom"] = QJsonArray{ g.x(), g.y(), g.width(), g.height() };
        fo["root"] = serializeNode(f->area()->root());
        floats.append(fo);
    }
    o["floats"] = floats;

    QJsonArray closed;
    for (DockPanel* p : m_panels)
        if (!isOpen(p) && !p->isPinned()) closed.append(p->id());
    o["closed"] = closed;
    return o;
}

QWidget* DockManager::buildNode(const QJsonObject& o, DockArea* area, QList<QString>* placed)
{
    const QString type = o["type"].toString();
    if (type == "group") {
        auto* g = new DockGroup(this, area);
        const QJsonArray ids = o["panels"].toArray();
        for (const QJsonValue& v : ids) {
            DockPanel* p = panel(v.toString());
            if (!p || placed->contains(p->id())) continue;
            g->addPanel(p);
            placed->append(p->id());
        }
        if (g->count() == 0) { delete g; return nullptr; }
        g->setCurrentIndex(qBound(0, o["current"].toInt(), g->count() - 1));
        return g;
    }
    if (type == "split") {
        QSplitter* s = makeSplitter(o["orient"].toString() == "h" ? Qt::Horizontal : Qt::Vertical);
        const QJsonArray kids = o["children"].toArray();
        QList<int> sizes;
        const QJsonArray js = o["sizes"].toArray();
        for (int i = 0; i < kids.size(); ++i) {
            QWidget* child = buildNode(kids[i].toObject(), area, placed);
            if (!child) continue;
            s->addWidget(child);
            sizes.append(i < js.size() ? js[i].toInt() : 200);
        }
        if (s->count() == 0) { delete s; return nullptr; }
        if (s->count() == 1) {
            QWidget* only = s->widget(0);
            only->setParent(nullptr);
            delete s;
            return only;
        }
        s->setSizes(sizes);
        return s;
    }
    return nullptr;
}

bool DockManager::restoreLayout(const QJsonObject& o)
{
    if (!o.contains("main")) return false;

    cancelDrag();

    // Empty every tree first, then rebuild from scratch: panels are shared, so
    // they have to be out of the old widgets before those are destroyed.
    for (DockPanel* p : m_panels)
        if (DockGroup* g = p->group()) g->removePanel(p);

    for (DockFloating* f : m_floats) { f->hide(); f->deleteLater(); }
    m_floats.clear();
    if (QWidget* root = m_mainArea->root()) {
        m_mainArea->setRoot(nullptr);
        root->deleteLater();
    }

    QList<QString> placed;
    QWidget* mainRoot = buildNode(o["main"].toObject(), m_mainArea, &placed);
    if (mainRoot) m_mainArea->setRoot(mainRoot);

    for (const QJsonValue& v : o["floats"].toArray()) {
        const QJsonObject fo = v.toObject();
        auto* f = new DockFloating(this);
        QWidget* root = buildNode(fo["root"].toObject(), f->area(), &placed);
        if (!root) { delete f; continue; }
        f->area()->setRoot(root);
        const QJsonArray g = fo["geom"].toArray();
        if (g.size() == 4)
            f->setGeometry(g[0].toInt(), g[1].toInt(), g[2].toInt(), g[3].toInt());
        m_floats.append(f);
        f->setVisible(m_panelsVisible);
    }

    // Anything the saved layout never mentioned: pinned panels must come back
    // no matter what — a workspace with no canvas in it is not a workspace —
    // and the rest stay closed only if they were closed on purpose.
    QList<QString> closed;
    for (const QJsonValue& v : o["closed"].toArray()) closed.append(v.toString());
    for (DockPanel* p : m_panels) {
        if (placed.contains(p->id())) continue;
        if (!p->isPinned() && closed.contains(p->id())) continue;
        placeAtAreaEdge(p, m_mainArea, p->isPinned() ? DockZone::Left : DockZone::Right);
        placed.append(p->id());
    }

    emit layoutChanged();
    return true;
}

void DockManager::captureDefaultLayout() { m_defaultLayout = saveLayout(); }

void DockManager::resetLayout()
{
    if (!m_defaultLayout.isEmpty()) restoreLayout(m_defaultLayout);
}
