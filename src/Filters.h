#pragma once
#include <QImage>
#include <QString>
#include <QList>

enum class FilterType {
    Brightness, Contrast, Saturation, Hue, Exposure, Levels,
    Grayscale, Invert, Pixelate, Blur
};

struct FilterParams {
    double p1 = 0, p2 = 0, p3 = 0;
};

struct SliderSpec {
    QString label;
    double min, max, def;
    double scale = 1.0;   // slider int value * scale -> param value
};

QString filterName(FilterType t);
QList<SliderSpec> filterSliders(FilterType t);
FilterParams filterDefaults(FilterType t);

// Applies filter in-place. Result is ARGB32_Premultiplied.
void applyFilter(QImage& img, FilterType t, const FilterParams& p);

// 3x box blur approximation of gaussian, Grayscale8 in place.
void boxBlurGray(QImage& gray, int radius);

// Grayscale8 -> ARGB32_Premultiplied where alpha = gray (white premultiplied).
QImage grayToAlphaImage(const QImage& gray);
