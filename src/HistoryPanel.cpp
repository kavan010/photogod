#include "HistoryPanel.h"
#include "Theme.h"
#include "Document.h"
#include <QUndoGroup>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QPainter>
#include <QMouseEvent>
#include <QDialog>
#include <QSlider>
#include <QCheckBox>
#include <QApplication>
#include <QScreen>
#include <QTimer>

// ============================ InstantHint ============================

InstantHint::InstantHint(QWidget* parent) : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(11, 9, 11, 10);
    lay->setSpacing(3);

    m_title = new QLabel;
    m_title->setObjectName("hintTitle");
    m_body = new QLabel;
    m_body->setObjectName("hintBody");
    m_body->setWordWrap(true);
    m_body->setMaximumWidth(230);
    lay->addWidget(m_title);
    lay->addWidget(m_body);

    setStyleSheet(QString(R"(
        #hintTitle { color: %TEXT%; font-size: 12px; }
        #hintBody  { color: %TEXT2%; font-size: 12px; }
    )").replace("%TEXT2%", Theme::Text2).replace("%TEXT%", Theme::Text));
}

InstantHint* InstantHint::instance()
{
    static QPointer<InstantHint> s_hint;
    if (!s_hint) s_hint = new InstantHint;
    return s_hint;
}

void InstantHint::showFor(QWidget* anchor, const QString& title, const QString& body)
{
    if (!anchor) return;
    InstantHint* h = instance();
    h->m_title->setText(title);
    h->m_body->setText(body);
    h->adjustSize();

    // Prefer sitting just above the control; flip below if that clips off-screen.
    QPoint topLeft = anchor->mapToGlobal(QPoint(0, 0));
    int x = topLeft.x() + anchor->width() / 2 - h->width() / 2;
    int y = topLeft.y() - h->height() - 8;
    if (QScreen* sc = QApplication::screenAt(topLeft)) {
        const QRect avail = sc->availableGeometry();
        if (y < avail.top()) y = topLeft.y() + anchor->height() + 8;
        x = std::clamp(x, avail.left() + 4, avail.right() - h->width() - 4);
    }
    h->move(x, y);
    h->show();
    h->raise();
}

void InstantHint::hideNow()
{
    InstantHint* h = instance();
    if (h) h->hide();
}

void InstantHint::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Theme::color(Theme::Line), 1));
    p.setBrush(Theme::color(Theme::Raised));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
}

// ============================ HintFilter ============================

HintFilter::HintFilter(QWidget* target, const QString& title, const QString& body)
    : QObject(target), m_title(title), m_body(body)
{
    target->installEventFilter(this);
}

bool HintFilter::eventFilter(QObject* o, QEvent* e)
{
    auto* w = qobject_cast<QWidget*>(o);
    if (!w) return false;
    switch (e->type()) {
    case QEvent::Enter:
        if (w->isEnabled()) InstantHint::showFor(w, m_title, m_body);
        break;
    case QEvent::Leave:
    case QEvent::Hide:
    case QEvent::MouseButtonPress:
        InstantHint::hideNow();
        break;
    default:
        break;
    }
    return false;
}

// ============================ HistoryRow ============================

HistoryRow::HistoryRow(int index, const QString& text, QWidget* parent)
    : QWidget(parent), m_index(index)
{
    setObjectName("histRow");
    setCursor(Qt::PointingHandCursor);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // --- header line: [revert] label ---
    auto* head = new QWidget;
    head->setObjectName("histHead");
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(4, 3, 8, 3);
    hl->setSpacing(7);

    m_revert = new QPushButton("↺");     // ANTICLOCKWISE OPEN CIRCLE ARROW
    m_revert->setObjectName("histRevert");
    m_revert->setFixedSize(22, 22);
    m_revert->setCursor(Qt::PointingHandCursor);
    m_revert->setFocusPolicy(Qt::NoFocus);
    new HintFilter(m_revert, "Revert to this version",
                   index == 0
                       ? "Undoes every action after this point, taking the document "
                         "back to how it opened. Your actions stay in the list — redo "
                         "or click a later entry to come back."
                       : "Rolls the document back to exactly this point in the "
                         "history. Later actions are undone, not deleted — click one "
                         "of them to jump forward again.");
    connect(m_revert, &QPushButton::clicked, this, [this] {
        InstantHint::hideNow();
        emit revertRequested(m_index);
    });
    hl->addWidget(m_revert);

    m_label = new QLabel(text);
    m_label->setObjectName("histLabel");
    hl->addWidget(m_label, 1);
    outer->addWidget(head);

    // --- drawer: snapshot + actions, hidden until the row is clicked ---
    m_drawer = new QWidget;
    m_drawer->setObjectName("histDrawer");
    auto* dl = new QVBoxLayout(m_drawer);
    dl->setContentsMargins(33, 2, 8, 8);
    dl->setSpacing(6);

    m_thumb = new QLabel;
    m_thumb->setObjectName("histThumb");
    m_thumb->setAlignment(Qt::AlignCenter);
    m_thumb->setMinimumHeight(74);
    dl->addWidget(m_thumb);

    m_thumbNote = new QLabel;
    m_thumbNote->setObjectName("histNote");
    m_thumbNote->setAlignment(Qt::AlignCenter);
    dl->addWidget(m_thumbNote);

    m_see = new QPushButton("See version");
    m_see->setObjectName("histSee");
    m_see->setCursor(Qt::PointingHandCursor);
    m_see->setFocusPolicy(Qt::NoFocus);
    new HintFilter(m_see, "See version",
                   "Opens a read-only side-by-side of this version against where you "
                   "are now, with a wipe slider and a changed-pixel highlight. Your "
                   "document is not modified.");
    connect(m_see, &QPushButton::clicked, this, [this] {
        InstantHint::hideNow();
        emit compareRequested(m_index);
    });
    dl->addWidget(m_see);

    m_drawer->hide();
    outer->addWidget(m_drawer);
}

void HistoryRow::setState(bool isCurrent, bool isUndone)
{
    if (m_styled && m_current == isCurrent && m_undone == isUndone) return;
    m_styled = true;
    m_current = isCurrent;
    m_undone = isUndone;
    // Undone entries read as "ahead of you" — dimmed, like a git log past HEAD.
    // Where you are is shown by tone and by the row's own fill — never by
    // thickening the text.
    m_label->setStyleSheet(QString("color: %1; font-weight: 400;")
                               .arg(isUndone ? Theme::Text4
                                             : (isCurrent ? Theme::Text : Theme::Text2)));
    // Reverting to where you already are is a no-op; say so by disabling it.
    m_revert->setEnabled(!isCurrent);
    update();
}

void HistoryRow::setSnapshot(const QImage& thumb)
{
    if (thumb.isNull()) {
        m_thumb->clear();
        m_thumb->setText("no snapshot");
        m_thumbNote->setText("This version predates the ones kept in memory.");
    } else {
        m_thumb->setPixmap(QPixmap::fromImage(thumb));
        m_thumbNote->setText(QString("%1 x %2").arg(thumb.width()).arg(thumb.height()));
    }
}

void HistoryRow::setExpanded(bool on)
{
    if (m_expanded == on) return;
    m_expanded = on;
    m_drawer->setVisible(on);
    update();
}

void HistoryRow::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) emit toggled(m_index);
    QWidget::mousePressEvent(e);
}

void HistoryRow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_current) {
        p.setBrush(Theme::color(Theme::AccentWash));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    } else if (m_expanded) {
        p.setBrush(Theme::color(Theme::Raised));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    }

    // Commit-graph rail: a dot per entry joined by a vertical line, so the list
    // reads as a chain of versions rather than a flat list of words.
    const int cx = 17;
    const int cy = 14;
    p.setPen(QPen(Theme::color(m_undone ? Theme::LineSoft : Theme::Line), 1.5));
    p.drawLine(cx, 0, cx, height());
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::color(m_current ? Theme::Accent : (m_undone ? Theme::Text4 : Theme::Text3)));
    p.drawEllipse(QPointF(cx, cy), m_current ? 4.5 : 3.2, m_current ? 4.5 : 3.2);
}

// ============================ HistoryPanel ============================

HistoryPanel::HistoryPanel(QUndoGroup* group, QWidget* parent)
    : QWidget(parent), m_group(group)
{
    setObjectName("histPanel");

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);

    m_empty = new QLabel("No document open");
    m_empty->setObjectName("histEmpty");
    m_empty->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_empty);

    auto* inner = new QWidget;
    inner->setObjectName("histInner");
    m_rowsLay = new QVBoxLayout(inner);
    m_rowsLay->setContentsMargins(2, 2, 2, 2);
    m_rowsLay->setSpacing(1);
    m_rowsLay->addStretch(1);

    m_scroll = new QScrollArea;
    m_scroll->setObjectName("histScroll");
    m_scroll->setWidget(inner);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lay->addWidget(m_scroll, 1);

    setStyleSheet(QString(R"(
        #histScroll, #histInner { background: transparent; border: none; }
        #histEmpty { color: %TEXT3%; font-size: 12px; padding: 16px 0; }
        #histLabel { color: %TEXT2%; font-size: 12px; }
        #histNote  { color: %TEXT3%; font-size: 11px; }
        #histRevert { background: transparent; border: none;
                      border-radius: 6px; color: %TEXT3%; font-size: 13px; padding: 0; }
        #histRevert:hover { background: %HOVER%; color: %TEXT%; }
        #histRevert:pressed { background: %ACTIVE%; }
        #histRevert:disabled { color: %TEXT4%; background: transparent; }
        #histThumb { background: %VOID%; border: 1px solid %LINE%;
                     border-radius: 6px; color: %TEXT4%; font-size: 11px; padding: 3px; }
        #histSee { background: %RAISED%; border: none; border-radius: 6px;
                   padding: 5px 12px; color: %TEXT2%; font-size: 12px; }
        #histSee:hover { background: %HOVER%; color: %TEXT%; }
    )")
        .replace("%VOID%",   Theme::Void)
        .replace("%RAISED%", Theme::Raised)
        .replace("%HOVER%",  Theme::Hover)
        .replace("%ACTIVE%", Theme::Active)
        .replace("%LINE%",   Theme::Line)
        .replace("%TEXT2%",  Theme::Text2)
        .replace("%TEXT3%",  Theme::Text3)
        .replace("%TEXT4%",  Theme::Text4)
        .replace("%TEXT%",   Theme::Text));
    m_scroll->hide();

    connect(m_group, &QUndoGroup::indexChanged, this, [this](int) {
        if (!m_stack) return;
        // Undo/redo just landed on a version — record it if this is the first
        // time the document has been here.
        m_stack->snapshotCurrent();
        // A push past the tip (or a fresh command replacing redone ones) changes
        // how many entries exist; anything else is just HEAD moving.
        if (m_stack->count() != m_knownCount) rebuild();
        else { refreshSnapshots(); syncStates(); }
    });
    connect(m_group, &QUndoGroup::cleanChanged, this, [this](bool) { syncStates(); });
}

void HistoryPanel::setDocument(Document* doc)
{
    if (m_doc == doc) return;
    m_doc = doc;
    m_stack = doc ? &doc->undo : nullptr;
    m_knownCount = -1;
    m_expandedIndex = -1;
    // The document may have been edited while this panel pointed elsewhere;
    // make sure wherever it sits now is on record before drawing the list.
    if (m_stack) m_stack->snapshotCurrent();
    rebuild();
}

QImage HistoryPanel::snapshotFor(int index) const
{
    return m_stack ? m_stack->snapshotAt(index) : QImage();
}

// Snapshots arrive lazily (a version is only recorded once the document has
// actually been there), so rows re-read them whenever the stack moves.
void HistoryPanel::refreshSnapshots()
{
    for (HistoryRow* r : m_rows) {
        const QImage s = snapshotFor(r->index());
        r->setSnapshot(s.isNull() ? QImage()
                                  : s.scaled(150, 150, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation));
    }
}

void HistoryPanel::rebuild()
{
    for (HistoryRow* r : m_rows) { m_rowsLay->removeWidget(r); r->deleteLater(); }
    m_rows.clear();

    if (!m_stack) {
        m_knownCount = -1;
        m_empty->setText("No document open");
        m_empty->show();
        m_scroll->hide();
        return;
    }

    m_knownCount = m_stack->count();
    m_empty->hide();
    m_scroll->show();

    // Entry 0 is the document as it was before any command — the root commit.
    auto addRow = [&](int index, const QString& text) {
        auto* row = new HistoryRow(index, text);
        connect(row, &HistoryRow::revertRequested, this, [this](int i) {
            if (m_stack) m_stack->setIndex(i);
        });
        connect(row, &HistoryRow::compareRequested, this, [this](int i) { showComparison(i); });
        connect(row, &HistoryRow::toggled, this, [this](int i) {
            m_expandedIndex = (m_expandedIndex == i) ? -1 : i;
            for (HistoryRow* r : m_rows) r->setExpanded(r->index() == m_expandedIndex);
        });
        m_rowsLay->insertWidget(m_rowsLay->count() - 1, row);
        m_rows.append(row);
    };

    addRow(0, "Original");
    for (int i = 0; i < m_stack->count(); ++i)
        addRow(i + 1, m_stack->text(i));

    for (HistoryRow* r : m_rows) r->setExpanded(r->index() == m_expandedIndex);
    refreshSnapshots();
    syncStates();
}

void HistoryPanel::syncStates()
{
    if (!m_stack) return;
    const int cur = m_stack->index();
    for (HistoryRow* r : m_rows)
        r->setState(r->index() == cur, r->index() > cur);

    // Keep the current version visible as undo/redo walks the list.
    for (HistoryRow* r : m_rows)
        if (r->index() == cur) { m_scroll->ensureWidgetVisible(r, 0, 20); break; }
}

// ---- side-by-side version comparison ----

// Read-only comparison window. Left = the chosen version, right = the document
// as it stands now; a wipe slider drags one over the other, and the changed
// pixels can be washed in like a diff highlight.
void HistoryPanel::showComparison(int index)
{
    if (!m_doc) return;
    const QImage before = snapshotFor(index);
    const QImage now = m_doc->composite();
    const int cur = m_stack ? m_stack->index() : 0;

    auto* dlg = new QDialog(window());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QString("Version %1 — %2")
                            .arg(index)
                            .arg(index == 0 ? QString("Original")
                                            : m_stack->text(index - 1)));
    dlg->resize(940, 640);

    auto* lay = new QVBoxLayout(dlg);
    lay->setContentsMargins(14, 14, 14, 12);
    lay->setSpacing(10);

    auto* caption = new QLabel(
        before.isNull()
            ? "No snapshot was recorded for this version — it fell outside the "
              "history kept in memory."
            : QString("Left: version %1 · Right: your document now (version %2). "
                      "Nothing here changes your document.")
                  .arg(index).arg(cur));
    caption->setWordWrap(true);
    caption->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::Text2));
    lay->addWidget(caption);

    auto* view = new QLabel;
    view->setAlignment(Qt::AlignCenter);
    view->setMinimumHeight(380);
    view->setStyleSheet(QString("background: %1; border: 1px solid %2; border-radius: 8px;")
                            .arg(Theme::Void, Theme::Line));
    lay->addWidget(view, 1);

    auto* wipe = new QSlider(Qt::Horizontal);
    wipe->setRange(0, 100);
    wipe->setValue(50);
    auto* diffBox = new QCheckBox("Highlight changed pixels");

    auto* ctrls = new QHBoxLayout;
    auto* lbl = new QLabel("Wipe");
    lbl->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::Text2));
    ctrls->addWidget(lbl);
    ctrls->addWidget(wipe, 1);
    ctrls->addWidget(diffBox);
    lay->addLayout(ctrls);

    auto* close = new QPushButton("Close");
    connect(close, &QPushButton::clicked, dlg, &QDialog::close);
    auto* bottom = new QHBoxLayout;
    bottom->addStretch(1);
    bottom->addWidget(close);
    lay->addLayout(bottom);

    // Composes the wipe/diff image at the current control values and paints it
    // scaled into the view.
    auto render = [view, wipe, diffBox, before, now] {
        if (before.isNull() && now.isNull()) return;
        const QSize sz = now.isNull() ? before.size() : now.size();
        QImage out(sz, QImage::Format_ARGB32_Premultiplied);
        out.fill(Theme::color(Theme::Void));
        QPainter p(&out);

        if (!now.isNull()) p.drawImage(0, 0, now);
        if (!before.isNull()) {
            const int split = sz.width() * wipe->value() / 100;
            p.drawImage(QRect(0, 0, split, sz.height()), before,
                        QRect(0, 0, split, before.height()));
            p.setPen(QPen(Theme::color(Theme::Accent), 2));
            p.drawLine(split, 0, split, sz.height());
        }

        if (diffBox->isChecked() && !before.isNull() && !now.isNull()
            && before.size() == now.size()) {
            // Pixels that differ get a translucent wash — the visual stand-in for
            // a diff's added/removed lines.
            QImage mark(sz, QImage::Format_ARGB32_Premultiplied);
            mark.fill(Qt::transparent);
            const QImage a = before.convertToFormat(QImage::Format_ARGB32);
            const QImage b = now.convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < sz.height(); ++y) {
                const QRgb* ra = reinterpret_cast<const QRgb*>(a.constScanLine(y));
                const QRgb* rb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
                QRgb* rm = reinterpret_cast<QRgb*>(mark.scanLine(y));
                for (int x = 0; x < sz.width(); ++x)
                    rm[x] = (ra[x] != rb[x]) ? qPremultiply(qRgba(255, 92, 92, 90)) : 0u;
            }
            p.drawImage(0, 0, mark);
        }
        p.end();

        view->setPixmap(QPixmap::fromImage(
            out.scaled(view->size() - QSize(8, 8), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    };

    connect(wipe, &QSlider::valueChanged, dlg, [render](int) { render(); });
    connect(diffBox, &QCheckBox::toggled, dlg, [render](bool) { render(); });
    // First paint has to wait for the dialog to get its real size.
    QTimer::singleShot(0, dlg, [render] { render(); });

    dlg->show();
}
