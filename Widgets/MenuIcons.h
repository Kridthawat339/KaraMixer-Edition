#ifndef MENUICONS_H
#define MENUICONS_H

// Crisp, theme-aware line icons for the main menu (drawn in code, so they follow dark / light themes).

#include <QApplication>
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QScreen>
#include <QtMath>

namespace MenuIcons {

enum Kind {
    Settings, Channel, Bus, Speaker, Vst, Meter, Eq, Chorus, Reverb,
    Soundfont, Reset, Save, Open, Language, Theme, About, Exit, Phone
};

inline bool isDark()
{
    return qApp->palette().color(QPalette::Window).lightness() < 128;
}

inline QColor normalColor()
{
    // clearly visible on both themes
    return qApp->palette().color(QPalette::Text);
}

inline QColor dimColor()
{
    QColor c = qApp->palette().color(QPalette::Text);
    c.setAlpha(150);
    return c;
}

inline QColor onColor()
{
    return isDark() ? QColor("#39ff88") : QColor("#10b957");
}

inline QIcon make(Kind kind, const QColor &color)
{
    const int S = 18;
    qreal dpr = 1.0;
    if (QScreen *scr = QGuiApplication::primaryScreen())
        dpr = scr->devicePixelRatio();

    QPixmap pm(int(S * dpr), int(S * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (kind) {
    case Settings: {
        p.drawEllipse(QPointF(9, 9), 5.2, 5.2);
        p.drawEllipse(QPointF(9, 9), 1.9, 1.9);
        for (int i = 0; i < 8; i++) {
            qreal a = i * M_PI / 4;
            p.drawLine(QPointF(9 + 5.2 * qCos(a), 9 + 5.2 * qSin(a)),
                       QPointF(9 + 7.6 * qCos(a), 9 + 7.6 * qSin(a)));
        }
        break; }
    case Channel:
        p.drawLine(QPointF(2, 9), QPointF(7, 9));
        p.drawLine(QPointF(7, 9), QPointF(15, 3.5));
        p.drawLine(QPointF(7, 9), QPointF(16, 9));
        p.drawLine(QPointF(7, 9), QPointF(15, 14.5));
        break;
    case Bus:
        for (int y : { 4, 9, 14 }) {
            p.drawEllipse(QPointF(3.5, y), 1.1, 1.1);
            p.drawLine(QPointF(7, y), QPointF(16, y));
        }
        break;
    case Speaker:
        p.drawRoundedRect(QRectF(3.5, 1.5, 11, 15), 2, 2);
        p.drawEllipse(QPointF(9, 11), 2.8, 2.8);
        p.drawEllipse(QPointF(9, 5), 0.9, 0.9);
        break;
    case Vst:
        p.drawRoundedRect(QRectF(3, 7, 12, 8), 2, 2);
        p.drawLine(QPointF(6.5, 2), QPointF(6.5, 7));
        p.drawLine(QPointF(11.5, 2), QPointF(11.5, 7));
        p.drawLine(QPointF(9, 15), QPointF(9, 17));
        break;
    case Meter:
        p.drawLine(QPointF(3, 15), QPointF(3, 11));
        p.drawLine(QPointF(6.5, 15), QPointF(6.5, 7));
        p.drawLine(QPointF(10, 15), QPointF(10, 3));
        p.drawLine(QPointF(13.5, 15), QPointF(13.5, 9));
        p.drawLine(QPointF(1.5, 16.5), QPointF(16.5, 16.5));
        break;
    case Eq: {
        const qreal xs[3] = { 4.5, 9, 13.5 };
        const qreal ks[3] = { 11, 5, 9 };
        for (int i = 0; i < 3; i++) {
            p.drawLine(QPointF(xs[i], 2), QPointF(xs[i], 16));
            p.setBrush(color);
            p.drawRoundedRect(QRectF(xs[i] - 2.2, ks[i] - 1.4, 4.4, 2.8), 1, 1);
            p.setBrush(Qt::NoBrush);
        }
        break; }
    case Chorus:
        for (int k = 0; k < 2; k++) {
            QPainterPath w;
            const qreal y = k == 0 ? 6.2 : 11.8;
            w.moveTo(1.5, y);
            w.cubicTo(4, y - 5, 6, y - 5, 9, y);
            w.cubicTo(12, y + 5, 14, y + 5, 16.5, y);
            QPen pp = pen;
            if (k == 1) { QColor c2 = color; c2.setAlphaF(0.55); pp.setColor(c2); }
            p.setPen(pp);
            p.drawPath(w);
        }
        break;
    case Reverb:
        p.setBrush(color);
        p.drawEllipse(QPointF(3, 9), 1.5, 1.5);
        p.setBrush(Qt::NoBrush);
        for (int r : { 5, 8, 11 }) {
            QRectF rc(3 - r, 9 - r, 2 * r, 2 * r);
            QPen pp = pen;
            QColor c2 = color; c2.setAlphaF(r == 5 ? 1.0 : (r == 8 ? 0.75 : 0.5)); pp.setColor(c2);
            p.setPen(pp);
            p.drawArc(rc, -42 * 16, 84 * 16);
        }
        break;
    case Soundfont:
        p.drawRoundedRect(QRectF(2, 2.5, 14, 13), 2, 2);
        p.drawLine(QPointF(2, 7), QPointF(16, 7));
        p.drawLine(QPointF(2, 11.3), QPointF(16, 11.3));
        p.drawLine(QPointF(7, 2.5), QPointF(7, 15.5));
        break;
    case Reset:
        p.drawArc(QRectF(3, 3, 12, 12), 40 * 16, 280 * 16);
        p.drawLine(QPointF(13.3, 2.3), QPointF(13.3, 5.6));
        p.drawLine(QPointF(13.3, 5.6), QPointF(10, 5.6));
        break;
    case Save:
        p.drawRoundedRect(QRectF(2.5, 2.5, 13, 13), 1.8, 1.8);
        p.drawRect(QRectF(5.5, 2.5, 7, 4.2));
        p.drawRect(QRectF(5.5, 10, 7, 5.5));
        break;
    case Open: {
        QPainterPath f;
        f.moveTo(2, 14.5); f.lineTo(2, 4); f.lineTo(7, 4); f.lineTo(8.5, 6); f.lineTo(15, 6);
        f.lineTo(15, 14.5); f.closeSubpath();
        p.drawPath(f);
        p.drawLine(QPointF(2, 14.5), QPointF(4.2, 9));
        p.drawLine(QPointF(4.2, 9), QPointF(17, 9));
        break; }
    case Language:
        p.drawEllipse(QPointF(9, 9), 7, 7);
        p.drawEllipse(QPointF(9, 9), 3, 7);
        p.drawLine(QPointF(2, 9), QPointF(16, 9));
        break;
    case Theme:
        p.drawEllipse(QPointF(9, 9), 7, 7);
        {
            QPainterPath half;
            half.moveTo(9, 2); half.arcTo(QRectF(2, 2, 14, 14), 90, -180); half.closeSubpath();
            p.setBrush(color);
            p.drawPath(half);
            p.setBrush(Qt::NoBrush);
        }
        break;
    case About:
        p.drawEllipse(QPointF(9, 9), 7, 7);
        p.drawLine(QPointF(9, 8), QPointF(9, 12.5));
        p.setBrush(color);
        p.drawEllipse(QPointF(9, 5.5), 0.5, 0.5);
        break;
    case Exit:
        p.drawArc(QRectF(3, 3.5, 12, 12), 120 * 16, -300 * 16);
        p.drawLine(QPointF(9, 1.5), QPointF(9, 8));
        break;
    case Phone:
        p.drawRoundedRect(QRectF(4.5, 1.5, 9, 15), 2, 2);
        p.drawLine(QPointF(7.5, 14), QPointF(10.5, 14));
        p.drawLine(QPointF(7.5, 3.8), QPointF(10.5, 3.8));
        break;
    }
    p.end();
    return QIcon(pm);
}

inline QIcon make(Kind kind) { return make(kind, normalColor()); }

} // namespace MenuIcons

#endif // MENUICONS_H
