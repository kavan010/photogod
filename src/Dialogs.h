#pragma once
#include "Document.h"
#include "Filters.h"
#include <QDialog>
#include <QFont>
#include <QColor>

class QSpinBox;
class QComboBox;
class QPlainTextEdit;
class QFontComboBox;
class QPushButton;
class QSlider;
class QLabel;
class QCheckBox;

class NewDocDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NewDocDialog(QWidget* parent = nullptr);
    QSize docSize() const;
    QColor background() const;
private:
    QSpinBox *m_w, *m_h;
    QComboBox* m_bg;
};

class TextDialog : public QDialog
{
    Q_OBJECT
public:
    TextDialog(const QString& text, const QFont& font, const QColor& color, QWidget* parent = nullptr);
    QString text() const;
    QFont font() const;
    QColor color() const { return m_color; }
private:
    QPlainTextEdit* m_edit;
    QFontComboBox* m_family;
    QSpinBox* m_size;
    QCheckBox *m_bold, *m_italic;
    QPushButton* m_colorBtn;
    QColor m_color;
};

// Live-preview destructive filter dialog operating on the active layer.
class AdjustmentDialog : public QDialog
{
    Q_OBJECT
public:
    AdjustmentDialog(Document* doc, FilterType type, QWidget* parent = nullptr);
    void done(int r) override;
private:
    void preview();
    FilterParams currentParams() const;

    Document* m_doc;
    FilterType m_type;
    std::shared_ptr<Layer> m_layer;
    LayerState m_pre;
    QList<QSlider*> m_sliders;
    QList<QLabel*> m_valueLabels;
    QList<SliderSpec> m_specs;
    bool m_finished = false;
};

// Generic width/height dialog for Image Resize / Canvas Size.
class SizeDialog : public QDialog
{
    Q_OBJECT
public:
    SizeDialog(const QString& title, const QSize& current, bool keepAspectDefault, QWidget* parent = nullptr);
    QSize newSize() const;
private:
    QSpinBox *m_w, *m_h;
    QCheckBox* m_aspect;
    QSize m_orig;
    bool m_updating = false;
};
