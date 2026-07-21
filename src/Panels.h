#pragma once
#include "Document.h"
#include "Common.h"
#include <QWidget>
#include <QTimer>

class QListWidget;
class QListWidgetItem;
class QComboBox;
class QSlider;
class QLabel;
class QToolButton;
class QSpinBox;
class QLineEdit;
class QVBoxLayout;

// ---------- color picker widgets ----------

// Saturation (x) / value (y) square for the current hue.
class SVSquare : public QWidget
{
    Q_OBJECT
public:
    explicit SVSquare(QWidget* parent = nullptr);
    void setHsv(double h, double s, double v);
signals:
    void svChanged(double s, double v);
    void released();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    void handle(const QPointF& pos);
    double m_h = 0, m_s = 0, m_v = 0;
};

// Vertical hue slider.
class HueBar : public QWidget
{
    Q_OBJECT
public:
    explicit HueBar(QWidget* parent = nullptr);
    void setHue(double h);
signals:
    void hueChanged(double h);
    void released();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    void handle(const QPointF& pos);
    double m_h = 0;
};

class ColorPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ColorPanel(ToolSettings* ts, QWidget* parent = nullptr);
    void setFg(const QColor& c);   // from eyedropper etc.
    void swapColors();

signals:
    void colorsChanged();

private:
    void applyHsv(bool notifyRecent);
    void syncFromColor(const QColor& c);
    void refresh();
    void addRecent(const QColor& c);

    ToolSettings* m_ts;
    double m_h = 0, m_s = 0, m_v = 0;
    bool m_updating = false;
    SVSquare* m_sv;
    HueBar* m_hue;
    QToolButton *m_fgBtn, *m_bgBtn;
    QSpinBox *m_r, *m_g, *m_b;
    QLineEdit* m_hex;
    QList<QToolButton*> m_recentBtns;
    QList<QColor> m_recent;
};

// ---------- layers ----------

class LayersPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LayersPanel(QWidget* parent = nullptr);
    void setDocument(Document* doc);

    // exposed so MainWindow menu actions can trigger the same behavior
    QToolButton* addButton() const { return m_btnAdd; }
    QToolButton* dupButton() const { return m_btnDup; }
    QToolButton* delButton() const { return m_btnDel; }
    QToolButton* mergeButton() const { return m_btnMerge; }
    QToolButton* maskButton() const { return m_btnMask; }
    QToolButton* delMaskButton() const { return m_btnDelMask; }

private:
    void rebuild();
    void refreshThumbnails();
    void syncControls();
    void handleReorder();
    int rowToLayer(int row) const;
    int layerToRow(int idx) const;
    bool m_reorderPending = false;

    Document* m_doc = nullptr;
    QListWidget* m_list;
    QComboBox* m_blend;
    QSlider* m_opacity;
    QLabel* m_opacityLabel;
    QList<QLabel*> m_thumbLabels;    // aligned with list rows
    QToolButton *m_btnAdd, *m_btnAdj, *m_btnDup, *m_btnMask, *m_btnDelMask,
                *m_btnEditMask, *m_btnMerge, *m_btnDel;
    QTimer m_thumbTimer;
    bool m_updating = false;
};

// ---------- brushes ----------

class BrushesPanel : public QWidget
{
    Q_OBJECT
public:
    explicit BrushesPanel(ToolSettings* ts, QWidget* parent = nullptr);
signals:
    void presetChosen();
private:
    ToolSettings* m_ts;
    QListWidget* m_list;
};

// ---------- adjustments ----------

class AdjustmentsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit AdjustmentsPanel(QWidget* parent = nullptr);
signals:
    void requestAdjustment(FilterType t);
};

// ---------- levels histogram ----------

class LevelsHistogram : public QWidget
{
    Q_OBJECT
public:
    LevelsHistogram(const QVector<int>& bins, std::shared_ptr<Layer> layer, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QVector<int> m_bins;
    std::weak_ptr<Layer> m_layer;
};

// ---------- properties ----------

class PropertiesPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PropertiesPanel(QWidget* parent = nullptr);
    void setDocument(Document* doc);

private:
    void rebuild();

    Document* m_doc = nullptr;
    QVBoxLayout* m_lay;
    QWidget* m_content = nullptr;
};
