#pragma once
#include "Document.h"
#include "Common.h"
#include <QWidget>
#include <QTimer>
#include <QPolygonF>
#include <memory>

class Canvas : public QWidget
{
    Q_OBJECT
public:
    Canvas(Document* doc, ToolSettings* ts, QWidget* parent = nullptr);

    Document* document() const { return m_doc; }
    void setTool(ToolType t);
    ToolType tool() const { return m_tool; }

    double zoom() const { return m_zoom; }
    void zoomAt(const QPointF& widgetPos, double factor);
    void zoomIn()  { zoomAt(rect().center(), 1.25); }
    void zoomOut() { zoomAt(rect().center(), 0.8); }
    void zoomActual();
    void fitToWindow();

    void startTransform();               // Ctrl+T
    bool inTransform() const { return m_xf.active; }
    void commitTransform();
    void cancelTransform();

    // Edit menu helpers
    void fillWith(const QColor& c);
    void clearSelectionArea();

signals:
    void colorPicked(const QColor& c);
    void zoomChanged(double z);
    void cursorMoved(const QPointF& docPos);
    void statusMessage(const QString& msg);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void tabletEvent(QTabletEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    enum class Act { None, Stroke, Marquee, Lasso, ShapeDrag, GradDrag, MoveLayer, PanView, CropDrag, XformDrag };
    enum class SelCombine { Replace, Add, Subtract };

    QPointF docToWidget(const QPointF& p) const { return p * m_zoom + m_pan; }
    QPointF widgetToDoc(const QPointF& p) const { return (p - m_pan) / m_zoom; }
    QTransform viewTransform() const;

    // stroke pipeline
    bool beginStroke(const QPointF& docPos, bool erase);
    void strokeTo(const QPointF& docPos);
    void endStroke();
    void stampAt(const QPointF& docPos);
    void applyStrokeRegion(QRect r);
    void rebuildStamp(double radius, double flow, const QColor& color, double hardness);
    void applySelectionToBuffer(QImage& buf, const QRect& r);
    void commitBuffer(QImage& buf, double opacity, bool erase, QRect dirty, const QString& name);

    // selection tools
    void finishSelection(const QPainterPath& newShape, const QString& name);
    void wandSelect(const QPoint& docPos);

    // text
    void textToolClick(const QPointF& docPos);

    void pickColor(const QPointF& docPos);
    void updateCursor();

    // transform helpers
    QTransform xfMatrix() const;
    QVector<QPointF> xfHandlesDoc() const;   // 4 corners + 4 edge mids of source rect, mapped
    int hitHandle(const QPointF& widgetPos) const;

    Document* m_doc;
    ToolSettings* m_ts;

    double m_zoom = 1.0;
    QPointF m_pan{0, 0};
    bool m_fitted = false;

    ToolType m_tool = ToolType::Move;
    Act m_act = Act::None;
    SelCombine m_selCombine = SelCombine::Replace;

    bool m_spaceDown = false;
    double m_pressure = 1.0;
    bool m_haveHover = false;
    QPointF m_hoverWidget;
    QPointF m_lastWidgetPos;
    QPointF m_dragStartDoc, m_dragCurDoc;

    // stroke state
    std::shared_ptr<Layer> m_strokeLayer;
    bool m_strokeErase = false;
    bool m_strokeOnMask = false;
    LayerState m_preState;
    QImage m_strokeBuf, m_strokeBase, m_strokeBaseMask;
    QImage m_stamp;
    double m_stampRadius = -1, m_stampFlow = -1;
    QRect m_strokeDirty;
    QPointF m_lastStamp;

    // move-layer state
    QPoint m_moveStartOffset;

    // lasso
    QPolygonF m_lassoPts;

    // crop
    QRectF m_cropRect;
    bool m_cropValid = false;

    // transform state
    struct XForm {
        bool active = false;
        std::shared_ptr<Layer> layer;
        QImage srcImage;
        QPoint srcOffset;
        LayerState preState;
        QPointF center;
        double angle = 0, sx = 1, sy = 1;
        QPointF trans;
        int dragMode = 0;       // 1=move 2=scale 3=rotate
        int handleIdx = -1;
        double startAngle = 0, startSx = 1, startSy = 1, grabAngle = 0;
        QPointF startTrans;
    } m_xf;

    QTimer m_antsTimer;
    int m_antsOffset = 0;
};
