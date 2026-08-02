#include "HomePage.h"
#include "Theme.h"
#include <QAbstractButton>
#include <QVariantAnimation>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QStackedLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QMenu>
#include <QContextMenuEvent>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QFrame>
#include <QTimer>

// ============================ Recents store ============================

namespace {
QString cacheDir()
{
    QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + "/recent_thumbs";
    QDir().mkpath(d);
    return d;
}
QString hashKey(const QString& path)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex());
}
}

namespace Recents {

QString thumbnailPath(const QString& projectPath)
{
    return cacheDir() + "/" + hashKey(projectPath) + ".png";
}

QList<RecentEntry> load()
{
    QSettings s;
    int n = s.beginReadArray("recents");
    QList<RecentEntry> out;
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        RecentEntry e;
        e.path = s.value("path").toString();
        e.name = s.value("name").toString();
        e.opened = s.value("opened").toDateTime();
        if (!e.path.isEmpty() && QFileInfo::exists(e.path))
            out.append(e);
    }
    s.endArray();
    // newest first
    std::sort(out.begin(), out.end(), [](const RecentEntry& a, const RecentEntry& b) {
        return a.opened > b.opened;
    });
    return out;
}

static void store(const QList<RecentEntry>& list)
{
    QSettings s;
    s.remove("recents");
    s.beginWriteArray("recents");
    for (int i = 0; i < list.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue("path", list[i].path);
        s.setValue("name", list[i].name);
        s.setValue("opened", list[i].opened);
    }
    s.endArray();
}

void touch(const QString& path, const QString& name)
{
    if (path.isEmpty()) return;
    QList<RecentEntry> list = load();
    QString canon = QFileInfo(path).absoluteFilePath();
    for (int i = list.size() - 1; i >= 0; --i)
        if (QFileInfo(list[i].path).absoluteFilePath() == canon)
            list.removeAt(i);
    RecentEntry e;
    e.path = canon;
    e.name = name.isEmpty() ? QFileInfo(path).completeBaseName() : name;
    e.opened = QDateTime::currentDateTime();
    list.prepend(e);
    while (list.size() > 60) list.removeLast();
    store(list);
}

void remove(const QString& path)
{
    QList<RecentEntry> list = load();
    QString canon = QFileInfo(path).absoluteFilePath();
    for (int i = list.size() - 1; i >= 0; --i)
        if (QFileInfo(list[i].path).absoluteFilePath() == canon)
            list.removeAt(i);
    store(list);
    QFile::remove(thumbnailPath(canon));
}

// Rename in the recents store, and — for .pgd projects — rewrite the document's
// own "name" field so the new title survives a reopen.
void rename(const QString& path, const QString& newName)
{
    QString trimmed = newName.trimmed();
    if (path.isEmpty() || trimmed.isEmpty()) return;
    QString canon = QFileInfo(path).absoluteFilePath();

    QList<RecentEntry> list = load();
    for (RecentEntry& e : list)
        if (QFileInfo(e.path).absoluteFilePath() == canon)
            e.name = trimmed;
    store(list);

    if (QFileInfo(canon).suffix().compare("pgd", Qt::CaseInsensitive) != 0) return;

    QFile f(canon);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument jd = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!jd.isObject()) return;
    QJsonObject root = jd.object();
    if (root["app"].toString() != "photogod") return;
    root["name"] = trimmed;
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void saveThumbnail(const QString& path, const QImage& composite)
{
    if (path.isEmpty() || composite.isNull()) return;
    QImage thumb = composite.scaled(600, 600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    thumb.save(thumbnailPath(QFileInfo(path).absoluteFilePath()), "PNG");
}

} // namespace Recents

// ============================ helpers ============================

static QString relativeTime(const QDateTime& dt)
{
    if (!dt.isValid()) return QString();
    qint64 secs = dt.secsTo(QDateTime::currentDateTime());
    if (secs < 60)      return "just now";
    if (secs < 3600)    return QString("%1m ago").arg(secs / 60);
    if (secs < 86400)   return QString("%1h ago").arg(secs / 3600);
    if (secs < 172800)  return "yesterday";
    if (secs < 2592000) return QString("%1d ago").arg(secs / 86400);
    return dt.toString("MMM d");
}

// Stable hue per project, kept at low chroma so a wall of placeholder cards
// stays calm instead of turning into a colour chart.
static QColor accentFor(const QString& key)
{
    uint h = qHash(key);
    return QColor::fromHsv(int(h % 360), 58, 92);
}

namespace {

constexpr int kCardW  = 264;
constexpr int kThumbW = 264;
constexpr int kThumbH = 166;
constexpr int kGapX   = 24;
constexpr int kGapY   = 30;
// Narrow enough to hug two cards, wide enough that the action row never
// squeezes when there is nothing to show yet.
constexpr int kMinColumnW = 470;

// ---- a quiet action row: label on the left, its key on the right ----
class ActionTile : public QAbstractButton
{
    Q_OBJECT
public:
    ActionTile(const QString& label, const QString& key, QWidget* parent = nullptr)
        : QAbstractButton(parent), m_key(key)
    {
        setText(label);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedHeight(46);

        // Same rule as the tool rail: the pointer brightens the label, it does
        // not switch a panel on behind it.
        m_glow.setDuration(120);
        m_glow.setStartValue(0.0);
        m_glow.setEndValue(1.0);
        connect(&m_glow, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& v) { m_t = v.toReal(); update(); });
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QFont kf = Theme::uiFont(11);
        QFontMetrics kfm(kf);
        int kw = kfm.horizontalAdvance(m_key) + 16;
        QRectF chip(width() - kw - 16, height() / 2.0 - 9, kw, 18);
        QPainterPath cp;
        cp.addRoundedRect(chip, 5, 5);
        p.fillPath(cp, Theme::color(Theme::Raised));
        p.setFont(kf);
        p.setPen(mix(Theme::Text3, Theme::Text2));
        p.drawText(chip, Qt::AlignCenter, m_key);

        p.setFont(Theme::uiFont(14));
        p.setPen(mix(Theme::Text2, Theme::Text));
        p.drawText(QRect(16, 0, int(chip.left()) - 26, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, text());
    }
    void enterEvent(QEnterEvent*) override { glow(true); }
    void leaveEvent(QEvent*) override       { glow(false); }

    // Width follows the words, so the key chip sits right beside its label
    // instead of across a canyon of empty button.
    QSize sizeHint() const override
    {
        QFontMetrics lf(Theme::uiFont(14)), kf(Theme::uiFont(11));
        return QSize(16 + lf.horizontalAdvance(text()) + 22
                        + kf.horizontalAdvance(m_key) + 16 + 16, 46);
    }

private:
    // idle → hover, along the current animation position
    QColor mix(const char* idle, const char* hot) const
    {
        const QColor a = Theme::color(idle), b = Theme::color(hot);
        return QColor(a.red()   + int((b.red()   - a.red())   * m_t),
                      a.green() + int((b.green() - a.green()) * m_t),
                      a.blue()  + int((b.blue()  - a.blue())  * m_t));
    }

    void glow(bool in)
    {
        m_glow.stop();
        m_glow.setDirection(in ? QAbstractAnimation::Forward
                               : QAbstractAnimation::Backward);
        m_glow.setCurrentTime(int(m_t * m_glow.duration()));
        m_glow.start();
    }

    QVariantAnimation m_glow;
    qreal m_t = 0.0;
    QString m_key;
};

// A project is its own thumbnail. No frame, no fill, no border — the picture
// sits on the page and the name lives quietly underneath it.
class ProjectCard : public QFrame
{
    Q_OBJECT
public:
    ProjectCard(const RecentEntry& e, QWidget* parent = nullptr)
        : QFrame(parent), m_entry(e)
    {
        setObjectName("projectCard");
        setCursor(Qt::PointingHandCursor);
        setFixedWidth(kCardW);
        setAttribute(Qt::WA_Hover, true);
        setFocusPolicy(Qt::StrongFocus);   // the grid is arrow-key navigable

        m_source = QImage(Recents::thumbnailPath(QFileInfo(m_entry.path).absoluteFilePath()));

        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);

        m_thumb = new QLabel;
        m_thumb->setFixedSize(kThumbW, kThumbH);
        m_thumb->setObjectName("cardThumb");
        m_thumb->setAlignment(Qt::AlignCenter);
        m_thumb->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        lay->addWidget(m_thumb);
        lay->addSpacing(12);

        // title area: label and inline editor share one slot
        auto* titleHost = new QWidget;
        titleHost->setFixedHeight(19);
        m_titleStack = new QStackedLayout(titleHost);
        m_titleStack->setContentsMargins(0, 0, 0, 0);

        m_title = new QLabel;
        m_title->setObjectName("cardTitle");
        m_title->setToolTip(m_entry.path);
        m_title->setWordWrap(false);
        m_title->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_titleStack->addWidget(m_title);

        m_editor = new QLineEdit;
        m_editor->setObjectName("cardTitleEdit");
        m_editor->installEventFilter(this);
        connect(m_editor, &QLineEdit::editingFinished, this, &ProjectCard::commitRename);
        m_titleStack->addWidget(m_editor);

        lay->addWidget(titleHost);
        lay->addSpacing(3);

        m_sub = new QLabel(relativeTime(m_entry.opened));
        m_sub->setObjectName("cardSub");
        m_sub->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        lay->addWidget(m_sub);

        applyTitle();
        repaintThumb();
    }

    QString path() const { return m_entry.path; }

    void beginRename()
    {
        m_editor->setText(m_entry.name);
        m_titleStack->setCurrentWidget(m_editor);
        m_editor->selectAll();
        m_editor->setFocus(Qt::OtherFocusReason);
    }

signals:
    void activated(const QString& path);
    void removeRequested(const QString& path);
    void renamed(const QString& path, const QString& newName);

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        // ignore clicks while the inline editor is up
        if (m_titleStack->currentWidget() == m_editor) { e->ignore(); return; }
        setFocus(Qt::MouseFocusReason);
        m_armed = true;
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (m_armed && rect().contains(e->pos())) emit activated(m_entry.path);
        m_armed = false;
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override
    {
        // double-click on the title text renames; anywhere else opens
        if (m_title->geometry().adjusted(-4, -4, 4, 4).contains(e->pos())) {
            m_armed = false;
            beginRename();
            return;
        }
        emit activated(m_entry.path);
    }
    void enterEvent(QEnterEvent*) override { m_hover = true;  repaintThumb(); applyTitle(); }
    void leaveEvent(QEvent*) override      { m_hover = false; repaintThumb(); applyTitle(); }
    void focusInEvent(QFocusEvent*) override  { repaintThumb(); applyTitle(); }
    void focusOutEvent(QFocusEvent*) override { repaintThumb(); applyTitle(); }

    // Open / rename / forget without ever reaching for the mouse.
    void keyPressEvent(QKeyEvent* e) override
    {
        switch (e->key()) {
        case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Space:
            emit activated(m_entry.path); return;
        case Qt::Key_F2:
            beginRename(); return;
        case Qt::Key_Delete: case Qt::Key_Backspace:
            emit removeRequested(m_entry.path); return;
        default:
            QFrame::keyPressEvent(e);
        }
    }
    void contextMenuEvent(QContextMenuEvent* e) override
    {
        QMenu menu(this);
        menu.addAction("Open", this, [this] { emit activated(m_entry.path); });
        menu.addAction("Rename", this, [this] { beginRename(); });
        menu.addSeparator();
        menu.addAction("Remove from recent", this, [this] { emit removeRequested(m_entry.path); });
        menu.exec(e->globalPos());
    }
    bool eventFilter(QObject* o, QEvent* ev) override
    {
        if (o == m_editor && ev->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(ev);
            if (ke->key() == Qt::Key_Escape) {
                m_cancelled = true;
                m_titleStack->setCurrentWidget(m_title);
                m_cancelled = false;
                setFocus(Qt::OtherFocusReason);
                return true;
            }
        }
        return QFrame::eventFilter(o, ev);
    }

private slots:
    void commitRename()
    {
        if (m_cancelled || m_titleStack->currentWidget() != m_editor) return;
        QString text = m_editor->text().trimmed();
        m_titleStack->setCurrentWidget(m_title);
        if (text.isEmpty() || text == m_entry.name) return;
        m_entry.name = text;
        applyTitle();
        emit renamed(m_entry.path, text);
    }

private:
    void applyTitle()
    {
        QFontMetrics fm(m_title->font());
        m_title->setText(fm.elidedText(m_entry.name, Qt::ElideMiddle, kThumbW - 8));
        m_title->setStyleSheet(QString("color: %1; background: transparent;")
                                   .arg(active() ? Theme::Text : Theme::Text2));
    }

    bool active() const { return m_hover || hasFocus(); }

    void repaintThumb() { m_thumb->setPixmap(buildThumb()); }

    QPixmap buildThumb() const
    {
        const int W = kThumbW, H = kThumbH;
        const qreal R = 8;
        qreal dpr = devicePixelRatioF();
        QPixmap pm(QSize(W, H) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        p.save();
        QPainterPath clip;
        clip.addRoundedRect(QRectF(0, 0, W, H), R, R);
        p.setClipPath(clip);

        if (!m_source.isNull()) {
            p.fillRect(QRect(0, 0, W, H), Theme::color(Theme::Void));
            QImage scaled = m_source.scaled(QSize(W, H) * dpr, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);
            scaled.setDevicePixelRatio(dpr);
            QSizeF ss = scaled.size() / dpr;
            p.drawImage(QPointF((W - ss.width()) / 2.0, (H - ss.height()) / 2.0), scaled);
        } else {
            // no cached preview — a barely-there tint plus the project's initial
            QColor a = accentFor(m_entry.path);
            QLinearGradient g(0, 0, W, H);
            g.setColorAt(0.0, a.darker(230));
            g.setColorAt(1.0, a.darker(330));
            p.fillRect(QRect(0, 0, W, H), g);

            QString initial = m_entry.name.trimmed().left(1).toUpper();
            if (initial.isEmpty()) initial = "?";
            p.setFont(Theme::uiFont(40));
            p.setPen(QColor(255, 255, 255, 46));
            p.drawText(QRect(0, 0, W, H), Qt::AlignCenter, initial);
        }
        p.restore();

        // Hover and keyboard focus are the only decoration a card ever gets.
        if (active()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(Theme::color(hasFocus() ? Theme::Accent : Theme::Text3,
                                       hasFocus() ? 255 : 150),
                          hasFocus() ? 2.0 : 1.0));
            qreal in = hasFocus() ? 1.0 : 0.5;
            p.drawRoundedRect(QRectF(in, in, W - 2 * in, H - 2 * in), R, R);
        }
        return pm;
    }

    RecentEntry m_entry;
    QImage m_source;
    QLabel* m_thumb = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_sub = nullptr;
    QLineEdit* m_editor = nullptr;
    QStackedLayout* m_titleStack = nullptr;
    bool m_armed = false;
    bool m_hover = false;
    bool m_cancelled = false;
};

} // namespace

// ============================ HomePage ============================

HomePage::HomePage(QWidget* parent) : QWidget(parent)
{
    setObjectName("homePage");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Everything scrolls together so the hero doesn't eat vertical space on
    // short windows.
    m_scroll = new QScrollArea;
    m_scroll->setObjectName("homeScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* canvas = new QWidget;
    canvas->setObjectName("homeCanvas");
    auto* canvasLay = new QHBoxLayout(canvas);
    canvasLay->setContentsMargins(0, 0, 0, 0);
    canvasLay->setSpacing(0);

    // ---- the centred content column (~60% of the window width) ----
    m_column = new QWidget;
    m_column->setObjectName("homeColumn");
    canvasLay->addStretch(1);
    canvasLay->addWidget(m_column);
    canvasLay->addStretch(1);

    auto* col = new QVBoxLayout(m_column);
    col->setContentsMargins(0, 0, 0, 64);
    col->setSpacing(0);

    // ---- the only branding on the page ----
    col->addSpacing(84);

    auto* wordmark = new QLabel("Make something awesome");
    wordmark->setObjectName("homeWordmark");
    wordmark->setAlignment(Qt::AlignHCenter);
    col->addWidget(wordmark);

    col->addSpacing(12);

    // ---- two ways in, each with the key that gets you there faster ----
    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    actions->addStretch();

    auto* newBtn = new ActionTile("New document", "N");
    connect(newBtn, &QAbstractButton::clicked, this, &HomePage::newRequested);
    actions->addWidget(newBtn);

    auto* openBtn = new ActionTile("Open image", "O");
    connect(openBtn, &QAbstractButton::clicked, this, &HomePage::openRequested);
    actions->addWidget(openBtn);

    actions->addStretch();
    col->addLayout(actions);

    col->addSpacing(56);

    // ---- section header: a label, and a filter that stays out of the way ----
    auto* sectionRow = new QHBoxLayout;
    sectionRow->setContentsMargins(0, 0, 0, 0);
    m_sectionLabel = new QLabel("Recent");
    m_sectionLabel->setObjectName("homeSectionLabel");
    sectionRow->addWidget(m_sectionLabel);
    sectionRow->addStretch();

    m_search = new QLineEdit;
    m_search->setObjectName("homeSearch");
    m_search->setPlaceholderText("Filter");
    m_search->setClearButtonEnabled(true);
    m_search->setFixedHeight(32);
    m_search->setFixedWidth(220);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& t) {
        m_filter = t.trimmed();
        rebuildGrid();
    });
    sectionRow->addWidget(m_search);
    col->addLayout(sectionRow);

    col->addSpacing(20);

    // ---- card grid ----
    m_gridHost = new QWidget;
    m_gridLay = new QGridLayout(m_gridHost);
    m_gridLay->setContentsMargins(0, 0, 0, 0);
    m_gridLay->setHorizontalSpacing(kGapX);
    m_gridLay->setVerticalSpacing(kGapY);
    m_gridLay->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    col->addWidget(m_gridHost);

    // ---- empty state: one line, no ornament ----
    m_empty = new QWidget;
    m_empty->setObjectName("homeEmpty");
    auto* el = new QVBoxLayout(m_empty);
    el->setContentsMargins(0, 6, 0, 34);
    el->setSpacing(0);
    m_emptyText = new QLabel;
    m_emptyText->setObjectName("homeEmptyText");
    m_emptyText->setAlignment(Qt::AlignHCenter);
    el->addWidget(m_emptyText);
    m_empty->setVisible(false);
    col->addWidget(m_empty);

    col->addStretch();

    m_scroll->setWidget(canvas);
    root->addWidget(m_scroll, 1);

    setStyleSheet(QString(R"(
        #homePage, #homeScroll, #homeCanvas, #homeColumn { background: %VOID%; }

        #homeWordmark { color: %TEXT%; font-size: 26px; letter-spacing: -0.2px; }

        #homeSearch {
            background: transparent; color: %TEXT%; border: 1px solid %LINE%;
            border-radius: 8px; padding: 0 12px; font-size: 13px;
        }
        #homeSearch:hover { border-color: %ACTIVE%; }
        #homeSearch:focus { border-color: %ACCENTDIM%; background: %PANEL%; }

        #homeSectionLabel { color: %TEXT3%; font-size: 13px; }

        #homeEmpty { background: transparent; }
        #homeEmptyText { color: %TEXT3%; font-size: 13px; }

        #projectCard { background: transparent; border: none; }
        #cardThumb { background: transparent; border: none; }
        #cardTitle { color: %TEXT2%; font-size: 14px; background: transparent; }
        #cardTitleEdit {
            background: %RAISED%; color: %TEXT%; border: 1px solid %ACCENTDIM%;
            border-radius: 5px; padding: 0 5px; font-size: 14px;
            selection-background-color: %ACCENTDIM%;
        }
        #cardSub { color: %TEXT3%; font-size: 12px; background: transparent; }
    )")
        .replace("%VOID%",      Theme::Void)
        .replace("%PANEL%",     Theme::Panel)
        .replace("%RAISED%",    Theme::Raised)
        .replace("%ACTIVE%",    Theme::Active)
        .replace("%LINE%",      Theme::Line)
        .replace("%TEXT2%",     Theme::Text2)
        .replace("%TEXT3%",     Theme::Text3)
        .replace("%TEXT%",      Theme::Text)
        .replace("%ACCENTDIM%", Theme::AccentDim));
}

// Keyboard first: the grid is arrow-navigable, N and O are the two doors out,
// and typing anything else lands in the filter.
void HomePage::keyPressEvent(QKeyEvent* e)
{
    auto* card = qobject_cast<ProjectCard*>(focusWidget());
    int idx = card ? m_cards.indexOf(card) : -1;

    auto focusAt = [this](int i) {
        if (m_cards.isEmpty()) return;
        i = qBound(0, i, int(m_cards.size()) - 1);
        m_cards[i]->setFocus(Qt::TabFocusReason);
        if (m_scroll) m_scroll->ensureWidgetVisible(m_cards[i], 0, 40);
    };

    switch (e->key()) {
    case Qt::Key_Right: focusAt(idx < 0 ? 0 : idx + 1); return;
    case Qt::Key_Left:  focusAt(idx < 0 ? 0 : idx - 1); return;
    case Qt::Key_Down:  focusAt(idx < 0 ? 0 : idx + m_cols); return;
    case Qt::Key_Up:
        if (idx >= m_cols) { focusAt(idx - m_cols); return; }
        if (idx >= 0) { m_search->setFocus(Qt::TabFocusReason); return; }
        return;
    case Qt::Key_Escape:
        if (!m_search->text().isEmpty()) m_search->clear();
        setFocus(Qt::OtherFocusReason);
        return;
    case Qt::Key_N: emit newRequested();  return;
    case Qt::Key_O: emit openRequested(); return;
    default: break;
    }

    // any other printable character starts filtering
    const QString t = e->text();
    if (!t.isEmpty() && t.at(0).isPrint() && !(e->modifiers() & Qt::ControlModifier)) {
        m_search->setFocus(Qt::OtherFocusReason);
        m_search->setText(m_search->text() + t);
        return;
    }
    QWidget::keyPressEvent(e);
}

void HomePage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    refresh();
    setFocus(Qt::OtherFocusReason);   // keys work the moment the page appears
}

void HomePage::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    applyColumnWidth();
    rebuildGrid();
}

// How many cards fit across in about two thirds of the window — the rest of
// the width stays as margin, because the page should breathe, not fill.
int HomePage::maxColumns() const
{
    int avail = m_scroll ? m_scroll->viewport()->width() : width();
    int target = qBound(kCardW, int(avail * 0.66), 1200);
    auto widthFor = [](int n) { return n * kCardW + (n - 1) * kGapX; };
    int cols = std::max(1, (target + kGapX) / (kCardW + kGapX));
    if (widthFor(cols + 1) <= avail - 48
        && std::abs(widthFor(cols + 1) - target) < std::abs(widthFor(cols) - target))
        ++cols;
    return cols;
}

// The column is centred, so it has to hug what it holds: sized to the cards
// actually on screen, never to the cards that could have been. Two projects
// centre as a pair rather than hugging the left edge of an empty four-wide bed.
void HomePage::applyColumnWidth()
{
    if (!m_column) return;
    int cols = m_cols > 0 ? m_cols : maxColumns();
    int w = cols * kCardW + (cols - 1) * kGapX;
    m_column->setFixedWidth(std::max(w, kMinColumnW));
}

void HomePage::refresh()
{
    m_entries = Recents::load();
    applyColumnWidth();
    rebuildGrid();
}

void HomePage::rebuildGrid()
{
    if (!m_gridLay) return;

    // clear existing cards
    m_cards.clear();
    QLayoutItem* item;
    while ((item = m_gridLay->takeAt(0))) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    QList<RecentEntry> shown;
    for (const RecentEntry& e : m_entries) {
        if (m_filter.isEmpty()
            || e.name.contains(m_filter, Qt::CaseInsensitive)
            || e.path.contains(m_filter, Qt::CaseInsensitive))
            shown.append(e);
    }

    // The filter is only worth showing once there is enough here to lose track of.
    m_search->setVisible(m_entries.size() > 6 || !m_filter.isEmpty());
    m_sectionLabel->setVisible(!m_entries.isEmpty());

    m_empty->setVisible(shown.isEmpty());
    m_gridHost->setVisible(!shown.isEmpty());
    if (shown.isEmpty()) {
        m_cols = 0;
        applyColumnWidth();
        m_emptyText->setText(m_filter.isEmpty()
            ? "Nothing open yet — press N to start."
            : "Nothing matches \"" + m_filter + "\".");
        return;
    }

    m_cols = std::max(1, std::min(maxColumns(), int(shown.size())));
    applyColumnWidth();
    int cols = m_cols;
    int r = 0, c = 0;
    for (const RecentEntry& e : shown) {
        auto* card = new ProjectCard(e);
        m_cards.append(card);
        connect(card, &ProjectCard::activated, this, &HomePage::projectRequested);
        connect(card, &ProjectCard::removeRequested, this, [this](const QString& p) {
            Recents::remove(p);
            refresh();
        });
        connect(card, &ProjectCard::renamed, this,
                [this](const QString& p, const QString& n) {
            Recents::rename(p, n);
            // keep the in-memory list in sync without rebuilding (which would
            // delete the card that just emitted this signal)
            for (RecentEntry& e : m_entries)
                if (QFileInfo(e.path).absoluteFilePath() == QFileInfo(p).absoluteFilePath())
                    e.name = n;
        });
        m_gridLay->addWidget(card, r, c, Qt::AlignTop | Qt::AlignLeft);
        if (++c >= cols) { c = 0; ++r; }
    }
    // keep trailing columns from stretching the last row's cards apart
    for (int i = 0; i < cols; ++i) m_gridLay->setColumnStretch(i, 0);
    m_gridLay->setColumnStretch(cols, 1);
}

#include "HomePage.moc"
