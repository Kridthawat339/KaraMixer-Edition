#ifndef DBLABEL_H
#define DBLABEL_H

// QLabel that draws the "-<infinity> dB" read-out itself (as a vector shape), so it never
// depends on the installed fonts having an infinity glyph. Header-only, no Q_OBJECT.

#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QtMath>

class DbLabel : public QLabel
{
public:
    explicit DbLabel(QWidget *parent = nullptr) : QLabel(parent) {}

protected:
    void paintEvent(QPaintEvent *e) override
    {
        const QString t = text();
        if (!t.startsWith(QLatin1String("-inf"))) {   // "-inf dB" is drawn with a vector infinity sign
            QLabel::paintEvent(e);
            return;
        }

        const QString rest = t.mid(4);   // " dB"
        const QFontMetrics fm(font());
        const QString minus = QStringLiteral("-");
        const qreal mw = fm.horizontalAdvance(minus);
        const qreal iw = fm.height() * 0.95;                         // width of the drawn infinity
        const qreal ih = iw * 0.42;
        const qreal rw = fm.horizontalAdvance(rest);
        const qreal gap = 1.0;
        const qreal total = mw + gap + iw + gap + rw;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setFont(font());
        p.setPen(palette().color(QPalette::WindowText));

        const QRectF r = contentsRect();
        qreal x = r.left() + (r.width() - total) / 2.0;
        const qreal base = r.top() + (r.height() - fm.height()) / 2.0 + fm.ascent();

        p.drawText(QPointF(x, base), minus);
        x += mw + gap;

        // lemniscate of Bernoulli, centred on the x-height line
        const qreal cx = x + iw / 2.0;
        const qreal cy = base - fm.xHeight() / 2.0;
        QPainterPath path;
        const int n = 64;
        for (int i = 0; i <= n; ++i) {
            const qreal a = 2.0 * M_PI * i / n;
            const qreal s = qSin(a), c = qCos(a);
            const qreal d = 1.0 + s * s;
            const QPointF pt(cx + (iw / 2.0) * c / d, cy + ih * 1.35 * s * c / d);
            if (i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        QPen pen(palette().color(QPalette::WindowText));
        pen.setWidthF(qMax<qreal>(1.2, fm.height() / 9.0));
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.drawPath(path);

        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(QPointF(x + iw + gap, base), rest);
    }
};

#endif // DBLABEL_H
