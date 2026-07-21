#include "Canvas.h"
#include "Commands.h"
#include "Dialogs.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QTabletEvent>
#include <QRadialGradient>
#include <QLinearGradient>
#include <QRandomGenerator>
#include <QtMath>
#include <vector>

static constexpr int kRuler = 22;   // ruler strip thickness in widget px

Canvas::Canvas(Document* doc, ToolSettings* ts, QWidget* parent)
    : QWidget(parent), m_doc(doc), m_ts(ts)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);

    connect(doc, &Document::changed, this, qOverload<>(&QWidget::update));
    connect(doc, &Document::selectionChanged, this, qOverload<>(&QWidget::update));
    connect(doc, &Document::structureChanged, this, qOverload<>(&QWidget::update));

    m_antsTimer.setInterval(120);
    connect(&m_antsTimer, &QTimer::timeout, this, [this] {
        if (m_doc->hasSelection() || m_act == Act::Marquee || m_act == Act::Lasso) {
            ++m_antsOffset;
            update();
        }
    });
    m_antsTimer.start();
}

QTransform Canvas::viewTransform() const
{
    QTransform t;
    t.translate(m_pan.x(), m_pan.y());
    t.scale(m_zoom, m_zoom);
    return t;
}

void Canvas::setTool(ToolType t)
{
    if (m_xf.active) commitTransform();
    m_cropValid = false;
    m_act = Act::None;
    m_tool = t;
    updateCursor();
    update();
}

void Canvas::updateCursor()
{
    if (m_spaceDown || m_tool == ToolType::Hand) { setCursor(Qt::OpenHandCursor); return; }
    switch (m_tool) {
    case ToolType::Move:      setCursor(Qt::SizeAllCursor); break;
    case ToolType::Zoom:      setCursor(Qt::PointingHandCursor); break;
    case ToolType::Text:      setCursor(Qt::IBeamCursor); break;
    case ToolType::Brush:
    case ToolType::Eraser:
    case ToolType::Blur:      setCursor(Qt::BlankCursor); break;
    default:                  setCursor(Qt::CrossCursor); break;
    }
}

void Canvas::zoomAt(const QPointF& widgetPos, double factor)
{
    double nz = std::clamp(m_zoom * factor, 0.02, 64.0);
    QPointF docPt = widgetToDoc(widgetPos);
    m_zoom = nz;
    m_pan = widgetPos - docPt * m_zoom;
    emit zoomChanged(m_zoom);
    update();
}

void Canvas::zoomActual()
{
    QPointF c = rect().center();
    QPointF docPt = widgetToDoc(c);
    m_zoom = 1.0;
    m_pan = c - docPt * m_zoom;
    emit zoomChanged(m_zoom);
    update();
}

void Canvas::fitToWindow()
{
    if (m_doc->width() < 1 || width() < 10) return;
    double z = std::min(double(width()) / m_doc->width(),
                        double(height()) / m_doc->height()) * 0.92;
    m_zoom = std::clamp(z, 0.02, 8.0);
    m_pan = QPointF((width() - m_doc->width() * m_zoom) / 2.0,
                    (height() - m_doc->height() * m_zoom) / 2.0);
    emit zoomChanged(m_zoom);
    update();
}

void Canvas::resizeEvent(QResizeEvent*)
{
    if (!m_fitted && width() > 20) {
        m_fitted = true;
        fitToWindow();
    }
}

void Canvas::leaveEvent(QEvent*)
{
    m_haveHover = false;
    update();
}

// ============================ painting ============================

void Canvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(36, 36, 40));

    QRectF docW(m_pan, QSizeF(m_doc->width() * m_zoom, m_doc->height() * m_zoom));

    // checkerboard
    static QPixmap checker = [] {
        QPixmap pm(16, 16);
        pm.fill(QColor(190, 190, 190));
        QPainter cp(&pm);
        cp.fillRect(0, 0, 8, 8, QColor(150, 150, 150));
        cp.fillRect(8, 8, 8, 8, QColor(150, 150, 150));
        return pm;
    }();
    p.setBrushOrigin(m_pan.toPoint());
    p.fillRect(docW, QBrush(checker));
    p.setBrushOrigin(0, 0);

    // document image
    p.save();
    p.translate(m_pan);
    p.scale(m_zoom, m_zoom);
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0);
    if (m_xf.active) {
        p.drawImage(0, 0, m_doc->composite(m_xf.layer.get()));
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setWorldTransform(xfMatrix(), true);
        p.setClipRect(QRectF(QPointF(-1e6, -1e6), QSizeF(2e6, 2e6)));
        p.drawImage(m_xf.srcOffset, m_xf.srcImage);
        p.restore();
    } else {
        p.drawImage(0, 0, m_doc->composite());
    }
    p.restore();

    p.setRenderHint(QPainter::Antialiasing, true);

    // selection marching ants
    auto drawAnts = [&](const QPainterPath& widgetPath) {
        QPen w(Qt::white, 1);
        p.setPen(w);
        p.setBrush(Qt::NoBrush);
        p.drawPath(widgetPath);
        QPen b(Qt::black, 1);
        b.setDashPattern({4, 4});
        b.setDashOffset(m_antsOffset);
        p.setPen(b);
        p.drawPath(widgetPath);
    };

    if (m_doc->hasSelection())
        drawAnts(viewTransform().map(m_doc->selection));

    // live tool overlays
    if (m_act == Act::Marquee) {
        QRectF r(docToWidget(m_dragStartDoc), docToWidget(m_dragCurDoc));
        r = r.normalized();
        QPainterPath path;
        if (m_tool == ToolType::MarqueeEllipse) path.addEllipse(r);
        else path.addRect(r);
        drawAnts(path);
    } else if (m_act == Act::Lasso && m_lassoPts.size() > 1) {
        QPolygonF wp;
        for (const auto& pt : m_lassoPts) wp << docToWidget(pt);
        QPainterPath path;
        path.addPolygon(wp);
        drawAnts(path);
    } else if (m_act == Act::ShapeDrag) {
        QPointF a = docToWidget(m_dragStartDoc), b = docToWidget(m_dragCurDoc);
        QPen pen(m_ts->fg, std::max(1.0, m_ts->shapeStrokeWidth * m_zoom));
        p.setPen(pen);
        p.setBrush(m_ts->shapeFill ? QBrush(QColor(m_ts->fg.red(), m_ts->fg.green(), m_ts->fg.blue(), 90))
                                   : QBrush(Qt::NoBrush));
        QRectF r = QRectF(a, b).normalized();
        if (m_tool == ToolType::ShapeRect) p.drawRect(r);
        else if (m_tool == ToolType::ShapeEllipse) p.drawEllipse(r);
        else p.drawLine(a, b);
    } else if (m_act == Act::GradDrag) {
        p.setPen(QPen(Qt::white, 1, Qt::DashLine));
        p.drawLine(docToWidget(m_dragStartDoc), docToWidget(m_dragCurDoc));
    }

    // crop overlay
    if (m_tool == ToolType::Crop && m_cropValid) {
        QRectF cw(docToWidget(m_cropRect.topLeft()), docToWidget(m_cropRect.bottomRight()));
        cw = cw.normalized();
        QPainterPath outer;
        outer.addRect(rect());
        QPainterPath inner;
        inner.addRect(cw);
        p.fillPath(outer.subtracted(inner), QColor(0, 0, 0, 130));
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(cw);
        p.setPen(QPen(QColor(255, 255, 255, 90), 1));
        for (int i = 1; i < 3; ++i) {
            p.drawLine(QPointF(cw.left() + cw.width() * i / 3.0, cw.top()),
                       QPointF(cw.left() + cw.width() * i / 3.0, cw.bottom()));
            p.drawLine(QPointF(cw.left(), cw.top() + cw.height() * i / 3.0),
                       QPointF(cw.right(), cw.top() + cw.height() * i / 3.0));
        }
        p.setPen(Qt::white);
        p.drawText(cw.topLeft() + QPointF(4, -6),
                   QString("%1 x %2  (Enter to crop, Esc to cancel)")
                       .arg(int(m_cropRect.width())).arg(int(m_cropRect.height())));
    }

    // transform overlay
    if (m_xf.active) {
        auto handles = xfHandlesDoc();
        QPolygonF box;
        for (int i = 0; i < 4; ++i) box << docToWidget(handles[i]);
        p.setPen(QPen(QColor(90, 160, 255), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(box);
        p.setBrush(Qt::white);
        p.setPen(QPen(Qt::black, 1));
        for (const auto& h : handles) {
            QPointF w = docToWidget(h);
            p.drawRect(QRectF(w - QPointF(4, 4), QSizeF(8, 8)));
        }
        p.setPen(Qt::white);
        p.drawText(10, height() - 12,
                   "Transform: drag=move, corners=scale (Shift=uniform), outside=rotate, Enter=apply, Esc=cancel");
    }

    // rulers + guides
    if (m_ts->showRulers)
        drawRulersAndGuides(p);

    // brush outline cursor
    if ((m_tool == ToolType::Brush || m_tool == ToolType::Eraser || m_tool == ToolType::Blur)
        && m_haveHover && !m_spaceDown) {
        p.setRenderHint(QPainter::Antialiasing, true);
        double r = m_ts->brushSize / 2.0 * m_zoom;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::black, 1));
        p.drawEllipse(m_hoverWidget, r, r);
        p.setPen(QPen(Qt::white, 1, Qt::DotLine));
        p.drawEllipse(m_hoverWidget, r, r);
        p.setPen(Qt::white);
        p.drawLine(m_hoverWidget - QPointF(3, 0), m_hoverWidget + QPointF(3, 0));
        p.drawLine(m_hoverWidget - QPointF(0, 3), m_hoverWidget + QPointF(0, 3));
    }
}

// ============================ stroke pipeline ============================

void Canvas::rebuildStamp(double radius, double flow, const QColor& color, double hardness)
{
    int size = int(std::ceil(radius * 2)) + 2;
    m_stamp = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
    m_stamp.fill(Qt::transparent);
    QPainter p(&m_stamp);
    p.setRenderHint(QPainter::Antialiasing);
    QPointF c(size / 2.0, size / 2.0);
    QRadialGradient g(c, radius);
    QColor ca = color;
    ca.setAlphaF(std::clamp(flow, 0.0, 1.0));
    QColor c0 = color;
    c0.setAlphaF(0.0);
    g.setColorAt(0.0, ca);
    g.setColorAt(std::clamp(hardness, 0.0, 0.995), ca);
    g.setColorAt(1.0, c0);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(c, radius, radius);
    p.end();

    if (m_ts->brushNoise > 0) {
        double n = m_ts->brushNoise / 100.0;
        auto* rng = QRandomGenerator::global();
        for (int y = 0; y < m_stamp.height(); ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(m_stamp.scanLine(y));
            for (int x = 0; x < m_stamp.width(); ++x) {
                if (qAlpha(line[x]) == 0) continue;
                double f = 1.0 - n * rng->generateDouble();
                line[x] = qRgba(int(qRed(line[x]) * f), int(qGreen(line[x]) * f),
                                int(qBlue(line[x]) * f), int(qAlpha(line[x]) * f));
            }
        }
    }
    m_stampRadius = radius;
    m_stampFlow = flow;
}

bool Canvas::beginStroke(const QPointF& docPos, bool erase, bool blur)
{
    auto l = m_doc->activeLayer();
    if (!l) return false;
    m_strokeOnMask = m_doc->maskEditing && l->hasMask();
    if (blur && m_strokeOnMask) m_strokeOnMask = false;   // blur always works on pixels
    if (!m_strokeOnMask) {
        if (!l->isPaintable()) {
            emit statusMessage("Cannot paint on an adjustment layer (paint on its mask instead)");
            return false;
        }
        if (l->locked) {
            emit statusMessage("Layer is locked");
            return false;
        }
    }
    m_strokeLayer = l;
    m_strokeErase = erase;
    m_strokeBlur = blur;
    m_preState = LayerState::capture(*l);
    if (!m_strokeOnMask) {
        l->ensureArea(m_doc->rect());
        m_strokeBase = l->image;
        if (blur) {
            m_blurSource = m_strokeBase;
            m_blurSource.detach();
            gaussianBlurPremult(m_blurSource, m_ts->blurRadius);
        }
    } else {
        m_strokeBaseMask = l->mask;
        m_strokeBaseMask.detach();
    }
    m_strokeBuf = QImage(m_doc->size(), QImage::Format_ARGB32_Premultiplied);
    m_strokeBuf.fill(Qt::transparent);
    m_strokeDirty = QRect();
    m_stampRadius = -1;
    m_lastStamp = docPos;
    stampAt(docPos);
    return true;
}

void Canvas::stampAt(const QPointF& docPos)
{
    double r = m_ts->brushSize / 2.0 * (m_ts->pressureSize ? m_pressure : 1.0);
    r = std::max(0.5, r);
    double flow = m_ts->brushFlow / 100.0 * (m_ts->pressureOpacity ? m_pressure : 1.0);
    QColor col = (m_strokeErase || m_strokeBlur) ? QColor(Qt::white) : m_ts->fg;
    if (m_strokeOnMask && m_strokeErase) col = Qt::black;

    if (std::abs(r - m_stampRadius) > 0.4 || std::abs(flow - m_stampFlow) > 0.03
        || m_ts->brushNoise > 0)   // regenerate grain per stamp
        rebuildStamp(r, flow, col, m_ts->brushHardness / 100.0);

    QPointF tl = docPos - QPointF(m_stamp.width() / 2.0, m_stamp.height() / 2.0);
    {
        QPainter p(&m_strokeBuf);
        p.drawImage(tl, m_stamp);
    }
    QRect sr(QPoint(int(std::floor(tl.x())) - 1, int(std::floor(tl.y())) - 1),
             m_stamp.size() + QSize(3, 3));
    sr &= m_doc->rect();
    if (sr.isEmpty()) return;
    applySelectionToBuffer(m_strokeBuf, sr);
    applyStrokeRegion(sr);
    m_strokeDirty = m_strokeDirty.isNull() ? sr : m_strokeDirty.united(sr);
}

void Canvas::strokeTo(const QPointF& docPos)
{
    if (!m_strokeLayer) return;
    double r = std::max(0.5, m_ts->brushSize / 2.0 * (m_ts->pressureSize ? m_pressure : 1.0));
    double spacing = std::max(1.0, r * 0.25);
    QPointF d = docPos - m_lastStamp;
    double dist = std::hypot(d.x(), d.y());
    while (dist >= spacing) {
        m_lastStamp += d / dist * spacing;
        stampAt(m_lastStamp);
        d = docPos - m_lastStamp;
        dist = std::hypot(d.x(), d.y());
    }
    m_doc->invalidate();
}

static void bufferToMask(QImage& mask, const QImage& baseMask, const QImage& buf,
                         double op, QRect r)
{
    r &= QRect(QPoint(0, 0), mask.size());
    r &= QRect(QPoint(0, 0), buf.size());
    if (r.isEmpty()) return;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        uchar* m = mask.scanLine(y);
        const uchar* bm = baseMask.constScanLine(y);
        const QRgb* b = reinterpret_cast<const QRgb*>(buf.constScanLine(y));
        for (int x = r.left(); x <= r.right(); ++x) {
            int a255 = qAlpha(b[x]);
            if (a255 == 0) { m[x] = bm[x]; continue; }
            double a = a255 / 255.0 * op;
            QRgb u = qUnpremultiply(b[x]);
            int gray = qGray(u);
            m[x] = uchar(std::clamp(bm[x] * (1.0 - a) + gray * a, 0.0, 255.0));
        }
    }
}

void Canvas::applyStrokeRegion(QRect r)
{
    auto l = m_strokeLayer;
    if (!l) return;
    r &= m_doc->rect();
    if (r.isEmpty()) return;

    double op = m_ts->brushOpacity / 100.0;
    if (m_strokeOnMask) {
        bufferToMask(l->mask, m_strokeBaseMask, m_strokeBuf, op, r);
        l->maskEdited();
    } else {
        QRect rl = r.translated(-l->offset);
        QPainter p(&l->image);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(rl.topLeft(), m_strokeBase, rl);
        if (m_strokeBlur) {
            // paint the pre-blurred layer through the stroke's alpha
            QImage part = m_blurSource.copy(rl);
            QPainter bp(&part);
            bp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            bp.drawImage(QPoint(0, 0), m_strokeBuf, r);
            bp.end();
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.setOpacity(op);
            p.drawImage(rl.topLeft(), part);
        } else {
            p.setCompositionMode(m_strokeErase ? QPainter::CompositionMode_DestinationOut
                                               : QPainter::CompositionMode_SourceOver);
            p.setOpacity(op);
            p.drawImage(rl.topLeft(), m_strokeBuf, r);
        }
    }
}

void Canvas::endStroke()
{
    if (!m_strokeLayer) return;
    m_doc->undo.push(new LayerEditCommand(m_doc, m_strokeLayer, m_preState,
                                          m_strokeBlur ? "Blur" : m_strokeErase ? "Eraser" : "Brush",
                                          m_strokeDirty));
    m_strokeLayer.reset();
    m_strokeBuf = QImage();
    m_strokeBase = QImage();
    m_strokeBaseMask = QImage();
    m_blurSource = QImage();
    m_doc->invalidate();
}

void Canvas::applySelectionToBuffer(QImage& buf, const QRect& r)
{
    if (!m_doc->hasSelection()) return;
    QPainter p(&buf);
    p.setClipRect(r);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.drawImage(0, 0, m_doc->selectionMaskAlpha());
}

void Canvas::commitBuffer(QImage& buf, double opacity, bool erase, QRect dirty, const QString& name)
{
    auto l = m_doc->activeLayer();
    if (!l) return;
    bool onMask = m_doc->maskEditing && l->hasMask();
    if (!onMask) {
        if (!l->isPaintable()) { emit statusMessage("Cannot paint on an adjustment layer"); return; }
        if (l->locked) { emit statusMessage("Layer is locked"); return; }
    }
    dirty &= m_doc->rect();
    if (dirty.isEmpty()) return;

    LayerState pre = LayerState::capture(*l);
    applySelectionToBuffer(buf, dirty);

    if (onMask) {
        bufferToMask(l->mask, l->mask, buf, opacity, dirty);
        l->maskEdited();
    } else {
        l->ensureArea(dirty);
        QPainter p(&l->image);
        p.setCompositionMode(erase ? QPainter::CompositionMode_DestinationOut
                                   : QPainter::CompositionMode_SourceOver);
        p.setOpacity(opacity);
        p.drawImage(dirty.topLeft() - l->offset, buf, dirty);
    }
    m_doc->undo.push(new LayerEditCommand(m_doc, l, pre, name, dirty));
    m_doc->invalidate();
}

void Canvas::fillWith(const QColor& c)
{
    QImage buf(m_doc->size(), QImage::Format_ARGB32_Premultiplied);
    buf.fill(c);
    QRect dirty = m_doc->hasSelection()
        ? m_doc->selection.boundingRect().toAlignedRect().adjusted(-2, -2, 2, 2)
        : m_doc->rect();
    commitBuffer(buf, 1.0, false, dirty, "Fill");
}

void Canvas::clearSelectionArea()
{
    QImage buf(m_doc->size(), QImage::Format_ARGB32_Premultiplied);
    buf.fill(Qt::white);
    QRect dirty = m_doc->hasSelection()
        ? m_doc->selection.boundingRect().toAlignedRect().adjusted(-2, -2, 2, 2)
        : m_doc->rect();
    commitBuffer(buf, 1.0, true, dirty, "Clear");
}

// ============================ selection ============================

void Canvas::finishSelection(const QPainterPath& newShape, const QString& name)
{
    QPainterPath before = m_doc->selection;
    double beforeFeather = m_doc->feather;
    QPainterPath after;
    switch (m_selCombine) {
    case SelCombine::Add:      after = before.united(newShape); break;
    case SelCombine::Subtract: after = before.subtracted(newShape); break;
    default:                   after = newShape; break;
    }
    after.setFillRule(Qt::WindingFill);
    m_doc->undo.push(new SelectionCommand(m_doc, before, beforeFeather, after, beforeFeather, name));
}

void Canvas::wandSelect(const QPoint& docPos)
{
    if (!m_doc->rect().contains(docPos)) return;
    QImage src = m_doc->composite().convertToFormat(QImage::Format_ARGB32);
    int w = src.width(), h = src.height();
    QRgb seed = reinterpret_cast<const QRgb*>(src.constScanLine(docPos.y()))[docPos.x()];
    int tol = m_ts->wandTolerance;

    auto match = [&](QRgb c) {
        return std::abs(qRed(c) - qRed(seed)) <= tol
            && std::abs(qGreen(c) - qGreen(seed)) <= tol
            && std::abs(qBlue(c) - qBlue(seed)) <= tol
            && std::abs(qAlpha(c) - qAlpha(seed)) <= tol;
    };

    QImage mark(w, h, QImage::Format_Grayscale8);
    mark.fill(0);

    if (m_ts->wandContiguous) {
        std::vector<QPoint> stack;
        stack.push_back(docPos);
        mark.scanLine(docPos.y())[docPos.x()] = 255;
        while (!stack.empty()) {
            QPoint pt = stack.back();
            stack.pop_back();
            static const QPoint dirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& d : dirs) {
                QPoint n = pt + d;
                if (n.x() < 0 || n.y() < 0 || n.x() >= w || n.y() >= h) continue;
                uchar* mrow = mark.scanLine(n.y());
                if (mrow[n.x()]) continue;
                QRgb c = reinterpret_cast<const QRgb*>(src.constScanLine(n.y()))[n.x()];
                if (match(c)) {
                    mrow[n.x()] = 255;
                    stack.push_back(n);
                }
            }
        }
    } else {
        for (int y = 0; y < h; ++y) {
            const QRgb* srow = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            uchar* mrow = mark.scanLine(y);
            for (int x = 0; x < w; ++x)
                if (match(srow[x])) mrow[x] = 255;
        }
    }

    // build path from horizontal runs, then let simplified() fuse them into an outline
    QPainterPath path;
    for (int y = 0; y < h; ++y) {
        const uchar* mrow = mark.constScanLine(y);
        int x = 0;
        while (x < w) {
            if (mrow[x]) {
                int x0 = x;
                while (x < w && mrow[x]) ++x;
                path.addRect(QRect(x0, y, x - x0, 1));
            } else {
                ++x;
            }
        }
    }
    if (!path.isEmpty()) path = path.simplified();
    finishSelection(path, "Magic Wand");
}

// ============================ text tool ============================

void Canvas::textToolClick(const QPointF& docPos)
{
    // edit existing text layer under cursor (topmost first)
    for (int i = m_doc->layers.size() - 1; i >= 0; --i) {
        auto l = m_doc->layers[i];
        if (l->type == Layer::Text && l->visible && l->rect().contains(docPos.toPoint())) {
            TextDialog dlg(l->text, l->font, l->color, this);
            if (dlg.exec() == QDialog::Accepted && !dlg.text().isEmpty()) {
                LayerState pre = LayerState::capture(*l);
                l->text = dlg.text();
                l->font = dlg.font();
                l->color = dlg.color();
                l->renderText();
                m_doc->activeIndex = i;
                m_doc->undo.push(new LayerEditCommand(m_doc, l, pre, "Edit Text"));
                m_doc->notifyStructure();
            }
            return;
        }
    }
    // new text layer
    QFont f(m_ts->fontFamily, m_ts->fontSize);
    TextDialog dlg(QString(), f, m_ts->fg, this);
    if (dlg.exec() == QDialog::Accepted && !dlg.text().isEmpty()) {
        auto l = Layer::makeText(dlg.text(), dlg.font(), dlg.color(), docPos.toPoint());
        m_ts->fontFamily = dlg.font().family();
        m_ts->fontSize = dlg.font().pointSize();
        m_doc->undo.push(new AddLayerCommand(m_doc, l, m_doc->activeIndex + 1, "Add Text"));
    }
}

void Canvas::pickColor(const QPointF& docPos)
{
    QPoint pt = docPos.toPoint();
    if (!m_doc->rect().contains(pt)) return;
    QRgb c = m_doc->composite().convertToFormat(QImage::Format_ARGB32).pixel(pt);
    emit colorPicked(QColor(qRed(c), qGreen(c), qBlue(c)));
}

// ============================ transform ============================

QTransform Canvas::xfMatrix() const
{
    QTransform t;
    t.translate(m_xf.center.x() + m_xf.trans.x(), m_xf.center.y() + m_xf.trans.y());
    t.rotate(m_xf.angle);
    t.scale(m_xf.sx, m_xf.sy);
    t.translate(-m_xf.center.x(), -m_xf.center.y());
    return t;
}

QVector<QPointF> Canvas::xfHandlesDoc() const
{
    QRectF r(m_xf.srcOffset, QSizeF(m_xf.srcImage.size()));
    QTransform m = xfMatrix();
    QVector<QPointF> pts;
    pts << m.map(r.topLeft()) << m.map(r.topRight())
        << m.map(r.bottomRight()) << m.map(r.bottomLeft());
    pts << m.map(QPointF(r.center().x(), r.top()))
        << m.map(QPointF(r.right(), r.center().y()))
        << m.map(QPointF(r.center().x(), r.bottom()))
        << m.map(QPointF(r.left(), r.center().y()));
    return pts;
}

int Canvas::hitHandle(const QPointF& widgetPos) const
{
    auto handles = xfHandlesDoc();
    for (int i = 0; i < handles.size(); ++i)
        if (QLineF(docToWidget(handles[i]), widgetPos).length() < 10.0)
            return i;
    return -1;
}

void Canvas::startTransform()
{
    if (m_xf.active) return;
    auto l = m_doc->activeLayer();
    if (!l || !l->isPaintable() || l->image.isNull()) {
        emit statusMessage("Select a raster or text layer to transform");
        return;
    }
    if (l->locked) { emit statusMessage("Layer is locked"); return; }
    m_xf = XForm();
    m_xf.active = true;
    m_xf.layer = l;
    m_xf.srcImage = l->image;
    m_xf.srcOffset = l->offset;
    m_xf.preState = LayerState::capture(*l);
    m_xf.center = QRectF(l->offset, QSizeF(l->image.size())).center();
    emit statusMessage("Transform: drag to move, corners to scale, outside to rotate. Enter=apply, Esc=cancel");
    update();
}

void Canvas::commitTransform()
{
    if (!m_xf.active) return;
    QTransform m = xfMatrix();
    if (m.isIdentity()) {
        m_xf.active = false;
        update();
        return;
    }
    QRectF b = m.mapRect(QRectF(m_xf.srcOffset, QSizeF(m_xf.srcImage.size())));
    QSize ns(std::max(1, int(std::ceil(b.width()))), std::max(1, int(std::ceil(b.height()))));
    QImage out(ns, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(-b.topLeft());
    p.setWorldTransform(m, true);
    p.drawImage(m_xf.srcOffset, m_xf.srcImage);
    p.end();

    auto l = m_xf.layer;
    l->image = out;
    l->offset = QPoint(int(std::floor(b.left())), int(std::floor(b.top())));
    m_doc->undo.push(new LayerEditCommand(m_doc, l, m_xf.preState, "Transform"));
    m_xf.active = false;
    m_doc->invalidate();
    m_doc->notifyStructure();
}

void Canvas::cancelTransform()
{
    if (!m_xf.active) return;
    m_xf.preState.apply(*m_xf.layer);
    m_xf.active = false;
    m_doc->invalidate();
    update();
}

// ============================ rulers / guides / snapping ============================

QList<double> Canvas::snapTargetsX() const
{
    QList<double> t = m_doc->guidesV;
    t << 0 << m_doc->width() << m_doc->width() / 2.0;
    return t;
}

QList<double> Canvas::snapTargetsY() const
{
    QList<double> t = m_doc->guidesH;
    t << 0 << m_doc->height() << m_doc->height() / 2.0;
    return t;
}

double Canvas::snap1D(double v, const QList<double>& targets, double tol) const
{
    double best = v, bd = tol;
    for (double t : targets) {
        double d = std::abs(t - v);
        if (d < bd) { bd = d; best = t; }
    }
    return best;
}

QPointF Canvas::snapPoint(const QPointF& p) const
{
    if (!m_ts->snapping) return p;
    double tol = 8.0 / m_zoom;
    return QPointF(snap1D(p.x(), snapTargetsX(), tol), snap1D(p.y(), snapTargetsY(), tol));
}

QPoint Canvas::snapMoveOffset(QPoint offset, const QSize& size) const
{
    if (!m_ts->snapping) return offset;
    double tol = 8.0 / m_zoom;
    auto bestDelta = [&](double e0, double e1, double ec, const QList<double>& targets) {
        double best = tol;
        double delta = 0;
        for (double edge : {e0, e1, ec}) {
            for (double t : targets) {
                double d = t - edge;
                if (std::abs(d) < std::abs(best)) { best = std::abs(d); delta = d; }
            }
        }
        return delta;
    };
    double dx = bestDelta(offset.x(), offset.x() + size.width(),
                          offset.x() + size.width() / 2.0, snapTargetsX());
    double dy = bestDelta(offset.y(), offset.y() + size.height(),
                          offset.y() + size.height() / 2.0, snapTargetsY());
    return offset + QPoint(int(std::lround(dx)), int(std::lround(dy)));
}

void Canvas::drawRulersAndGuides(QPainter& p)
{
    // guides
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QColor(0, 200, 220));
    for (double g : m_doc->guidesV) {
        int x = int(std::lround(docToWidget(QPointF(g, 0)).x()));
        p.drawLine(x, 0, x, height());
    }
    for (double g : m_doc->guidesH) {
        int y = int(std::lround(docToWidget(QPointF(0, g)).y()));
        p.drawLine(0, y, width(), y);
    }
    if (m_act == Act::GuideDrag) {
        p.setPen(QPen(QColor(80, 230, 255), 2));
        if (m_guideOrient == 0) {
            int y = int(std::lround(docToWidget(QPointF(0, m_guideVal)).y()));
            p.drawLine(0, y, width(), y);
        } else {
            int x = int(std::lround(docToWidget(QPointF(m_guideVal, 0)).x()));
            p.drawLine(x, 0, x, height());
        }
    }

    // ruler strips
    QColor strip(28, 28, 31), tick(120, 120, 125), label(160, 160, 165);
    p.fillRect(QRect(0, 0, width(), kRuler), strip);
    p.fillRect(QRect(0, 0, kRuler, height()), strip);
    p.setPen(QColor(60, 60, 65));
    p.drawLine(0, kRuler, width(), kRuler);
    p.drawLine(kRuler, 0, kRuler, height());

    static const double steps[] = {1, 2, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000};
    double step = 10000;
    for (double s : steps)
        if (s * m_zoom >= 55) { step = s; break; }

    QFont f = p.font();
    f.setPixelSize(9);
    p.setFont(f);

    // horizontal ruler
    double x0 = widgetToDoc(QPointF(kRuler, 0)).x();
    double x1 = widgetToDoc(QPointF(width(), 0)).x();
    for (double v = std::floor(x0 / step) * step; v <= x1; v += step) {
        int wx = int(std::lround(docToWidget(QPointF(v, 0)).x()));
        if (wx < kRuler) continue;
        p.setPen(tick);
        p.drawLine(wx, kRuler - 7, wx, kRuler);
        p.setPen(label);
        p.drawText(wx + 3, kRuler - 9, QString::number(v));
        int half = int(std::lround(docToWidget(QPointF(v + step / 2, 0)).x()));
        p.setPen(tick);
        if (half >= kRuler) p.drawLine(half, kRuler - 4, half, kRuler);
    }
    // vertical ruler
    double y0 = widgetToDoc(QPointF(0, kRuler)).y();
    double y1 = widgetToDoc(QPointF(0, height())).y();
    for (double v = std::floor(y0 / step) * step; v <= y1; v += step) {
        int wy = int(std::lround(docToWidget(QPointF(0, v)).y()));
        if (wy < kRuler) continue;
        p.setPen(tick);
        p.drawLine(kRuler - 7, wy, kRuler, wy);
        p.save();
        p.setPen(label);
        p.translate(kRuler - 10, wy + 3);
        p.rotate(90);
        p.drawText(0, 0, QString::number(v));
        p.restore();
        int half = int(std::lround(docToWidget(QPointF(0, v + step / 2)).y()));
        p.setPen(tick);
        if (half >= kRuler) p.drawLine(kRuler - 4, half, kRuler, half);
    }
    p.fillRect(QRect(0, 0, kRuler, kRuler), strip);
}

// ============================ events ============================

void Canvas::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    QPointF wp = e->position();
    QPointF dp = widgetToDoc(wp);
    m_lastWidgetPos = wp;
    m_dragStartDoc = dp;
    m_dragCurDoc = dp;

    if (e->button() == Qt::MiddleButton || m_spaceDown || m_tool == ToolType::Hand) {
        m_act = Act::PanView;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    // rulers: drag out a new guide
    if (m_ts->showRulers && !m_xf.active) {
        bool inTop = wp.y() < kRuler, inLeft = wp.x() < kRuler;
        if (inTop || inLeft) {
            m_act = Act::GuideDrag;
            m_guideOrient = (inLeft && !inTop) ? 1 : 0;
            if (inTop && inLeft) m_guideOrient = 0;
            m_guideIndex = -1;
            m_guideVal = m_guideOrient == 0 ? dp.y() : dp.x();
            update();
            return;
        }
        // move tool grabs existing guides
        if (m_tool == ToolType::Move) {
            double tol = 5.0 / m_zoom;
            for (int i = 0; i < m_doc->guidesH.size(); ++i) {
                if (std::abs(dp.y() - m_doc->guidesH[i]) < tol) {
                    m_act = Act::GuideDrag;
                    m_guideOrient = 0;
                    m_guideIndex = i;
                    m_guideVal = m_doc->guidesH[i];
                    return;
                }
            }
            for (int i = 0; i < m_doc->guidesV.size(); ++i) {
                if (std::abs(dp.x() - m_doc->guidesV[i]) < tol) {
                    m_act = Act::GuideDrag;
                    m_guideOrient = 1;
                    m_guideIndex = i;
                    m_guideVal = m_doc->guidesV[i];
                    return;
                }
            }
        }
    }

    // transform mode captures all clicks
    if (m_xf.active) {
        int h = hitHandle(wp);
        m_xf.startAngle = m_xf.angle;
        m_xf.startSx = m_xf.sx;
        m_xf.startSy = m_xf.sy;
        m_xf.startTrans = m_xf.trans;
        QPointF pivot = m_xf.center + m_xf.trans;
        if (h >= 0) {
            m_xf.dragMode = 2;
            m_xf.handleIdx = h;
        } else {
            QRectF r(m_xf.srcOffset, QSizeF(m_xf.srcImage.size()));
            QPolygonF quad = xfMatrix().map(QPolygonF(QVector<QPointF>{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()}));
            if (quad.containsPoint(dp, Qt::OddEvenFill)) {
                m_xf.dragMode = 1;
            } else {
                m_xf.dragMode = 3;
                m_xf.grabAngle = qRadiansToDegrees(std::atan2(dp.y() - pivot.y(), dp.x() - pivot.x()));
            }
        }
        m_act = Act::XformDrag;
        return;
    }

    m_selCombine = SelCombine::Replace;
    if (e->modifiers() & Qt::ShiftModifier) m_selCombine = SelCombine::Add;
    else if (e->modifiers() & Qt::AltModifier) m_selCombine = SelCombine::Subtract;

    switch (m_tool) {
    case ToolType::Move: {
        auto l = m_doc->activeLayer();
        if (l && !l->locked && !l->image.isNull()) {
            // only grab the layer if the cursor is on one of its actual pixels
            QPoint pl = dp.toPoint() - l->offset;
            bool hit = l->image.rect().contains(pl) && qAlpha(l->image.pixel(pl)) > 8;
            if (hit) {
                m_moveStartOffset = l->offset;
                m_preState = LayerState::capture(*l);
                m_act = Act::MoveLayer;
            } else {
                emit statusMessage("Click on the layer's pixels to move it");
            }
        }
        break;
    }
    case ToolType::Brush:
    case ToolType::Eraser:
    case ToolType::Blur:
        if (e->modifiers() & Qt::AltModifier) {
            pickColor(dp);
            break;
        }
        if (beginStroke(dp, m_tool == ToolType::Eraser, m_tool == ToolType::Blur)) {
            m_act = Act::Stroke;
            m_doc->invalidate();
        }
        break;
    case ToolType::MarqueeRect:
    case ToolType::MarqueeEllipse:
        m_dragStartDoc = m_dragCurDoc = snapPoint(dp);
        m_act = Act::Marquee;
        break;
    case ToolType::Lasso:
        m_lassoPts.clear();
        m_lassoPts << dp;
        m_act = Act::Lasso;
        break;
    case ToolType::Wand:
        wandSelect(dp.toPoint());
        break;
    case ToolType::Crop:
        m_dragStartDoc = m_dragCurDoc = snapPoint(dp);
        m_act = Act::CropDrag;
        m_cropValid = false;
        break;
    case ToolType::Eyedropper:
        pickColor(dp);
        break;
    case ToolType::Gradient:
        m_act = Act::GradDrag;
        break;
    case ToolType::ShapeRect:
    case ToolType::ShapeEllipse:
    case ToolType::ShapeLine:
        m_dragStartDoc = m_dragCurDoc = snapPoint(dp);
        m_act = Act::ShapeDrag;
        break;
    case ToolType::Text:
        textToolClick(dp);
        break;
    case ToolType::Zoom:
        zoomAt(wp, (e->modifiers() & Qt::AltModifier) ? 1.0 / 1.5 : 1.5);
        break;
    default:
        break;
    }
    update();
}

void Canvas::mouseMoveEvent(QMouseEvent* e)
{
    QPointF wp = e->position();
    QPointF dp = widgetToDoc(wp);
    m_haveHover = true;
    m_hoverWidget = wp;
    emit cursorMoved(dp);

    switch (m_act) {
    case Act::PanView:
        m_pan += wp - m_lastWidgetPos;
        break;
    case Act::Stroke:
        strokeTo(dp);
        break;
    case Act::MoveLayer: {
        auto l = m_doc->activeLayer();
        if (l) {
            QPoint off = m_moveStartOffset + (dp - m_dragStartDoc).toPoint();
            l->offset = snapMoveOffset(off, l->image.size());
            m_doc->invalidate();
        }
        break;
    }
    case Act::GuideDrag:
        m_guideVal = m_guideOrient == 0 ? dp.y() : dp.x();
        break;
    case Act::Marquee:
    case Act::ShapeDrag:
    case Act::GradDrag:
        m_dragCurDoc = snapPoint(dp);
        if (m_act == Act::Marquee && (e->modifiers() & Qt::ControlModifier)) {
            // ctrl = square/circle constraint
            QPointF d = dp - m_dragStartDoc;
            double s = std::max(std::abs(d.x()), std::abs(d.y()));
            m_dragCurDoc = m_dragStartDoc + QPointF(d.x() < 0 ? -s : s, d.y() < 0 ? -s : s);
        }
        break;
    case Act::Lasso:
        if (QLineF(docToWidget(m_lassoPts.last()), wp).length() > 2.0)
            m_lassoPts << dp;
        break;
    case Act::CropDrag:
        m_dragCurDoc = snapPoint(dp);
        m_cropRect = QRectF(m_dragStartDoc, m_dragCurDoc).normalized()
                         .intersected(QRectF(m_doc->rect()));
        m_cropValid = m_cropRect.width() > 2 && m_cropRect.height() > 2;
        break;
    case Act::XformDrag: {
        QPointF pivot = m_xf.center + m_xf.startTrans;
        if (m_xf.dragMode == 1) {
            m_xf.trans = m_xf.startTrans + (dp - m_dragStartDoc);
        } else if (m_xf.dragMode == 3) {
            double a = qRadiansToDegrees(std::atan2(dp.y() - pivot.y(), dp.x() - pivot.x()));
            double na = m_xf.startAngle + (a - m_xf.grabAngle);
            if (e->modifiers() & Qt::ShiftModifier) na = std::round(na / 15.0) * 15.0;
            m_xf.angle = na;
        } else if (m_xf.dragMode == 2) {
            QRectF r(m_xf.srcOffset, QSizeF(m_xf.srcImage.size()));
            static const auto srcPt = [](const QRectF& r, int i) -> QPointF {
                switch (i) {
                case 0: return r.topLeft();
                case 1: return r.topRight();
                case 2: return r.bottomRight();
                case 3: return r.bottomLeft();
                case 4: return {r.center().x(), r.top()};
                case 5: return {r.right(), r.center().y()};
                case 6: return {r.center().x(), r.bottom()};
                default: return {r.left(), r.center().y()};
                }
            };
            QPointF o = srcPt(r, m_xf.handleIdx) - r.center();
            // cursor in unrotated frame around pivot
            QTransform rot;
            rot.rotate(-m_xf.angle);
            QPointF v = rot.map(dp - pivot);
            double nsx = m_xf.startSx, nsy = m_xf.startSy;
            if (std::abs(o.x()) > 0.01) nsx = v.x() / o.x();
            if (std::abs(o.y()) > 0.01) nsy = v.y() / o.y();
            if (m_xf.handleIdx < 4 && (e->modifiers() & Qt::ShiftModifier)) {
                double lo = std::hypot(o.x(), o.y());
                double lv = std::hypot(v.x(), v.y());
                double s = lo > 0.01 ? lv / lo : 1.0;
                nsx = nsx < 0 ? -s : s;
                nsy = nsy < 0 ? -s : s;
            }
            auto clampS = [](double s) {
                if (std::abs(s) < 0.01) return s < 0 ? -0.01 : 0.01;
                return s;
            };
            m_xf.sx = clampS(nsx);
            m_xf.sy = clampS(nsy);
        }
        break;
    }
    default:
        break;
    }
    m_lastWidgetPos = wp;
    update();
}

void Canvas::mouseReleaseEvent(QMouseEvent* e)
{
    QPointF dp = widgetToDoc(e->position());

    switch (m_act) {
    case Act::PanView:
        updateCursor();
        break;
    case Act::Stroke:
        endStroke();
        break;
    case Act::MoveLayer: {
        auto l = m_doc->activeLayer();
        if (l && l->offset != m_moveStartOffset)
            m_doc->undo.push(new LayerEditCommand(m_doc, l, m_preState, "Move Layer"));
        break;
    }
    case Act::Marquee: {
        QRectF r(m_dragStartDoc, m_dragCurDoc);
        r = r.normalized();
        if (r.width() < 2 && r.height() < 2) {
            if (m_selCombine == SelCombine::Replace && m_doc->hasSelection())
                finishSelection(QPainterPath(), "Deselect");
        } else {
            QPainterPath path;
            if (m_tool == ToolType::MarqueeEllipse) path.addEllipse(r);
            else path.addRect(r);
            finishSelection(path, "Select");
        }
        break;
    }
    case Act::Lasso: {
        if (m_lassoPts.size() >= 3) {
            QPainterPath path;
            path.addPolygon(m_lassoPts);
            path.closeSubpath();
            finishSelection(path, "Lasso Select");
        } else if (m_selCombine == SelCombine::Replace && m_doc->hasSelection()) {
            finishSelection(QPainterPath(), "Deselect");
        }
        m_lassoPts.clear();
        break;
    }
    case Act::ShapeDrag: {
        QPointF end = m_dragCurDoc;
        QRectF r = QRectF(m_dragStartDoc, end).normalized();
        QImage buf(m_doc->size(), QImage::Format_ARGB32_Premultiplied);
        buf.fill(Qt::transparent);
        QPainter p(&buf);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(m_ts->bg, m_ts->shapeStrokeWidth);
        if (m_tool == ToolType::ShapeLine) {
            p.setPen(QPen(m_ts->fg, std::max(1, m_ts->shapeStrokeWidth)));
            p.drawLine(m_dragStartDoc, end);
        } else {
            p.setPen(m_ts->shapeStroke ? pen : QPen(Qt::NoPen));
            p.setBrush(m_ts->shapeFill ? QBrush(m_ts->fg) : QBrush(Qt::NoBrush));
            if (m_tool == ToolType::ShapeRect) p.drawRect(r);
            else p.drawEllipse(r);
        }
        p.end();
        int pad = m_ts->shapeStrokeWidth + 3;
        QRect dirty = r.toAlignedRect().adjusted(-pad, -pad, pad, pad);
        commitBuffer(buf, 1.0, false, dirty, "Shape");
        break;
    }
    case Act::GradDrag: {
        if (QLineF(m_dragStartDoc, m_dragCurDoc).length() > 2) {
            QImage buf(m_doc->size(), QImage::Format_ARGB32_Premultiplied);
            buf.fill(Qt::transparent);
            QLinearGradient g(m_dragStartDoc, m_dragCurDoc);
            g.setColorAt(0, m_ts->fg);
            if (m_ts->gradientMode == 1) {
                QColor end = m_ts->fg;
                end.setAlpha(0);
                g.setColorAt(1, end);
            } else {
                g.setColorAt(1, m_ts->bg);
            }
            QPainter p(&buf);
            p.fillRect(buf.rect(), g);
            p.end();
            commitBuffer(buf, 1.0, false, m_doc->rect(), "Gradient");
        }
        break;
    }
    case Act::CropDrag:
        break;
    case Act::GuideDrag: {
        QPointF wp = e->position();
        bool onRuler = m_guideOrient == 0 ? wp.y() < kRuler : wp.x() < kRuler;
        double val = std::round(m_guideOrient == 0 ? dp.y() : dp.x());
        bool inDoc = m_guideOrient == 0 ? (val >= 0 && val <= m_doc->height())
                                        : (val >= 0 && val <= m_doc->width());
        auto& list = m_guideOrient == 0 ? m_doc->guidesH : m_doc->guidesV;
        if (m_guideIndex >= 0 && m_guideIndex < list.size())
            list.removeAt(m_guideIndex);
        if (!onRuler && inDoc)
            list.append(val);
        m_guideIndex = -1;
        break;
    }
    case Act::XformDrag:
        m_xf.dragMode = 0;
        break;
    default:
        break;
    }
    m_act = Act::None;
    update();
}

void Canvas::mouseDoubleClickEvent(QMouseEvent*)
{
    if (m_xf.active) commitTransform();
    else if (m_tool == ToolType::Crop && m_cropValid) {
        DocState before = DocState::capture(m_doc);
        m_doc->cropTo(m_cropRect.toAlignedRect());
        m_doc->undo.push(new SnapshotCommand(m_doc, before, "Crop"));
        m_cropValid = false;
        fitToWindow();
    }
}

void Canvas::wheelEvent(QWheelEvent* e)
{
    double steps = e->angleDelta().y() / 120.0;
    if (steps != 0.0)
        zoomAt(e->position(), std::pow(1.25, steps));
}

void Canvas::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceDown = true;
        setCursor(Qt::OpenHandCursor);
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (m_xf.active) { commitTransform(); return; }
        if (m_tool == ToolType::Crop && m_cropValid) {
            DocState before = DocState::capture(m_doc);
            m_doc->cropTo(m_cropRect.toAlignedRect());
            m_doc->undo.push(new SnapshotCommand(m_doc, before, "Crop"));
            m_cropValid = false;
            fitToWindow();
            return;
        }
    }
    if (e->key() == Qt::Key_Escape) {
        if (m_xf.active) { cancelTransform(); return; }
        if (m_cropValid) { m_cropValid = false; update(); return; }
    }
    // fallback so Delete always clears the selection when the canvas has focus
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)
        && e->modifiers() == Qt::NoModifier && m_doc->hasSelection() && !m_xf.active) {
        clearSelectionArea();
        return;
    }
    QWidget::keyPressEvent(e);
}

void Canvas::keyReleaseEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceDown = false;
        updateCursor();
        return;
    }
    QWidget::keyReleaseEvent(e);
}

void Canvas::tabletEvent(QTabletEvent* e)
{
    // Only track pressure; let Qt synthesize mouse events for the actual strokes.
    m_pressure = std::clamp(double(e->pressure()), 0.05, 1.0);
    if (e->type() == QEvent::TabletRelease) m_pressure = 1.0;
    e->ignore();
}
