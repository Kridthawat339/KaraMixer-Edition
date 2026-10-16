#ifndef PROMENUBADGE_H
#define PROMENUBADGE_H

// Puts a coloured "Pro" right after the text of one menu item, whatever the theme / style sheet draws.
// QMenu cannot colour part of an item's text, so a small transparent label is laid over the menu. Its x position
// is found by looking at the pixels the real item was painted with (the last cluster of ink on the row = the text),
// so it follows the theme's padding / icon size by itself. Header-only, call attach() before menu.exec().

#include <QMenu>
#include <QAction>
#include <QLabel>
#include <QImage>
#include <QPalette>
#include <QFont>
#include <QFontMetrics>
#include <QPointer>
#include <QTimer>
#include <QtMath>

namespace ProMenuBadge {

inline QColor proColor(const QColor &bg)
{
    // readable on both the menu background and the highlighted row, light or dark theme
    return bg.lightness() < 140 ? QColor(0xff, 0xd0, 0x3d) : QColor(0xd2, 0x4a, 0x00);
}

inline void attach(QMenu *menu, QAction *act, const QString &word = QStringLiteral("Pro"))
{
    QLabel *lab = new QLabel(word, menu);
    lab->setAttribute(Qt::WA_TransparentForMouseEvents);
    lab->hide();
    QFont f = menu->font();
    f.setBold(true);
    lab->setFont(f);
    lab->setStyleSheet(QStringLiteral("QLabel { background: transparent; border: none; padding: 0; }"));

    QPointer<QLabel> pl(lab);
    QPointer<QMenu> pm(menu);

    auto recolor = [pl, pm](bool selected) {
        if (!pl || !pm) return;
        const QColor bg = selected ? pm->palette().color(QPalette::Highlight) : pm->palette().color(QPalette::Base);
        pl->setStyleSheet(QStringLiteral("QLabel { background: transparent; border: none; padding: 0; color: %1; }")
                              .arg(proColor(bg).name()));
    };
    recolor(false);

    QObject::connect(menu, &QMenu::hovered, lab, [act, recolor](QAction *a) { recolor(a == act); });

    // after the menu is on screen: find where the item's text ends and put the badge there
    QTimer::singleShot(0, lab, [pl, pm, act, recolor]() {
        if (!pl || !pm) return;
        const QRect g = pm->actionGeometry(act);
        const QFontMetrics fm(pl->font());
        const int h = fm.height() + 2;
        const int w = fm.horizontalAdvance(pl->text()) + 2;

        int x = g.right() - w - 12;   // fallback: right edge of the row

        const QImage im = pm->grab().toImage();
        const qreal dpr = im.devicePixelRatio() > 0 ? im.devicePixelRatio() : 1.0;
        const int y0 = qRound((g.center().y() - 4) * dpr), y1 = qRound((g.center().y() + 4) * dpr);
        const int xl = qRound((g.left() + 2) * dpr), xr = qRound((g.right() - 6) * dpr);
        if (xr > xl && y0 >= 0 && y1 < im.height() && xr < im.width()) {
            const QColor bg = im.pixelColor(xr, qRound(g.center().y() * dpr));
            auto ink = [&](int px) {
                for (int py = y0; py <= y1; py++) {
                    const QColor c = im.pixelColor(px, py);
                    if (qAbs(c.red() - bg.red()) + qAbs(c.green() - bg.green()) + qAbs(c.blue() - bg.blue()) > 90)
                        return true;
                }
                return false;
            };
            const int gap = qRound(9 * dpr);
            int lastRight = -1, runStart = -1, quiet = 0;
            for (int px = xl; px < xr; px++) {
                if (ink(px)) {
                    if (runStart < 0 || quiet > gap) runStart = px;   // a new cluster (icon, then text)
                    lastRight = px;
                    quiet = 0;
                } else if (runStart >= 0) {
                    quiet++;
                }
            }
            if (lastRight > 0 && runStart > xl + qRound(8 * dpr))
                x = qRound(lastRight / dpr) + fm.horizontalAdvance(QLatin1Char(' ')) + 3;
        }

        pl->setGeometry(x, g.center().y() - h / 2, w, h);
        recolor(pm->activeAction() == act);
        pl->show();
        pl->raise();
    });
}

} // namespace ProMenuBadge

#endif // PROMENUBADGE_H
