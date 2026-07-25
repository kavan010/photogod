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

private:
    void rebuildGrid();
    void applyColumnWidth();

    QWidget* m_column = nullptr;      // the centred ~60% content column
    QLineEdit* m_search = nullptr;
    QScrollArea* m_scroll = nullptr;
    QGridLayout* m_gridLay = nullptr;
    QWidget* m_gridHost = nullptr;
    QLabel* m_sectionLabel = nullptr;
    QLabel* m_countLabel = nullptr;
    QWidget* m_empty = nullptr;
    QLabel* m_emptyText = nullptr;
    QList<RecentEntry> m_entries;
    QString m_filter;
};
