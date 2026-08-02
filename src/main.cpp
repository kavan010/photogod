#include "MainWindow.h"
#include "Dock.h"
#include "Canvas.h"
#include "Commands.h"
#include "HistoryPanel.h"
#include <QPushButton>
#include <QDialog>
#include "CommandPalette.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include "Theme.h"
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

// Qt paints tooltips onto an opaque window, so a border-radius in the
// stylesheet leaves four square corners showing the desktop behind them. The
// tip label is created lazily and privately, so the only way in is to catch it
// being polished and make its window translucent.
class RoundTooltips : public QObject
{
protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (e->type() == QEvent::Polish && o->inherits("QTipLabel"))
            static_cast<QWidget*>(o)->setAttribute(Qt::WA_TranslucentBackground, true);
        return QObject::eventFilter(o, e);
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

// Headless test of the workspace itself: every way a panel can be moved, and
// the guarantee that the canvas survives all of them.
static int runDockTest(MainWindow& w)
{
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        fprintf(stderr, "[%s] %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) ++failures;
    };

    // Hit-testing reads real on-screen geometry, so the workspace has to be the
    // visible view: with no document open the app sits on the home screen.
    w.newDocument(QSize(400, 300), Qt::white);
    QApplication::processEvents();

    DockManager* dm = w.dockManager();
    auto* canvas  = dm->panel("Canvas");
    auto* layers  = dm->panel("Layers");
    auto* brushes = dm->panel("Brushes");
    auto* color   = dm->panel("Color");
    check(dm && canvas && layers && brushes && color, "every stock panel exists");
    if (!dm || !canvas || !layers || !brushes || !color) return 1;

    // Counts the groups a panel's own area currently holds.
    auto groupCount = [](DockPanel* p) {
        DockGroup* g = p->group();
        return (g && g->area()) ? g->area()->groups().size() : 0;
    };

    // ---- tab a panel into another group ----
    DockGroup* layersGroup = layers->group();
    const int before = layersGroup->count();
    dm->dockInto(brushes, layersGroup, DockZone::Center);
    check(brushes->group() == layersGroup && layersGroup->count() == before + 1,
          "dropping a panel on a group's middle adds it as a tab");
    check(layersGroup->currentPanel() == brushes, "the panel it just gained comes forward");

    // ---- split a group ----
    const int groupsBefore = groupCount(layers);
    dm->dockInto(brushes, layersGroup, DockZone::Bottom);
    check(brushes->group() != layersGroup, "splitting moves the panel to a group of its own");
    check(groupCount(layers) == groupsBefore + 1, "the split produced exactly one new group");

    // ---- tear off into a floating workspace, then dock it back ----
    dm->floatPanel(brushes, QPoint(300, 300), QSize(320, 400));
    check(brushes->group() && brushes->group()->area()
              && brushes->group()->area()->floatingWindow(),
          "a panel dropped outside becomes a floating workspace");
    check(dm->isOpen(brushes), "a floating panel still counts as open");

    // Group a second panel inside the floating workspace, proving a float is a
    // full dock area and not a one-panel special case.
    DockGroup* floatGroup = brushes->group();
    dm->dockInto(color, floatGroup, DockZone::Right);
    check(color->group() != floatGroup
              && color->group()->area() == floatGroup->area(),
          "a floating workspace can be split like any other");

    dm->dockToAreaEdge(brushes, dm->mainArea(), DockZone::Left);
    check(brushes->group() && brushes->group()->area() == dm->mainArea(),
          "a floating panel can be docked back to a main-window edge");
    dm->dockToAreaEdge(color, dm->mainArea(), DockZone::Right);

    // ---- the canvas is immovable ----
    dm->floatPanel(canvas, QPoint(10, 10));
    check(canvas->group() && canvas->group()->area() == dm->mainArea(),
          "the canvas refuses to be torn off");
    dm->closePanel(canvas);
    check(dm->isOpen(canvas), "the canvas refuses to be closed");

    // ---- save / restore round trip ----
    const QJsonObject saved = dm->saveLayout();
    dm->dockInto(layers, brushes->group(), DockZone::Center);
    dm->closePanel(color);
    check(!dm->isOpen(color), "a closed panel leaves the tree");
    check(dm->restoreLayout(saved), "the saved workspace restores");
    check(dm->isOpen(color), "restoring brings a closed panel back where it was");
    {
        // Structure has to survive exactly. Pixel sizes are the splitters' own
        // business — they are re-fitted to whatever room the window has.
        std::function<QJsonObject(QJsonObject)> shape = [&](QJsonObject o) {
            o.remove("sizes");
            if (o.contains("children")) {
                QJsonArray kids;
                for (const QJsonValue& v : o["children"].toArray())
                    kids.append(shape(v.toObject()));
                o["children"] = kids;
            }
            return o;
        };
        const QByteArray a = QJsonDocument(shape(saved["main"].toObject())).toJson(QJsonDocument::Compact);
        const QByteArray b = QJsonDocument(shape(dm->saveLayout()["main"].toObject())).toJson(QJsonDocument::Compact);
        if (a != b) fprintf(stderr, "  saved: %s\n  again: %s\n", a.constData(), b.constData());
        check(a == b, "a restored workspace rebuilds exactly the tree that was saved");
    }

    // ---- closing every panel leaves a usable window ----
    for (DockPanel* p : dm->panels())
        dm->closePanel(p);
    check(dm->isOpen(canvas), "the canvas is still there once every panel is closed");
    check(dm->mainArea()->groups().size() == 1,
          "closing the last panel of a group collapses it out of the tree");
    check(canvas->group() && canvas->group()->isBareCanvas(),
          "the lone canvas group drops its tab strip");

    dm->resetLayout();
    check(dm->isOpen(layers) && dm->isOpen(brushes) && dm->isOpen(color),
          "resetting the workspace brings every panel back");

    // ---- the real thing: a mouse drag, through hit-testing and all ----
    {
        // The workspace was just rebuilt; hit-testing reads real geometry, so
        // let the pending layout actually happen first.
        QApplication::processEvents();

        DockGroup* src = brushes->group();
        DockGroup* dst = layers->group();
        DockTabBar* bar = src->tabBar();
        const QPoint from = bar->tabRect(src->indexOf(brushes)).center();
        const QPoint toGlobal = QRect(dst->mapToGlobal(QPoint(0, 0)), dst->size()).center();

        auto send = [&](QEvent::Type t, const QPoint& local) {
            QMouseEvent ev(t, QPointF(local), QPointF(bar->mapToGlobal(local)),
                           Qt::LeftButton,
                           t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                           Qt::NoModifier);
            QApplication::sendEvent(bar, &ev);
        };

        send(QEvent::MouseButtonPress, from);
        send(QEvent::MouseMove, from + QPoint(20, 0));       // clears the drag slop
        check(dm->dragging(), "pressing a tab and pulling starts a drag");
        send(QEvent::MouseMove, bar->mapFromGlobal(toGlobal));
        send(QEvent::MouseButtonRelease, bar->mapFromGlobal(toGlobal));
        check(!dm->dragging(), "letting go ends the drag");
        check(brushes->group() == dst,
              "dropping on the middle of a group tabs the panel into it");

        // Same gesture, but let go over the group's left edge instead.
        DockTabBar* bar2 = dst->tabBar();
        const QPoint grab = bar2->tabRect(dst->indexOf(brushes)).center();
        const QRect dstRect(dst->mapToGlobal(QPoint(0, 0)), dst->size());
        const QPoint leftEdge(dstRect.left() + 6, dstRect.center().y());
        auto send2 = [&](QEvent::Type t, const QPoint& local) {
            QMouseEvent ev(t, QPointF(local), QPointF(bar2->mapToGlobal(local)),
                           Qt::LeftButton,
                           t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                           Qt::NoModifier);
            QApplication::sendEvent(bar2, &ev);
        };
        send2(QEvent::MouseButtonPress, grab);
        send2(QEvent::MouseMove, grab + QPoint(20, 0));
        send2(QEvent::MouseMove, bar2->mapFromGlobal(leftEdge));
        send2(QEvent::MouseButtonRelease, bar2->mapFromGlobal(leftEdge));
        check(brushes->group() && brushes->group() != dst,
              "dropping near a group's edge splits it off into its own group");
        check(brushes->group()->area() == dm->mainArea(),
              "an edge drop stays docked rather than floating away");
    }

    // ---- dropping outside every dock area floats the panel ----
    {
        QApplication::processEvents();
        DockGroup* src = layers->group();
        DockTabBar* bar = src->tabBar();
        const QPoint grab = bar->tabRect(src->indexOf(layers)).center();
        const QRect win(w.mapToGlobal(QPoint(0, 0)), w.size());
        const QPoint outside(win.right() + 220, win.center().y());
        auto send = [&](QEvent::Type t, const QPoint& local) {
            QMouseEvent ev(t, QPointF(local), QPointF(bar->mapToGlobal(local)),
                           Qt::LeftButton,
                           t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                           Qt::NoModifier);
            QApplication::sendEvent(bar, &ev);
        };
        send(QEvent::MouseButtonPress, grab);
        send(QEvent::MouseMove, grab + QPoint(20, 0));
        send(QEvent::MouseMove, bar->mapFromGlobal(outside));
        send(QEvent::MouseButtonRelease, bar->mapFromGlobal(outside));
        check(layers->group() && layers->group()->area()
                  && layers->group()->area()->floatingWindow(),
              "dropping past the window edge tears the panel into a floating workspace");
    }

    fprintf(stderr, failures ? "DOCKTEST: %d FAILURES\n" : "DOCKTEST: ALL PASS\n", failures);
    return failures;
}

static void applyDarkTheme(QApplication& app)
{
    // Fusion, wrapped so tooltips appear instantly instead of after ~700ms.
    app.setStyle(new InstantTooltipStyle(QStyleFactory::create("Fusion")));

    // One normal-weight grotesque, one size up from Qt's cramped default —
    // hierarchy comes from size and tone, never from thickness.
    app.setFont(Theme::uiFont(13));

    QPalette p;
    QColor window(Theme::Shell), base(Theme::Panel), text(Theme::Text),
           button(Theme::Raised), highlight(Theme::AccentDim), disabled(Theme::Text4);
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

    app.setStyleSheet(Theme::styleSheet());
    app.installEventFilter(new RoundTooltips);
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
    bool docktest = args.removeAll("--docktest") > 0;
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
    QString toolName;
    for (int i = args.size() - 1; i >= 0; --i) {
        if (args[i].startsWith("--tool=")) {
            toolName = args[i].mid(7);
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

    if (docktest) {
        // 300ms in: the workspace has settled and any saved layout is applied.
        QTimer::singleShot(300, &app, [&] { QApplication::exit(runDockTest(w)); });
        return app.exec();
    }
    // Startup never interrupts with a dialog: a path that no longer opens just
    // drops you on the home screen.
    for (const QString& a : args)
        if (!a.startsWith("-")) w.openPath(a, /*quiet=*/true);

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
        // --tool=<name> picks a tool first, so a shot can show the options strip
        // a given tool brings with it (or prove it brings none).
        if (!toolName.isEmpty()) w.runPaletteCommand(toolName);
        QTimer::singleShot(900, &app, [&] {
            if (!shotPath.isEmpty()) w.grab().save(shotPath);
            QApplication::quit();
        });
    }
    return app.exec();
}
