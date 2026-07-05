# PhotoGod

A fast, native, layer-based image editor with a Photoshop-style workflow.
C++20 · Qt 6 · runs on Linux and Windows.

![stack](https://img.shields.io/badge/C%2B%2B20-Qt6-blue)

## Build & run (Arch Linux)

```sh
sudo pacman -S --needed qt6-base cmake ninja gcc
# optional, adds WebP/TIFF import & export:
sudo pacman -S --needed qt6-imageformats

cd ~/Desktop/photogod
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build

./build/photogod                # run it
./build/photogod photo.jpg      # open an image straight away
```

Want it on your PATH? `echo 'alias photogod=~/Desktop/photogod/build/photogod' >> ~/.zshrc`

## Build (Windows)

Using [MSYS2](https://www.msys2.org/) (MinGW64 shell):

```sh
pacman -S --needed mingw-w64-x86_64-qt6-base mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-gcc
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
windeployqt6 build/photogod.exe   # copies the Qt DLLs next to the exe
build/photogod.exe
```

(Qt's official installer + MSVC works too — open the folder in Qt Creator and hit Run.)

## What it does

- **Import**: PNG, JPG, BMP, GIF, TIFF/WebP (with qt6-imageformats). Drag & drop onto the
  window, paste from clipboard, or *File → Open as Layer* to composite images together.
- **Export**: PNG / JPG (quality prompt) / WebP / BMP / TIFF via **Ctrl+Shift+E**.
  JPG/BMP are flattened over white automatically.
- **Projects**: save layered documents as `.pgd` (JSON + embedded PNG data) with Ctrl+S.
- **Layers**: unlimited, reorder by dragging, opacity, blend modes (Normal / Multiply /
  Screen / Overlay / Darken / Lighten), lock, duplicate, merge down, flatten,
  raster + text + adjustment layers.
- **Layer masks**: add from selection (or full-white), paint on them in black/white
  (toggle the `✎M` button in the Layers panel), gradients work on masks too.
- **Brush / Eraser**: size, opacity, flow, hardness, soft-to-hard round brushes,
  tablet pressure → size/opacity, `[` `]` to resize.
- **Selections**: rectangle & ellipse marquee, polygon lasso, magic wand with tolerance.
  Shift = add, Alt = subtract, Ctrl-drag = square/circle. Feather via *Select → Feather*.
  Everything (paint, fills, filters, gradients) respects the selection.
- **Free Transform (Ctrl+T)**: move, scale (corner/edge handles, Shift = uniform),
  rotate (drag outside the box, Shift snaps to 15°). Enter applies, Esc cancels.
- **Crop tool (C)**: drag a rect, Enter to crop. Also *Image → Crop to Selection*.
- **Adjustments**: Brightness, Contrast, Saturation, Hue, Exposure, Levels, Grayscale,
  Invert, Pixelate, Blur — as live-preview destructive filters (*Filter* menu) **or**
  non-destructive adjustment layers (`fx` button, edited in the Properties panel).
- **Text**: click with T, pick font/size/bold/italic/color, click existing text to re-edit.
- **Shapes**: rectangle / ellipse / line with fill (FG) and stroke (BG) options.
- **Gradient tool (G)**: FG→BG or FG→transparent linear gradients.
- **Color**: FG/BG swatches, X to swap, swatch grid, recent colors, eyedropper (I or
  Alt-click while painting).
- **Workflow**: tabs for multiple documents, dockable panels (Layers / Properties /
  History / Color), full undo history (40 steps) with a History panel, dark UI,
  smooth wheel-zoom at cursor, Space-drag or middle-drag to pan, fullscreen (F11).

## Keyboard shortcuts

| Key | Tool | | Key | Action |
|---|---|---|---|---|
| V | Move | | Ctrl+Z / Ctrl+Shift+Z | Undo / Redo |
| M | Marquee (toggles rect/ellipse) | | Ctrl+T | Free transform |
| L | Lasso | | Ctrl+J | Duplicate layer |
| W | Magic wand | | Ctrl+E | Merge down |
| C | Crop | | Ctrl+A / Ctrl+D | Select all / Deselect |
| I | Eyedropper | | Ctrl+Shift+I | Invert selection |
| B | Brush | | Ctrl+I | Invert colors |
| E | Eraser | | Ctrl+N / Ctrl+O | New / Open |
| G | Gradient | | Ctrl+S / Ctrl+Shift+S | Save / Save As |
| T | Text | | Ctrl+Shift+E | Export PNG/JPG |
| U | Shapes (cycles) | | Ctrl+0 / Ctrl+1 | Fit / 100% |
| Z | Zoom | | [ / ] | Brush smaller / bigger |
| H | Hand | | X | Swap FG/BG colors |
| Space | Temporary hand tool | | Alt+Backspace | Fill with FG |

## Design notes / what was intentionally left out

The spec allowed dropping the hard parts, so:

- **Compositing is CPU-based** (Qt raster paint engine) rather than a raw OpenGL 4.6
  pipeline. Qt's engine is SIMD-optimized and gives Multiply/Screen/Overlay natively;
  it's easily real-time at 1080p and keeps the build dependency-free beyond Qt itself.
- Pen/Bezier tool, warp/distort, layer groups, and ICC color management were skipped.
- Layer property tweaks (opacity/blend/visibility) apply instantly but are not in the
  undo history; pixel edits, layer add/remove/reorder and document ops all are.
- Undo keeps up to 40 steps; big documents use rectangle-diff storage for strokes.

## Testing

```sh
QT_QPA_PLATFORM=offscreen ./build/photogod --modeltest   # headless functional tests
./build/photogod --selftest --shot=ui.png                # boots the UI, saves a screenshot
```
