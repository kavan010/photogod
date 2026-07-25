#include "MainWindow.h"
#include "Canvas.h"
#include "Commands.h"
#include "HistoryPanel.h"
#include <QPushButton>
#include <QDialog>
#include "CommandPalette.h"
#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QProxyStyle>
#include <QTimer>
#include <QMouseEvent>
#include <QDir>

// Tooltips normally wait ~700ms before appearing, which is too slow for
// identifying a tool you are hovering over. Drop the delay to near-zero and
// keep them up long enough to actually read.
class InstantTooltipStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    int styleHint(StyleHint hint, const QStyleOption* opt, const QWidget* w,
                  QStyleHintReturn* ret) const override
    {
        switch (hint) {
        case SH_ToolTip_WakeUpDelay:   return 1;      // ms before the first tip
        case SH_ToolTip_FallAsleepDelay: return 0;    // stay awake moving tool to tool
        default: break;
        }
        return QProxyStyle::styleHint(hint, opt, w, ret);
    }
};

// Headless functional test: paints, undoes, composites, saves and reloads.
static int runModelTest(MainWindow& w)
{
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        fprintf(stderr, "[%s] %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) ++failures;
    };

    w.newDocument(QSize(400, 300), Qt::white);
    Document* doc = w.currentDoc();
    Canvas* canvas = w.currentCanvas();
    check(doc && canvas, "document and canvas created");
    if (!doc || !canvas) return 1;

    canvas->fitToWindow();
    canvas->setTool(ToolType::Brush);

    // synthesize a brush stroke through the middle (default fg = black)
    auto send = [&](QEvent::Type t, const QPointF& pos) {
        QMouseEvent ev(t, pos, canvas->mapToGlobal(pos.toPoint()),
                       Qt::LeftButton, t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                       Qt::NoModifier);
        QApplication::sendEvent(canvas, &ev);
    };
    QPointF mid(canvas->width() / 2.0, canvas->height() / 2.0);
    send(QEvent::MouseButtonPress, mid + QPointF(-60, 0));
    for (int i = -60; i <= 60; i += 10)
        send(QEvent::MouseMove, mid + QPointF(i, 0));
    send(QEvent::MouseButtonRelease, mid + QPointF(60, 0));

    QImage comp = doc->composite();
    QRgb center = comp.pixel(doc->width() / 2, doc->height() / 2);
    check(qRed(center) < 60 && qGreen(center) < 60, "brush stroke painted black at center");
    check(doc->undo.count() == 1, "stroke pushed one undo command");

    doc->undo.undo();
    comp = doc->composite();
    center = comp.pixel(doc->width() / 2, doc->height() / 2);
    check(qRed(center) > 200, "undo restored white background");
    doc->undo.redo();
    comp = doc->composite();
    check(qRed(comp.pixel(doc->width() / 2, doc->height() / 2)) < 60, "redo re-applied stroke");

    // ---- history snapshots ----
    // Version 0 is the document before any command; it must have been captured
    // on the way past, and must still show the untouched white background.
    {
        const QImage v0 = doc->undo.snapshotAt(0);
        const QImage v1 = doc->undo.snapshotAt(1);
        check(!v0.isNull(), "history kept a snapshot of the original version");
        check(!v1.isNull(), "history kept a snapshot of the post-stroke version");
        check(v0.size() == doc->size(), "snapshot matches the document size");
        check(qRed(v0.pixel(doc->width() / 2, doc->height() / 2)) > 200,
              "the original snapshot still shows the unpainted background");
        check(qRed(v1.pixel(doc->width() / 2, doc->height() / 2)) < 60,
              "the post-stroke snapshot shows the stroke");
        check(qRed(doc->composite().pixel(doc->width() / 2, doc->height() / 2)) < 60,
              "reading snapshots left the live document untouched");
    }

    // Branching: reverting and then editing discards the redo tail, so the
    // snapshots of those now-unreachable versions must go with it.
    {
        Document scratch(QSize(40, 30), Qt::white);
        auto a = Layer::makeRaster("A", scratch.size(), Qt::red);
        auto b = Layer::makeRaster("B", scratch.size(), Qt::green);
        scratch.undo.push(new AddLayerCommand(&scratch, a, scratch.layers.size(), "A"));
        scratch.undo.push(new AddLayerCommand(&scratch, b, scratch.layers.size(), "B"));
        check(!scratch.undo.snapshotAt(2).isNull(), "branch test reached version 2");

        scratch.undo.setIndex(1);            // revert to just after "A"
        auto c = Layer::makeRaster("C", scratch.size(), Qt::blue);
        scratch.undo.push(new AddLayerCommand(&scratch, c, scratch.layers.size(), "C"));

        check(scratch.undo.count() == 2, "editing after a revert dropped the redo tail");
        check(!scratch.undo.snapshotAt(1).isNull(), "the reverted-to version keeps its snapshot");
        check(!scratch.undo.snapshotAt(2).isNull(), "the new branch tip has a snapshot");
        const QImage tip = scratch.undo.snapshotAt(2);
        check(qBlue(tip.pixel(2, 2)) > 180 && qGreen(tip.pixel(2, 2)) < 90,
              "the new tip's snapshot shows the branch's own content, not the discarded one");
    }

    // adjustment layer: strong brightness lift
    auto adj = Layer::makeAdjustment(FilterType::Brightness);
    adj->params.p1 = 80;
    doc->undo.push(new AddLayerCommand(doc, adj, doc->layers.size(), "New Adjustment"));
    comp = doc->composite();
    check(qRed(comp.pixel(doc->width() / 2, doc->height() / 2)) > 150,
          "brightness adjustment layer lifted dark pixels");

    // blend mode multiply on a red layer
    auto red = Layer::makeRaster("Red", doc->size(), QColor(255, 0, 0));
    red->blend = BlendMode::Multiply;
    doc->undo.push(new AddLayerCommand(doc, red, doc->layers.size(), "Red Layer"));
    comp = doc->composite();
    QRgb corner = comp.pixel(4, 4);
    check(qRed(corner) > 180 && qGreen(corner) < 90, "multiply blend keeps red, kills green");

    // selection + fill
    QPainterPath sel;
    sel.addRect(10, 10, 40, 40);
    doc->setSelection(sel, 0);
    doc->setActiveIndex(0);
    canvas->fillWith(QColor(0, 255, 0));
    doc->setSelection(QPainterPath(), 0);

    // save / load project
    QString proj = QDir::temp().filePath("photogod_selftest.pgd");
    check(doc->saveProject(proj), "project saved");
    Document* re = Document::loadProject(proj);
    check(re != nullptr, "project reloaded");
    if (re) {
        check(re->layers.size() == doc->layers.size(), "layer count survives round-trip");
        check(re->size() == doc->size(), "size survives round-trip");
        check(re->layers.last()->blend == BlendMode::Multiply, "blend mode survives round-trip");
        delete re;
    }

    // export png
    QString png = QDir::temp().filePath("photogod_selftest.png");
    check(doc->composite().save(png), "composite exported as PNG");

    // ---- round 2: blur brush, move hit-test, delete-key, snapping ----
    w.newDocument(QSize(300, 200), Qt::white);
    doc = w.currentDoc();
    canvas = w.currentCanvas();
    canvas->fitToWindow();

    // black left half via selection + fill
    QPainterPath half;
    half.addRect(0, 0, 150, 200);
    doc->setSelection(half, 0);
    canvas->fillWith(Qt::black);
    doc->setSelection(QPainterPath(), 0);

    // blur stroke across the boundary
    canvas->setTool(ToolType::Blur);
    auto sendAt = [&](QEvent::Type t, const QPointF& docPos) {
        QPointF wp = canvas->docToWidget(docPos);
        QMouseEvent ev(t, wp, canvas->mapToGlobal(wp.toPoint()),
                       Qt::LeftButton, t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                       Qt::NoModifier);
        QApplication::sendEvent(canvas, &ev);
    };
    sendAt(QEvent::MouseButtonPress, QPointF(120, 100));
    for (int x = 120; x <= 180; x += 5)
        sendAt(QEvent::MouseMove, QPointF(x, 100));
    sendAt(QEvent::MouseButtonRelease, QPointF(180, 100));
    comp = doc->composite();
    int edge = qRed(comp.pixel(152, 100));
    check(edge > 20 && edge < 235, "blur brush softened the black/white boundary");

    // move tool only grabs actual pixels
    auto red2 = Layer::makeRaster("Red2", QSize(50, 50), QColor(255, 0, 0));
    red2->offset = QPoint(10, 10);
    doc->undo.push(new AddLayerCommand(doc, red2, doc->layers.size(), "Red2"));
    canvas->setTool(ToolType::Move);
    QPoint before2 = red2->offset;
    sendAt(QEvent::MouseButtonPress, QPointF(250, 150));   // empty area
    sendAt(QEvent::MouseMove, QPointF(220, 120));
    sendAt(QEvent::MouseButtonRelease, QPointF(220, 120));
    check(red2->offset == before2, "move tool ignores drags that start off the layer's pixels");
    sendAt(QEvent::MouseButtonPress, QPointF(30, 30));     // on the red square
    sendAt(QEvent::MouseMove, QPointF(80, 60));
    sendAt(QEvent::MouseButtonRelease, QPointF(80, 60));
    check(red2->offset != before2, "move tool drags when grabbing the layer's pixels");

    // delete key clears the selection on the active layer
    QPainterPath sel2;
    sel2.addRect(red2->offset.x(), red2->offset.y(), 50, 50);
    doc->setSelection(sel2, 0);
    QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(canvas, &del);
    QPoint docPt(int(sel2.boundingRect().center().x()), int(sel2.boundingRect().center().y()));
    QPoint pl = docPt - red2->offset;
    bool erased = !red2->image.rect().contains(pl) || qAlpha(red2->image.pixel(pl)) < 10;
    check(erased, "Delete key erased the selected pixels");
    doc->setSelection(QPainterPath(), 0);

    // snapping: a guide at x=100 attracts marquee starts
    doc->guidesV.append(100);
    QPointF snapped = canvas->widgetToDoc(canvas->docToWidget(QPointF(0, 0)));  // sanity of mapping
    check(std::abs(snapped.x()) < 0.001, "doc/widget mapping round-trips");

    // Levels adjustment layer (also exercises the Properties histogram build)
    auto lev = Layer::makeAdjustment(FilterType::Levels);
    doc->undo.push(new AddLayerCommand(doc, lev, doc->layers.size(), "Levels"));
    lev->params.p1 = 140;   // raise black point: mid grays go dark
    doc->invalidate();
    comp = doc->composite();
    int levEdge = qRed(comp.pixel(152, 100));
    check(levEdge < edge - 10, "Levels adjustment layer crushed midtones via black point");

    // ---- command palette fuzzy matching ----
    check(paletteFuzzyScore("brush", "Brush") > 0, "palette matches an exact title");
    check(paletteFuzzyScore("gs", "Grayscale") > 0, "palette matches a subsequence");
    check(paletteFuzzyScore("zzz", "Grayscale") < 0, "palette rejects a non-match");
    check(paletteFuzzyScore("brush", "Brush") > paletteFuzzyScore("brush", "Blur Brush Radius"),
          "palette ranks the tighter title higher");
    check(paletteFuzzyScore("", "anything") == 0, "palette treats an empty query as neutral");

    // ---- command palette actually drives the editor ----
    // name the layers so we can address them by title
    doc->layers[0]->name = "Base Plate";
    doc->layers.last()->name = "Levels Adj";
    doc->setActiveIndex(doc->layers.size() - 1);

    check(w.runPaletteCommand("Base Plate"), "palette lists the document's layers");
    check(doc->activeIndex == 0, "choosing a layer in the palette makes it active");

    check(w.runPaletteCommand("Magic Wand"), "palette lists tools");
    check(canvas->tool() == ToolType::Wand, "choosing a tool in the palette activates it");

    const int layersBefore = doc->layers.size();
    check(w.runPaletteCommand("New Brightness Adjustment Layer"),
          "palette lists adjustment-layer commands");
    check(doc->layers.size() == layersBefore + 1,
          "palette adjustment-layer command added a layer");

    check(!w.runPaletteCommand("No Such Command Exists"),
          "palette reports unknown commands instead of running something else");

    // ---- free transform: tight bounding box (not the whole canvas) ----
    {
        w.newDocument(QSize(200, 150), Qt::white);
        doc = w.currentDoc();
        canvas = w.currentCanvas();
        canvas->fitToWindow();

        auto blob = Layer::makeRaster("Blob", doc->size());
        {
            QPainter p(&blob->image);
            p.fillRect(90, 65, 20, 20, Qt::black);
        }
        doc->undo.push(new AddLayerCommand(doc, blob, doc->layers.size(), "Blob"));
        doc->setActiveIndex(doc->layers.size() - 1);

        canvas->startTransform();
        check(canvas->inTransform(), "free transform started on the blob layer");

        // drag from the middle of the (now tight) bounding quad to translate it
        QPointF centerDoc(100, 75);
        QPointF centerW = canvas->docToWidget(centerDoc);
        QPointF toW = canvas->docToWidget(centerDoc + QPointF(15, 0));
        auto sendW = [&](QEvent::Type t, const QPointF& wp) {
            QMouseEvent ev(t, wp, canvas->mapToGlobal(wp.toPoint()),
                           Qt::LeftButton, t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                           Qt::NoModifier);
            QApplication::sendEvent(canvas, &ev);
        };
        sendW(QEvent::MouseButtonPress, centerW);
        sendW(QEvent::MouseMove, toW);
        canvas->commitTransform();

        check(!canvas->inTransform(), "commit ends the transform");
        check(blob->image.width() <= 30 && blob->image.height() <= 30,
              "free transform cropped to the blob's own pixels, not the full 200x150 canvas");
        comp = doc->composite();
        check(qRed(comp.pixel(112, 75)) < 60 && qRed(comp.pixel(88, 75)) > 200,
              "translated blob landed at its new position");
    }

    // ---- free transform on a selection: rest of the layer stays put mid-drag ----
    {
        w.newDocument(QSize(200, 150), Qt::white);
        doc = w.currentDoc();
        canvas = w.currentCanvas();
        canvas->fitToWindow();

        auto full = Layer::makeRaster("Full", doc->size(), QColor(255, 0, 0));
        doc->undo.push(new AddLayerCommand(doc, full, doc->layers.size(), "Full"));
        doc->setActiveIndex(doc->layers.size() - 1);

        QPainterPath sel;
        sel.addRect(20, 20, 30, 30);
        doc->setSelection(sel, 0);

        canvas->startTransform();
        comp = doc->composite();
        check(qAlpha(comp.pixel(35, 35)) < 30 || qRed(comp.pixel(35, 35)) > 200,
              "starting a selection transform punches a hole where the lifted piece was");
        check(qRed(comp.pixel(150, 100)) > 180,
              "unselected pixels stay visible while a selection is being transformed");

        canvas->cancelTransform();
        comp = doc->composite();
        check(qRed(comp.pixel(35, 35)) > 180, "cancel restores the original pixels under the selection");
        doc->setSelection(QPainterPath(), 0);
    }

    // ---- distort / perspective / skew / warp: mode switch + commit smoke test ----
    {
        w.newDocument(QSize(120, 120), Qt::white);
        doc = w.currentDoc();
        canvas = w.currentCanvas();
        canvas->fitToWindow();
        auto sq = Layer::makeRaster("Square", doc->size());
        { QPainter p(&sq->image); p.fillRect(40, 40, 40, 40, Qt::blue); }
        doc->undo.push(new AddLayerCommand(doc, sq, doc->layers.size(), "Square"));
        doc->setActiveIndex(doc->layers.size() - 1);

        for (auto mode : {Canvas::XformMode::Distort, Canvas::XformMode::Perspective,
                          Canvas::XformMode::Skew, Canvas::XformMode::Warp}) {
            canvas->startTransform();
            canvas->setTransformMode(mode);
            check(canvas->transformMode() == mode, "transform mode switch took effect");
            canvas->commitTransform();
            check(!canvas->inTransform() && !sq->image.isNull(),
                  "distort/perspective/skew/warp commit without a flat identity produces a valid layer image");
        }
    }

    fprintf(stderr, failures ? "MODELTEST: %d FAILURES\n" : "MODELTEST: ALL PASS\n", failures);
    return failures;
}

static void applyDarkTheme(QApplication& app)
{
    // Fusion, wrapped so tooltips appear instantly instead of after ~700ms.
    app.setStyle(new InstantTooltipStyle(QStyleFactory::create("Fusion")));
    QPalette p;
    QColor window(32, 32, 34), base(26, 26, 28), text(222, 222, 226),
           button(42, 42, 45), highlight(79, 124, 255), disabled(120, 120, 128);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, highlight);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    app.setPalette(p);

    // Borderless, flat, modern-Photoshop chrome. No bevels or glossy frames —
    // panels read as one continuous dark surface separated only by subtle tone.
    app.setStyleSheet(R"(
        QMainWindow, QMainWindow > QWidget { background: #202022; }
        QMainWindow::separator { background: #202022; width: 4px; height: 4px; }
        QMainWindow::separator:hover { background: #33333a; }

        /* menu bar / file-edit strip — flat, no underline frame */
        QMenuBar { background: #202022; border: none; padding: 2px 4px; }
        QMenuBar::item { background: transparent; padding: 4px 9px; margin: 0; border-radius: 5px; color: #cfcfd4; }
        QMenuBar::item:selected { background: #303036; color: #ffffff; }
        QMenuBar::item:pressed { background: #3a3a42; }
        QMenu { background: #26262a; border: 1px solid #34343a; padding: 4px; }
        QMenu::item { padding: 5px 22px 5px 20px; border-radius: 5px; }
        QMenu::item:selected { background: #4f7cff; color: #ffffff; }
        QMenu::separator { height: 1px; background: #34343a; margin: 4px 8px; }

        /* toolbars — no handles, no borders */
        QToolBar { background: #202022; border: none; padding: 2px; spacing: 2px; }
        QToolBar::separator { background: #313138; width: 1px; height: 1px; margin: 4px; }
        QToolButton { background: transparent; border: none; border-radius: 6px; padding: 4px; color: #cfcfd4; }
        QToolButton:hover { background: #303036; }
        QToolButton:pressed { background: #3a3a42; }
        QToolButton:checked { background: #34435f; }

        /* status bar */
        QStatusBar { background: #1c1c1e; border-top: 1px solid #2a2a2e; color: #9a9aa2; }
        QStatusBar::item { border: none; }

        /* dock panels — no frame. No ::title rule: every dock uses a custom
           DockTitleBar strip, and ::title padding would squeeze it into
           eliding its own tab labels. */
        QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none;
                      color: #b6b6be; font-size: 11px; }
        QDockWidget > QWidget { background: #242427; }

        /* the grab strip between stacked panels — the horizontal divider you
           drag to resize. Wide enough to hit, tinted on hover so it reads. */
        QMainWindow::separator { background: #2a2a2e; height: 4px; width: 4px; }
        QMainWindow::separator:hover { background: #4f7cff; }

        /* DockTitleBar paints its own tab strip — it needs no stylesheet rules.
           Qt's native dock tab bar is suppressed in code (QTabBar child of
           QMainWindow, height 0) so tabbed panels show one header, not two. */

        /* tab bars inside panels — NOT the dock tab bar, which is hidden */
        QTabBar::tab { background: transparent; color: #8a8a92; padding: 6px 12px;
                       border: none; margin-right: 2px; }
        QTabBar::tab:selected { color: #f0f0f4; }
        QTabBar::tab:hover { color: #d4d4da; }
        QTabWidget::pane { border: none; }

        /* document tabs across the top of the canvas */
        #docTabs::pane { border: none; background: #17171a; }
        #docTabs QTabBar::tab { background: #202022; color: #9a9aa2; padding: 6px 16px;
                                border: none; border-right: 1px solid #191919; }
        #docTabs QTabBar::tab:selected { background: #17171a; color: #f0f0f4; }
        #docTabs QTabBar::tab:hover:!selected { background: #262629; }

        /* inputs */
        QLineEdit, QSpinBox, QComboBox, QFontComboBox {
            background: #1a1a1c; border: 1px solid #313138; border-radius: 5px;
            padding: 3px 6px; color: #e4e4e8; selection-background-color: #4f7cff;
        }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QFontComboBox:focus { border-color: #4f7cff; }
        QComboBox::drop-down, QFontComboBox::drop-down { border: none; width: 16px; }
        QComboBox QAbstractItemView { background: #26262a; border: 1px solid #34343a;
                                      selection-background-color: #4f7cff; outline: none; }

        QListWidget, QUndoView { background: #1c1c1e; border: 1px solid #2a2a2e;
                                 border-radius: 6px; outline: none; }
        QListWidget::item { border-radius: 4px; padding: 1px; }
        QListWidget::item:selected { background: #34435f; }
        QListWidget::item:hover:!selected { background: #26262b; }

        QPushButton { background: #2c2c31; border: 1px solid #38383f; border-radius: 6px;
                      padding: 5px 12px; color: #e0e0e4; }
        QPushButton:hover { background: #34343a; border-color: #45454e; }
        QPushButton:pressed { background: #2a2a30; }

        QCheckBox { color: #c8c8d0; spacing: 6px; }
        QLabel { color: #c8c8d0; }

        /* sliders — thin flat track, round handle */
        QSlider::groove:horizontal { height: 4px; background: #34343a; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: #4f7cff; border-radius: 2px; }
        QSlider::handle:horizontal { background: #e8e8ee; width: 13px; height: 13px;
                                     margin: -5px 0; border-radius: 7px; }
        QSlider::handle:horizontal:hover { background: #ffffff; }

        /* scrollbars — slim, no arrows */
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: #3a3a42; border-radius: 5px; min-height: 32px; }
        QScrollBar::handle:vertical:hover { background: #4a4a54; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
        QScrollBar::handle:horizontal { background: #3a3a42; border-radius: 5px; min-width: 32px; }
        QScrollBar::handle:horizontal:hover { background: #4a4a54; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

        /* tooltips — dark card, readable at a glance */
        QToolTip { background: #2a2a30; color: #ececf2; border: 1px solid #3a3a44;
                   border-radius: 6px; padding: 6px 9px; font-size: 11px; }
    )");
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("PhotoGod");
    app.setOrganizationName("photogod");
    applyDarkTheme(app);

    MainWindow w;
    w.resize(1500, 950);
    w.show();

    QStringList args = app.arguments().mid(1);
    bool modeltest = args.removeAll("--modeltest") > 0;
    bool selftest = args.removeAll("--selftest") > 0;
    bool homeshot = args.removeAll("--homeshot") > 0;
    bool paletteshot = args.removeAll("--paletteshot") > 0;
    bool tabshot = args.removeAll("--tabshot") > 0;
    bool historyshot = args.removeAll("--historyshot") > 0;
    QString diffShotPath;
    for (int i = args.size() - 1; i >= 0; --i) {
        if (args[i].startsWith("--diffshot=")) {
            diffShotPath = args[i].mid(11);
            args.removeAt(i);
        }
    }
    QString paletteQuery;
    for (int i = args.size() - 1; i >= 0; --i) {
        if (args[i].startsWith("--query=")) {
            paletteQuery = args[i].mid(8);
            args.removeAt(i);
        }
    }
    QString shotPath;
    for (int i = args.size() - 1; i >= 0; --i) {
        if (args[i].startsWith("--shot=")) {
            shotPath = args[i].mid(7);
            args.removeAt(i);
        }
    }
    if (modeltest) {
        int rc = 1;
        QTimer::singleShot(300, &app, [&] {
            rc = runModelTest(w);
            QApplication::exit(rc);
        });
        return app.exec();
    }
    for (const QString& a : args)
        if (!a.startsWith("-")) w.openPath(a);

    if (homeshot) {
        w.showHome();
        QTimer::singleShot(600, &app, [&] {
            if (!shotPath.isEmpty()) w.grab().save(shotPath);
            QApplication::quit();
        });
        return app.exec();
    }

    if (tabshot) {
        // Merge two panels into one group to prove a tabbed group shows exactly
        // one header — our strip with both tabs, and no native Qt tab bar.
        if (!w.currentDoc())
            w.newDocument(QSize(1000, 700), Qt::white);
        QTimer::singleShot(500, &app, [&] {
            w.tabifyPanelsForTest("Layers", "History");
            QTimer::singleShot(500, &app, [&] {
                if (!shotPath.isEmpty()) w.grab().save(shotPath);
                QApplication::quit();
            });
        });
        return app.exec();
    }

    if (paletteshot) {
        if (!w.currentDoc())
            w.newDocument(QSize(1000, 700), Qt::white);
        QTimer::singleShot(600, &app, [&] {
            w.showCommandPalette(paletteQuery);
            QTimer::singleShot(400, &app, [&] {
                if (!shotPath.isEmpty()) {
                    // Composite the popup over the window: the palette is a
                    // separate top-level, so w.grab() alone would miss it.
                    QImage shot = w.grab().toImage();
                    if (QWidget* pal = QApplication::activePopupWidget()) {
                        QPainter p(&shot);
                        p.drawPixmap(pal->mapToGlobal(QPoint(0, 0)) - w.mapToGlobal(QPoint(0, 0)),
                                     pal->grab());
                    }
                    shot.save(shotPath);
                }
                QApplication::quit();
            });
        });
        return app.exec();
    }

    if (historyshot) {
        // Build a document with a few named versions so the history panel has
        // something to show, then expand the middle entry's snapshot drawer.
        w.newDocument(QSize(900, 620), Qt::white);
        Document* d = w.currentDoc();
        QTimer::singleShot(500, &app, [&, d] {
            auto tint = [&](const QColor& c, const QString& name) {
                auto l = Layer::makeRaster(name, d->size(), c);
                l->opacity = 0.55;
                d->undo.push(new AddLayerCommand(d, l, d->layers.size(), name));
            };
            tint(QColor(70, 120, 200), "Blue Wash");
            tint(QColor(210, 120, 60), "Warm Pass");
            auto adjL = Layer::makeAdjustment(FilterType::Brightness);
            adjL->params.p1 = 30;
            d->undo.push(new AddLayerCommand(d, adjL, d->layers.size(), "Brightness"));

            QTimer::singleShot(300, &app, [&] {
                // Click the "Warm Pass" row to open its snapshot drawer.
                for (HistoryRow* r : w.findChildren<HistoryRow*>()) {
                    if (r->index() == 2) {
                        QMouseEvent ev(QEvent::MouseButtonPress, QPointF(60, 12),
                                       r->mapToGlobal(QPoint(60, 12)),
                                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                        QApplication::sendEvent(r, &ev);
                        break;
                    }
                }
                QTimer::singleShot(300, &app, [&] {
                    // Grab the panel alone: in the stock layout the History dock
                    // is short enough to clip the expanded snapshot drawer.
                    if (!shotPath.isEmpty()) {
                        if (auto* hp = w.findChild<HistoryPanel*>()) {
                            hp->resize(300, 620);
                            hp->grab().save(shotPath);
                        } else {
                            w.grab().save(shotPath);
                        }
                    }
                    // Open the diff view too, so the comparison window can be
                    // eyeballed in the same run.
                    if (!diffShotPath.isEmpty()) {
                        // Click the expanded row's "See version" button.
                        for (HistoryRow* r : w.findChildren<HistoryRow*>()) {
                            if (r->index() != 2) continue;
                            if (auto* b = r->findChild<QPushButton*>("histSee")) b->click();
                            break;
                        }
                        QTimer::singleShot(400, &app, [&] {
                            for (QWidget* d : QApplication::topLevelWidgets())
                                if (qobject_cast<QDialog*>(d) && d->isVisible())
                                    { d->grab().save(diffShotPath); break; }
                            QApplication::quit();
                        });
                        return;
                    }
                    QApplication::quit();
                });
            });
        });
        return app.exec();
    }

    if (selftest) {
        if (!w.currentDoc())
            w.newDocument(QSize(1000, 700), Qt::white);
        QTimer::singleShot(900, &app, [&] {
            if (!shotPath.isEmpty()) w.grab().save(shotPath);
            QApplication::quit();
        });
    }
    return app.exec();
}
