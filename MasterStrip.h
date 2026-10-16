#ifndef MASTERSTRIP_H
#define MASTERSTRIP_H

#include "Widgets/DbLabel.h"
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPalette>
#include <QFont>
#include <cmath>

#include "Midi/FaderTaper.h"
#include "Widgets/FaderSlider.h"
#include "Widgets/LEDVu.h"

// Master fader strip: laid out like an instrument strip (fader with dB ruler + level meter +
// dB read-out + name tag) but in crimson, so it is obvious at a glance that it is the master.
// Header-only, no signals of its own: connect to `fader` directly.
class MasterStrip : public QFrame
{
public:
    FaderSlider *fader;
    LEDVu *vu;
    QLabel *lbDb;
    QLabel *lbName;

    explicit MasterStrip(QWidget *parent = nullptr) : QFrame(parent)
    {
        setObjectName("masterStrip");
        setFixedSize(76, 322);   // same height as the instrument strips; the fader takes the extra length
        setStyleSheet("QFrame#masterStrip { background: rgba(229, 56, 74, 22); border: 1px solid #d23342; border-radius: 5px; }");   // thin crisp red frame, tint kept faint so it does not outshine the other strips

        lbName = new QLabel(QStringLiteral("MASTER"), this);
        lbName->setAlignment(Qt::AlignCenter);
        lbName->setFixedHeight(22);
        lbName->setStyleSheet("QLabel { background-color: #e5384a; color: #ffffff; border-radius: 3px;"
                              " font-weight: bold; font-size: 9pt; }");

        fader = new FaderSlider(this);
        fader->setShowScale(true);
        fader->setMaximumLevel(100);
        fader->setMinimumHeight(121);
        fader->setFixedWidth(48);
        // red neon instead of the theme's accent colour
        QPalette pal = fader->palette();
        pal.setColor(QPalette::Link, QColor(0xf2, 0x2b, 0x3d));
        fader->setPalette(pal);
        fader->setProperty("solidNeon", true);   // keep the level bar / handle stripe pure red (no pale highlight)

        vu = new LEDVu(this);
        vu->setMaximumLevel(127);
        vu->setFixedWidth(14);
        vu->setMinimumHeight(121);

        lbDb = new DbLabel(this);
        lbDb->setAlignment(Qt::AlignCenter);
        
        QFont f = lbDb->font();
        f.setFamily(QStringLiteral("DejaVu Sans"));   // bundled font (Fonts/DejaVu) - has the infinity sign
        f.setPointSizeF(7.5);
        f.setBold(true);
        lbDb->setFont(f);

        QHBoxLayout *mid = new QHBoxLayout();
        mid->setContentsMargins(0, 0, 0, 0);
        mid->setSpacing(4);
        mid->addWidget(fader);
        mid->addWidget(vu);

        QVBoxLayout *lv = new QVBoxLayout(this);
        lv->setContentsMargins(3, 5, 3, 5);
        lv->setSpacing(4);
        lv->addWidget(lbName);
        lv->addLayout(mid, 1);
        lv->addWidget(lbDb);

        QObject::connect(fader, static_cast<void (FaderSlider::*)(int)>(&FaderSlider::levelChanged),
                         lbDb, [this](int v) { lbDb->setText(dbText(v)); });
        QObject::connect(fader, &FaderSlider::mouseDoubleClicked, fader, [this]() { fader->setLevel(FaderTaper::kUnityLevel); });

        lbDb->setText(dbText(fader->level()));
    }

    // level 0..100: 50 = 0 dB, 100 = +12 dB (Midi/FaderTaper.h)
    static QString dbText(int level)
    {
        if (FaderTaper::isSilent(level))
            return QStringLiteral("-inf dB");
        const double d = FaderTaper::db(level);
        return (d > 0.05 ? QStringLiteral("+") : QString()) + QString::number(d, 'f', 1) + QStringLiteral(" dB");
    }

    void setLevel(int v) { fader->setLevel(v); lbDb->setText(dbText(v)); }

    void peak(int v)
    {
        if (v > vu->level())
            vu->peak(v);
    }
};

#endif // MASTERSTRIP_H
