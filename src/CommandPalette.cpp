#include "CommandPalette.h"
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QScrollBar>

// ---------------------------------------------------------------- fuzzy match

// Subsequence match with bonuses for consecutive hits and word-start hits, so
// "gs" finds "Grayscale" and "brsz" finds "Brush Size" ahead of loose matches.
int paletteFuzzyScore(const QString& needle, const QString& haystack)
{
    if (needle.isEmpty()) return 0;
    if (haystack.isEmpty()) return -1;

    const QString n = needle.toLower();
    const QString h = haystack.toLower();

    int score = 0;
    int hi = 0;
    int prevMatch = -2;
    for (int ni = 0; ni < n.size(); ++ni) {
        const QChar c = n[ni];
        if (c == ' ') continue;                 // spaces just separate terms
        int found = -1;
        for (; hi < h.size(); ++hi) {
            if (h[hi] == c) { found = hi; break; }
        }
        if (found < 0) return -1;               // ran out of haystack

        score += 1;
        if (found == prevMatch + 1) score += 6;         // consecutive run
        if (found == 0) score += 12;                    // matches at the very start
        else if (!h[found - 1].isLetterOrNumber()) score += 8;   // start of a word
        score -= std::min(found - std::max(prevMatch, 0), 4);    // penalize big gaps

        prevMatch = found;
        ++hi;
    }
    // shorter targets are more likely to be what was meant
    score += std::max(0, 24 - int(haystack.size())) / 3;
    return score;
}

// ------------------------------------------------------------------- delegate

namespace {

constexpr int kRoleCategory = Qt::UserRole + 1;
constexpr int kRoleTitle    = Qt::UserRole + 2;
constexpr int kRoleDetail   = Qt::UserRole + 3;
constexpr int kRoleIndex    = Qt::UserRole + 4;
constexpr int kRoleEnabled  = Qt::UserRole + 5;

constexpr int kRowHeight = 30;

// Draws: [category chip] Title .................... detail
class RowDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return QSize(100, kRowHeight);
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);

        const bool selected = opt.state & QStyle::State_Selected;
        const bool hovered  = opt.state & QStyle::State_MouseOver;
        const bool enabled  = idx.data(kRoleEnabled).toBool();

        QRect r = opt.rect.adjusted(4, 1, -4, -1);
        if (selected) {
            QPainterPath bg;
            bg.addRoundedRect(r, 6, 6);
            p->fillPath(bg, QColor("#34435f"));
        } else if (hovered) {
            QPainterPath bg;
            bg.addRoundedRect(r, 6, 6);
            p->fillPath(bg, QColor("#26262b"));
        }

        int x = r.left() + 10;

        // category chip
        const QString cat = idx.data(kRoleCategory).toString();
        if (!cat.isEmpty()) {
            QFont cf = opt.font;
            cf.setPointSizeF(std::max(7.0, opt.font.pointSizeF() - 1.5));
            cf.setCapitalization(QFont::AllUppercase);
            cf.setLetterSpacing(QFont::PercentageSpacing, 104);
            p->setFont(cf);
            QFontMetrics cfm(cf);
            int cw = cfm.horizontalAdvance(cat) + 12;
            QRect chip(x, r.center().y() - 8, cw, 16);
            QPainterPath cp;
            cp.addRoundedRect(chip, 4, 4);
            p->fillPath(cp, QColor(selected ? "#4a5b7d" : "#2b2b31"));
            p->setPen(QColor(enabled ? (selected ? "#cbd6ee" : "#8a8a95") : "#5c5c64"));
            p->drawText(chip, Qt::AlignCenter, cat);
            x = chip.right() + 10;
        }

        // detail on the right
        const QString detail = idx.data(kRoleDetail).toString();
        int rightEdge = r.right() - 10;
        if (!detail.isEmpty()) {
            QFont df = opt.font;
            df.setPointSizeF(std::max(7.5, opt.font.pointSizeF() - 1.0));
            p->setFont(df);
            QFontMetrics dfm(df);
            int dw = dfm.horizontalAdvance(detail);
            QRect dr(rightEdge - dw, r.top(), dw, r.height());
            p->setPen(QColor(selected ? "#a9b6d2" : "#6f6f79"));
            p->drawText(dr, Qt::AlignVCenter | Qt::AlignRight, detail);
            rightEdge = dr.left() - 12;
        }

        // title
        QFont tf = opt.font;
        p->setFont(tf);
        p->setPen(QColor(enabled ? (selected ? "#ffffff" : "#dcdce2") : "#63636b"));
        QRect tr(x, r.top(), std::max(10, rightEdge - x), r.height());
        QString title = QFontMetrics(tf).elidedText(idx.data(kRoleTitle).toString(),
                                                    Qt::ElideRight, tr.width());
        p->drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, title);

        p->restore();
    }
};

} // namespace

// ------------------------------------------------------------------- palette

CommandPalette::CommandPalette(QWidget* parent)
    : QWidget(parent, Qt::Popup)
{
    setObjectName("commandPalette");
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);

    auto* outer = new QVBoxLayout(this);
    // outer margin is the drop-shadow gutter painted in paintEvent
    outer->setContentsMargins(12, 12, 12, 12);

    auto* card = new QWidget;
    card->setObjectName("paletteCard");
    outer->addWidget(card);

    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_input = new QLineEdit;
    m_input->setObjectName("paletteInput");
    m_input->setPlaceholderText("Search tools, adjustments, layers, commands…");
    m_input->setClearButtonEnabled(false);
    lay->addWidget(m_input);

    m_list = new QListWidget;
    m_list->setObjectName("paletteList");
    m_list->setItemDelegate(new RowDelegate(m_list));
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setMouseTracking(true);
    m_list->setUniformItemSizes(true);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setFocusPolicy(Qt::NoFocus);          // keep typing in the line edit
    lay->addWidget(m_list);

    m_hint = new QLabel("↑↓ navigate · ↵ run · esc close");
    m_hint->setObjectName("paletteHint");
    lay->addWidget(m_hint);

    card->setStyleSheet(R"(
        #paletteCard { background: #232327; border: 1px solid #3a3a44; border-radius: 10px; }
        #paletteInput {
            background: transparent; border: none;
            border-bottom: 1px solid #33333b; border-radius: 0;
            padding: 10px 12px; color: #ecedf2; font-size: 14px;
            selection-background-color: #4f7cff;
        }
        #paletteList { background: transparent; border: none; outline: none; padding: 4px 2px; }
        #paletteHint {
            color: #6a6a74; font-size: 10px; padding: 5px 12px;
            border-top: 1px solid #2c2c34;
        }
    )");

    connect(m_input, &QLineEdit::textChanged, this, [this] { refilter(); });
    connect(m_input, &QLineEdit::returnPressed, this, [this] { accept(); });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem*) { accept(); });

    m_input->installEventFilter(this);
    setFixedWidth(620);
}

void CommandPalette::setProvider(std::function<QList<PaletteCommand>()> provider)
{
    m_provider = std::move(provider);
}

void CommandPalette::openPalette(const QString& initialQuery)
{
    m_all = m_provider ? m_provider() : QList<PaletteCommand>();

    m_input->blockSignals(true);
    m_input->setText(initialQuery);
    m_input->blockSignals(false);
    m_input->selectAll();

    refilter();
    positionOverParent();
    show();
    raise();
    m_input->setFocus(Qt::PopupFocusReason);
}

void CommandPalette::positionOverParent()
{
    QWidget* ref = parentWidget() ? parentWidget()->window() : nullptr;
    QRect area = ref ? QRect(ref->mapToGlobal(QPoint(0, 0)), ref->size())
                     : QGuiApplication::primaryScreen()->availableGeometry();
    int x = area.center().x() - width() / 2;
    int y = area.top() + std::max(40, area.height() / 10);
    move(x, y);
}

void CommandPalette::refilter()
{
    const QString q = m_input->text().trimmed();

    struct Scored { int idx; int score; };
    QList<Scored> hits;
    hits.reserve(m_all.size());

    for (int i = 0; i < m_all.size(); ++i) {
        const PaletteCommand& c = m_all[i];
        if (q.isEmpty()) {
            hits.append({i, 0});
            continue;
        }
        // Score the title, then fall back to category/keywords at a discount so
        // exact-ish title matches always sort above incidental keyword hits.
        int best = paletteFuzzyScore(q, c.title);
        if (int s = paletteFuzzyScore(q, c.category + " " + c.title); s > best) best = s;
        if (!c.keywords.isEmpty())
            if (int s = paletteFuzzyScore(q, c.keywords); s - 10 > best) best = s - 10;
        if (best < 0) continue;
        if (!c.enabled) best -= 40;              // available things first
        hits.append({i, best});
    }

    if (!q.isEmpty()) {
        std::stable_sort(hits.begin(), hits.end(),
                         [](const Scored& a, const Scored& b) { return a.score > b.score; });
    }

    m_shown.clear();
    m_list->clear();
    for (const Scored& s : hits) {
        const PaletteCommand& c = m_all[s.idx];
        auto* item = new QListWidgetItem;
        item->setData(kRoleCategory, c.category);
        item->setData(kRoleTitle, c.title);
        item->setData(kRoleDetail, c.detail);
        item->setData(kRoleIndex, s.idx);
        item->setData(kRoleEnabled, c.enabled);
        if (!c.enabled) item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        m_list->addItem(item);
        m_shown.append(s.idx);
    }

    // select the first selectable row
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->flags() & Qt::ItemIsSelectable) {
            m_list->setCurrentRow(i);
            break;
        }
    }

    const int rows = std::min(m_list->count(), 10);
    const int listH = rows > 0 ? rows * kRowHeight + 8 : 0;
    m_list->setFixedHeight(listH);
    m_list->setVisible(rows > 0);

    if (m_list->count() == 0 && !q.isEmpty())
        m_hint->setText("No matches for \"" + q + "\"");
    else
        m_hint->setText(QString("%1 result%2 · ↑↓ navigate · ↵ run · esc close")
                            .arg(m_list->count()).arg(m_list->count() == 1 ? "" : "s"));

    adjustSize();
    setFixedWidth(620);
}

void CommandPalette::moveSelection(int delta)
{
    const int n = m_list->count();
    if (n == 0) return;
    int row = m_list->currentRow();
    for (int step = 0; step < n; ++step) {
        row = (row + delta + n) % n;             // wrap around
        if (m_list->item(row)->flags() & Qt::ItemIsSelectable) {
            m_list->setCurrentRow(row);
            m_list->scrollToItem(m_list->item(row));
            return;
        }
    }
}

void CommandPalette::accept()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_shown.size()) return;
    const PaletteCommand cmd = m_all[m_shown[row]];   // copy: hide() may invalidate
    if (!cmd.enabled) return;
    hide();
    if (cmd.run) cmd.run();
}

void CommandPalette::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Escape:    hide(); return;
    case Qt::Key_Up:        moveSelection(-1); return;
    case Qt::Key_Down:      moveSelection(1); return;
    case Qt::Key_PageUp:    moveSelection(-8); return;
    case Qt::Key_PageDown:  moveSelection(8); return;
    case Qt::Key_Return:
    case Qt::Key_Enter:     accept(); return;
    default: break;
    }
    QWidget::keyPressEvent(e);
}

bool CommandPalette::eventFilter(QObject* o, QEvent* ev)
{
    // The line edit has focus, so navigation keys must be intercepted there.
    if (o == m_input && ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        switch (ke->key()) {
        case Qt::Key_Up:       moveSelection(-1); return true;
        case Qt::Key_Down:     moveSelection(1); return true;
        case Qt::Key_PageUp:   moveSelection(-8); return true;
        case Qt::Key_PageDown: moveSelection(8); return true;
        case Qt::Key_Escape:   hide(); return true;
        default: break;
        }
    }
    return QWidget::eventFilter(o, ev);
}

void CommandPalette::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    m_input->setFocus();
}

void CommandPalette::paintEvent(QPaintEvent*)
{
    // soft drop shadow in the translucent margin around the card
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QRect card = rect().adjusted(12, 12, -12, -12);
    for (int i = 10; i >= 1; --i) {
        QColor c(0, 0, 0, 6);
        QPainterPath path;
        path.addRoundedRect(card.adjusted(-i, -i + 2, i, i + 2), 10 + i, 10 + i);
        p.fillPath(path, c);
    }
}
