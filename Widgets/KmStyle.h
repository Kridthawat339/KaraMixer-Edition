#ifndef KMSTYLE_H
#define KMSTYLE_H

// Fusion with a studio-style rotary knob: a dark, softly shaded round knob with a bright pointer line and a ring
// of small dots around it. Dials also stop at the left / right end instead of jumping across the gap.
// Header-only, no Q_OBJECT.

#include <QProxyStyle>
#include <QStyleOptionSlider>
#include <QPainter>
#include <QPen>
#include <QRadialGradient>
#include <QtMath>
#include <QApplication>
#include <QDial>
#include <QMouseEvent>
#include <QPushButton>
#include <QDialog>

// Keeps a (non-wrapping) QDial from jumping from one end to the other when the mouse is dragged across the gap.
class DialStopFilter : public QObject
{
public:
    explicit DialStopFilter(QObject *parent) : QObject(parent) {}

    // Push buttons in dialogs get comfortable padding / minimum size (via the "kmRoomy" style-sheet rule),
    // except fixed-size ones, tiny one-letter buttons, message boxes (they size their own buttons) and the mixer window.
    static void makeRoomy(QPushButton *b)
    {
        if (b->property("kmRoomy").isValid())
            return;
        b->setProperty("kmRoomy", false);
        if (b->isFlat() || b->text().trimmed().size() < 2)
            return;
        if (b->maximumWidth() < QWIDGETSIZE_MAX || b->maximumHeight() < QWIDGETSIZE_MAX)
            return;
        QWidget *w = b->window();
        if (!qobject_cast<QDialog *>(w) || w->inherits("SynthMixerDialog") || w->inherits("QMessageBox"))
            return;
        b->setProperty("kmRoomy", true);
        if (b->style()) {
            b->style()->unpolish(b);
            b->style()->polish(b);
        }
        b->updateGeometry();
    }

    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::Polish) {
            if (QPushButton *pb = qobject_cast<QPushButton *>(o))
                makeRoomy(pb);
            return false;
        }
        if (e->type() != QEvent::MouseMove)
            return false;
        QDial *d = qobject_cast<QDial *>(o);
        if (!d || d->wrapping() || d->maximum() <= d->minimum())
            return false;
        QMouseEvent *me = static_cast<QMouseEvent *>(e);
        if (!(me->buttons() & Qt::LeftButton))
            return false;
        const double yy = d->height() / 2.0 - me->pos().y();
        const double xx = me->pos().x() - d->width() / 2.0;
        double a = (xx != 0.0 || yy != 0.0) ? qAtan2(yy, xx) : 0.0;
        if (a < -M_PI / 2.0)
            a += 2.0 * M_PI;
        const double ratio = (4.0 * M_PI / 3.0 - a) / (5.0 * M_PI / 3.0);   // 0 .. 1 over the 300 degree arc
        const double cur = double(d->sliderPosition() - d->minimum()) / double(d->maximum() - d->minimum());
        if (ratio < 0.0 || ratio > 1.0) {                                    // pointer is in the gap: stay at the stop
            d->setSliderPosition(cur >= 0.5 ? d->maximum() : d->minimum());
            return true;
        }
        if (qAbs(ratio - cur) > 0.6)                                         // pointer went round the back: ignore until it returns
            return true;
        return false;
    }
};

class KmStyle : public QProxyStyle
{
public:
    explicit KmStyle(const QString &baseKey) : QProxyStyle(baseKey)
    {
        static bool installed = false;
        if (!installed && qApp) {
            installed = true;
            qApp->installEventFilter(new DialStopFilter(qApp));
        }
    }

    void drawComplexControl(ComplexControl cc, const QStyleOptionComplex *opt, QPainter *p, const QWidget *w) const override
    {
        if (cc == CC_Dial) {
            if (const QStyleOptionSlider *d = qstyleoption_cast<const QStyleOptionSlider *>(opt)) {
                if (!d->dialWrapping) {
                    drawKnob(*d, p);
                    return;
                }
            }
        }
        QProxyStyle::drawComplexControl(cc, opt, p, w);
    }

private:
    static void drawKnob(const QStyleOptionSlider &d, QPainter *p)
    {
        const QRectF r = d.rect;
        const double side = qMin(r.width(), r.height());
        const double Rd = side / 2.0 - 1.5;                        // radius of the dot ring
        const double R = Rd * 0.74;                                // knob radius
        if (R < 5.0)
            return;
        const QPointF c = r.center();
        const double span = d.maximum - d.minimum;
        const double ratio = span > 0 ? double(d.sliderPosition - d.minimum) / span : 0.0;
        const double a = (8.0 * M_PI - ratio * 10.0 * M_PI) / 6.0;  // Qt's dial: 240 deg ... -60 deg
        const QPointF dir(qCos(a), -qSin(a));
        const bool enabled = d.state & State_Enabled;

        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        if (!enabled)
            p->setOpacity(0.45);

        // ring of small dots around the knob (like a hardware / DAW knob scale)
        {
            const double dr = qMax(0.9, Rd * 0.055);
            p->setPen(Qt::NoPen);
            // light dots on dark backgrounds, dark dots on light ones
            const bool lightBg = d.palette.color(QPalette::Window).lightness() > 128;
            p->setBrush(lightBg ? QColor(0x30, 0x34, 0x3c, 170) : QColor(0xc8, 0xcd, 0xd6, 120));
            for (int i = 0; i <= 10; ++i) {
                const double ai = (8.0 * M_PI - (i / 10.0) * 10.0 * M_PI) / 6.0;
                p->drawEllipse(c + QPointF(qCos(ai), -qSin(ai)) * Rd, dr, dr);
            }
        }

        // soft shadow under the knob
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0, 0, 0, 55));
        p->drawEllipse(c + QPointF(0.8, 1.6), R, R);

        // body: dark with a light spot at the top-left
        QRadialGradient g(c + QPointF(-R * 0.35, -R * 0.4), R * 1.5);
        g.setColorAt(0.0, QColor(0x5a, 0x5e, 0x66));
        g.setColorAt(0.55, QColor(0x25, 0x27, 0x2c));
        g.setColorAt(1.0, QColor(0x0e, 0x0f, 0x11));
        p->setBrush(g);
        p->setPen(QPen(QColor(0x08, 0x08, 0x0a), 1.0));
        p->drawEllipse(c, R, R);

        // thin lit rim
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(QColor(255, 255, 255, 40), 1.0));
        p->drawEllipse(c, R - 1.2, R - 1.2);

        // pointer
        QPen pen(QColor(0xf2, 0xf4, 0xf7), qMax(2.0, R * 0.12));
        pen.setCapStyle(Qt::RoundCap);
        p->setPen(pen);
        p->drawLine(c + dir * (R * 0.34), c + dir * (R * 0.84));
        p->restore();
    }
};

#endif // KMSTYLE_H
