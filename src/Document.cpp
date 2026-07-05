#include "Document.h"
#include <QPainter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QBuffer>
#include <QTransform>

Document::Document(const QSize& size, const QColor& bg, QObject* parent)
    : QObject(parent), m_size(size)
{
    undo.setUndoLimit(40);
    auto base = Layer::makeRaster("Background", size, bg);
    layers.append(base);
    activeIndex = 0;
}

Document* Document::fromImage(const QImage& img, const QString& name, QObject* parent)
{
    auto* doc = new Document(img.size(), Qt::transparent, parent);
    doc->name = name;
    doc->layers[0]->image = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    doc->layers[0]->name = name;
    return doc;
}

std::shared_ptr<Layer> Document::activeLayer() const
{
    if (activeIndex < 0 || activeIndex >= layers.size()) return nullptr;
    return layers[activeIndex];
}

void Document::setSelection(const QPainterPath& p, double featherRadius)
{
    selection = p;
    feather = featherRadius;
    m_selMaskDirty = true;
    emit selectionChanged();
}

const QImage& Document::selectionMask()
{
    if (m_selMaskDirty) {
        m_selMask = QImage(m_size, QImage::Format_Grayscale8);
        m_selMask.fill(0);
        if (hasSelection()) {
            QPainter p(&m_selMask);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawPath(selection);
            p.end();
            if (feather > 0.5)
                boxBlurGray(m_selMask, std::max(1, int(feather / 2)));
        }
        m_selMaskAlpha = grayToAlphaImage(m_selMask);
        m_selMaskDirty = false;
    }
    return m_selMask;
}

const QImage& Document::selectionMaskAlpha()
{
    selectionMask();
    return m_selMaskAlpha;
}

QImage Document::composite(const Layer* skip)
{
    if (skip == nullptr && !m_cacheDirty && !m_cache.isNull())
        return m_cache;

    QImage out(m_size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);

    for (const auto& l : layers) {
        if (!l->visible || l.get() == skip) continue;

        if (l->type == Layer::Adjustment) {
            p.end();
            QImage filtered = out.copy();
            applyFilter(filtered, l->filter, l->params);
            if (l->hasMask()) {
                QPainter mp(&filtered);
                mp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                mp.drawImage(0, 0, l->maskAlpha());
            }
            p.begin(&out);
            p.setOpacity(l->opacity);
            p.drawImage(0, 0, filtered);
            p.setOpacity(1.0);
            continue;
        }

        if (l->image.isNull()) continue;
        p.setOpacity(l->opacity);
        p.setCompositionMode(blendToQt(l->blend));
        if (l->hasMask()) {
            QImage src = l->image.copy();
            QPainter mp(&src);
            mp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            mp.drawImage(-l->offset, l->maskAlpha());
            mp.end();
            p.drawImage(l->offset, src);
        } else {
            p.drawImage(l->offset, l->image);
        }
        p.setOpacity(1.0);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    p.end();

    if (skip == nullptr) {
        m_cache = out;
        m_cacheDirty = false;
    }
    return out;
}

void Document::invalidate()
{
    m_cacheDirty = true;
    emit changed();
}

void Document::notifyStructure()
{
    m_cacheDirty = true;
    if (activeIndex >= layers.size()) activeIndex = layers.size() - 1;
    if (activeIndex < 0) activeIndex = 0;
    emit structureChanged();
    emit changed();
}

void Document::insertLayer(int idx, const std::shared_ptr<Layer>& l)
{
    idx = std::clamp(idx, 0, int(layers.size()));
    layers.insert(idx, l);
    activeIndex = idx;
    notifyStructure();
}

std::shared_ptr<Layer> Document::takeLayer(int idx)
{
    if (idx < 0 || idx >= layers.size()) return nullptr;
    auto l = layers.takeAt(idx);
    notifyStructure();
    return l;
}

void Document::moveLayerIdx(int from, int to)
{
    if (from < 0 || from >= layers.size() || to < 0 || to >= layers.size() || from == to) return;
    layers.move(from, to);
    activeIndex = to;
    notifyStructure();
}

int Document::indexOf(const std::shared_ptr<Layer>& l) const
{
    return layers.indexOf(l);
}

// ---------- whole-document operations ----------

static void resizeMask(Layer& l, const QSize& newSize, const QPoint& shift)
{
    if (!l.hasMask()) return;
    QImage m(newSize, QImage::Format_Grayscale8);
    m.fill(255);
    QPainter p(&m);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(shift, l.mask);
    p.end();
    l.mask = m;
    l.maskEdited();
}

void Document::resizeCanvas(const QSize& newSize)
{
    QPoint shift((newSize.width() - m_size.width()) / 2,
                 (newSize.height() - m_size.height()) / 2);
    for (auto& l : layers) {
        l->offset += shift;
        resizeMask(*l, newSize, shift);
    }
    m_size = newSize;
    setSelection(QPainterPath(), 0);
    notifyStructure();
}

void Document::scaleTo(const QSize& newSize)
{
    double sx = double(newSize.width()) / m_size.width();
    double sy = double(newSize.height()) / m_size.height();
    for (auto& l : layers) {
        if (!l->image.isNull()) {
            QSize ns(std::max(1, int(std::lround(l->image.width() * sx))),
                     std::max(1, int(std::lround(l->image.height() * sy))));
            l->image = l->image.scaled(ns, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            l->offset = QPoint(int(std::lround(l->offset.x() * sx)),
                               int(std::lround(l->offset.y() * sy)));
        }
        if (l->hasMask()) {
            l->mask = l->mask.scaled(newSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_Grayscale8);
            l->maskEdited();
        }
        if (l->type == Layer::Text)
            l->font.setPointSizeF(std::max(1.0, l->font.pointSizeF() * (sx + sy) / 2.0));
    }
    m_size = newSize;
    setSelection(QPainterPath(), 0);
    notifyStructure();
}

void Document::rotateDoc(int quarterTurnsCW)
{
    QTransform t;
    t.rotate(90.0 * quarterTurnsCW);
    QSize ns = (quarterTurnsCW % 2 == 1) ? m_size.transposed() : m_size;

    for (auto& l : layers) {
        if (!l->image.isNull()) {
            QRectF r(l->offset, QSizeF(l->image.size()));
            // rotate around doc center, then shift into new doc coords
            QTransform full;
            full.translate(ns.width() / 2.0, ns.height() / 2.0);
            full.rotate(90.0 * quarterTurnsCW);
            full.translate(-m_size.width() / 2.0, -m_size.height() / 2.0);
            l->image = l->image.transformed(t, Qt::SmoothTransformation);
            QRectF nr = full.mapRect(r);
            l->offset = nr.topLeft().toPoint();
        }
        if (l->hasMask()) {
            l->mask = l->mask.transformed(t).convertToFormat(QImage::Format_Grayscale8);
            l->maskEdited();
        }
    }
    m_size = ns;
    setSelection(QPainterPath(), 0);
    notifyStructure();
}

void Document::flipDoc(bool horizontal)
{
    for (auto& l : layers) {
        if (!l->image.isNull()) {
            l->image = l->image.flipped(horizontal ? Qt::Horizontal : Qt::Vertical);
            if (horizontal)
                l->offset.setX(m_size.width() - l->offset.x() - l->image.width());
            else
                l->offset.setY(m_size.height() - l->offset.y() - l->image.height());
        }
        if (l->hasMask()) {
            l->mask = l->mask.flipped(horizontal ? Qt::Horizontal : Qt::Vertical);
            l->maskEdited();
        }
    }
    setSelection(QPainterPath(), 0);
    notifyStructure();
}

void Document::cropTo(const QRect& r)
{
    QRect c = r.intersected(rect());
    if (c.isEmpty()) return;
    for (auto& l : layers) {
        l->offset -= c.topLeft();
        if (l->hasMask()) {
            l->mask = l->mask.copy(c);
            l->maskEdited();
        }
    }
    m_size = c.size();
    setSelection(QPainterPath(), 0);
    notifyStructure();
}

// ---------- project save / load ----------

static QString imageToB64(const QImage& img)
{
    if (img.isNull()) return {};
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return QString::fromLatin1(ba.toBase64());
}

static QImage imageFromB64(const QString& s, QImage::Format fmt)
{
    if (s.isEmpty()) return {};
    QImage img;
    img.loadFromData(QByteArray::fromBase64(s.toLatin1()), "PNG");
    return img.convertToFormat(fmt);
}

bool Document::saveProject(const QString& path)
{
    QJsonObject root;
    root["app"] = "photogod";
    root["version"] = 1;
    root["width"] = m_size.width();
    root["height"] = m_size.height();
    root["active"] = activeIndex;
    root["name"] = name;

    QJsonArray arr;
    for (const auto& l : layers) {
        QJsonObject o;
        o["type"] = int(l->type);
        o["name"] = l->name;
        o["visible"] = l->visible;
        o["locked"] = l->locked;
        o["opacity"] = l->opacity;
        o["blend"] = int(l->blend);
        o["x"] = l->offset.x();
        o["y"] = l->offset.y();
        if (l->type != Layer::Adjustment) o["image"] = imageToB64(l->image);
        if (l->hasMask()) o["mask"] = imageToB64(l->mask);
        if (l->type == Layer::Text) {
            o["text"] = l->text;
            o["fontFamily"] = l->font.family();
            o["fontSize"] = l->font.pointSizeF();
            o["fontBold"] = l->font.bold();
            o["fontItalic"] = l->font.italic();
            o["color"] = l->color.name(QColor::HexArgb);
        }
        if (l->type == Layer::Adjustment) {
            o["filter"] = int(l->filter);
            o["p1"] = l->params.p1;
            o["p2"] = l->params.p2;
            o["p3"] = l->params.p3;
        }
        arr.append(o);
    }
    root["layers"] = arr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    filePath = path;
    undo.setClean();
    return true;
}

Document* Document::loadProject(const QString& path, QObject* parent)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    QJsonDocument jd = QJsonDocument::fromJson(f.readAll());
    if (!jd.isObject()) return nullptr;
    QJsonObject root = jd.object();
    if (root["app"].toString() != "photogod") return nullptr;

    QSize size(root["width"].toInt(), root["height"].toInt());
    if (size.isEmpty()) return nullptr;

    auto* doc = new Document(size, Qt::transparent, parent);
    doc->layers.clear();
    doc->name = root["name"].toString("Untitled");
    doc->filePath = path;

    for (const auto& v : root["layers"].toArray()) {
        QJsonObject o = v.toObject();
        auto l = std::make_shared<Layer>();
        l->type = Layer::Type(o["type"].toInt());
        l->name = o["name"].toString();
        l->visible = o["visible"].toBool(true);
        l->locked = o["locked"].toBool(false);
        l->opacity = o["opacity"].toDouble(1.0);
        l->blend = BlendMode(o["blend"].toInt());
        l->offset = QPoint(o["x"].toInt(), o["y"].toInt());
        l->image = imageFromB64(o["image"].toString(), QImage::Format_ARGB32_Premultiplied);
        if (o.contains("mask"))
            l->mask = imageFromB64(o["mask"].toString(), QImage::Format_Grayscale8);
        if (l->type == Layer::Text) {
            l->text = o["text"].toString();
            l->font = QFont(o["fontFamily"].toString("Sans Serif"));
            l->font.setPointSizeF(o["fontSize"].toDouble(48));
            l->font.setBold(o["fontBold"].toBool(false));
            l->font.setItalic(o["fontItalic"].toBool(false));
            l->color = QColor(o["color"].toString("#ff000000"));
        }
        if (l->type == Layer::Adjustment) {
            l->filter = FilterType(o["filter"].toInt());
            l->params.p1 = o["p1"].toDouble();
            l->params.p2 = o["p2"].toDouble();
            l->params.p3 = o["p3"].toDouble();
        }
        doc->layers.append(l);
    }
    if (doc->layers.isEmpty())
        doc->layers.append(Layer::makeRaster("Background", size, Qt::white));
    doc->activeIndex = std::clamp(root["active"].toInt(), 0, int(doc->layers.size() - 1));
    return doc;
}
