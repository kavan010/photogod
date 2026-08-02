#pragma once
#include "Theme.h"

#include <QAbstractButton>
#include <QAction>
#include <QEvent>
#include <QPointer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QGraphicsDropShadowEffect>

// ============================================================================
// Hover lights the icon, never the background.
//
// Qt's stylesheet can only swap a hover state instantly, and only by painting
// something behind the icon. Both are wrong here: an icon rail should answer
// the pointer by brightening, the way a physical control catches the light.
// This attaches a short cross-fade to a button — idle grey → white on hover,
// and accent while it is the active tool — and leaves the surface untouched.
//
//   IconGlow::install(button, ":/icons/brush.svg");
//   IconGlow::installToggle(eye, ":/icons/eye.svg", ":/icons/eye-off.svg");
//
// Alongside the tint, hover breathes a soft halo of the same colour behind the
// glyph — enough to say "this one", not enough to be a light show; it is fully
// off at rest so idle buttons cost nothing.
//
// The base pixmap is rendered once; each frame only re-tints a copy of it, so
// animating costs a fill, not an SVG parse.
// ============================================================================
class IconGlow : public QObject
{
public:
    static void install(QAbstractButton* b, const QString& svgPath, int px = 18)
    {
        if (b) new IconGlow(b, svgPath, QString(), px, true);
    }

    // For two-state buttons (eye/eye-off, lock/unlock): each state keeps its own
    // glyph, and "checked" is an ordinary state rather than a selection, so it
    // stays in the neutral ramp instead of going accent.
    static void installToggle(QAbstractButton* b, const QString& svgOn,
                              const QString& svgOff, int px = 16)
    {
        if (b) new IconGlow(b, svgOn, svgOff, px, false);
    }

protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (o == m_btn) {
            if (e->type() == QEvent::Enter)      run(true);
            else if (e->type() == QEvent::Leave) run(false);
        }
        return QObject::eventFilter(o, e);
    }

private:
    IconGlow(QAbstractButton* b, const QString& svgOn, const QString& svgOff,
             int px, bool accentWhenChecked)
        : QObject(b), m_btn(b), m_accent(accentWhenChecked)
    {
        const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
        m_base = QIcon(svgOn).pixmap(QSize(px, px), dpr);
        if (!svgOff.isEmpty())
            m_baseOff = QIcon(svgOff).pixmap(QSize(px, px), dpr);

        m_glow = new QGraphicsDropShadowEffect(b);
        m_glow->setOffset(0, 0);
        m_glow->setBlurRadius(px * 0.8);
        m_glow->setEnabled(false);
        b->setGraphicsEffect(m_glow);

        m_anim.setDuration(120);
        m_anim.setStartValue(0.0);
        m_anim.setEndValue(1.0);
        connect(&m_anim, &QVariantAnimation::valueChanged, b,
                [this](const QVariant& v) { paint(v.toReal()); });
        connect(b, &QAbstractButton::toggled, b, [this](bool) { paint(m_t); });

        b->installEventFilter(this);
        paint(0.0);
    }

    void run(bool in)
    {
        m_anim.stop();
        m_anim.setDirection(in ? QAbstractAnimation::Forward
                               : QAbstractAnimation::Backward);
        m_anim.setCurrentTime(int(m_t * m_anim.duration()));
        m_anim.start();
    }

    void paint(qreal t)
    {
        m_t = t;
        if (!m_btn) return;

        // Selected sits at accent and only lifts a little on hover; idle
        // travels the whole way from grey to white.
        const bool on = m_btn->isCheckable() && m_btn->isChecked();
        const bool sel = on && m_accent;
        const QColor from = Theme::color(sel ? Theme::Accent
                                             : (on || m_baseOff.isNull()) ? Theme::Text2
                                                                          : Theme::Text3);
        const QColor to   = sel ? QColor("#A6BEEA") : Theme::color(Theme::Text);

        QColor c(from.red()   + int((to.red()   - from.red())   * t),
                 from.green() + int((to.green() - from.green()) * t),
                 from.blue()  + int((to.blue()  - from.blue())  * t));

        // t is small at rest, so the halo is genuinely absent until hovered.
        if (t <= 0.01) {
            m_glow->setEnabled(false);
        } else {
            QColor halo = c;
            halo.setAlphaF(0.5 * t);
            m_glow->setColor(halo);
            m_glow->setEnabled(true);
        }

        QPixmap pm = (on || m_baseOff.isNull()) ? m_base : m_baseOff;
        QPainter p(&pm);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), c);
        p.end();

        // A button driven by an action takes its icon from that action, so
        // writing to the button alone would be overwritten on the next update.
        const QIcon icon(pm);
        if (auto* tb = qobject_cast<QToolButton*>(m_btn.data()); tb && tb->defaultAction())
            tb->defaultAction()->setIcon(icon);
        else
            m_btn->setIcon(icon);
    }

    QPointer<QAbstractButton> m_btn;
    QPixmap m_base;      // checked / only state
    QPixmap m_baseOff;   // unchecked state, null for single-glyph buttons
    QGraphicsDropShadowEffect* m_glow = nullptr;
    bool m_accent = true;
    QVariantAnimation m_anim;
    qreal m_t = 0.0;
};
