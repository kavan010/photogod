#pragma once
#include <QWidget>
#include <QString>
#include <QList>
#include <QIcon>
#include <functional>

class QLineEdit;
class QListWidget;
class QLabel;
class QKeyEvent;

// One searchable thing: a tool, an adjustment, a menu command, a layer, a panel.
struct PaletteCommand
{
    QString category;            // "Tool", "Adjust", "Layer", "Panel", "Filter", ...
    QString title;               // primary label
    QString detail;              // right-aligned hint: shortcut or extra context
    QString keywords;            // extra search terms, not displayed
    QString iconPath;            // ":/icons/foo.svg", may be empty
    std::function<void()> run;
    bool enabled = true;         // greyed + unselectable when false
};

// VS Code-style universal search. Frameless popup centered near the top of its
// parent window; Esc or focus loss dismisses it.
class CommandPalette : public QWidget
{
    Q_OBJECT
public:
    explicit CommandPalette(QWidget* parent = nullptr);

    // Commands are rebuilt every time the palette opens, so dynamic entries
    // (layers of the current document) stay current.
    void setProvider(std::function<QList<PaletteCommand>()> provider);

    void openPalette(const QString& initialQuery = QString());

protected:
    void keyPressEvent(QKeyEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
    void showEvent(QShowEvent*) override;
    void paintEvent(QPaintEvent*) override;

private:
    void refilter();
    void accept();
    void moveSelection(int delta);
    void positionOverParent();

    std::function<QList<PaletteCommand>()> m_provider;
    QList<PaletteCommand> m_all;
    QList<int> m_shown;          // indices into m_all, best match first

    QLineEdit* m_input;
    QListWidget* m_list;
    QLabel* m_hint;
};

// Subsequence fuzzy match. Returns a score (higher = better) or -1 for no match.
// Exposed for testing.
int paletteFuzzyScore(const QString& needle, const QString& haystack);
