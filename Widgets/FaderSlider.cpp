#include "FaderSlider.h"

#include <QResizeEvent>
#include <QPainter>
#include <QEvent>
#include <QLinearGradient>
#include <QPixmap>
#include <QFont>
#include <cmath>
#include "Midi/FaderTaper.h"

FaderSlider::FaderSlider(QWidget *parent) : QFrame(parent)
{
    sHandle = new QLabel(this);
    buildHandle();
}

// Fader knob drawn in the current theme colors, with a neon glowing center line.
void FaderSlider::buildHandle()
{
    const qreal dpr = devicePixelRatioF();
    const int w = 19, h = 28;

    QPixmap pm(int(w * dpr), int(h * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QColor neon = palette().color(QPalette::Link);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient g(0, 0, 0, h);
    if (dark) {
        g.setColorAt(0, QColor("#4b4f5c"));
        g.setColorAt(1, QColor("#25272e"));
    } else {
        g.setColorAt(0, QColor("#ffffff"));
        g.setColorAt(1, QColor("#d3d7e0"));
    }
    p.setBrush(g);
    p.setPen(QPen(dark ? QColor("#6b7080") : QColor("#9aa0ae"), 1));
    p.drawRoundedRect(QRectF(1.5, 1.5, w - 3, h - 3), 3, 3);

    // grip ridges
    p.setPen(QPen(dark ? QColor(255, 255, 255, 40) : QColor(0, 0, 0, 35), 1));
    const int ridges[4] = { 6, 8, 20, 22 };
    for (int i = 0; i < 4; i++)
        p.drawLine(QPointF(4, ridges[i]), QPointF(w - 4, ridges[i]));

    // neon line in the middle
    const qreal cy = h / 2.0;
    QColor glow = neon;
    glow.setAlpha(55);
    p.setPen(QPen(glow, 7, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(4, cy), QPointF(w - 4, cy));
    glow.setAlpha(120);
    p.setPen(QPen(glow, 4, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(4, cy), QPointF(w - 4, cy));
    QColor core = (dark && !property("solidNeon").toBool()) ? neon.lighter(165) : neon;
    p.setPen(QPen(core, 1.6, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(4, cy), QPointF(w - 4, cy));
    p.end();

    sHandle->setScaledContents(false);
    sHandle->setFixedSize(w, h);
    sHandle->setPixmap(pm);
}

void FaderSlider::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        buildHandle();
        update();
    }
    QFrame::changeEvent(event);
}

FaderSlider::~FaderSlider()
{
    delete sHandle;
}

void FaderSlider::setLevel(int level)
{
    if (level == sLv)
        return;

    if (level > sMaxLv)
        sLv = sMaxLv;
    else if (level < sMinLv)
        sLv = sMinLv;
    else
        sLv = level;

    moveHandle();

    emit levelChanged(sLv);
    emit levelChanged(QString::number(sLv));
}

void FaderSlider::setMinimumLevel(int minLevel)
{
    if (minLevel == sMinLv || minLevel >= sMaxLv)
        return;

    sMinLv = minLevel;
    if (sLv < sMinLv) {
        sLv = sMinLv;
        emit levelChanged(sLv);
        emit levelChanged(QString::number(sLv));
    }

    moveHandle();
}

void FaderSlider::setMaximumLevel(int maxLevel)
{
    if (maxLevel == sMaxLv || maxLevel <= sMinLv)
        return;

    sMaxLv = maxLevel;
    if (sLv > sMaxLv) {
        sLv = sMaxLv;
        emit levelChanged(sLv);
        emit levelChanged(QString::number(sLv));
    }

    moveHandle();
}

void FaderSlider::setChangeStep(int cstp)
{
    if (cstp < 1)
        return;

    sChangStep = cstp;
}

void FaderSlider::setTickCount(int tc)
{
    if (tc == sTickCount || tc < 0)
        return;

    sTickCount = tc;
    update();
}

void FaderSlider::setEnableMousePress(bool mp)
{
    sMousePress = mp;
}

void FaderSlider::setShowScale(bool show)
{
    if (show == sShowScale)
        return;
    sShowScale = show;
    moveHandle();
}

// x of the fader track: centred normally, pushed to the right when the ruler is shown
qreal FaderSlider::trackX() const
{
    if (sShowScale)
        return width() - sHandle->width() / 2.0 - 1;
    return width() / 2.0;
}

// level under a mouse y: the knob centre follows the cursor (the travel is height - knob height), rounded to the
// nearest step, so the knob doesn't jump when it is grabbed and drags feel continuous
int FaderSlider::levelForY(int y) const
{
    const int abslv = abs(sMaxLv - sMinLv);
    const qreal travel = qMax(1, height() - sHandle->height());
    qreal f = 1.0 - (y - sHandle->height() / 2.0) / travel;
    f = qBound(0.0, f, 1.0);
    return sMinLv + int(std::lround(f * abslv));
}

bool FaderSlider::taperRange() const
{
    return sMinLv == 0 && sMaxLv == FaderTaper::kMaxLevel;
}

// dB of a fractional fader level (same law as FaderTaper::db)
static double dbOfLevelF(double L)
{
    if (L >= FaderTaper::kMaxLevel) return FaderTaper::kTopDb;
    if (L >= FaderTaper::kUnityLevel)
        return (L - FaderTaper::kUnityLevel) * FaderTaper::kTopDb / double(FaderTaper::kMaxLevel - FaderTaper::kUnityLevel);
    return FaderTaper::kFloorDb * (FaderTaper::kUnityLevel - L) / double(FaderTaper::kUnityLevel);
}

qreal FaderSlider::shownLevel() const
{
    if (sTrimDb == 0.0 || !taperRange() || sLv <= 0)
        return sLv;
    const double d = FaderTaper::db(sLv) + sTrimDb;
    return qBound(0.0, FaderTaper::levelForDb(d), double(sMaxLv));
}

int FaderSlider::userLevelForY(int y) const
{
    if (sTrimDb == 0.0 || !taperRange())
        return levelForY(y);
    const qreal travel = qMax(1, height() - sHandle->height());
    qreal f = 1.0 - (y - sHandle->height() / 2.0) / travel;
    f = qBound(0.0, f, 1.0);
    const double shown = f * (sMaxLv - sMinLv);         // where the knob should be drawn
    if (shown <= 0.0)
        return 0;
    const double faderDb = dbOfLevelF(shown) - sTrimDb;  // the fader level that puts the knob there
    return qBound(sMinLv, int(std::lround(FaderTaper::levelForDb(faderDb))), sMaxLv);
}

void FaderSlider::setTrimDb(double db)
{
    if (db == sTrimDb)
        return;
    sTrimDb = db;
    moveHandle();
}

qreal FaderSlider::handleCenterY(int level) const
{
    const int abslv = abs(sMaxLv - sMinLv);
    const int maxHeight = height() - sHandle->height();
    const int absCurrentLv = abs(sMinLv - level);
    return sHandle->height() / 2.0 + (abslv > 0 ? maxHeight * (abslv - absCurrentLv) / double(abslv) : 0);
}

void FaderSlider::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    emit mouseDoubleClicked();
}

void FaderSlider::wheelEvent(QWheelEvent *event)
{
    int lv = sLv;
    if (event->delta() > 0) { // up
        lv += sChangStep;
    }
    else {
        lv -= sChangStep;
    }

    if (lv > sMaxLv)
        lv = sMaxLv;
    else if (lv < sMinLv)
        lv = sMinLv;

    if (lv == sLv)
        return;

    setLevel(lv);

    emit userLevelChanged(level());
    emit userLevelChanged(QString::number(level()));

    event->accept();
}

void FaderSlider::mousePressEvent(QMouseEvent *event)
{
    if (!sMousePress)
        return;

    if (event->button() != Qt::LeftButton)
        return;

    int lv = userLevelForY(event->y());

    if (lv > sMaxLv)
        lv = sMaxLv;
    else if (lv < sMinLv)
        lv = sMinLv;

    if (lv == sLv)
        return;

    setLevel(lv);

    emit userLevelChanged(level());
    emit userLevelChanged(QString::number(level()));
}

void FaderSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() != Qt::LeftButton)
        return;

    int lv = userLevelForY(event->y());

    if (lv > sMaxLv)
        lv = sMaxLv;
    else if (lv < sMinLv)
        lv = sMinLv;

    if (lv == sLv)
        return;

    setLevel(lv);

    emit userLevelChanged(level());
    emit userLevelChanged(QString::number(level()));
}

void FaderSlider::resizeEvent(QResizeEvent *event)
{
    moveHandle();
}

void FaderSlider::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QColor neon = palette().color(QPalette::Link);
    const qreal x = trackX();
    const qreal hy = sHandle->y() + sHandle->height() / 2.0;

    // dB ruler: 0 dB sits in the middle of the travel, +12 dB at the top (Midi/FaderTaper.h)
    if (sShowScale && sMaxLv == FaderTaper::kMaxLevel && sMinLv == 0) {
        QFont f = font();
        f.setPointSizeF(6.5);
        p.setFont(f);
        QColor tc = palette().color(QPalette::WindowText);
        tc.setAlpha(150);
        p.setPen(tc);
        static const int labelled[5] = { 12, 0, -20, -40, -60 };
        static const int ticksOnly[2] = { -10, -30 };
        for (int i = 0; i < 5; i++) {
            const qreal y = handleCenterY(int(std::lround(FaderTaper::levelForDb(labelled[i]))));
            p.drawLine(QPointF(x - 14, y), QPointF(x - 11, y));
            p.drawText(QRectF(0, y - 6, x - 15, 12), Qt::AlignRight | Qt::AlignVCenter,
                       labelled[i] > 0 ? QString("+%1").arg(labelled[i]) : QString::number(labelled[i]));
        }
        for (int i = 0; i < 2; i++) {
            const qreal y = handleCenterY(int(std::lround(FaderTaper::levelForDb(ticksOnly[i]))));
            p.drawLine(QPointF(x - 13, y), QPointF(x - 11, y));
        }
    }

    // whole track: faint neon
    QColor dim = neon;
    dim.setAlpha(dark ? 70 : 90);
    p.setPen(QPen(dim, 2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(x, 0), QPointF(x, height()));

    // part below the knob (the level): bright neon with glow
    QColor glow = neon;
    glow.setAlpha(45);
    p.setPen(QPen(glow, 8, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(x, hy), QPointF(x, height()));
    glow.setAlpha(100);
    p.setPen(QPen(glow, 5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(x, hy), QPointF(x, height()));
    p.setPen(QPen((dark && !property("solidNeon").toBool()) ? neon.lighter(160) : neon, 2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(x, hy), QPointF(x, height()));

    p.end();
}

void FaderSlider::moveHandle()
{
    int abslv = abs(sMaxLv-sMinLv);
    int maxHeight = height() - sHandle->height();

    const qreal absCurrentLv = qAbs(shownLevel() - sMinLv);     // fractional: the strip trim moves the knob in fine steps

    sHandle->move(sShowScale ? this->width() - sHandle->width() - 1 : (this->width() - sHandle->width()) / 2,
                  int(std::lround(maxHeight * (abslv - absCurrentLv) / abslv)));
    update();
}
