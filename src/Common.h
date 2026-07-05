#pragma once
#include <QPainter>
#include <QColor>
#include <QString>
#include <QStringList>

enum class BlendMode { Normal = 0, Multiply, Screen, Overlay, Darken, Lighten };

inline QPainter::CompositionMode blendToQt(BlendMode m)
{
    switch (m) {
    case BlendMode::Multiply: return QPainter::CompositionMode_Multiply;
    case BlendMode::Screen:   return QPainter::CompositionMode_Screen;
    case BlendMode::Overlay:  return QPainter::CompositionMode_Overlay;
    case BlendMode::Darken:   return QPainter::CompositionMode_Darken;
    case BlendMode::Lighten:  return QPainter::CompositionMode_Lighten;
    default:                  return QPainter::CompositionMode_SourceOver;
    }
}

inline QStringList blendModeNames()
{
    return {"Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten"};
}

enum class ToolType {
    Move, MarqueeRect, MarqueeEllipse, Lasso, Wand, Crop,
    Eyedropper, Brush, Eraser, Gradient, Text,
    ShapeRect, ShapeEllipse, ShapeLine, Zoom, Hand
};

struct ToolSettings {
    int  brushSize     = 40;
    int  brushOpacity  = 100;   // percent
    int  brushFlow     = 100;   // percent
    int  brushHardness = 80;    // percent
    bool pressureSize    = true;
    bool pressureOpacity = false;

    int  wandTolerance  = 32;
    bool wandContiguous = true;

    bool shapeFill        = true;
    bool shapeStroke      = false;
    int  shapeStrokeWidth = 4;

    int gradientMode = 0;       // 0 = FG->BG, 1 = FG->Transparent

    QString fontFamily = "Sans Serif";
    int     fontSize   = 48;

    QColor fg = Qt::black;
    QColor bg = Qt::white;
};
