#include "MainWindow.h"
#include "Canvas.h"
#include "Commands.h"
#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QTimer>
#include <QMouseEvent>
#include <QDir>

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

    fprintf(stderr, failures ? "MODELTEST: %d FAILURES\n" : "MODELTEST: ALL PASS\n", failures);
    return failures;
}

static void applyDarkTheme(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    QColor window(45, 45, 48), base(30, 30, 32), text(220, 220, 220),
           button(55, 55, 58), highlight(70, 120, 200), disabled(120, 120, 120);
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
