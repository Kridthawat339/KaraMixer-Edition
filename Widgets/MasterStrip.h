#ifndef MASTERSTRIP_H
#define MASTERSTRIP_H

#include "Widgets/DbLabel.h"
#include "Widgets/CustomFXList.h"
#include "Widgets/VSTLabel.h"
#include <QListWidget>
#include <QScrollBar>
#include <QMouseEvent>
#include <functional>
#include <QFrame>
#include <QPainter>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPalette>
#include <QFont>
#include <QFontMetrics>
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
    QPushButton *btnMute = nullptr;
    QPushButton *btnHide = nullptr;   // hides every mixer strip, leaving only this master strip
    bool hidePlaced = false;
    bool blinkDim = false;
    QLabel *fxHint = nullptr;
    bool mutePlaced = false;
    bool columnPlaced = false;
    int foldDrop = 0;                 // pixels the strips are folded by (PAN / REVERB row hidden)
    QWidget *trimBox = nullptr;
    QLabel *trimCaption = nullptr;
    QPushButton *unityBtn = nullptr;
    QPushButton *trimDn = nullptr;
    QPushButton *trimUp = nullptr;
    LEDVu *vu;
    QLabel *lbDb;
    QLabel *lbName;
    CustomFXList *fxList = nullptr;
    std::function<void()> onResized;   // the mixer re-aligns MUTE with the name tags

    // set by the mixer dialog (this header-only widget has no signals of its own)
    std::function<void(int, bool)> onFxBypass;          // fxIndex, bypass
    std::function<void(int)> onFxOpen;                  // fxIndex (double click)
    std::function<void(int, const QPoint &)> onFxMenu;  // fxIndex (-1 = empty area), global position

    explicit MasterStrip(QWidget *parent = nullptr) : QFrame(parent)
    {
        setObjectName("masterStrip");
        setFixedWidth(88);   // same width as the Menu button above it
        setMinimumHeight(398);   // grows to reach the bottom edge of the strip area
        setStyleSheet("QFrame#masterStrip { background: transparent; border: none; }");

        lbName = new QLabel(QStringLiteral("MASTER"), this);
        lbName->setAlignment(Qt::AlignCenter);
        lbName->setFixedHeight(22);
        lbName->setStyleSheet("QLabel { background-color: #e5384a; color: #ffffff; border-radius: 4px;"
                              " font-weight: bold; font-size: 9pt; }");

        fader = new FaderSlider(this);
        fader->setShowScale(true);
        fader->setMaximumLevel(100);
        fader->setFixedWidth(48);
        // red neon instead of the theme's accent colour
        QPalette pal = fader->palette();
        pal.setColor(QPalette::Link, QColor(0xf2, 0x2b, 0x3d));
        fader->setPalette(pal);
        fader->setProperty("solidNeon", true);   // keep the level bar / handle stripe pure red (no pale highlight)

        vu = new LEDVu(this);
        vu->setMaximumLevel(127);
        vu->setFixedWidth(14);

        lbDb = new DbLabel(this);
        lbDb->setAlignment(Qt::AlignCenter);
        
        QFont f = lbDb->font();
        f.setFamily(QStringLiteral("DejaVu Sans"));   // bundled font (Fonts/DejaVu) - has the infinity sign
        f.setPointSizeF(7.5);
        f.setBold(true);
        lbDb->setFont(f);

        QVBoxLayout *lv = new QVBoxLayout(this);
        lv->setContentsMargins(3, 2, 3, 2);   // bottom 2 = same as the instrument strips, so MUTE lines up with the name tags
        lv->setSpacing(4);
        lv->addWidget(lbName);

        // FX list: sits where the instrument strips have their effect list + M/S row, so the fader below
        // starts at the same height as the other faders (2 + 49 + 4 + 17 + 4 = 76 from the top).
        // Right-click (or click on the empty area) = add an effect, double-click a row = open it.
        fxList = new CustomFXList(this);
        fxList->setFixedHeight(44);
        fxList->setFocusPolicy(Qt::NoFocus);
        fxList->setSelectionMode(QAbstractItemView::NoSelection);
        fxList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        fxList->setToolTip(QStringLiteral("Master FX - click here (or right-click) to add an effect"));
        fxList->setStyleSheet("QListWidget { background: transparent; border: none; }"
                              "QListWidget::item, QListWidget::item:selected, QListWidget::item:hover,"
                              "QListWidget::item:selected:active, QListWidget::item:selected:!active"
                              " { background: transparent; border: none; }");
        fxList->verticalScrollBar()->setStyleSheet(
            "QScrollBar:vertical { width: 5px; margin: 3px 0 3px 0; background: transparent; border: none; }"
            "QScrollBar::handle:vertical { background: #d23342; border-radius: 2px; min-height: 8px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { width: 0; height: 0; border: none; background: none; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }");
        fxList->viewport()->installEventFilter(this);
        fxHint = new QLabel(QStringLiteral("+ FX"), fxList->viewport());
        fxHint->setAlignment(Qt::AlignCenter);
        fxHint->setAttribute(Qt::WA_TransparentForMouseEvents);
        fxHint->setStyleSheet("QLabel { background: transparent; color: #d23342; font-weight: bold; font-size: 8pt; border: none; }");
        fxHint->setGeometry(0, 0, 80, 42);
        lv->addWidget(fxList);
        // fader, meter, dB read-out and TRIM block are NOT in the layout: the mixer places them by absolute
        // position, copied from a real strip (placeColumn), so they can never end up shorter / higher than the strips'.
        lv->addStretch(1);

        // TRIM block: fills the space where the instrument strips have their PAN/REVERB knobs and icon
        // (keeps the fader exactly as tall as on the strips). - / + nudge the master one fader step, 0 dB resets.
        QWidget *trim = new QWidget(this);
        trimBox = trim;
        const QString trimBtn =
            "QPushButton { background: transparent; color: #d23342; border: 1px solid #d23342;"
            " border-radius: 4px; font-weight: bold; font-size: 9pt; }"
            "QPushButton:hover { border: 1px solid #f22b3d; color: #f22b3d; }"
            "QPushButton:pressed { background: #e5384a; color: #ffffff; }";
        QLabel *trimCap = new QLabel(QStringLiteral("TRIM"), trim);
        trimCap->setAlignment(Qt::AlignCenter);
        trimCaption = trimCap;
        trimCap->setStyleSheet("QLabel { color: #d23342; font-weight: bold; font-size: 7pt; background: transparent; }");
        QPushButton *btnDn = new QPushButton(QString(QChar(0x2212)), trim);
        QPushButton *btnUp = new QPushButton(QStringLiteral("+"), trim);
        QPushButton *btnUnity = new QPushButton(QStringLiteral("0 dB"), trim);
        unityBtn = btnUnity;
        trimDn = btnDn;
        trimUp = btnUp;
        for (QPushButton *b : { btnDn, btnUp, btnUnity }) {
            b->setFocusPolicy(Qt::NoFocus);
            b->setCursor(Qt::PointingHandCursor);
            b->setFixedHeight(26);
            b->setStyleSheet(trimBtn);
        }
        btnDn->setToolTip(QStringLiteral("Master down one step"));
        btnUp->setToolTip(QStringLiteral("Master up one step"));
        btnUnity->setToolTip(QStringLiteral("Master back to 0 dB"));
        QVBoxLayout *tv = new QVBoxLayout(trim);
        tv->setContentsMargins(4, 0, 4, 0);
        tv->setSpacing(4);
        tv->addStretch(1);
        tv->addWidget(trimCap);
        QHBoxLayout *th = new QHBoxLayout();
        th->setSpacing(4);
        th->addWidget(btnDn);
        th->addWidget(btnUp);
        tv->addLayout(th);
        tv->addWidget(btnUnity);
        tv->addStretch(1);
        QObject::connect(btnDn, &QPushButton::clicked, fader, [this]() { fader->setLevel(qMax(0, fader->level() - 1)); });
        QObject::connect(btnUp, &QPushButton::clicked, fader, [this]() { fader->setLevel(qMin(100, fader->level() + 1)); });
        QObject::connect(btnUnity, &QPushButton::clicked, fader, [this]() { fader->setLevel(FaderTaper::kUnityLevel); });

        btnMute = new QPushButton(QStringLiteral("MUTE"), this);
        btnMute->setCheckable(true);
        btnMute->setFocusPolicy(Qt::NoFocus);
        // height / position are set by the mixer (placeMute), to match the strips' name tags
        btnMute->setCursor(Qt::PointingHandCursor);
        btnMute->setToolTip(QStringLiteral("Mute master"));
        btnMute->setStyleSheet(
            "QPushButton { background: transparent; color: #d23342; border: 1px solid #d23342;"
            " border-radius: 4px; font-weight: bold; font-size: 8pt; }"
            "QPushButton:hover { background: transparent; border: 1px solid #f22b3d; color: #f22b3d; }"
            "QPushButton:checked, QPushButton:checked:hover { background: #e5384a; color: #ffffff; border: 1px solid #ff7a7a; }");

        btnHide = new QPushButton(QStringLiteral("HIDE"), this);
        btnHide->setCheckable(true);
        btnHide->setFocusPolicy(Qt::NoFocus);
        btnHide->setCursor(Qt::PointingHandCursor);
        btnHide->setToolTip(QString::fromUtf8("ซ่อนแถบมิกเซอร์ทั้งหมด เหลือเฉพาะ Master (กดอีกครั้งเพื่อแสดงทั้งหมด)"));
        btnHide->setStyleSheet(btnMute->styleSheet());   // same look; lit red while the strips are hidden

        // while muted the MASTER tag blinks so nobody wonders where the sound went
        const QString tagOn  = lbName->styleSheet();
        const QString tagDim = "QLabel { background-color: #4a1820; color: #ff9aa5; border-radius: 4px;"
                               " font-weight: bold; font-size: 9pt; }";
        QTimer *blink = new QTimer(this);
        blink->setInterval(500);
        QObject::connect(blink, &QTimer::timeout, this, [this, tagOn, tagDim]() {
            blinkDim = !blinkDim;
            lbName->setStyleSheet(blinkDim ? tagDim : tagOn);
        });
        QObject::connect(btnMute, &QPushButton::toggled, this, [this, blink, tagOn](bool on) {
            blinkDim = false;
            lbName->setStyleSheet(tagOn);
            if (on) blink->start(); else blink->stop();
        });

        QObject::connect(fader, static_cast<void (FaderSlider::*)(int)>(&FaderSlider::levelChanged),
                         lbDb, [this](int v) { lbDb->setText(dbText(v)); });
        QObject::connect(fader, &FaderSlider::mouseDoubleClicked, fader, [this]() { fader->setLevel(FaderTaper::kUnityLevel); });

        lbDb->setText(dbText(fader->level()));
    }

    // ---- master FX rows ----
    void addFxLabel(const QString &name, int fxIndex, bool bypass)
    {
        QListWidgetItem *item = new QListWidgetItem();
        VSTLabel *lab = new VSTLabel(fxList, name, fxIndex, bypass);
        QObject::connect(lab, &VSTLabel::byPassChanged, this, [this](int i, bool b) { if (onFxBypass) onFxBypass(i, b); });
        QObject::connect(lab, &VSTLabel::doubleClicked, this, [this](int i) { if (onFxOpen) onFxOpen(i); });
        QObject::connect(lab, &VSTLabel::menuRequested, this, [this, lab](int i, const QPoint &pos) {
            if (onFxMenu) onFxMenu(i, lab->mapToGlobal(pos));
        });
        item->setSizeHint(QSize(0, 16));
        fxList->addItem(item);
        fxList->setItemWidget(item, lab);
        updateFxHint();
        QTimer::singleShot(0, this, [this, item]() { fxList->scrollToItem(item, QAbstractItemView::EnsureVisible); });
    }

    void removeFxLabel(int fxIndex)
    {
        QListWidgetItem *item = fxList->takeItem(fxIndex);
        if (!item) return;
        delete fxList->itemWidget(item);
        delete item;
        for (int i = fxIndex; i < fxList->count(); i++)
            if (VSTLabel *lab = dynamic_cast<VSTLabel *>(fxList->itemWidget(fxList->item(i))))
                lab->setFxIndex(i);
        updateFxHint();
    }

    int fxCount() const { return fxList->count(); }

    void updateFxHint()
    {
        if (fxHint) {
            fxHint->setGeometry(0, 0, fxList->viewport()->width(), fxList->viewport()->height());
            fxHint->setVisible(fxList->count() == 0);
        }
    }

    // exact rectangle (in this strip's coordinates) for the MUTE button
    // The mixer folds the strips' PAN / REVERB row: shrink the TRIM block by the same number of pixels so the
    // master stays exactly as tall as the strips (the TRIM caption is dropped when space gets tight).
    void setCollapse(int drop)
    {
        if (!trimBox) return;
        drop = qMax(0, drop);
        foldDrop = drop;
        updateTrimVisibility();
        setMinimumHeight(qMax(0, 398 - drop));
        updateGeometry();
    }

    // The TRIM block (caption, - / + row, 0 dB button) is shown only as far as it really fits in the box the mixer
    // gave it: on a small screen the strips are short and the box is small, and a block that does not fit used to
    // spill over the HIDE button (text drawn on top of text). Folded strips: the whole block goes.
    void updateTrimVisibility()
    {
        if (!trimBox) return;
        const int h = trimBox->height();
        const bool open = foldDrop <= 0;
        if (trimCaption) trimCaption->setVisible(open && h >= 80);
        if (unityBtn)    unityBtn->setVisible(open && h >= 62);
        if (trimDn)      trimDn->setVisible(open && h >= 30);
        if (trimUp)      trimUp->setVisible(open && h >= 30);
    }

    // Line the fader, meter and dB read-out up with a strip's (the mixer measures that strip): FX box `fxHeight`
    // px tall, fader + meter exactly `faderH` px tall.
    void alignLayout(int fxHeight)
    {
        if (fxHeight > 0 && fxList)
            fxList->setFixedHeight(fxHeight);
        if (layout()) layout()->activate();
    }

    // Absolute placement (this strip's coordinates) copied from a real strip.
    void placeColumn(const QRect &faderR, const QRect &dbR, const QRect &trimR)
    {
        columnPlaced = true;
        fader->setGeometry(faderR);
        vu->setGeometry(faderR.x() + 48 + 4, faderR.y(), 14, faderR.height());
        lbDb->setGeometry(dbR);
        trimBox->setGeometry(trimR);
        updateTrimVisibility();
    }

    void placeHide(const QRect &r)
    {
        hidePlaced = true;
        btnHide->setGeometry(r);
        btnHide->raise();
    }

    void placeMute(const QRect &r)
    {
        mutePlaced = true;
        btnMute->setGeometry(r);
        btnMute->raise();
    }

    // Back to the built-in placement (used when a measured placement came out of the frame - e.g. on a small screen /
    // odd DPI the reference strip was not laid out yet): fader, meter, read-out, MUTE and HIDE are all inside the frame.
    void useDefaultPlacement(bool column = true, bool mute = true, bool hide = true)
    {
        if (column) { columnPlaced = false; applyDefaultColumn(); }
        if (mute && btnMute) { mutePlaced = false; btnMute->setGeometry(3, height() - 24, width() - 6, 22); btnMute->raise(); }
        if (hide && btnHide) { hidePlaced = false; btnHide->setGeometry(3, height() - 24 - 22 - 6, width() - 6, 22); btnHide->raise(); }
    }

protected:
    void applyDefaultColumn()
    {
        if (!fader || !vu || !lbDb || !trimBox)
            return;
        const int top = 76, fh = qMax(100, height() - top - 4 - 22 - 4 - 18 - 4 - 70);
        fader->setGeometry(3, top, 48, fh);
        vu->setGeometry(3 + 48 + 4, top, 14, fh);
        lbDb->setGeometry(3, top + fh + 4, width() - 6, 18);
        trimBox->setGeometry(3, top + fh + 26, width() - 6, qMax(0, height() - (top + fh + 26) - 28 - 28));   // ends above HIDE
        updateTrimVisibility();
    }

    void resizeEvent(QResizeEvent *e) override
    {
        QFrame::resizeEvent(e);
        if (!mutePlaced && btnMute)
            btnMute->setGeometry(3, height() - 24, width() - 6, 22);
        if (!columnPlaced)   // sensible default until the mixer has measured a real strip
            applyDefaultColumn();
        if (onResized)
            onResized();
    }

    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (fxList && obj == fxList->viewport()) {
            if (e->type() == QEvent::Resize)
                updateFxHint();
            else if (e->type() == QEvent::MouseButtonRelease) {
                QMouseEvent *me = static_cast<QMouseEvent *>(e);
                if (!fxList->itemAt(me->pos()) && onFxMenu) {   // empty area -> add menu
                    onFxMenu(-1, me->globalPos());
                    return true;
                }
            }
        }
        return QFrame::eventFilter(obj, e);
    }

    // thin crisp red frame (no fill), painted directly (does not depend on style sheets)
    void paintEvent(QPaintEvent *e) override
    {
        QFrame::paintEvent(e);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setBrush(Qt::NoBrush);   // no red fill: the strip blends with the mixer background
        p.setPen(QPen(QColor(0xd2, 0x33, 0x42), 1.2));
        p.drawRoundedRect(r, 5, 5);

        // plain thin rounded frame around the FX list, like the strips' FX boxes
        if (fxList) {
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(0xd2, 0x33, 0x42, 150), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(fxList->geometry()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        }
    }

public:
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
