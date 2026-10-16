#ifndef PANKNOB_H
#define PANKNOB_H

// Console-style rotary knob, 270 degree sweep.
//   bipolar (default): -100 (L) .. 0 (C) .. +100 (R)   - pan
//   unipolar:           0 .. 100                         - reverb send etc.
// Drag up/down (or the mouse wheel) to turn, double-click to return to the default value.

#include <QWidget>
#include <QPainter>
#include <QRadialGradient>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>

class PanKnob : public QWidget
{
    Q_OBJECT

public:
    explicit PanKnob(QWidget *parent = nullptr, bool bipolarMode = true, const QColor &arc = QColor(79, 195, 247))
        : QWidget(parent), bipolar(bipolarMode), arcColor(arc)
    {
        minV = bipolar ? -100 : 0;
        val = 0;
        setFixedSize(34, 34);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }

    int value() const { return val; }

public slots:
    void setValue(int v)
    {
        v = qBound(minV, v, 100);
        if (v == val)
            return;
        val = v;
        update();
        emit valueChanged(val);
    }

signals:
    void valueChanged(int v);

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            pressY = e->globalPos().y();
            pressVal = val;
            dragging = true;
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!dragging)
            return;
        const double base = bipolar ? 2.0 : 1.0;
        const double perPx = (e->modifiers() & Qt::ShiftModifier) ? base / 4.0 : base;
        int nv = pressVal + qRound((pressY - e->globalPos().y()) * perPx);
        if (bipolar && qAbs(nv) <= 3)          // detent at centre
            nv = 0;
        setValue(nv);
    }

    void mouseReleaseEvent(QMouseEvent *) override { dragging = false; }

    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            dragging = false;
            setValue(0);
        }
    }

    void wheelEvent(QWheelEvent *e) override
    {
        const int step = e->angleDelta().y() > 0 ? 5 : -5;
        int nv = val + step;
        if (bipolar && qAbs(nv) <= 2)
            nv = 0;
        setValue(nv);
        e->accept();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const bool lightBg = palette().color(QPalette::Window).lightness() >= 128;
        const qreal s = qMin(width(), height());
        const QPointF c(width() / 2.0, height() / 2.0);
        const qreal R = s / 2.0 - 1.0;
        const qreal ringR = R * 0.90;
        const qreal bodyR = R * 0.64;

        // angle (degrees clockwise from 12 o'clock) of a value
        auto angleOf = [&](int v) {
            return bipolar ? v / 100.0 * 135.0 : -135.0 + v * 2.7;
        };
        auto pt = [&](qreal deg, qreal r) {
            const qreal a = qDegreesToRadians(deg);
            return QPointF(c.x() + qSin(a) * r, c.y() - qCos(a) * r);
        };

        // scale ticks
        const int nTicks = bipolar ? 11 : 6;
        for (int i = 0; i < nTicks; ++i) {
            const qreal deg = -135.0 + i * (270.0 / (nTicks - 1));
            const bool end = (i == 0 || i == nTicks - 1);
            const bool mid = bipolar && i == nTicks / 2;
            const bool major = end || mid;
            const QColor tickC = lightBg ? QColor(70, 74, 84) : QColor(150, 155, 162);
            const QColor midC  = lightBg ? QColor(20, 22, 28) : QColor(235, 235, 235);
            p.setPen(QPen(mid ? midC : tickC, major ? 1.6 : 1.0, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(pt(deg, ringR), pt(deg, ringR - (major ? 4.0 : 2.5)));
        }

        // value arc
        const qreal a0 = bipolar ? 0.0 : -135.0;
        const qreal a1 = angleOf(val);
        if (qAbs(a1 - a0) > 0.5) {
            const QRectF ar(c.x() - ringR + 1.2, c.y() - ringR + 1.2, 2 * (ringR - 1.2), 2 * (ringR - 1.2));
            p.setPen(QPen(lightBg ? darker(arcColor) : arcColor, 2.2, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(ar, int((90.0 - a0) * 16), int(-(a1 - a0) * 16));
        }

        // soft shadow under the cap
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 120));
        p.drawEllipse(c + QPointF(0.8, 1.4), bodyR + 1.2, bodyR + 1.2);

        QRadialGradient edge(c - QPointF(bodyR * 0.3, bodyR * 0.4), bodyR * 1.6);
        edge.setColorAt(0.0, QColor(120, 125, 133));
        edge.setColorAt(1.0, QColor(28, 30, 34));
        p.setBrush(edge);
        p.setPen(QPen(QColor(10, 10, 12), 1.0));
        p.drawEllipse(c, bodyR, bodyR);

        QRadialGradient cap(c - QPointF(bodyR * 0.35, bodyR * 0.45), bodyR * 1.3);
        cap.setColorAt(0.0, QColor(96, 101, 110));
        cap.setColorAt(0.55, QColor(52, 55, 62));
        cap.setColorAt(1.0, QColor(30, 32, 37));
        p.setBrush(cap);
        p.setPen(QPen(QColor(70, 74, 82), 0.8));
        p.drawEllipse(c, bodyR * 0.82, bodyR * 0.82);

        // pointer
        p.setPen(QPen(QColor(250, 250, 250), 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(pt(a1, bodyR * 0.18), pt(a1, bodyR * 0.74));
    }

private:
    static QColor darker(const QColor &c) { return c.darker(160); }

    bool bipolar = true;
    QColor arcColor;
    int minV = -100;
    int val = 0;
    int pressY = 0;
    int pressVal = 0;
    bool dragging = false;
};

#endif // PANKNOB_H
