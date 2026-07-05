#include "Layer.h"
#include <QPainter>
#include <QFontMetrics>

void Layer::ensureArea(const QRect& r)
{
    if (type != Raster && type != Text) return;
    QRect cur = rect();
    if (cur.contains(r)) return;
    QRect uni = cur.isEmpty() ? r : cur.united(r);
    QImage img(uni.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    if (!image.isNull()) {
        QPainter p(&img);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(offset - uni.topLeft(), image);
    }
    image = img;
    offset = uni.topLeft();
}

void Layer::renderText()
{
    QFontMetrics fm(font);
    QRect br = fm.boundingRect(QRect(0, 0, 8000, 8000),
                               Qt::AlignLeft | Qt::AlignTop | Qt::TextExpandTabs, text);
    br = br.adjusted(-4, -4, 8, 8);
    if (br.width() < 1 || br.height() < 1) br.setSize(QSize(1, 1));

    QImage img(br.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(font);
    p.setPen(color);
    p.translate(-br.topLeft());
    p.drawText(QRect(0, 0, 8000, 8000), Qt::AlignLeft | Qt::AlignTop | Qt::TextExpandTabs, text);
    p.end();
    image = img;
}

const QImage& Layer::maskAlpha() const
{
    if (m_maskAlphaDirty) {
        m_maskAlphaCache = grayToAlphaImage(mask);
        m_maskAlphaDirty = false;
    }
    return m_maskAlphaCache;
}

QImage Layer::thumbnail(const QSize& docSize, int h) const
{
    int w = std::max(1, docSize.height() > 0 ? h * docSize.width() / docSize.height() : h);
    w = std::min(w, h * 3);
    QImage thumb(w, h, QImage::Format_ARGB32_Premultiplied);

    // checkerboard
    QPainter p(&thumb);
    p.fillRect(thumb.rect(), QColor(200, 200, 200));
    for (int y = 0; y < h; y += 6)
        for (int x = (y / 6 % 2) * 6; x < w; x += 12)
            p.fillRect(x, y, 6, 6, QColor(150, 150, 150));

    if (type == Adjustment) {
        p.fillRect(thumb.rect(), QColor(90, 90, 110));
        p.setPen(Qt::white);
        p.drawText(thumb.rect(), Qt::AlignCenter, "fx");
    } else if (!image.isNull() && !docSize.isEmpty()) {
        double sx = double(w) / docSize.width();
        double sy = double(h) / docSize.height();
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.save();
        p.scale(sx, sy);
        p.drawImage(offset, image);
        p.restore();
    }
    if (hasMask()) {
        p.setPen(QPen(QColor(255, 255, 255), 1));
        p.drawRect(0, 0, w - 1, h - 1);
    }
    p.end();
    return thumb;
}

std::shared_ptr<Layer> Layer::clone() const
{
    auto l = std::make_shared<Layer>(*this);   // QImage is implicitly shared -> cheap
    l->maskEdited();
    return l;
}

std::shared_ptr<Layer> Layer::makeRaster(const QString& name, const QSize& size, const QColor& fill)
{
    auto l = std::make_shared<Layer>();
    l->type = Raster;
    l->name = name;
    l->image = QImage(size, QImage::Format_ARGB32_Premultiplied);
    l->image.fill(fill);
    return l;
}

std::shared_ptr<Layer> Layer::makeText(const QString& text, const QFont& font,
                                       const QColor& color, const QPoint& pos)
{
    auto l = std::make_shared<Layer>();
    l->type = Text;
    l->name = text.left(16).simplified();
    if (l->name.isEmpty()) l->name = "Text";
    l->text = text;
    l->font = font;
    l->color = color;
    l->offset = pos;
    l->renderText();
    return l;
}

std::shared_ptr<Layer> Layer::makeAdjustment(FilterType t)
{
    auto l = std::make_shared<Layer>();
    l->type = Adjustment;
    l->name = filterName(t);
    l->filter = t;
    l->params = filterDefaults(t);
    return l;
}

LayerState LayerState::capture(const Layer& l)
{
    LayerState s;
    s.image = l.image;
    s.offset = l.offset;
    s.mask = l.mask;
    s.text = l.text;
    s.font = l.font;
    s.color = l.color;
    s.params = l.params;
    return s;
}

void LayerState::apply(Layer& l) const
{
    l.image = image;
    l.offset = offset;
    l.mask = mask;
    l.text = text;
    l.font = font;
    l.color = color;
    l.params = params;
    l.maskEdited();
}
