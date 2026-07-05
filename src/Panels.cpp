#include "Panels.h"
#include "Commands.h"
#include "Dialogs.h"
#include <QListWidget>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMenu>
#include <QInputDialog>
#include <QColorDialog>
#include <QPainter>
#include <QAbstractItemModel>
#include <QSet>
#include <QTimer>

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
    m_list->setIconSize(QSize(56, 40));
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
    m_btnLock = mkBtn("🔒", "Lock layer (toggle)");
    m_btnLock->setCheckable(true);
    m_btnMerge = mkBtn("⤓", "Merge down (Ctrl+E)");
    m_btnDel = mkBtn("🗑", "Delete layer");

    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(1);
    for (auto* b : {m_btnAdd, m_btnAdj, m_btnDup, m_btnMask, m_btnDelMask,
                    m_btnEditMask, m_btnLock, m_btnMerge, m_btnDel})
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
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        if (m_updating || !m_doc) return;
        int idx = rowToLayer(m_list->row(it));
        if (idx < 0 || idx >= m_doc->layers.size()) return;
        m_doc->layers[idx]->visible = (it->checkState() == Qt::Checked);
        m_doc->invalidate();
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
            it->setText(name);
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
        for (FilterType t : {FilterType::Brightness, FilterType::Contrast, FilterType::Saturation,
                             FilterType::Hue, FilterType::Exposure, FilterType::Levels,
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
        syncControls();
        refreshThumbnails();
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
        syncControls();
        refreshThumbnails();
    });
    connect(m_btnEditMask, &QToolButton::toggled, this, [this](bool on) {
        if (m_updating || !m_doc) return;
        m_doc->maskEditing = on;
        syncControls();
    });
    connect(m_btnLock, &QToolButton::toggled, this, [this](bool on) {
        if (m_updating || !m_doc) return;
        if (auto l = m_doc->activeLayer()) l->locked = on;
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
        auto drawOne = [&](const std::shared_ptr<Layer>& l) {
            QImage src = l->image;
            if (l->hasMask()) {
                src = src.copy();
                QPainter mp(&src);
                mp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                mp.drawImage(-l->offset, l->maskAlpha());
            }
            p.setOpacity(l->opacity);
            p.setCompositionMode(blendToQt(l->blend));
            p.drawImage(l->offset - uni.topLeft(), src);
        };
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setOpacity(bottom->opacity);
        {
            QImage src = bottom->image;
            if (bottom->hasMask()) {
                src = src.copy();
                QPainter mp(&src);
                mp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                mp.drawImage(-bottom->offset, bottom->maskAlpha());
            }
            p.drawImage(bottom->offset - uni.topLeft(), src);
        }
        drawOne(top);
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

void LayersPanel::rebuild()
{
    m_updating = true;
    m_list->clear();
    if (m_doc) {
        for (int i = m_doc->layers.size() - 1; i >= 0; --i) {
            const auto& l = m_doc->layers[i];
            auto* it = new QListWidgetItem;
            QString suffix;
            if (l->type == Layer::Text) suffix += " [T]";
            if (l->type == Layer::Adjustment) suffix += " [fx]";
            if (l->hasMask()) suffix += " [mask]";
            if (l->locked) suffix += " 🔒";
            it->setText(l->name + suffix);
            it->setData(Qt::UserRole, i);
            it->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable
                         | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            it->setCheckState(l->visible ? Qt::Checked : Qt::Unchecked);
            it->setIcon(QIcon(QPixmap::fromImage(l->thumbnail(m_doc->size()))));
            m_list->addItem(it);
        }
        m_list->setCurrentRow(layerToRow(m_doc->activeIndex));
    }
    m_updating = false;
    syncControls();
}

void LayersPanel::refreshThumbnails()
{
    if (!m_doc) return;
    for (int row = 0; row < m_list->count(); ++row) {
        int idx = rowToLayer(row);
        if (idx >= 0 && idx < m_doc->layers.size())
            m_list->item(row)->setIcon(QIcon(QPixmap::fromImage(
                m_doc->layers[idx]->thumbnail(m_doc->size()))));
    }
}

void LayersPanel::syncControls()
{
    if (!m_doc) return;
    auto l = m_doc->activeLayer();
    if (!l) return;
    m_updating = true;
    m_blend->setCurrentIndex(int(l->blend));
    m_opacity->setValue(int(std::lround(l->opacity * 100)));
    m_btnLock->setChecked(l->locked);
    m_btnMask->setEnabled(!l->hasMask());
    m_btnDelMask->setEnabled(l->hasMask());
    m_btnEditMask->setEnabled(l->hasMask());
    if (!l->hasMask()) m_doc->maskEditing = false;
    m_btnEditMask->setChecked(m_doc->maskEditing && l->hasMask());
    m_updating = false;
}

// ============================ ColorPanel ============================

static const QColor kSwatches[] = {
    QColor(0, 0, 0), QColor(64, 64, 64), QColor(128, 128, 128), QColor(192, 192, 192), QColor(255, 255, 255),
    QColor(220, 40, 40), QColor(240, 130, 30), QColor(250, 210, 50), QColor(60, 180, 75), QColor(40, 130, 220),
    QColor(90, 60, 200), QColor(200, 60, 200), QColor(245, 160, 180), QColor(140, 90, 50), QColor(0, 160, 160),
    QColor(130, 200, 60), QColor(20, 60, 120), QColor(120, 20, 40), QColor(255, 240, 200), QColor(30, 30, 60),
};

ColorPanel::ColorPanel(ToolSettings* ts, QWidget* parent) : QWidget(parent), m_ts(ts)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);

    auto* row = new QHBoxLayout;
    m_fgBtn = new QToolButton;
    m_fgBtn->setFixedSize(44, 34);
    m_fgBtn->setToolTip("Foreground color (click to change)");
    m_bgBtn = new QToolButton;
    m_bgBtn->setFixedSize(44, 34);
    m_bgBtn->setToolTip("Background color (click to change)");
    auto* swap = new QToolButton;
    swap->setText("⇄");
    swap->setToolTip("Swap colors (X)");
    row->addWidget(m_fgBtn);
    row->addWidget(m_bgBtn);
    row->addWidget(swap);
    row->addStretch();
    lay->addLayout(row);

    connect(m_fgBtn, &QToolButton::clicked, this, [this] { pickFg(); });
    connect(m_bgBtn, &QToolButton::clicked, this, [this] { pickBg(); });
    connect(swap, &QToolButton::clicked, this, [this] { swapColors(); });

    auto* grid = new QGridLayout;
    grid->setSpacing(2);
    int i = 0;
    for (const QColor& c : kSwatches) {
        auto* b = new QToolButton;
        b->setFixedSize(20, 20);
        b->setStyleSheet(QString("background:%1; border:1px solid #666;").arg(c.name()));
        b->setToolTip(c.name());
        connect(b, &QToolButton::clicked, this, [this, c] { setFg(c); });
        grid->addWidget(b, i / 5, i % 5);
        ++i;
    }
    lay->addLayout(grid);

    lay->addWidget(new QLabel("Recent:"));
    auto* recRow = new QHBoxLayout;
    recRow->setSpacing(2);
    for (int r = 0; r < 8; ++r) {
        auto* b = new QToolButton;
        b->setFixedSize(20, 20);
        b->setStyleSheet("background:transparent; border:1px dashed #555;");
        connect(b, &QToolButton::clicked, this, [this, r] {
            if (r < m_recent.size()) setFg(m_recent[r]);
        });
        m_recentBtns << b;
        recRow->addWidget(b);
    }
    recRow->addStretch();
    lay->addLayout(recRow);
    lay->addStretch();
    refresh();
}

void ColorPanel::refresh()
{
    m_fgBtn->setStyleSheet(QString("background:%1; border:2px solid #ddd;").arg(m_ts->fg.name()));
    m_bgBtn->setStyleSheet(QString("background:%1; border:1px solid #888;").arg(m_ts->bg.name()));
    for (int i = 0; i < m_recentBtns.size(); ++i) {
        if (i < m_recent.size())
            m_recentBtns[i]->setStyleSheet(QString("background:%1; border:1px solid #666;").arg(m_recent[i].name()));
        else
            m_recentBtns[i]->setStyleSheet("background:transparent; border:1px dashed #555;");
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
    addRecent(c);
    refresh();
    emit colorsChanged();
}

void ColorPanel::swapColors()
{
    std::swap(m_ts->fg, m_ts->bg);
    refresh();
    emit colorsChanged();
}

void ColorPanel::pickFg()
{
    QColor c = QColorDialog::getColor(m_ts->fg, this, "Foreground Color");
    if (c.isValid()) setFg(c);
}

void ColorPanel::pickBg()
{
    QColor c = QColorDialog::getColor(m_ts->bg, this, "Background Color");
    if (c.isValid()) {
        m_ts->bg = c;
        refresh();
        emit colorsChanged();
    }
}

// ============================ PropertiesPanel ============================

#include <QSlider>
#include <QFormLayout>
#include <QPushButton>

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
                connect(s, &QSlider::valueChanged, this, [wl, doc, i, scale, val](int v) {
                    auto lp = wl.lock();
                    if (!lp) return;
                    double value = v * scale;
                    val->setText(QString::number(value, 'g', 4));
                    if (i == 0) lp->params.p1 = value;
                    else if (i == 1) lp->params.p2 = value;
                    else lp->params.p3 = value;
                    doc->invalidate();
                });
            }
            lay->addLayout(form);
            if (specs.isEmpty())
                lay->addWidget(new QLabel("No parameters"));
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
