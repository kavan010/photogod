#include "MainWindow.h"
#include "Canvas.h"
#include "Panels.h"
#include "Dialogs.h"
#include "Commands.h"
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QStackedWidget>
#include <QMenuBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QUndoView>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QClipboard>
#include <QGuiApplication>
#include <QImageReader>
#include <QImageWriter>
#include <QActionGroup>
#include <QPainter>
#include <QFileInfo>

MainWindow::MainWindow()
{
    setWindowTitle("PhotoGod");
    setAcceptDrops(true);

    m_tabs = new QTabWidget;
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int i) { closeTab(i); });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        Document* doc = currentDoc();
        m_layers->setDocument(doc);
        m_props->setDocument(doc);
        if (doc) {
            m_undoGroup.setActiveStack(&doc->undo);
            m_statusSize->setText(QString("%1 x %2").arg(doc->width()).arg(doc->height()));
        } else {
            m_undoGroup.setActiveStack(nullptr);
            m_statusSize->setText("-");
        }
        if (Canvas* c = currentCanvas()) {
            c->setTool(m_tool);
            c->setFocus();
        }
    });

    buildToolbar();
    buildOptionsBar();
    buildDocks();
    buildMenus();

    m_statusPos = new QLabel("-");
    m_statusZoom = new QLabel("100%");
    m_statusSize = new QLabel("-");
    statusBar()->addPermanentWidget(m_statusSize);
    statusBar()->addPermanentWidget(m_statusZoom);
    statusBar()->addWidget(m_statusPos);
    statusBar()->showMessage("Ctrl+N new document · Ctrl+O open image · drag & drop images onto the window", 8000);
}

Canvas* MainWindow::currentCanvas() const
{
    return qobject_cast<Canvas*>(m_tabs->currentWidget());
}

Document* MainWindow::currentDoc() const
{
    Canvas* c = currentCanvas();
    return c ? c->document() : nullptr;
}

// ============================ documents / tabs ============================

void MainWindow::addDocument(Document* doc)
{
    auto* canvas = new Canvas(doc, &m_ts);
    doc->setParent(canvas);
    m_undoGroup.addStack(&doc->undo);

    connect(canvas, &Canvas::colorPicked, this, [this](const QColor& c) {
        m_color->setFg(c);
        statusBar()->showMessage("Picked " + c.name(), 2000);
    });
    connect(canvas, &Canvas::zoomChanged, this, [this](double z) {
        m_statusZoom->setText(QString::number(int(std::lround(z * 100))) + "%");
    });
    connect(canvas, &Canvas::cursorMoved, this, [this](const QPointF& p) {
        m_statusPos->setText(QString("%1, %2").arg(int(p.x())).arg(int(p.y())));
    });
    connect(canvas, &Canvas::statusMessage, this, [this](const QString& m) {
        statusBar()->showMessage(m, 4000);
    });
    connect(&doc->undo, &QUndoStack::cleanChanged, this, [this, doc](bool) { updateTabTitle(doc); });
    connect(doc, &Document::structureChanged, this, [this, doc] {
        if (doc == currentDoc())
            m_statusSize->setText(QString("%1 x %2").arg(doc->width()).arg(doc->height()));
    });

    int idx = m_tabs->addTab(canvas, doc->name);
    m_tabs->setCurrentIndex(idx);
    canvas->setTool(m_tool);
    canvas->setFocus();
}

void MainWindow::updateTabTitle(Document* doc)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* c = qobject_cast<Canvas*>(m_tabs->widget(i));
        if (c && c->document() == doc) {
            m_tabs->setTabText(i, doc->name + (doc->undo.isClean() ? "" : " *"));
            return;
        }
    }
}

void MainWindow::newDocument(const QSize& size, const QColor& bg)
{
    if (size.isValid() && !size.isEmpty()) {
        auto* doc = new Document(size, bg);
        addDocument(doc);
        return;
    }
    NewDocDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        auto* doc = new Document(dlg.docSize(), dlg.background());
        addDocument(doc);
    }
}

void MainWindow::openPath(const QString& path)
{
    if (path.endsWith(".pgd", Qt::CaseInsensitive)) {
        Document* doc = Document::loadProject(path);
        if (!doc) {
            QMessageBox::warning(this, "Open", "Could not open project:\n" + path);
            return;
        }
        addDocument(doc);
        return;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        QMessageBox::warning(this, "Open", "Could not read image:\n" + path + "\n" + reader.errorString());
        return;
    }
    Document* doc = Document::fromImage(img, QFileInfo(path).completeBaseName());
    addDocument(doc);
}

bool MainWindow::closeTab(int index)
{
    auto* canvas = qobject_cast<Canvas*>(m_tabs->widget(index));
    if (!canvas) return true;
    Document* doc = canvas->document();
    if (!doc->undo.isClean()) {
        auto ret = QMessageBox::question(this, "Unsaved Changes",
                                         QString("Save changes to \"%1\"?").arg(doc->name),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (ret == QMessageBox::Cancel) return false;
        if (ret == QMessageBox::Save && !saveDoc(doc, false)) return false;
    }
    m_undoGroup.removeStack(&doc->undo);
    m_tabs->removeTab(index);
    canvas->deleteLater();
    return true;
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    while (m_tabs->count() > 0) {
        if (!closeTab(0)) {
            e->ignore();
            return;
        }
    }
    e->accept();
}

// ============================ save / export ============================

bool MainWindow::saveDoc(Document* doc, bool saveAs)
{
    QString path = doc->filePath;
    if (path.isEmpty() || saveAs) {
        path = QFileDialog::getSaveFileName(this, "Save",
                                            doc->name + ".pgd",
                                            "PhotoGod Project (*.pgd);;PNG Image (*.png);;JPEG Image (*.jpg);;WebP Image (*.webp)");
        if (path.isEmpty()) return false;
    }
    if (path.endsWith(".pgd", Qt::CaseInsensitive)) {
        if (!doc->saveProject(path)) {
            QMessageBox::warning(this, "Save", "Could not write file:\n" + path);
            return false;
        }
        doc->name = QFileInfo(path).completeBaseName();
        updateTabTitle(doc);
        statusBar()->showMessage("Saved " + path, 4000);
        return true;
    }
    // saving directly to an image format = flatten & export
    QImage img = doc->composite();
    bool noAlpha = path.endsWith(".jpg", Qt::CaseInsensitive) || path.endsWith(".jpeg", Qt::CaseInsensitive)
                || path.endsWith(".bmp", Qt::CaseInsensitive);
    if (noAlpha) {
        QImage bg(img.size(), QImage::Format_RGB32);
        bg.fill(Qt::white);
        QPainter p(&bg);
        p.drawImage(0, 0, img);
        p.end();
        img = bg;
    }
    if (!img.save(path, nullptr, 92)) {
        QMessageBox::warning(this, "Save", "Could not write image:\n" + path);
        return false;
    }
    statusBar()->showMessage("Exported " + path + " (project itself not saved as .pgd)", 5000);
    return true;
}

void MainWindow::exportDoc(Document* doc)
{
    if (!doc) return;
    QStringList filters = {"PNG Image (*.png)", "JPEG Image (*.jpg)"};
    if (QImageWriter::supportedImageFormats().contains("webp"))
        filters << "WebP Image (*.webp)";
    filters << "BMP Image (*.bmp)" << "TIFF Image (*.tif)";
    QString path = QFileDialog::getSaveFileName(this, "Export As", doc->name + ".png",
                                                filters.join(";;"));
    if (path.isEmpty()) return;

    int quality = -1;
    bool lossy = path.endsWith(".jpg", Qt::CaseInsensitive) || path.endsWith(".jpeg", Qt::CaseInsensitive)
              || path.endsWith(".webp", Qt::CaseInsensitive);
    if (lossy) {
        bool ok = false;
        quality = QInputDialog::getInt(this, "Export Quality", "Quality (1-100):", 92, 1, 100, 1, &ok);
        if (!ok) return;
    }
    QImage img = doc->composite();
    bool noAlpha = path.endsWith(".jpg", Qt::CaseInsensitive) || path.endsWith(".jpeg", Qt::CaseInsensitive)
                || path.endsWith(".bmp", Qt::CaseInsensitive);
    if (noAlpha) {
        QImage bg(img.size(), QImage::Format_RGB32);
        bg.fill(Qt::white);
        QPainter p(&bg);
        p.drawImage(0, 0, img);
        p.end();
        img = bg;
    }
    if (!img.save(path, nullptr, quality))
        QMessageBox::warning(this, "Export", "Could not write image:\n" + path);
    else
        statusBar()->showMessage("Exported " + path, 5000);
}

// ============================ clipboard / dnd ============================

void MainWindow::doCopy()
{
    Document* doc = currentDoc();
    if (!doc) return;
    QImage img = doc->composite();
    if (doc->hasSelection()) {
        QRect r = doc->selection.boundingRect().toAlignedRect().intersected(doc->rect());
        if (r.isEmpty()) return;
        QImage cut = img.copy(r);
        QPainter p(&cut);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawImage(-r.topLeft(), doc->selectionMaskAlpha());
        p.end();
        img = cut;
    }
    QGuiApplication::clipboard()->setImage(img.convertToFormat(QImage::Format_ARGB32));
    statusBar()->showMessage("Copied", 2000);
}

void MainWindow::doPaste()
{
    QImage img = QGuiApplication::clipboard()->image();
    if (img.isNull()) {
        statusBar()->showMessage("Clipboard has no image", 3000);
        return;
    }
    if (!currentDoc()) {
        addDocument(Document::fromImage(img, "Pasted"));
        return;
    }
    placeImageAsLayer(img, "Pasted");
}

void MainWindow::placeImageAsLayer(const QImage& img, const QString& name)
{
    Document* doc = currentDoc();
    if (!doc) return;
    auto l = std::make_shared<Layer>();
    l->type = Layer::Raster;
    l->name = name;
    l->image = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    l->offset = QPoint((doc->width() - img.width()) / 2, (doc->height() - img.height()) / 2);
    doc->undo.push(new AddLayerCommand(doc, l, doc->activeIndex + 1, "Place Image"));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls() || e->mimeData()->hasImage())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e)
{
    if (e->mimeData()->hasUrls()) {
        for (const QUrl& url : e->mimeData()->urls()) {
            QString path = url.toLocalFile();
            if (path.isEmpty()) continue;
            if (!currentDoc() || path.endsWith(".pgd", Qt::CaseInsensitive)) {
                openPath(path);
            } else {
                QImageReader reader(path);
                reader.setAutoTransform(true);
                QImage img = reader.read();
                if (!img.isNull())
                    placeImageAsLayer(img, QFileInfo(path).completeBaseName());
                else
                    openPath(path);   // will show the error
            }
        }
        e->acceptProposedAction();
    } else if (e->mimeData()->hasImage()) {
        QImage img = qvariant_cast<QImage>(e->mimeData()->imageData());
        if (!img.isNull()) {
            if (currentDoc()) placeImageAsLayer(img, "Dropped");
            else addDocument(Document::fromImage(img, "Dropped"));
        }
        e->acceptProposedAction();
    }
}

// ============================ filters ============================

static QImage mergeFilteredWithSelection(Document* doc, const std::shared_ptr<Layer>& l,
                                         const QImage& original, QImage filtered)
{
    if (!doc->hasSelection()) return filtered;
    QImage sel = doc->selectionMaskAlpha();
    QPainter cp(&filtered);
    cp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    cp.drawImage(-l->offset, sel);
    cp.end();
    QImage out = original.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    p.drawImage(-l->offset, sel);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.drawImage(0, 0, filtered);
    p.end();
    return out;
}

void MainWindow::applyDirectFilter(FilterType t)
{
    Document* doc = currentDoc();
    if (!doc) return;
    auto l = doc->activeLayer();
    if (!l || !l->isPaintable() || l->image.isNull()) {
        statusBar()->showMessage("Select a raster or text layer first", 3000);
        return;
    }
    if (l->locked) { statusBar()->showMessage("Layer is locked", 3000); return; }
    LayerState pre = LayerState::capture(*l);
    QImage img = l->image;
    img.detach();
    applyFilter(img, t, filterDefaults(t));
    l->image = mergeFilteredWithSelection(doc, l, pre.image, img);
    doc->undo.push(new LayerEditCommand(doc, l, pre, filterName(t)));
    doc->invalidate();
}

void MainWindow::openFilterDialog(FilterType t)
{
    Document* doc = currentDoc();
    if (!doc) return;
    auto l = doc->activeLayer();
    if (!l || !l->isPaintable() || l->image.isNull()) {
        statusBar()->showMessage("Select a raster or text layer first", 3000);
        return;
    }
    if (l->locked) { statusBar()->showMessage("Layer is locked", 3000); return; }
    AdjustmentDialog dlg(doc, t, this);
    dlg.exec();
}

// ============================ toolbar ============================

void MainWindow::buildToolbar()
{
    auto* tb = new QToolBar("Tools");
    tb->setObjectName("toolsBar");
    tb->setOrientation(Qt::Vertical);
    tb->setMovable(false);
    tb->setIconSize(QSize(20, 20));
    addToolBar(Qt::LeftToolBarArea, tb);

    auto* group = new QActionGroup(this);
    group->setExclusive(true);

    auto addTool = [&](ToolType t, const QString& label, const QString& name, const QString& shortcut) {
        auto* a = new QAction(label, this);
        a->setCheckable(true);
        a->setToolTip(name + (shortcut.isEmpty() ? "" : " (" + shortcut + ")"));
        if (!shortcut.isEmpty()) a->setShortcut(QKeySequence(shortcut));
        connect(a, &QAction::triggered, this, [this, t] { setTool(t); });
        group->addAction(a);
        tb->addAction(a);
        m_toolActions[int(t)] = a;
        return a;
    };

    addTool(ToolType::Move, "V", "Move", "V");
    addTool(ToolType::MarqueeRect, "▭", "Rectangular marquee — Shift adds, Alt subtracts, Ctrl-drag square", "");
    addTool(ToolType::MarqueeEllipse, "◯", "Elliptical marquee (press M to toggle)", "");
    addTool(ToolType::Lasso, "L", "Polygon lasso selection", "L");
    addTool(ToolType::Wand, "W", "Magic wand", "W");
    addTool(ToolType::Crop, "C", "Crop — drag then Enter", "C");
    addTool(ToolType::Eyedropper, "I", "Eyedropper", "I");
    tb->addSeparator();
    addTool(ToolType::Brush, "B", "Brush", "B");
    addTool(ToolType::Eraser, "E", "Eraser", "E");
    addTool(ToolType::Gradient, "G", "Gradient", "G");
    addTool(ToolType::Text, "T", "Text — click canvas", "T");
    tb->addSeparator();
    addTool(ToolType::ShapeRect, "▬", "Rectangle shape (press U to cycle shapes)", "");
    addTool(ToolType::ShapeEllipse, "●", "Ellipse shape", "");
    addTool(ToolType::ShapeLine, "╱", "Line shape", "");
    tb->addSeparator();
    addTool(ToolType::Zoom, "Z", "Zoom — click to zoom in, Alt-click out", "Z");
    addTool(ToolType::Hand, "H", "Hand (pan) — or hold Space", "H");

    m_toolActions[int(ToolType::Move)]->setChecked(true);

    // M toggles marquee shape, U cycles shape tools
    auto* marqueeShortcut = new QAction(this);
    marqueeShortcut->setShortcut(QKeySequence("M"));
    connect(marqueeShortcut, &QAction::triggered, this, [this] {
        setTool(m_tool == ToolType::MarqueeRect ? ToolType::MarqueeEllipse : ToolType::MarqueeRect);
    });
    addAction(marqueeShortcut);

    auto* shapeShortcut = new QAction(this);
    shapeShortcut->setShortcut(QKeySequence("U"));
    connect(shapeShortcut, &QAction::triggered, this, [this] {
        if (m_tool == ToolType::ShapeRect) setTool(ToolType::ShapeEllipse);
        else if (m_tool == ToolType::ShapeEllipse) setTool(ToolType::ShapeLine);
        else setTool(ToolType::ShapeRect);
    });
    addAction(shapeShortcut);

    auto* swapColorsAct = new QAction(this);
    swapColorsAct->setShortcut(QKeySequence("X"));
    connect(swapColorsAct, &QAction::triggered, this, [this] { m_color->swapColors(); });
    addAction(swapColorsAct);

    auto* brushSmaller = new QAction(this);
    brushSmaller->setShortcut(QKeySequence("["));
    connect(brushSmaller, &QAction::triggered, this, [this] {
        m_ts.brushSize = std::max(1, int(m_ts.brushSize / 1.2));
        if (m_brushSizeSlider) m_brushSizeSlider->setValue(m_ts.brushSize);
        if (Canvas* c = currentCanvas()) c->update();
    });
    addAction(brushSmaller);

    auto* brushBigger = new QAction(this);
    brushBigger->setShortcut(QKeySequence("]"));
    connect(brushBigger, &QAction::triggered, this, [this] {
        m_ts.brushSize = std::min(500, std::max(m_ts.brushSize + 1, int(m_ts.brushSize * 1.2)));
        if (m_brushSizeSlider) m_brushSizeSlider->setValue(m_ts.brushSize);
        if (Canvas* c = currentCanvas()) c->update();
    });
    addAction(brushBigger);
}

void MainWindow::setTool(ToolType t)
{
    m_tool = t;
    if (auto* a = m_toolActions.value(int(t)); a && !a->isChecked())
        a->setChecked(true);
    m_optStack->setCurrentIndex(pageForTool(t));
    if (Canvas* c = currentCanvas()) {
        c->setTool(t);
        c->setFocus();
    }
}

// ============================ options bar ============================

int MainWindow::pageForTool(ToolType t) const
{
    switch (t) {
    case ToolType::Brush:
    case ToolType::Eraser:       return 1;
    case ToolType::Wand:         return 2;
    case ToolType::ShapeRect:
    case ToolType::ShapeEllipse:
    case ToolType::ShapeLine:    return 3;
    case ToolType::Gradient:     return 4;
    case ToolType::Text:         return 5;
    default:                     return 0;
    }
}

void MainWindow::buildOptionsBar()
{
    auto* bar = new QToolBar("Tool Options");
    bar->setObjectName("optionsBar");
    bar->setMovable(false);
    addToolBar(Qt::TopToolBarArea, bar);

    m_optStack = new QStackedWidget;
    m_optStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(m_optStack);

    auto mkPage = [&] {
        auto* w = new QWidget;
        auto* lay = new QHBoxLayout(w);
        lay->setContentsMargins(8, 2, 8, 2);
        lay->setSpacing(8);
        m_optStack->addWidget(w);
        return lay;
    };

    // page 0: generic hint
    {
        auto* lay = mkPage();
        lay->addWidget(new QLabel("Wheel = zoom · Space-drag / middle-drag = pan · Shift adds to selection, Alt subtracts"));
        lay->addStretch();
    }
    // page 1: brush / eraser
    {
        auto* lay = mkPage();
        auto addSlider = [&](const QString& label, int min, int max, int val,
                             std::function<void(int)> setter) {
            lay->addWidget(new QLabel(label));
            auto* s = new QSlider(Qt::Horizontal);
            s->setRange(min, max);
            s->setValue(val);
            s->setFixedWidth(110);
            auto* num = new QLabel(QString::number(val));
            num->setMinimumWidth(28);
            connect(s, &QSlider::valueChanged, this, [setter, num](int v) {
                num->setText(QString::number(v));
                setter(v);
            });
            lay->addWidget(s);
            lay->addWidget(num);
            return s;
        };
        m_brushSizeSlider = addSlider("Size", 1, 500, m_ts.brushSize, [this](int v) {
            m_ts.brushSize = v;
            if (Canvas* c = currentCanvas()) c->update();
        });
        addSlider("Opacity", 1, 100, m_ts.brushOpacity, [this](int v) { m_ts.brushOpacity = v; });
        addSlider("Flow", 1, 100, m_ts.brushFlow, [this](int v) { m_ts.brushFlow = v; });
        addSlider("Hardness", 0, 100, m_ts.brushHardness, [this](int v) { m_ts.brushHardness = v; });
        auto* pSize = new QCheckBox("Pen pressure → size");
        pSize->setChecked(m_ts.pressureSize);
        connect(pSize, &QCheckBox::toggled, this, [this](bool b) { m_ts.pressureSize = b; });
        auto* pOp = new QCheckBox("→ opacity");
        pOp->setChecked(m_ts.pressureOpacity);
        connect(pOp, &QCheckBox::toggled, this, [this](bool b) { m_ts.pressureOpacity = b; });
        lay->addWidget(pSize);
        lay->addWidget(pOp);
        lay->addStretch();
    }
    // page 2: wand
    {
        auto* lay = mkPage();
        lay->addWidget(new QLabel("Tolerance"));
        auto* s = new QSlider(Qt::Horizontal);
        s->setRange(0, 255);
        s->setValue(m_ts.wandTolerance);
        s->setFixedWidth(160);
        auto* num = new QLabel(QString::number(m_ts.wandTolerance));
        connect(s, &QSlider::valueChanged, this, [this, num](int v) {
            m_ts.wandTolerance = v;
            num->setText(QString::number(v));
        });
        lay->addWidget(s);
        lay->addWidget(num);
        auto* cont = new QCheckBox("Contiguous");
        cont->setChecked(m_ts.wandContiguous);
        connect(cont, &QCheckBox::toggled, this, [this](bool b) { m_ts.wandContiguous = b; });
        lay->addWidget(cont);
        lay->addStretch();
    }
    // page 3: shapes
    {
        auto* lay = mkPage();
        auto* fill = new QCheckBox("Fill (FG color)");
        fill->setChecked(m_ts.shapeFill);
        connect(fill, &QCheckBox::toggled, this, [this](bool b) { m_ts.shapeFill = b; });
        auto* stroke = new QCheckBox("Stroke (BG color)");
        stroke->setChecked(m_ts.shapeStroke);
        connect(stroke, &QCheckBox::toggled, this, [this](bool b) { m_ts.shapeStroke = b; });
        auto* width = new QSpinBox;
        width->setRange(1, 200);
        width->setValue(m_ts.shapeStrokeWidth);
        connect(width, &QSpinBox::valueChanged, this, [this](int v) { m_ts.shapeStrokeWidth = v; });
        lay->addWidget(fill);
        lay->addWidget(stroke);
        lay->addWidget(new QLabel("Width"));
        lay->addWidget(width);
        lay->addStretch();
    }
    // page 4: gradient
    {
        auto* lay = mkPage();
        lay->addWidget(new QLabel("Gradient:"));
        auto* mode = new QComboBox;
        mode->addItems({"Foreground → Background", "Foreground → Transparent"});
        connect(mode, &QComboBox::currentIndexChanged, this, [this](int i) { m_ts.gradientMode = i; });
        lay->addWidget(mode);
        lay->addWidget(new QLabel("Drag across the canvas to apply (respects selection)"));
        lay->addStretch();
    }
    // page 5: text
    {
        auto* lay = mkPage();
        auto* family = new QFontComboBox;
        family->setCurrentFont(QFont(m_ts.fontFamily));
        connect(family, &QFontComboBox::currentFontChanged, this, [this](const QFont& f) {
            m_ts.fontFamily = f.family();
        });
        auto* size = new QSpinBox;
        size->setRange(4, 800);
        size->setValue(m_ts.fontSize);
        connect(size, &QSpinBox::valueChanged, this, [this](int v) { m_ts.fontSize = v; });
        lay->addWidget(family);
        lay->addWidget(size);
        lay->addWidget(new QLabel("Click the canvas to add text; click existing text to edit"));
        lay->addStretch();
    }
}

// ============================ docks ============================

void MainWindow::buildDocks()
{
    auto mkDock = [&](const QString& title, QWidget* w, Qt::DockWidgetArea area) {
        auto* d = new QDockWidget(title);
        d->setObjectName(title);
        d->setWidget(w);
        d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
        addDockWidget(area, d);
        return d;
    };

    m_color = new ColorPanel(&m_ts);
    m_props = new PropertiesPanel;
    m_layers = new LayersPanel;
    auto* history = new QUndoView(&m_undoGroup);
    history->setEmptyLabel("<empty history>");

    auto* dColor = mkDock("Color", m_color, Qt::RightDockWidgetArea);
    auto* dProps = mkDock("Properties", m_props, Qt::RightDockWidgetArea);
    auto* dLayers = mkDock("Layers", m_layers, Qt::RightDockWidgetArea);
    auto* dHistory = mkDock("History", history, Qt::RightDockWidgetArea);
    tabifyDockWidget(dProps, dHistory);
    dProps->raise();
    resizeDocks({dColor, dProps, dLayers}, {150, 170, 320}, Qt::Vertical);
}

// ============================ menus ============================

void MainWindow::buildMenus()
{
    // ---- File ----
    QMenu* file = menuBar()->addMenu("&File");
    file->addAction("New…", QKeySequence::New, this, [this] { newDocument(); });
    file->addAction("Open…", QKeySequence::Open, this, [this] {
        QStringList paths = QFileDialog::getOpenFileNames(this, "Open",
            QString(), "Images / Projects (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff *.pgd);;All files (*)");
        for (const QString& p : paths) openPath(p);
    });
    file->addAction("Open as Layer…", QKeySequence("Ctrl+Shift+O"), this, [this] {
        if (!currentDoc()) { statusBar()->showMessage("Open a document first", 3000); return; }
        QStringList paths = QFileDialog::getOpenFileNames(this, "Open as Layer",
            QString(), "Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff)");
        for (const QString& p : paths) {
            QImageReader r(p);
            r.setAutoTransform(true);
            QImage img = r.read();
            if (!img.isNull()) placeImageAsLayer(img, QFileInfo(p).completeBaseName());
        }
    });
    file->addSeparator();
    file->addAction("Save", QKeySequence::Save, this, [this] {
        if (currentDoc()) saveDoc(currentDoc(), false);
    });
    file->addAction("Save As…", QKeySequence("Ctrl+Shift+S"), this, [this] {
        if (currentDoc()) saveDoc(currentDoc(), true);
    });
    file->addAction("Export As… (PNG/JPG)", QKeySequence("Ctrl+Shift+E"), this, [this] {
        exportDoc(currentDoc());
    });
    file->addSeparator();
    file->addAction("Close Tab", QKeySequence("Ctrl+W"), this, [this] {
        if (m_tabs->count() > 0) closeTab(m_tabs->currentIndex());
    });
    file->addAction("Quit", QKeySequence("Ctrl+Q"), this, &QWidget::close);

    // ---- Edit ----
    QMenu* edit = menuBar()->addMenu("&Edit");
    QAction* undoA = m_undoGroup.createUndoAction(this, "Undo");
    undoA->setShortcut(QKeySequence("Ctrl+Z"));
    QAction* redoA = m_undoGroup.createRedoAction(this, "Redo");
    redoA->setShortcuts({QKeySequence("Ctrl+Shift+Z"), QKeySequence("Ctrl+Y")});
    edit->addAction(undoA);
    edit->addAction(redoA);
    edit->addSeparator();
    edit->addAction("Cut", QKeySequence::Cut, this, [this] {
        doCopy();
        if (Canvas* c = currentCanvas()) c->clearSelectionArea();
    });
    edit->addAction("Copy (merged)", QKeySequence::Copy, this, [this] { doCopy(); });
    edit->addAction("Paste as Layer", QKeySequence::Paste, this, [this] { doPaste(); });
    edit->addSeparator();
    edit->addAction("Fill with Foreground", QKeySequence("Alt+Backspace"), this, [this] {
        if (Canvas* c = currentCanvas()) c->fillWith(m_ts.fg);
    });
    edit->addAction("Fill with Background", QKeySequence("Ctrl+Backspace"), this, [this] {
        if (Canvas* c = currentCanvas()) c->fillWith(m_ts.bg);
    });
    edit->addAction("Clear", QKeySequence::Delete, this, [this] {
        if (Canvas* c = currentCanvas()) c->clearSelectionArea();
    });
    edit->addSeparator();
    edit->addAction("Free Transform", QKeySequence("Ctrl+T"), this, [this] {
        if (Canvas* c = currentCanvas()) c->startTransform();
    });

    // ---- Image ----
    QMenu* image = menuBar()->addMenu("&Image");
    auto snapOp = [this](const QString& name, std::function<void(Document*)> fn, bool refit = true) {
        Document* doc = currentDoc();
        if (!doc) return;
        DocState before = DocState::capture(doc);
        fn(doc);
        doc->undo.push(new SnapshotCommand(doc, before, name));
        if (refit)
            if (Canvas* c = currentCanvas()) c->fitToWindow();
    };
    image->addAction("Image Size (Scale)…", QKeySequence("Ctrl+Alt+I"), this, [this, snapOp] {
        Document* doc = currentDoc();
        if (!doc) return;
        SizeDialog dlg("Image Size", doc->size(), true, this);
        if (dlg.exec() == QDialog::Accepted && dlg.newSize() != doc->size())
            snapOp("Scale Image", [&](Document* d) { d->scaleTo(dlg.newSize()); });
    });
    image->addAction("Canvas Size…", QKeySequence("Ctrl+Alt+C"), this, [this, snapOp] {
        Document* doc = currentDoc();
        if (!doc) return;
        SizeDialog dlg("Canvas Size", doc->size(), false, this);
        if (dlg.exec() == QDialog::Accepted && dlg.newSize() != doc->size())
            snapOp("Canvas Size", [&](Document* d) { d->resizeCanvas(dlg.newSize()); });
    });
    image->addSeparator();
    image->addAction("Rotate 90° CW", this, [snapOp] { snapOp("Rotate 90 CW", [](Document* d) { d->rotateDoc(1); }); });
    image->addAction("Rotate 90° CCW", this, [snapOp] { snapOp("Rotate 90 CCW", [](Document* d) { d->rotateDoc(3); }); });
    image->addAction("Rotate 180°", this, [snapOp] { snapOp("Rotate 180", [](Document* d) { d->rotateDoc(2); }); });
    image->addAction("Flip Horizontal", this, [snapOp] { snapOp("Flip H", [](Document* d) { d->flipDoc(true); }, false); });
    image->addAction("Flip Vertical", this, [snapOp] { snapOp("Flip V", [](Document* d) { d->flipDoc(false); }, false); });
    image->addSeparator();
    image->addAction("Crop to Selection", this, [this, snapOp] {
        Document* doc = currentDoc();
        if (!doc || !doc->hasSelection()) {
            statusBar()->showMessage("Make a selection first", 3000);
            return;
        }
        QRect r = doc->selection.boundingRect().toAlignedRect();
        snapOp("Crop", [r](Document* d) { d->cropTo(r); });
    });
    image->addAction("Flatten Image", this, [snapOp] {
        snapOp("Flatten", [](Document* d) {
            QImage img = d->composite();
            auto flat = std::make_shared<Layer>();
            flat->type = Layer::Raster;
            flat->name = "Background";
            flat->image = img;
            d->layers.clear();
            d->layers.append(flat);
            d->activeIndex = 0;
            d->notifyStructure();
        }, false);
    });

    // ---- Layer ----
    QMenu* layer = menuBar()->addMenu("&Layer");
    layer->addAction("New Layer", QKeySequence("Ctrl+Shift+N"), this, [this] { m_layers->addButton()->click(); });
    layer->addAction("Duplicate Layer", QKeySequence("Ctrl+J"), this, [this] { m_layers->dupButton()->click(); });
    layer->addAction("Delete Layer", this, [this] { m_layers->delButton()->click(); });
    layer->addAction("Merge Down", QKeySequence("Ctrl+E"), this, [this] { m_layers->mergeButton()->click(); });
    layer->addSeparator();
    layer->addAction("Add Layer Mask", this, [this] { m_layers->maskButton()->click(); });
    layer->addAction("Delete Layer Mask", this, [this] { m_layers->delMaskButton()->click(); });

    // ---- Select ----
    QMenu* select = menuBar()->addMenu("&Select");
    select->addAction("All", QKeySequence::SelectAll, this, [this] {
        Document* doc = currentDoc();
        if (!doc) return;
        QPainterPath p;
        p.addRect(doc->rect());
        doc->undo.push(new SelectionCommand(doc, doc->selection, doc->feather, p, doc->feather, "Select All"));
    });
    select->addAction("Deselect", QKeySequence("Ctrl+D"), this, [this] {
        Document* doc = currentDoc();
        if (!doc || !doc->hasSelection()) return;
        doc->undo.push(new SelectionCommand(doc, doc->selection, doc->feather, QPainterPath(), doc->feather, "Deselect"));
    });
    select->addAction("Invert Selection", QKeySequence("Ctrl+Shift+I"), this, [this] {
        Document* doc = currentDoc();
        if (!doc) return;
        QPainterPath all;
        all.addRect(doc->rect());
        QPainterPath inv = doc->hasSelection() ? all.subtracted(doc->selection) : all;
        doc->undo.push(new SelectionCommand(doc, doc->selection, doc->feather, inv, doc->feather, "Invert Selection"));
    });
    select->addAction("Feather…", QKeySequence("Shift+F6"), this, [this] {
        Document* doc = currentDoc();
        if (!doc || !doc->hasSelection()) {
            statusBar()->showMessage("Make a selection first", 3000);
            return;
        }
        bool ok = false;
        double f = QInputDialog::getDouble(this, "Feather Selection", "Radius (px):",
                                           doc->feather, 0, 500, 1, &ok);
        if (ok)
            doc->undo.push(new SelectionCommand(doc, doc->selection, doc->feather,
                                                doc->selection, f, "Feather"));
    });

    // ---- Filter ----
    QMenu* filter = menuBar()->addMenu("Fi&lter");
    for (FilterType t : {FilterType::Brightness, FilterType::Contrast, FilterType::Saturation,
                         FilterType::Hue, FilterType::Exposure, FilterType::Levels}) {
        filter->addAction(filterName(t) + "…", this, [this, t] { openFilterDialog(t); });
    }
    filter->addSeparator();
    filter->addAction("Grayscale", this, [this] { applyDirectFilter(FilterType::Grayscale); });
    filter->addAction("Invert Colors", QKeySequence("Ctrl+I"), this, [this] { applyDirectFilter(FilterType::Invert); });
    filter->addSeparator();
    filter->addAction("Pixelate…", this, [this] { openFilterDialog(FilterType::Pixelate); });
    filter->addAction("Blur…", this, [this] { openFilterDialog(FilterType::Blur); });

    // ---- View ----
    QMenu* view = menuBar()->addMenu("&View");
    view->addAction("Zoom In", QKeySequence::ZoomIn, this, [this] {
        if (Canvas* c = currentCanvas()) c->zoomIn();
    });
    view->addAction("Zoom Out", QKeySequence::ZoomOut, this, [this] {
        if (Canvas* c = currentCanvas()) c->zoomOut();
    });
    view->addAction("Fit on Screen", QKeySequence("Ctrl+0"), this, [this] {
        if (Canvas* c = currentCanvas()) c->fitToWindow();
    });
    view->addAction("100%", QKeySequence("Ctrl+1"), this, [this] {
        if (Canvas* c = currentCanvas()) c->zoomActual();
    });
    view->addSeparator();
    QAction* fs = view->addAction("Fullscreen", QKeySequence("F11"), this, [this] {
        if (isFullScreen()) showNormal();
        else showFullScreen();
    });
    fs->setCheckable(true);
    view->addSeparator();
    // dock toggles are added in buildDocks order
    for (QDockWidget* d : findChildren<QDockWidget*>())
        view->addAction(d->toggleViewAction());

    // ---- Help ----
    QMenu* help = menuBar()->addMenu("&Help");
    help->addAction("About", this, [this] {
        QMessageBox::about(this, "About PhotoGod",
            "<b>PhotoGod</b> — a fast layer-based image editor.<br><br>"
            "Qt 6 · C++20 · layers, masks, brushes, selections, adjustments.<br>"
            "Save projects as .pgd, export PNG/JPG/WebP.");
    });
}
