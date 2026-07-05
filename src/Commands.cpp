#include "Commands.h"
#include <QPainter>

// ---------- LayerEditCommand ----------

LayerEditCommand::LayerEditCommand(Document* doc, std::shared_ptr<Layer> layer,
                                   const LayerState& before, const QString& text,
                                   const QRect& dirtyRect)
    : QUndoCommand(text), m_doc(doc), m_layer(std::move(layer)), m_before(before)
{
    m_after = LayerState::capture(*m_layer);

    bool sameGeom = m_before.offset == m_after.offset
                 && m_before.image.size() == m_after.image.size()
                 && m_before.mask.size() == m_after.mask.size()
                 && m_before.text == m_after.text
                 && m_before.font == m_after.font
                 && m_before.color == m_after.color;

    if (sameGeom && dirtyRect.isValid() && !m_after.image.isNull()) {
        m_rectMode = true;
        m_rect = dirtyRect.translated(-m_after.offset)
                     .intersected(QRect(QPoint(0, 0), m_after.image.size()));
        if (!m_rect.isEmpty()) {
            m_beforeImg = m_before.image.copy(m_rect);
            m_afterImg = m_after.image.copy(m_rect);
        }
        if (!m_after.mask.isNull()) {
            m_maskRect = dirtyRect.intersected(QRect(QPoint(0, 0), m_after.mask.size()));
            if (!m_maskRect.isEmpty()) {
                m_beforeMask = m_before.mask.copy(m_maskRect);
                m_afterMask = m_after.mask.copy(m_maskRect);
            }
        }
        // drop the full copies
        m_before = LayerState();
        m_after = LayerState();
    }
}

void LayerEditCommand::applyRect(const QImage& img, const QImage& mask)
{
    if (!m_rect.isEmpty() && !img.isNull()) {
        QPainter p(&m_layer->image);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(m_rect.topLeft(), img);
    }
    if (!m_maskRect.isEmpty() && !mask.isNull()) {
        QPainter p(&m_layer->mask);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(m_maskRect.topLeft(), mask);
        m_layer->maskEdited();
    }
    m_doc->invalidate();
}

void LayerEditCommand::applyState(const LayerState& s)
{
    s.apply(*m_layer);
    if (m_layer->type == Layer::Text) m_layer->renderText();
    m_doc->invalidate();
    m_doc->notifyStructure();  // thumbnails / properties refresh
}

void LayerEditCommand::redo()
{
    if (m_first) { m_first = false; return; }
    if (m_rectMode) applyRect(m_afterImg, m_afterMask);
    else applyState(m_after);
}

void LayerEditCommand::undo()
{
    if (m_rectMode) applyRect(m_beforeImg, m_beforeMask);
    else applyState(m_before);
}

// ---------- AddLayerCommand ----------

AddLayerCommand::AddLayerCommand(Document* doc, std::shared_ptr<Layer> layer,
                                 int index, const QString& text)
    : QUndoCommand(text), m_doc(doc), m_layer(std::move(layer)), m_index(index) {}

void AddLayerCommand::redo() { m_doc->insertLayer(m_index, m_layer); }

void AddLayerCommand::undo()
{
    int idx = m_doc->indexOf(m_layer);
    if (idx >= 0) m_doc->takeLayer(idx);
}

// ---------- RemoveLayerCommand ----------

RemoveLayerCommand::RemoveLayerCommand(Document* doc, int index)
    : QUndoCommand("Delete Layer"), m_doc(doc), m_index(index)
{
    m_layer = doc->layers.value(index);
}

void RemoveLayerCommand::redo()
{
    int idx = m_doc->indexOf(m_layer);
    if (idx >= 0) m_doc->takeLayer(idx);
}

void RemoveLayerCommand::undo() { m_doc->insertLayer(m_index, m_layer); }

// ---------- ReorderLayerCommand ----------

ReorderLayerCommand::ReorderLayerCommand(Document* doc, int from, int to)
    : QUndoCommand("Reorder Layer"), m_doc(doc), m_from(from), m_to(to) {}

void ReorderLayerCommand::redo() { m_doc->moveLayerIdx(m_from, m_to); }
void ReorderLayerCommand::undo() { m_doc->moveLayerIdx(m_to, m_from); }

// ---------- SelectionCommand ----------

SelectionCommand::SelectionCommand(Document* doc, const QPainterPath& before, double beforeFeather,
                                   const QPainterPath& after, double afterFeather, const QString& text)
    : QUndoCommand(text), m_doc(doc), m_before(before), m_after(after),
      m_beforeFeather(beforeFeather), m_afterFeather(afterFeather) {}

void SelectionCommand::redo()
{
    if (m_first) { m_first = false; m_doc->setSelection(m_after, m_afterFeather); return; }
    m_doc->setSelection(m_after, m_afterFeather);
}

void SelectionCommand::undo() { m_doc->setSelection(m_before, m_beforeFeather); }

// ---------- SnapshotCommand ----------

DocState DocState::capture(Document* doc)
{
    DocState s;
    s.size = doc->size();
    for (const auto& l : doc->layers) s.layers.append(l->clone());
    s.activeIndex = doc->activeIndex;
    s.selection = doc->selection;
    s.feather = doc->feather;
    return s;
}

void DocState::apply(Document* doc) const
{
    doc->setSize(size);
    doc->layers.clear();
    for (const auto& l : layers) doc->layers.append(l->clone());
    doc->activeIndex = activeIndex;
    doc->setSelection(selection, feather);
    doc->notifyStructure();
}

SnapshotCommand::SnapshotCommand(Document* doc, const DocState& before, const QString& text)
    : QUndoCommand(text), m_doc(doc), m_before(before)
{
    m_after = DocState::capture(doc);
}

void SnapshotCommand::redo()
{
    if (m_first) { m_first = false; return; }
    m_after.apply(m_doc);
}

void SnapshotCommand::undo() { m_before.apply(m_doc); }
