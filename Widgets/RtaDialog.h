#ifndef RTADIALOG_H
#define RTADIALOG_H

// Real-time analyzer: 31-band (1/3 octave) spectrum of the master output.
// Header-only, no Q_OBJECT (no moc step). The source callback fills 1024 FFT bins
// (BASS_DATA_FFT2048 layout, 44100 Hz) and returns false when no data is available.

#include <QDialog>
#include <QWidget>
#include <QVBoxLayout>
#include <QPainter>
#include <QLinearGradient>
#include <QTimer>
#include <QElapsedTimer>
#include <QShowEvent>
#include <QHideEvent>
#include <functional>
#include <cmath>

class RtaView : public QWidget
{
public:
    typedef std::function<bool(float *)> Source;

    explicit RtaView(Source src, QWidget *parent = nullptr) : QWidget(parent), source(src)
    {
        for (int i = 0; i < kBands; ++i) { level[i] = kFloor; peak[i] = kFloor; }
        setMinimumSize(520, 240);
        timer = new QTimer(this);
        timer->setInterval(33);
        QObject::connect(timer, &QTimer::timeout, this, [this]() { tick(); });
    }

    void startTimer() { clock.start(); if (!timer->isActive()) timer->start(); }
    void stopTimer() { timer->stop(); }

private:
    static const int kBands = 31;
    static constexpr float kFloor = -80.0f;     // dB at the bottom of the display
    static constexpr float kCeil = 0.0f;
    static constexpr float kSampleRate = 44100.0f;

    Source source;
    QTimer *timer;
    QElapsedTimer clock;
    float level[kBands];
    float peak[kBands];
    float peakAge[kBands] = {0};

    static float centre(int i)
    {
        static const float c[kBands] = { 20, 25, 31.5f, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500,
                                         630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000,
                                         10000, 12500, 16000, 20000 };
        return c[i];
    }

    void tick()
    {
        float fft[1024];
        const float dt = 0.033f;
        bool ok = source && source(fft);
        if (!ok)
            for (int i = 0; i < 1024; ++i) fft[i] = 0.0f;

        const float bin = kSampleRate / 2048.0f;
        for (int b = 0; b < kBands; ++b) {
            const float c = centre(b);
            const float lo = c / 1.1225f, hi = c * 1.1225f;     // 2^(1/6)
            int i0 = int(std::floor(lo / bin)), i1 = int(std::ceil(hi / bin));
            i0 = qBound(1, i0, 1023);
            i1 = qBound(i0, i1, 1023);
            float m = 0.0f;
            for (int i = i0; i <= i1; ++i) m = qMax(m, fft[i]);
            if (i1 - i0 < 1) {                                   // narrow band: interpolate between bins
                const float pos = c / bin;
                const int a = qBound(1, int(pos), 1022);
                const float fr = pos - a;
                m = fft[a] * (1.0f - fr) + fft[a + 1] * fr;
            }
            float db = 20.0f * std::log10(m + 1e-9f);
            db += 3.0f * std::log2(c / 1000.0f) + 6.0f;          // slight pink-noise tilt, like a hardware RTA
            db = qBound(kFloor, db, kCeil);

            if (db > level[b]) level[b] = db;                    // fast attack
            else level[b] = qMax(db, level[b] - 60.0f * dt);     // falls at 60 dB/s

            if (level[b] >= peak[b]) { peak[b] = level[b]; peakAge[b] = 0.0f; }
            else {
                peakAge[b] += dt;
                if (peakAge[b] > 0.8f) peak[b] = qMax(level[b], peak[b] - 25.0f * dt);   // hold, then fall
            }
        }
        update();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(rect(), QColor("#0d0f14"));

        const int L = 34, R = 10, T = 10, B = 24;
        const QRect plot(L, T, width() - L - R, height() - T - B);
        QFont f = font();
        f.setPointSizeF(7.5);
        p.setFont(f);

        // dB grid
        for (int db = 0; db >= -80; db -= 10) {
            const int y = plot.top() + int((kCeil - db) / (kCeil - kFloor) * plot.height());
            p.setPen(QColor(255, 255, 255, db == 0 ? 70 : 28));
            p.drawLine(plot.left(), y, plot.right(), y);
            p.setPen(QColor("#8a91a0"));
            p.drawText(QRect(0, y - 7, L - 4, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(db));
        }

        // bars
        const float slot = plot.width() / float(kBands);
        const float bw = qMax(2.0f, slot * 0.72f);
        QLinearGradient g(0, plot.top(), 0, plot.bottom());
        g.setColorAt(0.00, QColor("#ff3b3b"));
        g.setColorAt(0.22, QColor("#ffb020"));
        g.setColorAt(0.45, QColor("#d8e04a"));
        g.setColorAt(0.70, QColor("#3ccf6a"));
        g.setColorAt(1.00, QColor("#1c8f4c"));
        for (int b = 0; b < kBands; ++b) {
            const float x = plot.left() + b * slot + (slot - bw) / 2.0f;
            const float h = (level[b] - kFloor) / (kCeil - kFloor) * plot.height();
            p.fillRect(QRectF(x, plot.bottom() - h, bw, h), g);
            const float ph = (peak[b] - kFloor) / (kCeil - kFloor) * plot.height();
            p.fillRect(QRectF(x, plot.bottom() - ph - 2, bw, 2), QColor("#f2f4f8"));
        }

        // frequency labels
        p.setPen(QColor("#8a91a0"));
        static const struct { int band; const char *txt; } lab[] = {
            {2, "31"}, {5, "63"}, {8, "125"}, {11, "250"}, {14, "500"}, {17, "1k"},
            {20, "2k"}, {23, "4k"}, {26, "8k"}, {29, "16k"} };
        for (const auto &l : lab) {
            const float x = plot.left() + l.band * slot + slot / 2.0f;
            p.drawText(QRectF(x - 20, plot.bottom() + 4, 40, 14), Qt::AlignCenter, QString::fromLatin1(l.txt));
        }
        p.setPen(QColor(255, 255, 255, 60));
        p.drawRect(plot.adjusted(0, 0, -1, 0));
    }
};

class RtaDialog : public QDialog
{
public:
    explicit RtaDialog(QWidget *parent, RtaView::Source src) : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("RTA - Real Time Analyzer"));
        resize(760, 340);
        QVBoxLayout *lv = new QVBoxLayout(this);
        lv->setContentsMargins(8, 8, 8, 8);
        view = new RtaView(src, this);
        lv->addWidget(view);
    }

protected:
    void showEvent(QShowEvent *e) override { QDialog::showEvent(e); view->startTimer(); }
    void hideEvent(QHideEvent *e) override { view->stopTimer(); QDialog::hideEvent(e); }

private:
    RtaView *view;
};

#endif // RTADIALOG_H
