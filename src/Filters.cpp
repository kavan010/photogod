#include "Filters.h"
#include <QtMath>
#include <algorithm>
#include <vector>

QString filterName(FilterType t)
{
    switch (t) {
    case FilterType::Brightness: return "Brightness";
    case FilterType::Contrast:   return "Contrast";
    case FilterType::Saturation: return "Saturation";
    case FilterType::Hue:        return "Hue Shift";
    case FilterType::Exposure:   return "Exposure";
    case FilterType::Levels:     return "Levels";
    case FilterType::Grayscale:  return "Grayscale";
    case FilterType::Invert:     return "Invert";
    case FilterType::Pixelate:   return "Pixelate";
    case FilterType::Blur:       return "Blur";
    }
    return {};
}

QList<SliderSpec> filterSliders(FilterType t)
{
    switch (t) {
    case FilterType::Brightness: return {{"Brightness", -100, 100, 0}};
    case FilterType::Contrast:   return {{"Contrast", -100, 100, 0}};
    case FilterType::Saturation: return {{"Saturation", -100, 100, 0}};
    case FilterType::Hue:        return {{"Hue", -180, 180, 0}};
    case FilterType::Exposure:   return {{"Exposure", -300, 300, 0, 0.01}};
    case FilterType::Levels:     return {{"Black point", 0, 254, 0},
                                         {"White point", 1, 255, 255},
                                         {"Gamma", 10, 400, 100, 0.01}};
    case FilterType::Pixelate:   return {{"Block size", 2, 128, 8}};
    case FilterType::Blur:       return {{"Radius", 1, 100, 5}};
    default: return {};
    }
}

FilterParams filterDefaults(FilterType t)
{
    FilterParams p;
    auto specs = filterSliders(t);
    if (specs.size() > 0) p.p1 = specs[0].def * specs[0].scale;
    if (specs.size() > 1) p.p2 = specs[1].def * specs[1].scale;
    if (specs.size() > 2) p.p3 = specs[2].def * specs[2].scale;
    return p;
}

static inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// Apply a per-channel LUT to RGB (alpha untouched), on straight-alpha ARGB32.
static void applyLut(QImage& img, const uchar lut[256])
{
    for (int y = 0; y < img.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            QRgb c = line[x];
            line[x] = qRgba(lut[qRed(c)], lut[qGreen(c)], lut[qBlue(c)], qAlpha(c));
        }
    }
}

static void hueSaturate(QImage& img, double hueShift, double satFactor)
{
    for (int y = 0; y < img.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            QRgb c = line[x];
            int a = qAlpha(c);
            if (a == 0) continue;
            QColor col(qRed(c), qGreen(c), qBlue(c));
            float h, s, v;
            col.getHsvF(&h, &s, &v);
            if (h < 0) h = 0;
            h = std::fmod(h + float(hueShift / 360.0) + 1.0f, 1.0f);
            s = std::clamp(float(s * satFactor), 0.0f, 1.0f);
            col.setHsvF(h, s, v);
            line[x] = qRgba(col.red(), col.green(), col.blue(), a);
        }
    }
}

static void saturate(QImage& img, double factor)
{
    for (int y = 0; y < img.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            QRgb c = line[x];
            int a = qAlpha(c);
            if (a == 0) continue;
            int r = qRed(c), g = qGreen(c), b = qBlue(c);
            int gray = (r * 54 + g * 183 + b * 19) >> 8;
            r = clamp255(int(gray + (r - gray) * factor));
            g = clamp255(int(gray + (g - gray) * factor));
            b = clamp255(int(gray + (b - gray) * factor));
            line[x] = qRgba(r, g, b, a);
        }
    }
}

static void pixelate(QImage& img, int block)
{
    block = std::max(2, block);
    int w = img.width(), h = img.height();
    for (int by = 0; by < h; by += block) {
        int bh = std::min(block, h - by);
        for (int bx = 0; bx < w; bx += block) {
            int bw = std::min(block, w - bx);
            quint64 r = 0, g = 0, b = 0, a = 0;
            for (int y = by; y < by + bh; ++y) {
                const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
                for (int x = bx; x < bx + bw; ++x) {
                    QRgb c = line[x];
                    int al = qAlpha(c);
                    r += qRed(c) * al; g += qGreen(c) * al; b += qBlue(c) * al; a += al;
                }
            }
            int n = bw * bh;
            QRgb avg;
            if (a == 0) avg = qRgba(0, 0, 0, 0);
            else avg = qRgba(int(r / a), int(g / a), int(b / a), int(a / n));
            for (int y = by; y < by + bh; ++y) {
                QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
                for (int x = bx; x < bx + bw; ++x) line[x] = avg;
            }
        }
    }
}

// Box blur one channel plane of ARGB32_Premultiplied (channel offset 0..3), separable.
static void boxBlurPremult(QImage& img, int radius)
{
    if (radius < 1) return;
    int w = img.width(), h = img.height();
    if (w == 0 || h == 0) return;
    std::vector<quint32> tmp(size_t(w) * h);
    const int div = radius * 2 + 1;

    for (int pass = 0; pass < 3; ++pass) {
        // horizontal
        for (int y = 0; y < h; ++y) {
            const QRgb* src = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            int sr = 0, sg = 0, sb = 0, sa = 0;
            for (int i = -radius; i <= radius; ++i) {
                QRgb c = src[std::clamp(i, 0, w - 1)];
                sr += qRed(c); sg += qGreen(c); sb += qBlue(c); sa += qAlpha(c);
            }
            for (int x = 0; x < w; ++x) {
                tmp[size_t(y) * w + x] = qRgba(sr / div, sg / div, sb / div, sa / div);
                QRgb cin = src[std::clamp(x + radius + 1, 0, w - 1)];
                QRgb cout = src[std::clamp(x - radius, 0, w - 1)];
                sr += qRed(cin) - qRed(cout); sg += qGreen(cin) - qGreen(cout);
                sb += qBlue(cin) - qBlue(cout); sa += qAlpha(cin) - qAlpha(cout);
            }
        }
        // vertical
        for (int x = 0; x < w; ++x) {
            int sr = 0, sg = 0, sb = 0, sa = 0;
            for (int i = -radius; i <= radius; ++i) {
                QRgb c = tmp[size_t(std::clamp(i, 0, h - 1)) * w + x];
                sr += qRed(c); sg += qGreen(c); sb += qBlue(c); sa += qAlpha(c);
            }
            for (int y = 0; y < h; ++y) {
                QRgb* dst = reinterpret_cast<QRgb*>(img.scanLine(y));
                dst[x] = qRgba(sr / div, sg / div, sb / div, sa / div);
                QRgb cin = tmp[size_t(std::clamp(y + radius + 1, 0, h - 1)) * w + x];
                QRgb cout = tmp[size_t(std::clamp(y - radius, 0, h - 1)) * w + x];
                sr += qRed(cin) - qRed(cout); sg += qGreen(cin) - qGreen(cout);
                sb += qBlue(cin) - qBlue(cout); sa += qAlpha(cin) - qAlpha(cout);
            }
        }
    }
}

void gaussianBlurPremult(QImage& img, int radius)
{
    img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    boxBlurPremult(img, std::max(1, radius / 2));
}

void applyFilter(QImage& img, FilterType t, const FilterParams& p)
{
    if (img.isNull()) return;

    if (t == FilterType::Blur) {
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        boxBlurPremult(img, std::max(1, int(p.p1 / 2)));
        return;
    }

    img = img.convertToFormat(QImage::Format_ARGB32);
    uchar lut[256];

    switch (t) {
    case FilterType::Brightness: {
        int add = int(p.p1 * 255.0 / 100.0);
        for (int i = 0; i < 256; ++i) lut[i] = uchar(clamp255(i + add));
        applyLut(img, lut);
        break;
    }
    case FilterType::Contrast: {
        double c = p.p1 * 2.55;
        double f = (259.0 * (c + 255.0)) / (255.0 * (259.0 - c));
        for (int i = 0; i < 256; ++i) lut[i] = uchar(clamp255(int(f * (i - 128) + 128)));
        applyLut(img, lut);
        break;
    }
    case FilterType::Exposure: {
        double mul = std::pow(2.0, p.p1);
        for (int i = 0; i < 256; ++i) lut[i] = uchar(clamp255(int(i * mul)));
        applyLut(img, lut);
        break;
    }
    case FilterType::Levels: {
        double black = p.p1, white = std::max(p.p1 + 1.0, p.p2);
        double gamma = std::max(0.05, p.p3);
        for (int i = 0; i < 256; ++i) {
            double v = std::clamp((i - black) / (white - black), 0.0, 1.0);
            lut[i] = uchar(clamp255(int(std::pow(v, 1.0 / gamma) * 255.0 + 0.5)));
        }
        applyLut(img, lut);
        break;
    }
    case FilterType::Invert: {
        for (int i = 0; i < 256; ++i) lut[i] = uchar(255 - i);
        applyLut(img, lut);
        break;
    }
    case FilterType::Grayscale:
        saturate(img, 0.0);
        break;
    case FilterType::Saturation:
        saturate(img, 1.0 + p.p1 / 100.0);
        break;
    case FilterType::Hue:
        hueSaturate(img, p.p1, 1.0);
        break;
    case FilterType::Pixelate:
        pixelate(img, int(p.p1));
        break;
    default:
        break;
    }
    img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

void boxBlurGray(QImage& gray, int radius)
{
    if (radius < 1 || gray.isNull()) return;
    int w = gray.width(), h = gray.height();
    std::vector<uchar> tmp(size_t(w) * h);
    const int div = radius * 2 + 1;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            const uchar* src = gray.constScanLine(y);
            int s = 0;
            for (int i = -radius; i <= radius; ++i) s += src[std::clamp(i, 0, w - 1)];
            for (int x = 0; x < w; ++x) {
                tmp[size_t(y) * w + x] = uchar(s / div);
                s += src[std::clamp(x + radius + 1, 0, w - 1)] - src[std::clamp(x - radius, 0, w - 1)];
            }
        }
        for (int x = 0; x < w; ++x) {
            int s = 0;
            for (int i = -radius; i <= radius; ++i) s += tmp[size_t(std::clamp(i, 0, h - 1)) * w + x];
            for (int y = 0; y < h; ++y) {
                gray.scanLine(y)[x] = uchar(s / div);
                s += tmp[size_t(std::clamp(y + radius + 1, 0, h - 1)) * w + x]
                   - tmp[size_t(std::clamp(y - radius, 0, h - 1)) * w + x];
            }
        }
    }
}

QImage grayToAlphaImage(const QImage& gray)
{
    QImage out(gray.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < gray.height(); ++y) {
        const uchar* src = gray.constScanLine(y);
        QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < gray.width(); ++x) {
            uchar g = src[x];
            dst[x] = qRgba(g, g, g, g); // premultiplied white * alpha
        }
    }
    return out;
}
