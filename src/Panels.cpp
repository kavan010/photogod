#include "Panels.h"
#include "Commands.h"
#include "Dialogs.h"
#include <QListWidget>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QSpinBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QMenu>
#include <QInputDialog>
#include <QColorDialog>
#include <QPainter>
#include <QMouseEvent>
#include <QAbstractItemModel>
#include <QRandomGenerator>
#include <QSet>
#include <QTimer>

// ============================ SVSquare ============================

SVSquare::SVSquare(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(120, 110);
    setFixedHeight(130);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::CrossCursor);
}

void SVSquare::setHsv(double h, double s, double v)
{
    m_h = h; m_s = s; m_v = v;
    update();
}

void SVSquare::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    QLinearGradient sat(0, 0, width(), 0);
    sat.setColorAt(0, Qt::white);
    sat.setColorAt(1, QColor::fromHsvF(float(m_h), 1.0f, 1.0f));
    p.fillRect(rect(), sat);
    QLinearGradient val(0, 0, 0, height());
    val.setColorAt(0, QColor(0, 0, 0, 0));
    val.setColorAt(1, QColor(0, 0, 0, 255));
    p.fillRect(rect(), val);

    p.setRenderHint(QPainter::Antialiasing);
    QPointF m(m_s * width(), (1.0 - m_v) * height());
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::black, 1.4));
    p.drawEllipse(m, 5.5, 5.5);
    p.setPen(QPen(Qt::white, 1.4));
    p.drawEllipse(m, 4.2, 4.2);
}

void SVSquare::handle(const QPointF& pos)
{
    m_s = std::clamp(pos.x() / width(), 0.0, 1.0);
    m_v = std::clamp(1.0 - pos.y() / height(), 0.0, 1.0);
    update();
    emit svChanged(m_s, m_v);
}

void SVSquare::mousePressEvent(QMouseEvent* e) { handle(e->position()); }
void SVSquare::mouseMoveEvent(QMouseEvent* e) { if (e->buttons() & Qt::LeftButton) handle(e->position()); }
void SVSquare::mouseReleaseEvent(QMouseEvent*) { emit released(); }

// ============================ HueBar ============================

HueBar::HueBar(QWidget* parent) : QWidget(parent)
{
    setFixedWidth(18);
    setFixedHeight(130);
    setCursor(Qt::CrossCursor);
}

void HueBar::setHue(double h)
{
    m_h = h;
    update();
}

void HueBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    QLinearGradient g(0, 0, 0, height());
    static const double stops[] = {0, 1 / 6.0, 2 / 6.0, 3 / 6.0, 4 / 6.0, 5 / 6.0, 0.9999};
    for (double s : stops)
        g.setColorAt(s, QColor::fromHsvF(float(s), 1.0f, 1.0f));
    p.fillRect(rect(), g);

    int y = int(std::lround(m_h * height()));
    p.setPen(QPen(Qt::white, 2));
    p.drawLine(0, y, width(), y);
    p.setPen(QPen(Qt::black, 1));
    p.drawRect(0, y - 2, width() - 1, 4);
}

void HueBar::handle(const QPointF& pos)
{
    m_h = std::clamp(pos.y() / height(), 0.0, 0.9999);
    update();
    emit hueChanged(m_h);
}

void HueBar::mousePressEvent(QMouseEvent* e) { handle(e->position()); }
void HueBar::mouseMoveEvent(QMouseEvent* e) { if (e->buttons() & Qt::LeftButton) handle(e->position()); }
void HueBar::mouseReleaseEvent(QMouseEvent*) { emit released(); }

// ============================ ColorPanel ============================

ColorPanel::ColorPanel(ToolSettings* ts, QWidget* parent) : QWidget(parent), m_ts(ts)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(6);

    // fg/bg swatches
    auto* row = new QHBoxLayout;
    m_fgBtn = new QToolButton;
    m_fgBtn->setFixedSize(38, 30);
    m_fgBtn->setToolTip("Foreground color");
    m_bgBtn = new QToolButton;
    m_bgBtn->setFixedSize(38, 30);
    m_bgBtn->setToolTip("Background color (click to change)");
    auto* swap = new QToolButton;
    swap->setText("⇄");
    swap->setToolTip("Swap colors (X)");
    row->addWidget(m_fgBtn);
    row->addWidget(m_bgBtn);
    row->addWidget(swap);
    row->addStretch();
    lay->addLayout(row);

    connect(m_fgBtn, &QToolButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(m_ts->fg, this, "Foreground Color");
        if (c.isValid()) setFg(c);
    });
    connect(m_bgBtn, &QToolButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(m_ts->bg, this, "Background Color");
        if (c.isValid()) {
            m_ts->bg = c;
            refresh();
            emit colorsChanged();
        }
    });
    connect(swap, &QToolButton::clicked, this, [this] { swapColors(); });

    // SV square + hue bar
    auto* pickRow = new QHBoxLayout;
    m_sv = new SVSquare;
    m_hue = new HueBar;
    pickRow->addWidget(m_sv, 1);
    pickRow->addWidget(m_hue);
    lay->addLayout(pickRow);

    connect(m_sv, &SVSquare::svChanged, this, [this](double s, double v) {
        if (m_updating) return;
        m_s = s; m_v = v;
        applyHsv(false);
    });
    connect(m_hue, &HueBar::hueChanged, this, [this](double h) {
        if (m_updating) return;
        m_h = h;
        m_sv->setHsv(m_h, m_s, m_v);
        applyHsv(false);
    });
    connect(m_sv, &SVSquare::released, this, [this] { addRecent(m_ts->fg); refresh(); });
    connect(m_hue, &HueBar::released, this, [this] { addRecent(m_ts->fg); refresh(); });

    // RGB + hex
    auto* rgbRow = new QHBoxLayout;
    rgbRow->setSpacing(3);
    auto mkSpin = [&](const QString& lbl) {
        rgbRow->addWidget(new QLabel(lbl));
        auto* s = new QSpinBox;
        s->setRange(0, 255);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setFixedWidth(40);
        rgbRow->addWidget(s);
        return s;
    };
    m_r = mkSpin("R");
    m_g = mkSpin("G");
    m_b = mkSpin("B");
    m_hex = new QLineEdit;
    m_hex->setFixedWidth(66);
    m_hex->setPlaceholderText("#rrggbb");
    rgbRow->addWidget(m_hex);
    rgbRow->addStretch();
    lay->addLayout(rgbRow);

    auto rgbChanged = [this] {
        if (m_updating) return;
        QColor c(m_r->value(), m_g->value(), m_b->value());
        m_ts->fg = c;
        syncFromColor(c);
        refresh();
        emit colorsChanged();
    };
    connect(m_r, &QSpinBox::valueChanged, this, rgbChanged);
    connect(m_g, &QSpinBox::valueChanged, this, rgbChanged);
    connect(m_b, &QSpinBox::valueChanged, this, rgbChanged);
    connect(m_r, &QSpinBox::editingFinished, this, [this] { addRecent(m_ts->fg); refresh(); });
    connect(m_g, &QSpinBox::editingFinished, this, [this] { addRecent(m_ts->fg); refresh(); });
    connect(m_b, &QSpinBox::editingFinished, this, [this] { addRecent(m_ts->fg); refresh(); });
    connect(m_hex, &QLineEdit::editingFinished, this, [this] {
        QColor c(m_hex->text().trimmed());
        if (c.isValid()) setFg(c);
    });

    // recent colors
    lay->addWidget(new QLabel("Recent:"));
    auto* recRow = new QHBoxLayout;
    recRow->setSpacing(2);
    for (int r = 0; r < 8; ++r) {
        auto* b = new QToolButton;
        b->setFixedSize(20, 20);
        connect(b, &QToolButton::clicked, this, [this, r] {
            if (r < m_recent.size()) setFg(m_recent[r]);
        });
        m_recentBtns << b;
        recRow->addWidget(b);
    }
    recRow->addStretch();
    lay->addLayout(recRow);
    lay->addStretch();

    syncFromColor(m_ts->fg);
    refresh();
}

void ColorPanel::syncFromColor(const QColor& c)
{
    float h, s, v;
    c.getHsvF(&h, &s, &v);
    if (h >= 0) m_h = h;          // hue undefined for grays: keep previous
    if (v > 0.001) m_s = s;       // saturation undefined at black
    m_v = v;
    m_updating = true;
    m_sv->setHsv(m_h, m_s, m_v);
    m_hue->setHue(m_h);
    m_updating = false;
}

void ColorPanel::applyHsv(bool notifyRecent)
{
    QColor c = QColor::fromHsvF(float(m_h), float(m_s), float(m_v));
    m_ts->fg = c;
    if (notifyRecent) addRecent(c);
    refresh();
    emit colorsChanged();
}

void ColorPanel::refresh()
{
    m_fgBtn->setStyleSheet(QString("background:%1; border:1px solid #55555c; border-radius:4px;").arg(m_ts->fg.name()));
    m_bgBtn->setStyleSheet(QString("background:%1; border:1px solid #55555c; border-radius:4px;").arg(m_ts->bg.name()));
    m_updating = true;
    m_r->setValue(m_ts->fg.red());
    m_g->setValue(m_ts->fg.green());
    m_b->setValue(m_ts->fg.blue());
    m_hex->setText(m_ts->fg.name());
    m_updating = false;
    for (int i = 0; i < m_recentBtns.size(); ++i) {
        if (i < m_recent.size())
            m_recentBtns[i]->setStyleSheet(QString("background:%1; border:1px solid #4a4a50; border-radius:3px;").arg(m_recent[i].name()));
        else
            m_recentBtns[i]->setStyleSheet("background:transparent; border:1px solid #313136; border-radius:3px;");
    }
}

void ColorPanel::addRecent(const QColor& c)
{
    m_recent.removeAll(c);
    m_recent.prepend(c);
    while (m_recent.size() > 8) m_recent.removeLast();
}

void ColorPanel::setFg(const QColor& c)
{
    m_ts->fg = c;
    syncFromColor(c);
    addRecent(c);
    refresh();
    emit colorsChanged();
}

void ColorPanel::swapColors()
{
    std::swap(m_ts->fg, m_ts->bg);
    syncFromColor(m_ts->fg);
    refresh();
    emit colorsChanged();
}

// ============================ LayersPanel ============================

LayersPanel::LayersPanel(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    auto* topRow = new QHBoxLayout;
    m_blend = new QComboBox;
    m_blend->addItems(blendModeNames());
    topRow->addWidget(m_blend, 1);
    lay->addLayout(topRow);

    auto* opRow = new QHBoxLayout;
    opRow->addWidget(new QLabel("Opacity"));
    m_opacity = new QSlider(Qt::Horizontal);
    m_opacity->setRange(0, 100);
    m_opacity->setValue(100);
    m_opacityLabel = new QLabel("100%");
    m_opacityLabel->setMinimumWidth(38);
    opRow->addWidget(m_opacity, 1);
    opRow->addWidget(m_opacityLabel);
    lay->addLayout(opRow);

    m_list = new QListWidget;
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    lay->addWidget(m_list, 1);

    auto mkBtn = [](const QString& text, const QString& tip) {
        auto* b = new QToolButton;
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    m_btnAdd = mkBtn("+", "New layer");
    m_btnAdj = mkBtn("fx", "New adjustment layer");
    m_btnDup = mkBtn("⧉", "Duplicate layer (Ctrl+J)");
    m_btnMask = mkBtn("◧", "Add layer mask (from selection if any)");
    m_btnDelMask = mkBtn("◧✕", "Delete layer mask");
    m_btnEditMask = mkBtn("✎M", "Paint on mask (toggle)");
    m_btnEditMask->setCheckable(true);
    m_btnMerge = mkBtn("⤓", "Merge down (Ctrl+E)");
    m_btnDel = mkBtn("🗑", "Delete layer");

    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(1);
    for (auto* b : {m_btnAdd, m_btnAdj, m_btnDup, m_btnMask, m_btnDelMask,
                    m_btnEditMask, m_btnMerge, m_btnDel})
        btnRow->addWidget(b);
    btnRow->addStretch();
    lay->addLayout(btnRow);

    m_thumbTimer.setSingleShot(true);
    m_thumbTimer.setInterval(400);
    connect(&m_thumbTimer, &QTimer::timeout, this, &LayersPanel::refreshThumbnails);

    // ---- interactions ----
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_updating || !m_doc || row < 0) return;
        m_doc->setActiveIndex(rowToLayer(row));
        syncControls();
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        if (!m_doc) return;
        int idx = rowToLayer(m_list->row(it));
        if (idx < 0) return;
        bool ok = false;
        QString name = QInputDialog::getText(this, "Rename Layer", "Name:",
                                             QLineEdit::Normal, m_doc->layers[idx]->name, &ok);
        if (ok && !name.isEmpty()) {
            m_doc->layers[idx]->name = name;
            rebuild();
        }
    });

    // QListWidget's InternalMove is implemented as insert+remove, so watch all
    // three signals and reconcile the order once the drop has fully settled.
    auto scheduleReorderCheck = [this] {
        if (m_updating || m_reorderPending || !m_doc) return;
        m_reorderPending = true;
        QTimer::singleShot(0, this, [this] {
            m_reorderPending = false;
            handleReorder();
        });
    };
    connect(m_list->model(), &QAbstractItemModel::rowsInserted, this, scheduleReorderCheck);
    connect(m_list->model(), &QAbstractItemModel::rowsRemoved, this, scheduleReorderCheck);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, scheduleReorderCheck);

    connect(m_blend, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_updating || !m_doc) return;
        if (auto l = m_doc->activeLayer()) {
            l->blend = BlendMode(i);
            m_doc->invalidate();
        }
    });
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        m_opacityLabel->setText(QString::number(v) + "%");
        if (m_updating || !m_doc) return;
        if (auto l = m_doc->activeLayer()) {
            l->opacity = v / 100.0;
            m_doc->invalidate();
        }
    });

    connect(m_btnAdd, &QToolButton::clicked, this, [this] {
        if (!m_doc) return;
        auto l = Layer::makeRaster(QString("Layer %1").arg(m_doc->layers.size() + 1), m_doc->size());
        m_doc->undo.push(new AddLayerCommand(m_doc, l, m_doc->activeIndex + 1, "New Layer"));
    });
    connect(m_btnAdj, &QToolButton::clicked, this, [this] {
        if (!m_doc) return;
        QMenu menu(this);
        for (FilterType t : {FilterType::Brightness, FilterType::Contrast, FilterType::Levels,
                             FilterType::Saturation, FilterType::Hue, FilterType::Exposure,
                             FilterType::Grayscale, FilterType::Invert}) {
            menu.addAction(filterName(t), [this, t] {
                auto l = Layer::makeAdjustment(t);
                m_doc->undo.push(new AddLayerCommand(m_doc, l, m_doc->activeIndex + 1, "New Adjustment"));
            });
        }
        menu.exec(QCursor::pos());
    });
    connect(m_btnDup, &QToolButton::clicked, this, [this] {
        if (!m_doc) return;
        if (auto l = m_doc->activeLayer()) {
            auto c = l->clone();
            c->name = l->name + " copy";
            m_doc->undo.push(new AddLayerCommand(m_doc, c, m_doc->activeIndex + 1, "Duplicate Layer"));
        }
    });
    connect(m_btnMask, &QToolButton::clicked, this, [this] {
        if (!m_doc) return;
        auto l = m_doc->activeLayer();
        if (!l || l->hasMask()) return;
        LayerState pre = LayerState::capture(*l);
        if (m_doc->hasSelection()) {
            l->mask = m_doc->selectionMask().copy();
        } else {
            l->mask = QImage(m_doc->size(), QImage::Format_Grayscale8);
            l->mask.fill(255);
        }
        l->maskEdited();
        m_doc->undo.push(new LayerEditCommand(m_doc, l, pre, "Add Mask"));
        m_doc->maskEditing = true;
        m_doc->invalidate();
        rebuild();
    });
    connect(m_btnDelMask, &QToolButton::clicked, this, [this] {
        if (!m_doc) return;
        auto l = m_doc->activeLayer();
        if (!l || !l->hasMask()) return;
        LayerState pre = LayerState::capture(*l);
        l->mask = QImage();
        l->maskEdited();
        m_doc->maskEditing = false;
        m_doc->undo.push(new LayerEditCommand(m_doc, l, pre, "Delete Mask"));
        m_doc->invalidate();
        rebuild();
    });
    connect(m_btnEditMask, &QToolButton::toggled, this, [this](bool on) {
        if (m_updating || !m_doc) return;
        m_doc->maskEditing = on;
        syncControls();
    });
    connect(m_btnMerge, &QToolButton::clicked, this, [this] {
        if (!m_doc) return;
        int i = m_doc->activeIndex;
        if (i <= 0) return;
        auto top = m_doc->layers[i];
        auto bottom = m_doc->layers[i - 1];
        if (top->type == Layer::Adjustment || bottom->type == Layer::Adjustment) return;

        DocState before = DocState::capture(m_doc);
        QRect uni = top->rect().united(bottom->rect());
        QImage img(uni.size(), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        auto maskedSource = [](const std::shared_ptr<Layer>& l) {
            QImage src = l->image;
            if (l->hasMask()) {
                src = src.copy();
                QPainter mp(&src);
                mp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                mp.drawImage(-l->offset, l->maskAlpha());
            }
            return src;
        };
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setOpacity(bottom->opacity);
        p.drawImage(bottom->offset - uni.topLeft(), maskedSource(bottom));
        p.setOpacity(top->opacity);
        p.setCompositionMode(blendToQt(top->blend));
        p.drawImage(top->offset - uni.topLeft(), maskedSource(top));
        p.end();

        auto merged = std::make_shared<Layer>();
        merged->type = Layer::Raster;
        merged->name = bottom->name;
        merged->blend = bottom->blend;
        merged->image = img;
        merged->offset = uni.topLeft();
        m_doc->layers.removeAt(i);
        m_doc->layers[i - 1] = merged;
        m_doc->activeIndex = i - 1;
        m_doc->undo.push(new SnapshotCommand(m_doc, before, "Merge Down"));
        m_doc->notifyStructure();
    });
    connect(m_btnDel, &QToolButton::clicked, this, [this] {
        if (!m_doc || m_doc->layers.size() <= 1) return;
        m_doc->undo.push(new RemoveLayerCommand(m_doc, m_doc->activeIndex));
    });
}

void LayersPanel::handleReorder()
{
    if (!m_doc || m_updating) return;
    if (m_list->count() != m_doc->layers.size()) return;   // mid-drop or stale

    QList<std::shared_ptr<Layer>> newLayers;
    QSet<int> seen;
    for (int row = m_list->count() - 1; row >= 0; --row) {   // bottom of list = layer 0
        int idx = m_list->item(row)->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_doc->layers.size() || seen.contains(idx)) { rebuild(); return; }
        seen.insert(idx);
        newLayers.append(m_doc->layers[idx]);
    }
    if (newLayers == m_doc->layers) return;

    auto active = m_doc->activeLayer();
    DocState before = DocState::capture(m_doc);
    m_doc->layers = newLayers;
    int ai = int(m_doc->layers.indexOf(active));
    m_doc->activeIndex = ai >= 0 ? ai : 0;
    m_doc->undo.push(new SnapshotCommand(m_doc, before, "Reorder Layers"));
    m_doc->notifyStructure();
}

int LayersPanel::rowToLayer(int row) const
{
    return m_doc ? m_doc->layers.size() - 1 - row : -1;
}

int LayersPanel::layerToRow(int idx) const
{
    return m_doc ? m_doc->layers.size() - 1 - idx : -1;
}

void LayersPanel::setDocument(Document* doc)
{
    if (m_doc) disconnect(m_doc, nullptr, this, nullptr);
    m_doc = doc;
    if (m_doc) {
        connect(m_doc, &Document::structureChanged, this, &LayersPanel::rebuild);
        connect(m_doc, &Document::changed, this, [this] { m_thumbTimer.start(); });
        connect(m_doc, &Document::activeLayerChanged, this, [this] {
            m_updating = true;
            m_list->setCurrentRow(layerToRow(m_doc->activeIndex));
            m_updating = false;
            syncControls();
        });
    }
    rebuild();
}

void LayersPanel::highlightLayer(int layerIndex)
{
    if (!m_doc || layerIndex < 0 || layerIndex >= m_doc->layers.size()) return;
    const int row = layerToRow(layerIndex);
    if (row < 0 || row >= m_list->count()) return;

    m_list->setCurrentRow(row);
    if (auto* it = m_list->item(row))
        m_list->scrollToItem(it, QAbstractItemView::PositionAtCenter);
    m_list->setFocus(Qt::OtherFocusReason);
}

void LayersPanel::rebuild()
{
    m_updating = true;
    m_list->clear();
    m_thumbLabels.clear();
    if (m_doc) {
        for (int i = m_doc->layers.size() - 1; i >= 0; --i) {
            const auto& l = m_doc->layers[i];
            auto* it = new QListWidgetItem;
            it->setData(Qt::UserRole, i);
            it->setSizeHint(QSize(10, 46));
            it->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled
                         | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            m_list->addItem(it);

            auto* w = new QWidget;
            auto* h = new QHBoxLayout(w);
            h->setContentsMargins(2, 2, 4, 2);
            h->setSpacing(4);

            auto* eye = new QToolButton;
            eye->setAutoRaise(true);
            eye->setCheckable(true);
            QIcon eyeIcon;
            eyeIcon.addFile(":/icons/eye.svg", QSize(), QIcon::Normal, QIcon::On);
            eyeIcon.addFile(":/icons/eye-off.svg", QSize(), QIcon::Normal, QIcon::Off);
            eye->setIcon(eyeIcon);
            eye->setChecked(l->visible);
            eye->setToolTip("Show/hide layer");
            std::weak_ptr<Layer> wl = l;
            connect(eye, &QToolButton::toggled, this, [this, wl](bool on) {
                if (m_updating) return;
                if (auto lp = wl.lock()) {
                    lp->visible = on;
                    m_doc->invalidate();
                }
            });

            auto* thumb = new QLabel;
            thumb->setPixmap(QPixmap::fromImage(l->thumbnail(m_doc->size())));
            thumb->setFixedSize(62, 42);
            thumb->setAlignment(Qt::AlignCenter);
            m_thumbLabels.prepend(thumb);   // keep index aligned with layer order later

            QString suffix;
            if (l->type == Layer::Text) suffix += " <span style='color:#888'>[T]</span>";
            if (l->type == Layer::Adjustment) suffix += " <span style='color:#888'>[fx]</span>";
            if (l->hasMask()) suffix += " <span style='color:#888'>[mask]</span>";
            auto* name = new QLabel(l->name.toHtmlEscaped() + suffix);
            name->setTextFormat(Qt::RichText);

            auto* lock = new QToolButton;
            lock->setAutoRaise(true);
            lock->setCheckable(true);
            QIcon lockIcon;
            lockIcon.addFile(":/icons/lock.svg", QSize(), QIcon::Normal, QIcon::On);
            lockIcon.addFile(":/icons/unlock.svg", QSize(), QIcon::Normal, QIcon::Off);
            lock->setIcon(lockIcon);
            lock->setChecked(l->locked);
            lock->setToolTip("Lock/unlock layer");
            connect(lock, &QToolButton::toggled, this, [this, wl](bool on) {
                if (m_updating) return;
                if (auto lp = wl.lock()) lp->locked = on;
            });

            h->addWidget(eye);
            h->addWidget(thumb);
            h->addWidget(name, 1);
            h->addWidget(lock);
            m_list->setItemWidget(it, w);
        }
        m_list->setCurrentRow(layerToRow(m_doc->activeIndex));
    }
    m_updating = false;
    syncControls();
}

void LayersPanel::refreshThumbnails()
{
    if (!m_doc || m_thumbLabels.size() != m_doc->layers.size()) return;
    for (int i = 0; i < m_doc->layers.size(); ++i)
        m_thumbLabels[i]->setPixmap(QPixmap::fromImage(m_doc->layers[i]->thumbnail(m_doc->size())));
}

void LayersPanel::syncControls()
{
    if (!m_doc) return;
    auto l = m_doc->activeLayer();
    if (!l) return;
    m_updating = true;
    m_blend->setCurrentIndex(int(l->blend));
    m_opacity->setValue(int(std::lround(l->opacity * 100)));
    m_btnMask->setEnabled(!l->hasMask());
    m_btnDelMask->setEnabled(l->hasMask());
    m_btnEditMask->setEnabled(l->hasMask());
    if (!l->hasMask()) m_doc->maskEditing = false;
    m_btnEditMask->setChecked(m_doc->maskEditing && l->hasMask());
    m_updating = false;
}

// ============================ BrushesPanel ============================

struct BrushPreset {
    const char* name;
    int hardness, flow, opacity, noise, size;   // size < 0 keeps current
};

static const BrushPreset kBrushPresets[] = {
    {"Hard Round", 100, 100, 100, 0, -1},
    {"Soft Round", 25, 100, 100, 0, -1},
    {"Airbrush", 0, 8, 100, 0, -1},
    {"Marker", 95, 100, 55, 0, -1},
    {"Pencil", 100, 100, 100, 0, 3},
    {"Chalk", 70, 80, 100, 55, -1},
    {"Spray Paint", 15, 35, 100, 85, -1},
    {"Ink Wash", 40, 25, 70, 20, -1},
};

static QImage brushPresetThumb(const BrushPreset& bp)
{
    QImage img(64, 40, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(50, 50, 54));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    auto* rng = QRandomGenerator::global();

    double radius = 7;
    for (int i = 0; i <= 20; ++i) {
        double t = i / 20.0;
        QPointF c(8 + t * 48, 20 + std::sin(t * 3.14159 * 2) * 7);
        QRadialGradient g(c, radius);
        QColor ca(230, 230, 235);
        ca.setAlphaF(bp.flow / 100.0 * bp.opacity / 100.0 + 0.15);
        QColor c0 = ca;
        c0.setAlphaF(0.0);
        g.setColorAt(0, ca);
        g.setColorAt(std::clamp(bp.hardness / 100.0, 0.0, 0.99), ca);
        g.setColorAt(1, c0);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        if (bp.noise > 0) {
            for (int k = 0; k < 26; ++k) {
                double a = rng->generateDouble() * 6.28, r2 = rng->generateDouble() * radius;
                if (rng->generateDouble() < bp.noise / 130.0) continue;
                p.drawEllipse(c + QPointF(std::cos(a) * r2, std::sin(a) * r2), 1.1, 1.1);
            }
        } else {
            p.drawEllipse(c, radius, radius);
        }
    }
    return img;
}

BrushesPanel::BrushesPanel(ToolSettings* ts, QWidget* parent) : QWidget(parent), m_ts(ts)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    m_list = new QListWidget;
    m_list->setIconSize(QSize(64, 40));
    for (const auto& bp : kBrushPresets) {
        auto* it = new QListWidgetItem(QIcon(QPixmap::fromImage(brushPresetThumb(bp))), bp.name);
        m_list->addItem(it);
    }
    lay->addWidget(m_list, 1);
    lay->addWidget(new QLabel("<span style='color:#999'>Click a preset, then paint (B).<br>"
                              "Size/opacity stay as you set them.</span>"));

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        int i = m_list->row(it);
        if (i < 0 || i >= int(std::size(kBrushPresets))) return;
        const auto& bp = kBrushPresets[i];
        m_ts->brushHardness = bp.hardness;
        m_ts->brushFlow = bp.flow;
        m_ts->brushOpacity = bp.opacity;
        m_ts->brushNoise = bp.noise;
        if (bp.size > 0) m_ts->brushSize = bp.size;
        emit presetChosen();
    });
}

// ============================ AdjustmentsPanel ============================

AdjustmentsPanel::AdjustmentsPanel(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->addWidget(new QLabel("<b>Add adjustment layer</b>"));
    auto* grid = new QGridLayout;
    grid->setSpacing(4);

    struct Entry { const char* label; FilterType t; };
    static const Entry entries[] = {
        {"Brightness", FilterType::Brightness},
        {"Contrast", FilterType::Contrast},
        {"Levels", FilterType::Levels},
        {"Exposure", FilterType::Exposure},
        {"Saturation", FilterType::Saturation},
        {"Hue", FilterType::Hue},
        {"Black && White", FilterType::Grayscale},
        {"Invert", FilterType::Invert},
    };
    int i = 0;
    for (const auto& e : entries) {
        auto* b = new QPushButton(e.label);
        FilterType t = e.t;
        connect(b, &QPushButton::clicked, this, [this, t] { emit requestAdjustment(t); });
        grid->addWidget(b, i / 2, i % 2);
        ++i;
    }
    lay->addLayout(grid);
    lay->addWidget(new QLabel("<span style='color:#999'>Non-destructive: edit parameters<br>"
                              "any time in the Properties panel.</span>"));
    lay->addStretch();
}

// ============================ LevelsHistogram ============================

LevelsHistogram::LevelsHistogram(const QVector<int>& bins, std::shared_ptr<Layer> layer, QWidget* parent)
    : QWidget(parent), m_bins(bins), m_layer(layer)
{
    setMinimumHeight(96);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(96);
}

void LevelsHistogram::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 24, 26));
    p.setPen(QColor(70, 70, 75));
    p.drawRect(0, 0, width() - 1, height() - 1);

    int maxBin = 1;
    for (int b : m_bins) maxBin = std::max(maxBin, b);

    double bw = width() / 256.0;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(150, 150, 158));
    for (int i = 0; i < 256 && i < m_bins.size(); ++i) {
        double hgt = double(m_bins[i]) / maxBin * (height() - 14);
        p.drawRect(QRectF(i * bw, height() - 4 - hgt, std::max(1.0, bw), hgt));
    }

    auto lp = m_layer.lock();
    if (!lp) return;
    double black = lp->params.p1, white = lp->params.p2;
    double gamma = std::max(0.05, lp->params.p3);
    auto xAt = [&](double v) { return v / 255.0 * width(); };

    p.setPen(QPen(QColor(240, 240, 240), 1));
    p.drawLine(int(xAt(white)), 4, int(xAt(white)), height() - 4);
    p.setPen(QPen(QColor(20, 20, 20), 3));
    p.drawLine(int(xAt(black)), 4, int(xAt(black)), height() - 4);
    p.setPen(QPen(QColor(140, 180, 255), 1));
    double mid = black + (white - black) * std::pow(0.5, 1.0 / gamma);
    p.drawLine(int(xAt(mid)), 8, int(xAt(mid)), height() - 8);
}

// ============================ PropertiesPanel ============================

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent)
{
    m_lay = new QVBoxLayout(this);
    m_lay->setContentsMargins(6, 6, 6, 6);
    rebuild();
}

void PropertiesPanel::setDocument(Document* doc)
{
    if (m_doc) disconnect(m_doc, nullptr, this, nullptr);
    m_doc = doc;
    if (m_doc) {
        connect(m_doc, &Document::structureChanged, this, &PropertiesPanel::rebuild);
        connect(m_doc, &Document::activeLayerChanged, this, &PropertiesPanel::rebuild);
    }
    rebuild();
}

void PropertiesPanel::rebuild()
{
    delete m_content;
    m_content = new QWidget;
    auto* lay = new QVBoxLayout(m_content);
    lay->setContentsMargins(0, 0, 0, 0);

    auto l = m_doc ? m_doc->activeLayer() : nullptr;
    if (!m_doc || !l) {
        lay->addWidget(new QLabel("No layer selected"));
    } else {
        QString type = l->type == Layer::Text ? "Text layer"
                     : l->type == Layer::Adjustment ? "Adjustment layer" : "Raster layer";
        auto* title = new QLabel(QString("<b>%1</b><br><span style='color:#999'>%2</span>")
                                     .arg(l->name.toHtmlEscaped(), type));
        lay->addWidget(title);

        if (l->type == Layer::Raster && !l->image.isNull()) {
            lay->addWidget(new QLabel(QString("Size: %1 x %2  Offset: %3, %4")
                                          .arg(l->image.width()).arg(l->image.height())
                                          .arg(l->offset.x()).arg(l->offset.y())));
        }

        if (l->type == Layer::Adjustment) {
            LevelsHistogram* hist = nullptr;
            if (l->filter == FilterType::Levels) {
                // histogram of the content this layer sits on
                QImage below = m_doc->composite(l.get()).convertToFormat(QImage::Format_ARGB32);
                QVector<int> bins(256, 0);
                for (int y = 0; y < below.height(); ++y) {
                    const QRgb* line = reinterpret_cast<const QRgb*>(below.constScanLine(y));
                    for (int x = 0; x < below.width(); ++x) {
                        if (qAlpha(line[x]) < 8) continue;
                        ++bins[qGray(line[x])];
                    }
                }
                hist = new LevelsHistogram(bins, l);
                lay->addWidget(hist);
            }

            auto* form = new QFormLayout;
            auto specs = filterSliders(l->filter);
            Document* doc = m_doc;
            for (int i = 0; i < specs.size(); ++i) {
                const auto& spec = specs[i];
                auto* s = new QSlider(Qt::Horizontal);
                s->setRange(int(spec.min), int(spec.max));
                double cur = (i == 0 ? l->params.p1 : i == 1 ? l->params.p2 : l->params.p3);
                s->setValue(int(std::lround(cur / spec.scale)));
                auto* val = new QLabel(QString::number(cur, 'g', 4));
                auto* row = new QHBoxLayout;
                row->addWidget(s, 1);
                row->addWidget(val);
                form->addRow(spec.label + ":", row);
                std::weak_ptr<Layer> wl = l;
                double scale = spec.scale;
                connect(s, &QSlider::valueChanged, this, [wl, doc, i, scale, val, hist](int v) {
                    auto lp = wl.lock();
                    if (!lp) return;
                    double value = v * scale;
                    val->setText(QString::number(value, 'g', 4));
                    if (i == 0) lp->params.p1 = value;
                    else if (i == 1) lp->params.p2 = value;
                    else lp->params.p3 = value;
                    doc->invalidate();
                    if (hist) hist->update();
                });
            }
            lay->addLayout(form);
            if (specs.isEmpty())
                lay->addWidget(new QLabel("No parameters — toggle visibility to compare"));
        }

        if (l->type == Layer::Text) {
            auto* btn = new QPushButton("Edit Text…");
            lay->addWidget(btn);
            Document* doc = m_doc;
            std::weak_ptr<Layer> wl = l;
            connect(btn, &QPushButton::clicked, this, [this, doc, wl] {
                auto lp = wl.lock();
                if (!lp) return;
                TextDialog dlg(lp->text, lp->font, lp->color, this);
                if (dlg.exec() == QDialog::Accepted && !dlg.text().isEmpty()) {
                    LayerState pre = LayerState::capture(*lp);
                    lp->text = dlg.text();
                    lp->font = dlg.font();
                    lp->color = dlg.color();
                    lp->renderText();
                    doc->undo.push(new LayerEditCommand(doc, lp, pre, "Edit Text"));
                    doc->notifyStructure();
                }
            });
        }

        if (l->hasMask())
            lay->addWidget(new QLabel(m_doc->maskEditing
                                          ? "Mask: editing (painting affects mask)"
                                          : "Mask: present"));
    }
    lay->addStretch();
    m_lay->addWidget(m_content);
}
