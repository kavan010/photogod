#pragma once
#include <QWidget>
#include <QString>
#include <QDateTime>
#include <QList>
#include <QImage>

class QLineEdit;
class QScrollArea;
class QGridLayout;
class QLabel;
class QVBoxLayout;

// ---- persistent store of recently opened/saved .pgd projects ----
struct RecentEntry {
    QString path;
    QString name;
    QDateTime opened;
};

namespace Recents {
    QList<RecentEntry> load();                     // most-recent first, missing files pruned
    void touch(const QString& path, const QString& name);   // add / bump to top
    void remove(const QString& path);
    void rename(const QString& path, const QString& newName);  // store + .pgd "name" field
    void saveThumbnail(const QString& path, const QImage& composite);
    QString thumbnailPath(const QString& projectPath);      // cached png path (may not exist)
}

// ---- the start screen ----
class HomePage : public QWidget
{
    Q_OBJECT
public:
    explicit HomePage(QWidget* parent = nullptr);

    void refresh();   // reload cards from the store

signals:
    void newRequested();
    void openRequested();                 // browse dialog
    void projectRequested(const QString& path);

protected:
    void showEvent(QShowEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    void rebuildGrid();
    void applyColumnWidth();
    int  maxColumns() const;   // how many cards fit across the window

    QWidget* m_column = nullptr;      // the centred content column
    QLineEdit* m_search = nullptr;
    QScrollArea* m_scroll = nullptr;
    QGridLayout* m_gridLay = nullptr;
    QWidget* m_gridHost = nullptr;
    QLabel* m_sectionLabel = nullptr;
    QWidget* m_empty = nullptr;
    QLabel* m_emptyText = nullptr;
    QList<RecentEntry> m_entries;
    QList<QWidget*> m_cards;          // in grid order, for arrow-key navigation
    int m_cols = 0;            // columns actually in use; 0 = nothing shown
    QString m_filter;
};
