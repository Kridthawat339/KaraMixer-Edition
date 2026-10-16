#ifndef SYSMONITOR_H
#define SYSMONITOR_H

// Slim "CPU | RAM" live monitor for the free space in the mixer's top bar.
// Two rounded tiles, each with a mini history graph, a load bar and the value.
// (No Q_OBJECT on purpose: header-only, uses a QTimer functor connection.)

#include <QWidget>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QPalette>
#include <QVector>
#include <QFontMetrics>
#include <QToolTip>
#include "Theme.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

class SysMonitor : public QWidget
{
public:
    explicit SysMonitor(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(27);
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        cpuHist.fill(0.0, HIST);
        ramHist.fill(0.0, HIST);
        sample();   // prime the CPU counters
        timer.setInterval(1000);
        QObject::connect(&timer, &QTimer::timeout, [this]() { sample(); update(); });
    }

    QSize sizeHint() const override { return QSize(300, 27); }
    QSize minimumSizeHint() const override { return QSize(0, 27); }

protected:
    void showEvent(QShowEvent *e) override { QWidget::showEvent(e); sample(); timer.start(); }
    void hideEvent(QHideEvent *e) override { QWidget::hideEvent(e); timer.stop(); }

    void paintEvent(QPaintEvent *) override
    {
        if (width() < 70)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const int gap = 8;
        const int cw = (width() - gap) / 2;
        const QRectF cpuR(0.5, 1.5, cw - 1, height() - 3);
        const QRectF ramR(cw + gap + 0.5, 1.5, width() - cw - gap - 1, height() - 3);
        drawTile(p, cpuR, QStringLiteral("CPU"), cpuHist, cpuNow,
                 QStringLiteral("%1%").arg(qRound(cpuNow * 100)));
        QString ramText = QStringLiteral("%1%").arg(qRound(ramNow * 100));
        if (ramR.width() > 175)
            ramText = QStringLiteral("%1 / %2 GB  \u00b7  %3")
                          .arg(usedGb, 0, 'f', 1).arg(totalGb, 0, 'f', 1).arg(ramText);
        drawTile(p, ramR, QStringLiteral("RAM"), ramHist, ramNow, ramText);
    }

    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::ToolTip) {
            setToolTip(tr("CPU %1%\nRAM %2 / %3 GB (%4%)")
                           .arg(qRound(cpuNow * 100)).arg(usedGb, 0, 'f', 1)
                           .arg(totalGb, 0, 'f', 1).arg(qRound(ramNow * 100)));
        }
        return QWidget::event(e);
    }

private:
    static const int HIST = 40;
    QTimer timer;
    QVector<double> cpuHist, ramHist;
    double cpuNow = 0, ramNow = 0, usedGb = 0, totalGb = 0;
#ifdef Q_OS_WIN
    quint64 lastIdle = 0, lastKernel = 0, lastUser = 0;
    static quint64 ft(const FILETIME &f) { return (quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime; }
#endif

    void sample()
    {
#ifdef Q_OS_WIN
        FILETIME i, k, u;
        if (GetSystemTimes(&i, &k, &u)) {
            const quint64 idle = ft(i), kern = ft(k), user = ft(u);
            const quint64 dI = idle - lastIdle, dK = kern - lastKernel, dU = user - lastUser;
            const quint64 total = dK + dU;   // kernel time already includes idle
            if (lastKernel != 0 && total > 0)
                cpuNow = qBound(0.0, 1.0 - double(dI) / double(total), 1.0);
            lastIdle = idle; lastKernel = kern; lastUser = user;
        }
        MEMORYSTATUSEX m;
        m.dwLength = sizeof(m);
        if (GlobalMemoryStatusEx(&m)) {
            totalGb = double(m.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
            usedGb = double(m.ullTotalPhys - m.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
            ramNow = qBound(0.0, double(m.dwMemoryLoad) / 100.0, 1.0);
        }
#endif
        cpuHist.removeFirst(); cpuHist.append(cpuNow);
        ramHist.removeFirst(); ramHist.append(ramNow);
    }

    static QColor levelColor(const QColor &base, double v)
    {
        if (v >= 0.85) return QColor(0xff, 0x4d, 0x4d);
        if (v >= 0.65) return QColor(0xff, 0xb0, 0x20);
        return base;
    }

    void drawTile(QPainter &p, const QRectF &r, const QString &label, const QVector<double> &hist,
                  double now, const QString &valueText)
    {
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        const QColor base = Theme::isOrange() ? QColor(0xff, 0x9a, 0x3c) : QColor(0x3f, 0xc1, 0xff);
        const QColor lvl = levelColor(base, now);

        // card
        QPainterPath card;
        card.addRoundedRect(r, 7, 7);
        p.fillPath(card, dark ? QColor(255, 255, 255, 14) : QColor(0, 0, 0, 14));
        p.setPen(QPen(QColor(lvl.red(), lvl.green(), lvl.blue(), 90), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(card);

        p.save();
        p.setClipPath(card);

        // history graph (area + line)
        const int n = hist.size();
        const double step = r.width() / double(n - 1);
        const double gh = r.height() - 5;
        QPainterPath line;
        for (int i = 0; i < n; i++) {
            const QPointF pt(r.left() + i * step, r.bottom() - 3 - hist[i] * gh);
            if (i == 0) line.moveTo(pt); else line.lineTo(pt);
        }
        QPainterPath area = line;
        area.lineTo(r.right(), r.bottom());
        area.lineTo(r.left(), r.bottom());
        area.closeSubpath();
        QLinearGradient ag(0, r.top(), 0, r.bottom());
        ag.setColorAt(0, QColor(lvl.red(), lvl.green(), lvl.blue(), 95));
        ag.setColorAt(1, QColor(lvl.red(), lvl.green(), lvl.blue(), 8));
        p.fillPath(area, ag);
        p.setPen(QPen(QColor(lvl.red(), lvl.green(), lvl.blue(), 190), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);

        // load bar along the bottom edge
        const QRectF track(r.left(), r.bottom() - 3, r.width(), 3);
        p.fillRect(track, QColor(128, 128, 128, 55));
        QLinearGradient bg(track.left(), 0, track.right(), 0);
        bg.setColorAt(0, base);
        bg.setColorAt(1, lvl);
        p.fillRect(QRectF(track.left(), track.top(), track.width() * now, track.height()), bg);
        p.restore();

        // texts
        QFont f = font();
        f.setPixelSize(10);
        f.setBold(true);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
        p.setFont(f);
        p.setPen(lvl);
        p.drawText(QRectF(r.left() + 9, r.top(), 34, r.height() - 3), Qt::AlignVCenter | Qt::AlignLeft, label);

        QFont v = font();
        v.setPixelSize(12);
        v.setBold(true);
        p.setFont(v);
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(QRectF(r.left() + 40, r.top(), r.width() - 49, r.height() - 3),
                   Qt::AlignVCenter | Qt::AlignRight, valueText);
    }
};

#endif // SYSMONITOR_H
