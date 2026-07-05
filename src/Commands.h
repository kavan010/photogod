#pragma once
#include "Document.h"
#include <QUndoCommand>
#include <memory>

// Pixel/state edit of a single layer. Construct AFTER the edit has been applied:
// pass the state captured before the edit; "after" is captured from the layer now.
// If geometry is unchanged and dirtyRect (doc coords) is valid, stores only the
// changed rectangle to save memory.
class LayerEditCommand : public QUndoCommand
{
public:
    LayerEditCommand(Document* doc, std::shared_ptr<Layer> layer,
                     const LayerState& before, const QString& text,
                     const QRect& dirtyRect = QRect());
    void undo() override;
    void redo() override;

private:
    void applyState(const LayerState& s);
    void applyRect(const QImage& img, const QImage& mask);

    Document* m_doc;
    std::shared_ptr<Layer> m_layer;
    bool m_first = true;
    bool m_rectMode = false;
    LayerState m_before, m_after;
    QRect m_rect;                                  // in layer image coords
    QRect m_maskRect;                              // in doc coords
    QImage m_beforeImg, m_afterImg;                // rect-mode image patches
    QImage m_beforeMask, m_afterMask;              // rect-mode mask patches
};

class AddLayerCommand : public QUndoCommand
{
public:
    AddLayerCommand(Document* doc, std::shared_ptr<Layer> layer, int index, const QString& text);
    void undo() override;
    void redo() override;
private:
    Document* m_doc;
    std::shared_ptr<Layer> m_layer;
    int m_index;
};

class RemoveLayerCommand : public QUndoCommand
{
public:
    RemoveLayerCommand(Document* doc, int index);
    void undo() override;
    void redo() override;
private:
    Document* m_doc;
    std::shared_ptr<Layer> m_layer;
    int m_index;
};

class ReorderLayerCommand : public QUndoCommand
{
public:
    ReorderLayerCommand(Document* doc, int from, int to);
    void undo() override;
    void redo() override;
private:
    Document* m_doc;
    int m_from, m_to;
};

class SelectionCommand : public QUndoCommand
{
public:
    SelectionCommand(Document* doc, const QPainterPath& before, double beforeFeather,
                     const QPainterPath& after, double afterFeather, const QString& text);
    void undo() override;
    void redo() override;
private:
    Document* m_doc;
    QPainterPath m_before, m_after;
    double m_beforeFeather, m_afterFeather;
    bool m_first = true;
};

// Full-document snapshot for crop/resize/rotate/flatten/merge.
// Capture "before" prior to the operation, construct after it.
struct DocState
{
    QSize size;
    QList<std::shared_ptr<Layer>> layers;
    int activeIndex = 0;
    QPainterPath selection;
    double feather = 0;
    static DocState capture(Document* doc);
    void apply(Document* doc) const;
};

class SnapshotCommand : public QUndoCommand
{
public:
    SnapshotCommand(Document* doc, const DocState& before, const QString& text);
    void undo() override;
    void redo() override;
private:
    Document* m_doc;
    DocState m_before, m_after;
    bool m_first = true;
};
