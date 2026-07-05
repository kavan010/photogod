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
class QGridLayout;
class QVBoxLayout;

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
    QToolButton *m_btnAdd, *m_btnAdj, *m_btnDup, *m_btnMask, *m_btnDelMask,
                *m_btnEditMask, *m_btnLock, *m_btnMerge, *m_btnDel;
    QTimer m_thumbTimer;
    bool m_updating = false;
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
    void refresh();
    void addRecent(const QColor& c);
    void pickFg();
    void pickBg();

    ToolSettings* m_ts;
    QToolButton *m_fgBtn, *m_bgBtn;
    QList<QToolButton*> m_recentBtns;
    QList<QColor> m_recent;
};

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
