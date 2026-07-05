#include "Dialogs.h"
#include "Commands.h"
#include <QSpinBox>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QFontComboBox>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QTimer>

// ---------- NewDocDialog ----------

NewDocDialog::NewDocDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("New Document");
    auto* form = new QFormLayout;
    m_w = new QSpinBox;
    m_w->setRange(1, 16000);
    m_w->setValue(1920);
    m_h = new QSpinBox;
    m_h->setRange(1, 16000);
    m_h->setValue(1080);
    m_bg = new QComboBox;
    m_bg->addItems({"White", "Transparent", "Black"});
    form->addRow("Width:", m_w);
    form->addRow("Height:", m_h);
    form->addRow("Background:", m_bg);

    auto* presets = new QComboBox;
    presets->addItems({"Preset...", "1920 x 1080", "1280 x 720", "3840 x 2160", "1080 x 1080", "800 x 600"});
    connect(presets, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        auto parts = t.split(" x ");
        if (parts.size() == 2) {
            m_w->setValue(parts[0].toInt());
            m_h->setValue(parts[1].toInt());
        }
    });
    form->addRow("Preset:", presets);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(buttons);
}

QSize NewDocDialog::docSize() const { return QSize(m_w->value(), m_h->value()); }

QColor NewDocDialog::background() const
{
    switch (m_bg->currentIndex()) {
    case 1: return Qt::transparent;
    case 2: return Qt::black;
    default: return Qt::white;
    }
}

// ---------- TextDialog ----------

TextDialog::TextDialog(const QString& text, const QFont& font, const QColor& color, QWidget* parent)
    : QDialog(parent), m_color(color)
{
    setWindowTitle(text.isEmpty() ? "Add Text" : "Edit Text");
    resize(460, 320);

    m_edit = new QPlainTextEdit;
    m_edit->setPlainText(text);
    m_family = new QFontComboBox;
    m_family->setCurrentFont(font);
    m_size = new QSpinBox;
    m_size->setRange(4, 800);
    m_size->setValue(std::max(4, font.pointSize()));
    m_bold = new QCheckBox("Bold");
    m_bold->setChecked(font.bold());
    m_italic = new QCheckBox("Italic");
    m_italic->setChecked(font.italic());
    m_colorBtn = new QPushButton;
    m_colorBtn->setFixedSize(48, 24);

    auto refreshSwatch = [this] {
        m_colorBtn->setStyleSheet(QString("background:%1; border:1px solid #888;").arg(m_color.name()));
    };
    refreshSwatch();
    connect(m_colorBtn, &QPushButton::clicked, this, [this, refreshSwatch] {
        QColor c = QColorDialog::getColor(m_color, this, "Text Color");
        if (c.isValid()) { m_color = c; refreshSwatch(); }
    });

    auto* row = new QHBoxLayout;
    row->addWidget(m_family, 1);
    row->addWidget(m_size);
    row->addWidget(m_bold);
    row->addWidget(m_italic);
    row->addWidget(m_colorBtn);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(row);
    lay->addWidget(m_edit, 1);
    lay->addWidget(buttons);
    m_edit->setFocus();
}

QString TextDialog::text() const { return m_edit->toPlainText(); }

QFont TextDialog::font() const
{
    QFont f = m_family->currentFont();
    f.setPointSize(m_size->value());
    f.setBold(m_bold->isChecked());
    f.setItalic(m_italic->isChecked());
    return f;
}

// ---------- AdjustmentDialog ----------

AdjustmentDialog::AdjustmentDialog(Document* doc, FilterType type, QWidget* parent)
    : QDialog(parent), m_doc(doc), m_type(type)
{
    setWindowTitle(filterName(type));
    m_layer = doc->activeLayer();
    m_pre = LayerState::capture(*m_layer);
    m_specs = filterSliders(type);

    auto* form = new QFormLayout;
    auto* debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(60);
    connect(debounce, &QTimer::timeout, this, &AdjustmentDialog::preview);

    for (const auto& spec : m_specs) {
        auto* s = new QSlider(Qt::Horizontal);
        s->setRange(int(spec.min), int(spec.max));
        s->setValue(int(spec.def));
        s->setMinimumWidth(260);
        auto* val = new QLabel(QString::number(spec.def * spec.scale));
        val->setMinimumWidth(44);
        auto* row = new QHBoxLayout;
        row->addWidget(s, 1);
        row->addWidget(val);
        form->addRow(spec.label + ":", row);
        m_sliders << s;
        m_valueLabels << val;
        double scale = spec.scale;
        connect(s, &QSlider::valueChanged, this, [debounce, val, scale](int v) {
            val->setText(QString::number(v * scale, 'g', 4));
            debounce->start();
        });
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(buttons);

    preview();  // apply defaults (usually identity)
}

FilterParams AdjustmentDialog::currentParams() const
{
    FilterParams p;
    if (m_sliders.size() > 0) p.p1 = m_sliders[0]->value() * m_specs[0].scale;
    if (m_sliders.size() > 1) p.p2 = m_sliders[1]->value() * m_specs[1].scale;
    if (m_sliders.size() > 2) p.p3 = m_sliders[2]->value() * m_specs[2].scale;
    return p;
}

void AdjustmentDialog::preview()
{
    if (m_finished || !m_layer) return;
    QImage img = m_pre.image;
    img.detach();
    applyFilter(img, m_type, currentParams());

    if (m_doc->hasSelection()) {
        // keep original outside the selection
        QImage masked = m_pre.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter p(&masked);
        QImage sel = m_doc->selectionMaskAlpha();
        QImage cut = img;
        QPainter cp(&cut);
        cp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        cp.drawImage(-m_layer->offset, sel);
        cp.end();
        // also erase original inside selection so semi-transparent filtered pixels don't stack
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.drawImage(-m_layer->offset, sel);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.drawImage(0, 0, cut);
        p.end();
        img = masked;
    }
    m_layer->image = img;
    m_doc->invalidate();
}

void AdjustmentDialog::done(int r)
{
    if (!m_finished && m_layer) {
        m_finished = true;
        if (r == QDialog::Accepted) {
            preview();  // make sure latest values applied
            m_finished = true;
            m_doc->undo.push(new LayerEditCommand(m_doc, m_layer, m_pre, filterName(m_type)));
        } else {
            m_pre.apply(*m_layer);
            m_doc->invalidate();
        }
    }
    QDialog::done(r);
}

// ---------- SizeDialog ----------

SizeDialog::SizeDialog(const QString& title, const QSize& current, bool keepAspectDefault, QWidget* parent)
    : QDialog(parent), m_orig(current)
{
    setWindowTitle(title);
    auto* form = new QFormLayout;
    m_w = new QSpinBox;
    m_w->setRange(1, 32000);
    m_w->setValue(current.width());
    m_h = new QSpinBox;
    m_h->setRange(1, 32000);
    m_h->setValue(current.height());
    m_aspect = new QCheckBox("Keep aspect ratio");
    m_aspect->setChecked(keepAspectDefault);
    form->addRow("Width:", m_w);
    form->addRow("Height:", m_h);
    form->addRow(m_aspect);

    connect(m_w, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_updating || !m_aspect->isChecked()) return;
        m_updating = true;
        m_h->setValue(std::max(1, int(std::lround(double(v) * m_orig.height() / m_orig.width()))));
        m_updating = false;
    });
    connect(m_h, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_updating || !m_aspect->isChecked()) return;
        m_updating = true;
        m_w->setValue(std::max(1, int(std::lround(double(v) * m_orig.width() / m_orig.height()))));
        m_updating = false;
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(buttons);
}

QSize SizeDialog::newSize() const { return QSize(m_w->value(), m_h->value()); }
