#include "MainWindow.h"
#include "Canvas.h"
#include "Panels.h"
#include "Dialogs.h"
#include "Commands.h"
#include "HomePage.h"
#include "CommandPalette.h"
#include "DockTitleBar.h"
#include "HistoryPanel.h"
#include <QPointer>
#include <QSettings>
#include <QDockWidget>
#include <QTabWidget>
#include <QTabBar>
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
    m_tabs->setObjectName("docTabs");
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);

    // tabbed dock panels show their selector on top, not at the bottom
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    // DockTitleBar drives dragging itself (Photoshop-style tab handles + drop
    // hints), so Qt's own GroupedDragging rubber-band stays off — two drag
    // systems on one press fight each other.
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks
                   | QMainWindow::AllowNestedDocks);

    m_home = new HomePage;
    connect(m_home, &HomePage::newRequested, this, [this] { newDocument(); });
    connect(m_home, &HomePage::openRequested, this, [this] {
        QString path = QFileDialog::getOpenFileName(this, "Open",
            QString(), "Images / Projects (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff *.pgd);;All files (*)");
        if (!path.isEmpty()) openPath(path);
    });
    connect(m_home, &HomePage::projectRequested, this, [this](const QString& path) {
        openPath(path);
    });

    m_central = new QStackedWidget;
    m_central->addWidget(m_home);   // index 0
    m_central->addWidget(m_tabs);   // index 1
    setCentralWidget(m_central);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int i) { closeTab(i); });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        Document* doc = currentDoc();
        m_layers->setDocument(doc);
        m_props->setDocument(doc);
        // built later than this connect, but always live by the time it fires
        if (m_history) m_history->setDocument(doc);
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
    buildCommandPalette();
    buildMenus();

    m_statusPos = new QLabel("-");
    m_statusZoom = new QLabel("100%");
    m_statusSize = new QLabel("-");
    statusBar()->addPermanentWidget(m_statusSize);
    statusBar()->addPermanentWidget(m_statusZoom);
    statusBar()->addWidget(m_statusPos);
    statusBar()->showMessage("Ctrl+N new document · Ctrl+O open image · drag & drop images onto the window", 8000);

    // start on the home screen (docks/toolbars hidden until a document is open)
    m_central->setCurrentWidget(m_home);
    updateChromeForView();
}

MainWindow::~MainWindow()
{
    m_shuttingDown = true;
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

// ============================ home / editor view ============================

void MainWindow::showHome()
{
    m_home->refresh();
    m_central->setCurrentWidget(m_home);
    updateChromeForView();
    statusBar()->showMessage("Home — pick a recent project or start a new one", 4000);
}

void MainWindow::showEditor()
{
    m_central->setCurrentWidget(m_tabs);
    updateChromeForView();
    if (Canvas* c = currentCanvas()) c->setFocus();
}

void MainWindow::updateChromeForView()
{
    // the editing chrome (tool docks + toolbars) is meaningless on the start screen
    bool onHome = (m_central->currentWidget() == m_home);
    m_switchingView = true;
    if (onHome) {
        // Hide the panels, but remember the user's intent so panels they closed
        // themselves stay closed on the way back. isVisible() is useless here —
        // during construction the window isn't shown yet, so it reports false
        // for everything; m_userClosed is maintained from the close signal.
        for (QDockWidget* d : findChildren<QDockWidget*>())
            d->hide();
    } else {
        for (QDockWidget* d : findChildren<QDockWidget*>())
            if (!m_userClosed.contains(d)) d->show();
    }
    m_switchingView = false;
    for (QToolBar* t : findChildren<QToolBar*>())
        t->setVisible(!onHome);
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
    connect(doc, &Document::activeLayerChanged, this, [this, doc] {
        if (doc != currentDoc() || !m_dockProps) return;
        auto l = doc->activeLayer();
        if (l && l->type == Layer::Adjustment)
            m_dockProps->raise();
    });
    connect(doc, &Document::structureChanged, this, [this, doc] {
        if (doc == currentDoc())
            m_statusSize->setText(QString("%1 x %2").arg(doc->width()).arg(doc->height()));
    });

    int idx = m_tabs->addTab(canvas, doc->name);
    m_tabs->setCurrentIndex(idx);
    showEditor();
    canvas->setTool(m_tool);
    canvas->setFocus();
}

void MainWindow::updateTabTitle(Document* doc)
{
    // On shutdown the tab widget is torn down before the documents it owns, and
    // each dying QUndoStack still emits cleanChanged on its way out. Touching
    // the half-destroyed QTabWidget from that signal crashes.
    if (m_shuttingDown || !m_tabs) return;
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
        Recents::touch(path, doc->name);
        Recents::saveThumbnail(path, doc->composite());
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
    if (m_tabs->count() == 0)
        showHome();
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
    savePanelLayout();
    e->accept();
}

void MainWindow::savePanelLayout()
{
    QSettings s;
    s.setValue("layout/geometry", saveGeometry());
    s.setValue("layout/state", saveState());
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
        Recents::touch(path, doc->name);
        Recents::saveThumbnail(path, doc->composite());
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

    // name = short label, desc = what the tool actually does (shown on hover).
    auto addTool = [&](ToolType t, const QString& iconName, const QString& name,
                       const QString& shortcut, const QString& desc) {
        auto* a = new QAction(name, this);
        a->setIcon(QIcon(QString(":/icons/%1.svg").arg(iconName)));
        a->setCheckable(true);
        // Rich text so the tool name reads as a heading above its description.
        QString tip = "<b>" + name.toHtmlEscaped() + "</b>";
        if (!shortcut.isEmpty())
            tip += " <span style='color:#8a8a92;'>" + shortcut.toHtmlEscaped() + "</span>";
        if (!desc.isEmpty())
            tip += "<br><span style='color:#b6b6be;'>" + desc.toHtmlEscaped() + "</span>";
        a->setToolTip(tip);
        a->setStatusTip(desc.isEmpty() ? name : desc);
        if (!shortcut.isEmpty()) a->setShortcut(QKeySequence(shortcut));
        connect(a, &QAction::triggered, this, [this, t] { setTool(t); });
        group->addAction(a);
        tb->addAction(a);
        m_toolActions[int(t)] = a;
        return a;
    };

    addTool(ToolType::Move, "move", "Move", "V",
            "Drag layers and selections around the canvas");
    // No "M" here — a separate action below owns M to toggle marquee shape.
    addTool(ToolType::MarqueeRect, "marquee-rect", "Rectangular Marquee", "",
            "Select a box. Shift adds, Alt subtracts, Ctrl-drag for a square");
    addTool(ToolType::MarqueeEllipse, "marquee-ellipse", "Elliptical Marquee", "",
            "Select an oval — press M to toggle with the rectangle marquee");
    addTool(ToolType::Lasso, "lasso", "Polygon Lasso", "L",
            "Click points to outline a freeform selection; close it to finish");
    addTool(ToolType::Wand, "wand", "Magic Wand", "W",
            "Select areas of similar color — tune tolerance in the options bar");
    addTool(ToolType::Crop, "crop", "Crop", "C",
            "Drag the area to keep, then press Enter to apply");
    addTool(ToolType::Eyedropper, "eyedropper", "Eyedropper", "I",
            "Click the canvas to sample a color into the foreground swatch");
    tb->addSeparator();
    addTool(ToolType::Brush, "brush", "Brush", "B",
            "Paint with the foreground color — [ and ] change the size");
    addTool(ToolType::Eraser, "eraser", "Eraser", "E",
            "Erase pixels to transparency on the active layer");
    addTool(ToolType::Blur, "blur", "Blur Brush", "R",
            "Paint a gaussian blur to soften detail where you drag");
    addTool(ToolType::Gradient, "gradient", "Gradient", "G",
            "Drag to fill with a foreground-to-background ramp");
    addTool(ToolType::Text, "text", "Text", "T",
            "Click the canvas to place an editable text layer");
    tb->addSeparator();
    addTool(ToolType::ShapeRect, "shape-rect", "Rectangle Shape", "",
            "Drag out a rectangle — press U to cycle through the shape tools");
    addTool(ToolType::ShapeEllipse, "shape-ellipse", "Ellipse Shape", "",
            "Drag out an ellipse — Shift constrains it to a circle");
    addTool(ToolType::ShapeLine, "shape-line", "Line Shape", "",
            "Drag a straight line — Shift snaps to 45° angles");
    tb->addSeparator();
    addTool(ToolType::Zoom, "zoom", "Zoom", "Z",
            "Click to zoom in, Alt-click to zoom out");
    addTool(ToolType::Hand, "hand", "Hand", "H",
            "Drag to pan the canvas — or just hold Space with any tool");

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
    for (const auto& sync : m_optionSync) sync();
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
    case ToolType::Blur:         return 6;
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
    // sliders that mirror a ToolSettings field and stay in sync across pages
    auto addSyncSlider = [this](QHBoxLayout* lay, const QString& label, int min, int max,
                                std::function<int()> getter, std::function<void(int)> setter) {
        lay->addWidget(new QLabel(label));
        auto* s = new QSlider(Qt::Horizontal);
        s->setRange(min, max);
        s->setValue(getter());
        s->setFixedWidth(110);
        auto* num = new QLabel(QString::number(getter()));
        num->setMinimumWidth(28);
        connect(s, &QSlider::valueChanged, this, [setter, num](int v) {
            num->setText(QString::number(v));
            setter(v);
        });
        lay->addWidget(s);
        lay->addWidget(num);
        m_optionSync << [s, getter] { s->setValue(getter()); };
        return s;
    };

    // page 1: brush / eraser
    {
        auto* lay = mkPage();
        m_brushSizeSlider = addSyncSlider(lay, "Size", 1, 500,
            [this] { return m_ts.brushSize; },
            [this](int v) {
                m_ts.brushSize = v;
                if (Canvas* c = currentCanvas()) c->update();
            });
        addSyncSlider(lay, "Opacity", 1, 100, [this] { return m_ts.brushOpacity; },
                      [this](int v) { m_ts.brushOpacity = v; });
        addSyncSlider(lay, "Flow", 1, 100, [this] { return m_ts.brushFlow; },
                      [this](int v) { m_ts.brushFlow = v; });
        addSyncSlider(lay, "Hardness", 0, 100, [this] { return m_ts.brushHardness; },
                      [this](int v) { m_ts.brushHardness = v; });
        addSyncSlider(lay, "Grain", 0, 100, [this] { return m_ts.brushNoise; },
                      [this](int v) { m_ts.brushNoise = v; });
        auto* pSize = new QCheckBox("Pressure → size");
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
    // page 6: blur brush
    {
        auto* lay = mkPage();
        addSyncSlider(lay, "Size", 1, 500,
            [this] { return m_ts.brushSize; },
            [this](int v) {
                m_ts.brushSize = v;
                if (Canvas* c = currentCanvas()) c->update();
            });
        addSyncSlider(lay, "Strength", 1, 100, [this] { return m_ts.brushOpacity; },
                      [this](int v) { m_ts.brushOpacity = v; });
        addSyncSlider(lay, "Blur radius", 1, 60, [this] { return m_ts.blurRadius; },
                      [this](int v) { m_ts.blurRadius = v; });
        lay->addWidget(new QLabel("Paint over areas to blur them"));
        lay->addStretch();
    }

    // right corner: rulers / snapping toggles
    auto* rulersBtn = new QAction(QIcon(":/icons/ruler.svg"), "Rulers && Guides", this);
    rulersBtn->setCheckable(true);
    rulersBtn->setChecked(m_ts.showRulers);
    rulersBtn->setToolTip("Show rulers and guides — drag out of a ruler to create a guide,\n"
                          "drag a guide back to the ruler to remove it (Move tool)");
    connect(rulersBtn, &QAction::triggered, this, [this](bool on) {
        m_ts.showRulers = on;
        if (Canvas* c = currentCanvas()) c->update();
    });
    auto* snapBtn = new QAction("Snap", this);
    snapBtn->setCheckable(true);
    snapBtn->setChecked(m_ts.snapping);
    snapBtn->setToolTip("Snap moves/selections/crops to guides and canvas edges/center");
    connect(snapBtn, &QAction::triggered, this, [this](bool on) { m_ts.snapping = on; });

    bar->addAction(rulersBtn);
    bar->addAction(snapBtn);
}

// ============================ docks ============================

void MainWindow::buildDocks()
{
    auto mkDock = [&](const QString& title, QWidget* w, Qt::DockWidgetArea area) {
        auto* d = new QDockWidget(title);
        d->setObjectName(title);
        d->setWidget(w);
        // No DockWidgetMovable: the tab strip runs the drag, and leaving Qt's
        // mover on would start a competing rubber-band drag from the same press.
        d->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);
        d->setAllowedAreas(Qt::AllDockWidgetAreas);
        // The tab strip IS the panel's header — its tabs are the drag handles.
        // Nothing sits above it, so there is only ever one header per panel.
        d->setTitleBarWidget(new DockTitleBar(d));
        addDockWidget(area, d);
        // Record deliberate show/hide only. updateChromeForView() flips panels
        // in bulk when swapping Home/editor; that must not look like intent.
        connect(d->toggleViewAction(), &QAction::toggled, this, [this, d](bool on) {
            if (m_switchingView) return;
            if (on) m_userClosed.remove(d);
            else    m_userClosed.insert(d);
        });
        return d;
    };

    m_color = new ColorPanel(&m_ts);
    m_props = new PropertiesPanel;
    m_layers = new LayersPanel;
    m_brushes = new BrushesPanel(&m_ts);
    m_adjust = new AdjustmentsPanel;
    m_history = new HistoryPanel(&m_undoGroup);

    connect(m_brushes, &BrushesPanel::presetChosen, this, [this] {
        setTool(ToolType::Brush);
        statusBar()->showMessage("Brush preset applied", 2000);
    });
    connect(m_adjust, &AdjustmentsPanel::requestAdjustment, this, [this](FilterType t) {
        Document* doc = currentDoc();
        if (!doc) { statusBar()->showMessage("Open a document first", 3000); return; }
        auto l = Layer::makeAdjustment(t);
        if (t == FilterType::Grayscale) l->name = "Black & White";
        doc->undo.push(new AddLayerCommand(doc, l, doc->activeIndex + 1, "New Adjustment"));
        if (m_dockProps) m_dockProps->raise();
    });

    auto* dColor = mkDock("Color", m_color, Qt::RightDockWidgetArea);
    auto* dBrushes = mkDock("Brushes", m_brushes, Qt::RightDockWidgetArea);
    auto* dLayers = mkDock("Layers", m_layers, Qt::RightDockWidgetArea);
    auto* dProps = mkDock("Properties", m_props, Qt::RightDockWidgetArea);
    auto* dAdjust = mkDock("Adjustments", m_adjust, Qt::RightDockWidgetArea);
    auto* dHistory = mkDock("History", m_history, Qt::RightDockWidgetArea);
    m_dockProps = dProps;
    m_dockLayers = dLayers;

    // Default layout: every panel is its own stacked slot down the right edge.
    // Nothing is pre-tabified — drag a header onto another panel to tab them,
    // or out of the window to float it.
    m_docks = QList<QDockWidget*>{dColor, dBrushes, dLayers, dProps, dAdjust, dHistory};
    resizeDocks(m_docks, QList<int>{200, 170, 260, 200, 180, 160}, Qt::Vertical);

    // Remember whatever arrangement the user lands on.
    m_defaultLayout = saveState();
    QSettings s;
    const QByteArray geo = s.value("layout/geometry").toByteArray();
    const QByteArray st = s.value("layout/state").toByteArray();
    if (!geo.isEmpty()) restoreGeometry(geo);
    if (!st.isEmpty()) restoreState(st);

    // Qt grows its own QTabBar for each tabbed dock group. Our strip already
    // shows those tabs, so the native one would be a second, duplicate header —
    // collapse it. They are created on demand, hence the polish-time sweep.
    hideNativeDockTabs();
    for (QDockWidget* d : m_docks)
        if (auto* strip = qobject_cast<DockTitleBar*>(d->titleBarWidget()))
            strip->refresh();
}

void MainWindow::tabifyPanelsForTest(const QString& a, const QString& b)
{
    QDockWidget *da = nullptr, *db = nullptr;
    for (QDockWidget* d : m_docks) {
        if (d->objectName() == a) da = d;
        if (d->objectName() == b) db = d;
    }
    if (!da || !db) return;
    tabifyDockWidget(da, db);
    da->show();
    da->raise();
    hideNativeDockTabs();
    for (QDockWidget* d : m_docks)
        if (auto* s = qobject_cast<DockTitleBar*>(d->titleBarWidget())) s->refresh();
}

void MainWindow::hideNativeDockTabs()
{
    for (QTabBar* tb : findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly)) {
        tb->setFixedHeight(0);
        tb->hide();
    }
}

void MainWindow::resetPanelLayout()
{
    // Undo any floating/tabbing and put every panel back in its own slot.
    // Reset means "all panels back", so previous closes are forgotten too —
    // otherwise updateChromeForView() would re-hide them on the next switch.
    m_switchingView = true;
    m_userClosed.clear();
    for (QDockWidget* d : m_docks) {
        d->setFloating(false);
        d->show();
    }
    restoreState(m_defaultLayout);
    m_switchingView = false;
}

// ============================ menus ============================

void MainWindow::buildMenus()
{
    // ---- Home / New icon buttons beside the File menu (icons only, minimal) ----
    auto* leftBar = new QWidget;
    auto* leftLay = new QHBoxLayout(leftBar);
    leftLay->setContentsMargins(6, 0, 4, 0);
    leftLay->setSpacing(2);

    auto* homeBtn = new QToolButton;
    homeBtn->setIcon(QIcon(":/icons/home.svg"));
    homeBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    homeBtn->setAutoRaise(true);
    homeBtn->setCursor(Qt::PointingHandCursor);
    homeBtn->setToolTip("Go to the start screen");
    connect(homeBtn, &QToolButton::clicked, this, [this] { showHome(); });

    auto* newBtn = new QToolButton;
    newBtn->setIcon(QIcon(":/icons/new-file.svg"));
    newBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    newBtn->setAutoRaise(true);
    newBtn->setCursor(Qt::PointingHandCursor);
    newBtn->setToolTip("New document (Ctrl+N)");
    connect(newBtn, &QToolButton::clicked, this, [this] { newDocument(); });

    leftLay->addWidget(homeBtn);
    leftLay->addWidget(newBtn);
    menuBar()->setCornerWidget(leftBar, Qt::TopLeftCorner);

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
    QMenu* xform = edit->addMenu("Transform");
    auto addXformMode = [&](const QString& label, const QKeySequence& seq, Canvas::XformMode mode) {
        xform->addAction(label, seq, this, [this, mode] {
            Canvas* c = currentCanvas();
            if (!c) return;
            if (!c->inTransform()) c->startTransform();
            if (c->inTransform()) c->setTransformMode(mode);
        });
    };
    addXformMode("Free Transform", QKeySequence("Ctrl+T"), Canvas::XformMode::Free);
    xform->addSeparator();
    addXformMode("Skew", QKeySequence(), Canvas::XformMode::Skew);
    addXformMode("Distort", QKeySequence(), Canvas::XformMode::Distort);
    addXformMode("Perspective", QKeySequence(), Canvas::XformMode::Perspective);
    addXformMode("Warp", QKeySequence(), Canvas::XformMode::Warp);

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

    // ---- Window ----
    QMenu* window = menuBar()->addMenu("&Window");
    for (QDockWidget* d : findChildren<QDockWidget*>())
        window->addAction(d->toggleViewAction());
    window->addSeparator();
    window->addAction("Show All Panels", this, [this] {
        for (QDockWidget* d : findChildren<QDockWidget*>()) d->show();
    });
    window->addAction("Hide All Panels (Tab)", QKeySequence("Tab"), this, [this] {
        bool anyVisible = false;
        for (QDockWidget* d : findChildren<QDockWidget*>())
            if (d->isVisible()) { anyVisible = true; break; }
        for (QDockWidget* d : findChildren<QDockWidget*>())
            d->setVisible(!anyVisible);
    });
    window->addSeparator();
    window->addAction("Reset Panel Layout", this, [this] {
        resetPanelLayout();
        statusBar()->showMessage("Panel layout reset", 2000);
    });

    // ---- Help ----
    QMenu* help = menuBar()->addMenu("&Help");
    // Shortcut lives on the application-wide action in buildCommandPalette();
    // declaring it here too would make Qt report an ambiguous shortcut.
    QAction* paletteAct = help->addAction("Command Palette…", this, [this] {
        showCommandPalette();
    });
    paletteAct->setToolTip("Universal search — Ctrl+Shift+P or Ctrl+Shift+Space");
    help->addSeparator();
    help->addAction("About", this, [this] {
        QMessageBox::about(this, "About PhotoGod",
            "<b>PhotoGod</b> — a fast layer-based image editor.<br><br>"
            "Qt 6 · C++20 · layers, masks, brushes, selections, adjustments.<br>"
            "Save projects as .pgd, export PNG/JPG/WebP.");
    });
}

// ============================ command palette ============================

void MainWindow::buildCommandPalette()
{
    m_palette = new CommandPalette(this);
    m_palette->setProvider([this] { return collectCommands(); });

    // Both shortcuts open the same universal search.
    for (const char* seq : {"Ctrl+Shift+P", "Ctrl+Shift+Space"}) {
        auto* a = new QAction(this);
        a->setShortcut(QKeySequence(seq));
        a->setShortcutContext(Qt::ApplicationShortcut);
        connect(a, &QAction::triggered, this, [this] { showCommandPalette(); });
        addAction(a);
    }
}

void MainWindow::showCommandPalette(const QString& query)
{
    if (m_palette) m_palette->openPalette(query);
}

bool MainWindow::runPaletteCommand(const QString& title)
{
    for (const PaletteCommand& c : collectCommands()) {
        if (c.title == title && c.enabled && c.run) {
            c.run();
            return true;
        }
    }
    return false;
}

void MainWindow::revealLayer(int index)
{
    Document* doc = currentDoc();
    if (!doc || index < 0 || index >= doc->layers.size()) return;
    doc->setActiveIndex(index);
    if (m_dockLayers) {
        m_dockLayers->show();
        m_dockLayers->raise();
    }
    m_layers->highlightLayer(index);
    statusBar()->showMessage("Selected layer: " + doc->layers[index]->name, 3000);
}

QList<PaletteCommand> MainWindow::collectCommands()
{
    QList<PaletteCommand> out;
    Document* doc = currentDoc();
    const bool hasDoc = (doc != nullptr);

    // ---- tools ----
    struct ToolEntry { ToolType t; const char* icon; const char* name; const char* keys; };
    static const ToolEntry kTools[] = {
        {ToolType::Move,           "move",            "Move",                "transform position drag"},
        {ToolType::MarqueeRect,    "marquee-rect",    "Rectangular Marquee", "select selection box"},
        {ToolType::MarqueeEllipse, "marquee-ellipse", "Elliptical Marquee",  "select selection circle oval"},
        {ToolType::Lasso,          "lasso",           "Polygon Lasso",       "select selection freeform"},
        {ToolType::Wand,           "wand",            "Magic Wand",          "select selection color range"},
        {ToolType::Crop,           "crop",            "Crop",                "trim resize"},
        {ToolType::Eyedropper,     "eyedropper",      "Eyedropper",          "pick color sample"},
        {ToolType::Brush,          "brush",           "Brush",               "paint draw"},
        {ToolType::Eraser,         "eraser",          "Eraser",              "delete rub out"},
        {ToolType::Blur,           "blur",            "Blur Brush",          "soften smudge gaussian"},
        {ToolType::Gradient,       "gradient",        "Gradient",            "ramp fade fill"},
        {ToolType::Text,           "text",            "Text",                "type font words"},
        {ToolType::ShapeRect,      "shape-rect",      "Rectangle Shape",     "draw box square"},
        {ToolType::ShapeEllipse,   "shape-ellipse",   "Ellipse Shape",       "draw circle oval"},
        {ToolType::ShapeLine,      "shape-line",      "Line Shape",          "draw stroke"},
        {ToolType::Zoom,           "zoom",            "Zoom",                "magnify scale view"},
        {ToolType::Hand,           "hand",            "Hand",                "pan scroll navigate"},
    };
    for (const ToolEntry& e : kTools) {
        PaletteCommand c;
        c.category = "Tool";
        c.title = e.name;
        c.keywords = e.keys;
        c.iconPath = QString(":/icons/%1.svg").arg(e.icon);
        c.enabled = hasDoc;
        if (auto* a = m_toolActions.value(int(e.t)))
            c.detail = a->shortcut().toString(QKeySequence::NativeText);
        ToolType t = e.t;
        c.run = [this, t] { setTool(t); };
        out.append(c);
    }

    // ---- adjustments / filters ----
    for (FilterType t : {FilterType::Brightness, FilterType::Contrast, FilterType::Saturation,
                         FilterType::Hue, FilterType::Exposure, FilterType::Levels,
                         FilterType::Pixelate, FilterType::Blur}) {
        PaletteCommand c;
        c.category = "Adjust";
        c.title = filterName(t) + "…";
        c.detail = "apply to layer";
        c.keywords = "filter adjustment " + filterName(t);
        c.enabled = hasDoc;
        c.run = [this, t] { openFilterDialog(t); };
        out.append(c);
    }
    for (FilterType t : {FilterType::Grayscale, FilterType::Invert}) {
        PaletteCommand c;
        c.category = "Adjust";
        c.title = (t == FilterType::Grayscale) ? "Grayscale" : "Invert Colors";
        c.detail = "apply to layer";
        c.keywords = (t == FilterType::Grayscale) ? "black white desaturate mono"
                                                  : "negative flip colors";
        c.enabled = hasDoc;
        c.run = [this, t] { applyDirectFilter(t); };
        out.append(c);
    }
    // adjustment layers (non-destructive)
    for (FilterType t : {FilterType::Brightness, FilterType::Contrast, FilterType::Saturation,
                         FilterType::Hue, FilterType::Exposure, FilterType::Levels,
                         FilterType::Grayscale}) {
        PaletteCommand c;
        c.category = "Adj Layer";
        c.title = "New " + QString(t == FilterType::Grayscale ? "Black & White" : filterName(t))
                + " Adjustment Layer";
        c.detail = "non-destructive";
        c.keywords = "adjustment layer new " + filterName(t);
        c.enabled = hasDoc;
        c.run = [this, t] {
            Document* d = currentDoc();
            if (!d) return;
            auto l = Layer::makeAdjustment(t);
            if (t == FilterType::Grayscale) l->name = "Black & White";
            d->undo.push(new AddLayerCommand(d, l, d->activeIndex + 1, "New Adjustment"));
            if (m_dockProps) { m_dockProps->show(); m_dockProps->raise(); }
        };
        out.append(c);
    }

    // ---- layers of the current document (top-most first, as shown in the panel) ----
    if (doc) {
        for (int i = doc->layers.size() - 1; i >= 0; --i) {
            const auto& l = doc->layers[i];
            PaletteCommand c;
            c.category = "Layer";
            c.title = l->name;
            QStringList bits;
            bits << (l->type == Layer::Adjustment ? "adjustment"
                   : l->type == Layer::Text       ? "text" : "raster");
            if (!l->visible) bits << "hidden";
            if (l->locked)   bits << "locked";
            if (l->hasMask()) bits << "mask";
            c.detail = bits.join(" · ");
            c.keywords = "layer " + bits.join(" ") + " " + l->name;
            c.run = [this, i] { revealLayer(i); };
            out.append(c);
        }
    }

    // ---- every menu action, harvested so the palette can't drift from the menus ----
    std::function<void(QMenu*, const QString&)> harvest =
        [&](QMenu* menu, const QString& prefix) {
            for (QAction* a : menu->actions()) {
                if (a->isSeparator()) continue;
                if (QMenu* sub = a->menu()) {
                    harvest(sub, prefix + a->text().remove('&') + " › ");
                    continue;
                }
                QString label = a->text().remove('&');
                if (label.isEmpty()) continue;
                PaletteCommand c;
                c.category = prefix.isEmpty() ? "Command" : prefix.chopped(3);
                c.title = label;
                c.detail = a->shortcut().toString(QKeySequence::NativeText);
                c.keywords = a->toolTip().remove('&');
                c.enabled = a->isEnabled();
                QPointer<QAction> ptr(a);
                c.run = [ptr] { if (ptr) ptr->trigger(); };
                out.append(c);
            }
        };
    for (QAction* topAct : menuBar()->actions()) {
        QString top = topAct->text().remove('&');
        // Filter/adjustment entries are already listed above with richer keywords.
        if (top == "Filter") continue;
        if (QMenu* m = topAct->menu())
            harvest(m, top + " › ");
    }

    // ---- panels ----
    for (QDockWidget* d : findChildren<QDockWidget*>()) {
        PaletteCommand c;
        c.category = "Panel";
        c.title = (d->isVisible() ? "Hide " : "Show ") + d->objectName() + " Panel";
        c.keywords = "panel dock toggle window " + d->objectName();
        QPointer<QDockWidget> ptr(d);
        c.run = [ptr] {
            if (!ptr) return;
            ptr->setVisible(!ptr->isVisible());
            if (ptr->isVisible()) ptr->raise();
        };
        out.append(c);
    }

    return out;
}
