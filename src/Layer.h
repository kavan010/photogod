#pragma once
#include "Common.h"
#include "Filters.h"
#include <QImage>
#include <QFont>
#include <memory>

class Layer
{
public:
    enum Type { Raster = 0, Text = 1, Adjustment = 2 };

    Type    type = Raster;
    QString name = "Layer";
    bool    visible = true;
    bool    locked  = false;
    double  opacity = 1.0;
    BlendMode blend = BlendMode::Normal;

    QImage image;             // ARGB32_Premultiplied (empty for Adjustment)
    QPoint offset{0, 0};      // position of image in document coords

    QImage mask;              // Grayscale8, document-sized; null = no mask

    // Text layer
    QString text;
    QFont   font{"Sans Serif", 48};
    QColor  color = Qt::black;

    // Adjustment layer
    FilterType   filter = FilterType::Brightness;
    FilterParams params;

    QRect rect() const { return QRect(offset, image.size()); }
    bool  hasMask() const { return !mask.isNull(); }
    bool  isPaintable() const { return type == Raster || type == Text; }

    // Grow image so that (doc-coords) rect r lies inside it.
    void ensureArea(const QRect& r);

    // Re-render text layer image from text/font/color. Keeps offset.
    void renderText();

    // Cached mask converted to an alpha image for DestinationIn compositing.
    const QImage& maskAlpha() const;
    void maskEdited() { m_maskAlphaDirty = true; }

    QImage thumbnail(const QSize& docSize, int h = 40) const;

    std::shared_ptr<Layer> clone() const;

    static std::shared_ptr<Layer> makeRaster(const QString& name, const QSize& size,
                                             const QColor& fill = Qt::transparent);
    static std::shared_ptr<Layer> makeText(const QString& text, const QFont& font,
                                           const QColor& color, const QPoint& pos);
    static std::shared_ptr<Layer> makeAdjustment(FilterType t);

private:
    mutable QImage m_maskAlphaCache;
    mutable bool   m_maskAlphaDirty = true;
};

// Snapshot of everything visual about a layer, for undo.
struct LayerState
{
    QImage image;
    QPoint offset;
    QImage mask;
    QString text;
    QFont font;
    QColor color;
    FilterParams params;

    static LayerState capture(const Layer& l);
    void apply(Layer& l) const;
};
