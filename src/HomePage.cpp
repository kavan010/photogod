#include "HomePage.h"
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
    if (secs < 3600)    return QString("%1 min ago").arg(secs / 60);
    if (secs < 86400)   return QString("%1 h ago").arg(secs / 3600);
    if (secs < 172800)  return "yesterday";
    if (secs < 2592000) return QString("%1 days ago").arg(secs / 86400);
    return dt.toString("MMM d, yyyy");
}

// Stable pleasant hue per project, so thumbnail-less cards still look intentional.
static QColor accentFor(const QString& key)
{
    uint h = qHash(key);
    return QColor::fromHsv(int(h % 360), 150, 190);
}

namespace {

constexpr int kCardW  = 236;
constexpr int kThumbW = 236;
constexpr int kThumbH = 150;
constexpr int kGap    = 18;

// A single clickable project card: rounded thumbnail + editable name + timestamp.
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

        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);   // no padding — thumbnail is flush
        lay->setSpacing(0);

        m_thumb = new QLabel;
        m_thumb->setFixedSize(kThumbW, kThumbH);
        m_thumb->setObjectName("cardThumb");
        m_thumb->setAlignment(Qt::AlignCenter);
        m_thumb->setPixmap(buildThumb());
        lay->addWidget(m_thumb);

        auto* meta = new QWidget;
        meta->setObjectName("cardMeta");
        auto* ml = new QVBoxLayout(meta);
        ml->setContentsMargins(12, 10, 12, 11);
        ml->setSpacing(3);

        // title area: label and inline editor share one slot
        auto* titleHost = new QWidget;
        m_titleStack = new QStackedLayout(titleHost);
        m_titleStack->setContentsMargins(0, 0, 0, 0);

        m_title = new QLabel;
        m_title->setObjectName("cardTitle");
        m_title->setToolTip(m_entry.path + "\n\nDouble-click the title to rename");
        m_title->setWordWrap(false);
        m_titleStack->addWidget(m_title);

        m_editor = new QLineEdit;
        m_editor->setObjectName("cardTitleEdit");
        m_editor->installEventFilter(this);
        connect(m_editor, &QLineEdit::editingFinished, this, &ProjectCard::commitRename);
        m_titleStack->addWidget(m_editor);

        ml->addWidget(titleHost);

        m_sub = new QLabel(relativeTime(m_entry.opened));
        m_sub->setObjectName("cardSub");
        ml->addWidget(m_sub);

        lay->addWidget(meta);
        applyTitle();
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
        if (m_title->geometry().translated(m_title->parentWidget()->mapTo(this, QPoint(0, 0)))
                .adjusted(-4, -4, 4, 4).contains(e->pos())) {
            m_armed = false;
            beginRename();
            return;
        }
        emit activated(m_entry.path);
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
        m_title->setText(fm.elidedText(m_entry.name, Qt::ElideMiddle, kThumbW - 30));
    }

    QPixmap buildThumb() const
    {
        const int W = kThumbW, H = kThumbH, R = 10;
        qreal dpr = devicePixelRatioF();
        QPixmap pm(QSize(W, H) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);

        QImage img(Recents::thumbnailPath(QFileInfo(m_entry.path).absoluteFilePath()));

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        // only the top corners are rounded — the meta strip closes the card below
        QPainterPath clip;
        clip.setFillRule(Qt::WindingFill);
        clip.addRoundedRect(QRectF(0, 0, W, H), R, R);
        clip.addRect(QRectF(0, H - R, W, R));
        p.setClipPath(clip);

        if (!img.isNull()) {
            p.fillRect(QRect(0, 0, W, H), QColor(24, 24, 27));
            QImage scaled = img.scaled(QSize(W, H) * dpr, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
            scaled.setDevicePixelRatio(dpr);
            QSizeF ss = scaled.size() / dpr;
            p.drawImage(QPointF((W - ss.width()) / 2.0, (H - ss.height()) / 2.0), scaled);
        } else {
            // no cached preview — a tinted gradient plus the project's initial
            QColor a = accentFor(m_entry.path);
            QLinearGradient g(0, 0, W, H);
            g.setColorAt(0.0, a.darker(190));
            g.setColorAt(1.0, a.darker(280));
            p.fillRect(QRect(0, 0, W, H), g);

            QString initial = m_entry.name.trimmed().left(1).toUpper();
            if (initial.isEmpty()) initial = "?";
            QFont f = p.font();
            f.setPointSize(30);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, 70));
            p.drawText(QRect(0, 0, W, H), Qt::AlignCenter, initial);
        }
        return pm;
    }

    RecentEntry m_entry;
    QLabel* m_thumb = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_sub = nullptr;
    QLineEdit* m_editor = nullptr;
    QStackedLayout* m_titleStack = nullptr;
    bool m_armed = false;
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
    col->setContentsMargins(0, 0, 0, 48);
    col->setSpacing(0);

    // ---- hero ----
    col->addSpacing(56);

    auto* eyebrow = new QLabel("PHOTOGOD");
    eyebrow->setObjectName("homeEyebrow");
    eyebrow->setAlignment(Qt::AlignHCenter);
    col->addWidget(eyebrow);
    col->addSpacing(10);

    auto* hero = new QLabel("Create Something");
    hero->setObjectName("homeHero");
    hero->setAlignment(Qt::AlignHCenter);
    col->addWidget(hero);
    col->addSpacing(6);

    auto* tagline = new QLabel("Start a blank canvas, or pick up where you left off.");
    tagline->setObjectName("homeTagline");
    tagline->setAlignment(Qt::AlignHCenter);
    col->addWidget(tagline);

    col->addSpacing(26);

    // ---- primary actions, centred ----
    auto* actions = new QHBoxLayout;
    actions->setSpacing(10);
    actions->addStretch();

    auto* newBtn = new QPushButton("＋   New Project");
    newBtn->setObjectName("homeNewBtn");
    newBtn->setCursor(Qt::PointingHandCursor);
    newBtn->setFixedHeight(38);
    connect(newBtn, &QPushButton::clicked, this, &HomePage::newRequested);
    actions->addWidget(newBtn);

    auto* openBtn = new QPushButton("Open Image");
    openBtn->setObjectName("homeOpenBtn");
    openBtn->setCursor(Qt::PointingHandCursor);
    openBtn->setFixedHeight(38);
    connect(openBtn, &QPushButton::clicked, this, &HomePage::openRequested);
    actions->addWidget(openBtn);

    actions->addStretch();
    col->addLayout(actions);

    col->addSpacing(14);

    // ---- search, centred and narrower than the column ----
    auto* searchRow = new QHBoxLayout;
    searchRow->addStretch();
    m_search = new QLineEdit;
    m_search->setObjectName("homeSearch");
    m_search->setPlaceholderText("Search projects…");
    m_search->setClearButtonEnabled(true);
    m_search->setFixedHeight(36);
    m_search->setFixedWidth(340);
    m_search->setAlignment(Qt::AlignHCenter);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& t) {
        m_filter = t.trimmed();
        rebuildGrid();
    });
    searchRow->addWidget(m_search);
    searchRow->addStretch();
    col->addLayout(searchRow);

    col->addSpacing(44);

    // ---- section header ----
    auto* sectionRow = new QHBoxLayout;
    sectionRow->setContentsMargins(2, 0, 2, 0);
    m_sectionLabel = new QLabel("Recent");
    m_sectionLabel->setObjectName("homeSectionLabel");
    sectionRow->addWidget(m_sectionLabel);
    sectionRow->addStretch();
    m_countLabel = new QLabel;
    m_countLabel->setObjectName("homeCount");
    sectionRow->addWidget(m_countLabel);
    col->addLayout(sectionRow);

    col->addSpacing(8);

    auto* rule = new QFrame;
    rule->setObjectName("homeRule");
    rule->setFixedHeight(1);
    col->addWidget(rule);

    col->addSpacing(20);

    // ---- card grid ----
    m_gridHost = new QWidget;
    m_gridLay = new QGridLayout(m_gridHost);
    m_gridLay->setContentsMargins(0, 0, 0, 0);
    m_gridLay->setHorizontalSpacing(kGap);
    m_gridLay->setVerticalSpacing(kGap);
    m_gridLay->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    col->addWidget(m_gridHost);

    // ---- empty state ----
    m_empty = new QWidget;
    m_empty->setObjectName("homeEmpty");
    auto* el = new QVBoxLayout(m_empty);
    el->setContentsMargins(0, 34, 0, 34);
    el->setSpacing(8);
    auto* glyph = new QLabel("✧");
    glyph->setObjectName("homeEmptyGlyph");
    glyph->setAlignment(Qt::AlignHCenter);
    el->addWidget(glyph);
    m_emptyText = new QLabel;
    m_emptyText->setObjectName("homeEmptyText");
    m_emptyText->setAlignment(Qt::AlignHCenter);
    el->addWidget(m_emptyText);
    m_empty->setVisible(false);
    col->addWidget(m_empty);

    col->addStretch();

    m_scroll->setWidget(canvas);
    root->addWidget(m_scroll, 1);

    setStyleSheet(R"(
        #homePage, #homeScroll, #homeCanvas, #homeColumn { background: #191919; }

        #homeEyebrow { color: #5c5c66; font-size: 10px; font-weight: 700;
                       letter-spacing: 3px; }
        #homeHero { color: #f2f2f4; font-size: 34px; font-weight: 700;
                    letter-spacing: -0.6px; }
        #homeTagline { color: #7d7d88; font-size: 13px; }

        #homeNewBtn {
            background: #4f7cff; color: #ffffff; border: none; border-radius: 8px;
            padding: 0 20px; font-size: 13px; font-weight: 600;
        }
        #homeNewBtn:hover  { background: #6b91ff; }
        #homeNewBtn:pressed { background: #4069e6; }

        #homeOpenBtn {
            background: #232326; color: #d4d4dc; border: 1px solid #303036;
            border-radius: 8px; padding: 0 20px; font-size: 13px; font-weight: 500;
        }
        #homeOpenBtn:hover  { background: #2b2b30; border-color: #3f3f48; color: #ffffff; }
        #homeOpenBtn:pressed { background: #202024; }

        #homeSearch {
            background: #202024; color: #e6e6e8; border: 1px solid #2b2b32;
            border-radius: 9px; padding: 0 14px; font-size: 13px;
        }
        #homeSearch:focus { border-color: #4f7cff; background: #1c1c20; }

        #homeSectionLabel { color: #8a8a95; font-size: 11px; font-weight: 700;
                            letter-spacing: 1.4px; }
        #homeCount { color: #55555f; font-size: 11px; font-weight: 500; }
        #homeRule  { background: #262629; border: none; }

        #homeEmpty { background: transparent; }
        #homeEmptyGlyph { color: #3d3d45; font-size: 26px; }
        #homeEmptyText  { color: #6b6b76; font-size: 13px; line-height: 155%; }

        #projectCard {
            background: #202024; border: 1px solid #2a2a30; border-radius: 11px;
        }
        #projectCard:hover { background: #26262c; border-color: #4a4a58; }
        #cardThumb { background: transparent; border: none; }
        #cardMeta  { background: transparent; border: none; }
        #cardTitle { color: #e9e9ee; font-size: 13px; font-weight: 600;
                     background: transparent; }
        #cardTitleEdit {
            background: #17171a; color: #ffffff; border: 1px solid #4f7cff;
            border-radius: 4px; padding: 0 4px; font-size: 13px; font-weight: 600;
            selection-background-color: #4f7cff;
        }
        #cardSub { color: #70707b; font-size: 11px; background: transparent; }

        QScrollBar:vertical { background: transparent; width: 10px; margin: 4px; }
        QScrollBar::handle:vertical { background: #33333a; border-radius: 5px; min-height: 36px; }
        QScrollBar::handle:vertical:hover { background: #45454e; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    )");
}

void HomePage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    refresh();
}

void HomePage::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    applyColumnWidth();
    rebuildGrid();
}

// The content column is 60% of the window, clamped so it never gets narrower
// than a single card or so wide that the grid reads as a wall.
void HomePage::applyColumnWidth()
{
    if (!m_column) return;
    int avail = m_scroll ? m_scroll->viewport()->width() : width();
    int target = qBound(kCardW + 24, int(avail * 0.60), 1180);
    // Snap up or down to a whole number of card columns — whichever lands
    // closer to the 60% target — so the grid is never left visually lopsided.
    int cols = std::max(1, (target + kGap) / (kCardW + kGap));
    auto widthFor = [](int n) { return n * kCardW + (n - 1) * kGap; };
    int w = widthFor(cols);
    if (widthFor(cols + 1) <= avail - 32
        && std::abs(widthFor(cols + 1) - target) < std::abs(w - target))
        w = widthFor(cols + 1);
    m_column->setFixedWidth(w);
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

    m_countLabel->setText(shown.isEmpty()
        ? QString()
        : QString("%1 project%2").arg(shown.size()).arg(shown.size() == 1 ? "" : "s"));

    m_empty->setVisible(shown.isEmpty());
    m_gridHost->setVisible(!shown.isEmpty());
    if (shown.isEmpty()) {
        m_emptyText->setText(m_filter.isEmpty()
            ? "Nothing here yet.\nCreate a new project or open an image to get started."
            : "No projects match \"" + m_filter + "\".");
        return;
    }

    int cols = std::max(1, (m_column->width() + kGap) / (kCardW + kGap));
    int r = 0, c = 0;
    for (const RecentEntry& e : shown) {
        auto* card = new ProjectCard(e);
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
