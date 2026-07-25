#pragma once
#include "Layer.h"
#include <QObject>
#include <QUndoStack>
#include <QPainterPath>
#include <QHash>
#include <QImage>
#include <QList>
#include <memory>

class Document;

// Undo stack that also keeps a picture of every version it passes through.
//
// QUndoStack can replay *state*, but only by mutating the live document, so
// showing an old version side-by-side with the current one is impossible from
// the stack alone. This subclass solves that by photographing the document at
// the one moment each version is guaranteed to be on screen: just before the
// next command is pushed. Combined with a capture of wherever the stack lands
// after undo/redo, every reachable version ends up with a snapshot.
class HistoryStack : public QUndoStack
{
    Q_OBJECT
public:
    explicit HistoryStack(QObject* parent = nullptr) : QUndoStack(parent) {}

    void setDocument(Document* d) { m_doc = d; }

    // Photographs the current version, then pushes. Index i in the snapshot map
    // means "the document after i commands", so index 0 is the original.
    void push(QUndoCommand* cmd);

    // Records the document as it looks right now under the stack's current
    // index. Cheap no-op if that version is already known.
    void snapshotCurrent();

    // Null image if this version was never recorded (e.g. it aged out of the
    // undo limit before the panel was ever opened).
    QImage snapshotAt(int index) const { return m_snaps.value(index); }

private:
    // Commands pushed while the stack is not at its tip drop every redoable
    // command — their snapshots become unreachable and must go with them.
    void dropSnapshotsAbove(int index);

    Document* m_doc = nullptr;
    QHash<int, QImage> m_snaps;
};

class Document : public QObject
{
    Q_OBJECT
public:
    Document(const QSize& size, const QColor& bg, QObject* parent = nullptr);

    static Document* fromImage(const QImage& img, const QString& name, QObject* parent = nullptr);
    static Document* loadProject(const QString& path, QObject* parent = nullptr);
    bool saveProject(const QString& path);

    QSize size() const { return m_size; }
    int width() const { return m_size.width(); }
    int height() const { return m_size.height(); }
    QRect rect() const { return QRect(QPoint(0, 0), m_size); }

    QString name = "Untitled";
    QString filePath;    // .pgd project path

    QList<std::shared_ptr<Layer>> layers;   // bottom -> top
    int activeIndex = 0;
    bool maskEditing = false;               // painting targets active layer's mask

    HistoryStack undo;

    // ruler guides (doc coords); not undoable, saved with the project
    QList<double> guidesH;   // horizontal lines (y values)
    QList<double> guidesV;   // vertical lines (x values)

    std::shared_ptr<Layer> activeLayer() const;
    void setActiveIndex(int i)
    {
        i = std::clamp(i, 0, int(layers.size()) - 1);
        if (i != activeIndex) {
            activeIndex = i;
            emit activeLayerChanged();
        }
    }

    // ----- selection -----
    QPainterPath selection;   // empty = select nothing (whole doc editable)
    double feather = 0;
    bool hasSelection() const { return !selection.isEmpty(); }
    void setSelection(const QPainterPath& p, double featherRadius);
    const QImage& selectionMask();        // Grayscale8, doc-sized
    const QImage& selectionMaskAlpha();   // ARGB32_Premultiplied alpha image

    // ----- compositing -----
    QImage composite(const Layer* skip = nullptr);
    void invalidate();          // pixels changed
    void notifyStructure();     // layer list / order changed

    // ----- structural ops (no undo; called by commands) -----
    void insertLayer(int idx, const std::shared_ptr<Layer>& l);
    std::shared_ptr<Layer> takeLayer(int idx);
    void moveLayerIdx(int from, int to);
    int indexOf(const std::shared_ptr<Layer>& l) const;

    // ----- whole-document ops (no undo; wrap in SnapshotCommand) -----
    void resizeCanvas(const QSize& newSize);          // centered
    void scaleTo(const QSize& newSize);
    void rotateDoc(int quarterTurnsCW);               // 1, 2 or 3
    void flipDoc(bool horizontal);
    void cropTo(const QRect& r);
    void setSize(const QSize& s) { m_size = s; }

signals:
    void changed();
    void structureChanged();
    void selectionChanged();
    void activeLayerChanged();

private:
    QSize m_size;
    QImage m_cache;
    bool m_cacheDirty = true;
    QImage m_selMask, m_selMaskAlpha;
    bool m_selMaskDirty = true;
};
