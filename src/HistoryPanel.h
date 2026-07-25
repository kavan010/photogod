#pragma once
#include "Document.h"
#include <QWidget>
#include <QImage>
#include <QList>
#include <QPointer>

class QUndoGroup;
class QVBoxLayout;
class QLabel;
class QScrollArea;
class QPushButton;

// Instant, self-positioning explainer shown while the cursor is over a control.
// Qt's own tooltips wait ~700ms; the history controls are destructive enough
// that the explanation has to be on screen the moment you hover.
class InstantHint : public QWidget
{
    Q_OBJECT
public:
    static void showFor(QWidget* anchor, const QString& title, const QString& body);
    static void hideNow();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    explicit InstantHint(QWidget* parent = nullptr);
    static InstantHint* instance();
    QLabel* m_title;
    QLabel* m_body;
};

// Watches a widget and drives InstantHint on enter/leave. Installed as an event
// filter so the buttons themselves stay plain QPushButtons.
class HintFilter : public QObject
{
    Q_OBJECT
public:
    HintFilter(QWidget* target, const QString& title, const QString& body);
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
private:
    QString m_title, m_body;
};

// One row in the history list: "● Brush Stroke" plus a revert button, and an
// expandable drawer holding the version snapshot.
class HistoryRow : public QWidget
{
    Q_OBJECT
public:
    // index is the undo-stack index this row represents *after* the command ran
    // (so index 0 is the "Original" pseudo-entry, index N is command N-1).
    HistoryRow(int index, const QString& text, QWidget* parent = nullptr);

    void setState(bool isCurrent, bool isUndone);
    void setSnapshot(const QImage& thumb);
    void setExpanded(bool on);
    bool isExpanded() const { return m_expanded; }
    int index() const { return m_index; }

signals:
    void revertRequested(int index);
    void toggled(int index);
    void compareRequested(int index);

protected:
    void mousePressEvent(QMouseEvent*) override;
    void paintEvent(QPaintEvent*) override;

private:
    int m_index;
    bool m_expanded = false;
    bool m_current = false;
    bool m_undone = false;
    bool m_styled = false;   // false until setState has run once
    QLabel* m_label;
    QLabel* m_thumb;
    QLabel* m_thumbNote;
    QWidget* m_drawer;
    QPushButton* m_revert;
    QPushButton* m_see;
};

// History panel: a git-style log of the active document's undo stack. Every
// entry keeps a pixel snapshot of the document as it looked at that point, so a
// version can be previewed side-by-side without touching the live document.
class HistoryPanel : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryPanel(QUndoGroup* group, QWidget* parent = nullptr);

    void setDocument(Document* doc);

private:
    void rebuild();                 // full row rebuild (stack contents changed)
    void syncStates();              // cheap: just repaint current/undone marks
    void refreshSnapshots();        // re-read thumbnails from the stack
    void showComparison(int index);
    QImage snapshotFor(int index) const;

    QUndoGroup* m_group;
    QPointer<Document> m_doc;
    HistoryStack* m_stack = nullptr;

    QVBoxLayout* m_rowsLay;
    QScrollArea* m_scroll;
    QLabel* m_empty;
    QList<HistoryRow*> m_rows;
    int m_expandedIndex = -1;
    int m_knownCount = -1;
};

