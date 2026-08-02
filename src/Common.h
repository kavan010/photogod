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
    ShapeRect, ShapeEllipse, ShapeLine, Zoom, Hand, Blur
};

struct ToolSettings {
    // Opening state: a fat black marker. You land in the app already holding
    // the bluntest, most obvious thing to draw with — no setup, just paint.
    // These four are the "Marker" preset from BrushesPanel, at max size.
    int  brushSize     = 500;   // the top of the size slider
    int  brushOpacity  = 55;    // percent
    int  brushFlow     = 100;   // percent
    int  brushHardness = 95;    // percent
    int  brushNoise    = 0;     // percent, texture grain
    bool pressureSize    = true;
    bool pressureOpacity = false;

    int blurRadius = 10;        // gaussian blur brush radius

    bool showRulers = false;    // rulers + guides overlay — off until asked for
    bool snapping   = true;     // snap to guides / canvas edges

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
