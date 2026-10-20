#include "InstCh.h"
#include "ui_InstCh.h"

#include "VSTLabel.h"
#include <QToolTip>
#include <QTimer>
#include <QEvent>
#include <QLabel>
#include <QFontMetrics>
#include <QScrollBar>
#include <QStyle>
#include <QCursor>
#include <QMouseEvent>
#include <QColor>
#include "Midi/FaderTaper.h"
#include "Widgets/DbLabel.h"
#include <QLayout>
#include "Widgets/PanKnob.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QSignalBlocker>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLocale>
#include <QEvent>

// fader level 0..100 -> dB text (50 = 0 dB, 100 = +12 dB, see Midi/FaderTaper.h)
static QString levelToDbText(int level)
{
    if (FaderTaper::isSilent(level))
        return QStringLiteral("-inf dB");
    const double d = FaderTaper::db(level);
    return (d > 0.05 ? QStringLiteral("+") : QString()) + QString::number(d, 'f', 1) + QStringLiteral(" dB");
}

// dB read-out of the strip: fader level + trim
static QString levelTrimToDbText(int level, double trim)
{
    if (FaderTaper::isSilent(level))
        return QStringLiteral("-inf dB");
    const double d = FaderTaper::db(level) + trim;
    return (d > 0.05 ? QStringLiteral("+") : QString()) + QString::number(d, 'f', 1) + QStringLiteral(" dB");
}

// Tooltips use the system tooltip font, which may lack the infinity glyph -> plain text there.
static QString levelToTipText(int level)
{
    if (FaderTaper::isSilent(level))
        return QStringLiteral("-inf dB");
    return levelToDbText(level);
}


// pan read-out for the knob tooltip
static QString panText(int v)
{
    if (v == 0)
        return QStringLiteral("Pan: C");
    return QStringLiteral("Pan: ") + (v < 0 ? QStringLiteral("L ") : QStringLiteral("R ")) + QString::number(qAbs(v));
}

static QString panValueText(int v)
{
    if (v == 0)
        return QStringLiteral("0%");
    return (v < 0 ? QStringLiteral("L ") : QStringLiteral("R ")) + QString::number(qAbs(v)) + QStringLiteral("%");
}

static QString revCaptionText(int v)
{
    return v <= 0 ? QStringLiteral("REVERB") : QStringLiteral("REV ") + QString::number(v) + QStringLiteral("%");
}

// Trim read-out: sign, one decimal, always Western digits
static QString trimText(double db)
{
    return (db > 0.0 ? QStringLiteral("+") : QString()) + QLocale::c().toString(db, 'f', 1);   // no unit: the strip is narrow
}

static QString revText(int v)
{
    return QStringLiteral("Reverb: ") + QString::number(v) + QStringLiteral("%");
}

// colour of the name tag, per instrument family (same idea as a console's colour-coded strips)
static QColor nameColorFor(InstrumentType t)
{
    const int i = static_cast<int>(t);
    if (t == InstrumentType::WeSing) return QColor(0xFF, 0x6E, 0xA8);   // WeSing app strip
    if (t == InstrumentType::Canthai) return QColor(0xE8, 0x7A, 0x3A);   // khaen: same family colour as the pipe strip
    if (i <= 4)  return QColor(0xE0, 0xA0, 0x30);   // keys / chromatic / percussive
    if (i == 5)  return QColor(0x8A, 0x6B, 0xE8);   // bass
    if (i <= 12) return QColor(0xE0, 0x5A, 0x9A);   // guitars
    if (i <= 18) return QColor(0xE8, 0x7A, 0x3A);   // brass / reeds / pipe
    if (i <= 20) return QColor(0x3A, 0xB8, 0x8A);   // strings / ensemble
    if (i <= 25) return QColor(0x3A, 0x9A, 0xE0);   // synths / ethnic / fx
    if (i <= 28) return QColor(0x4A, 0x6A, 0xE8);   // kick / snare / side stick
    if (i <= 31) return QColor(0x9A, 0x4A, 0xD8);   // toms
    if (i <= 35) return QColor(0xE0, 0xC0, 0x30);   // hihat / cowbell / cymbals
    if (i <= 41) return QColor(0x4A, 0xC0, 0x6A);   // latin / thai / etc.
    if (i <= 45) return QColor(0x2A, 0xB8, 0xC8);   // VSTi
    // bus groups: one saturated colour per bus (16 distinct)
    static const QColor bus[16] = {
        QColor(0xE5, 0x48, 0x4D), QColor(0xE8, 0x79, 0x3A), QColor(0xE0, 0xA0, 0x30), QColor(0xB8, 0xC0, 0x30),
        QColor(0x4A, 0xC0, 0x6A), QColor(0x2A, 0xB8, 0xC8), QColor(0x3A, 0x9A, 0xE0), QColor(0x4A, 0x6A, 0xE8),
        QColor(0x8A, 0x6B, 0xE8), QColor(0x9A, 0x4A, 0xD8), QColor(0xE0, 0x5A, 0x9A), QColor(0xD1, 0x4D, 0x72),
        QColor(0x7A, 0xB8, 0x4A), QColor(0x3A, 0xB8, 0x8A), QColor(0xB8, 0x7A, 0x4A), QColor(0x7A, 0x84, 0xA0) };
    const int b = i - static_cast<int>(InstrumentType::BusGroup1);
    return bus[(b >= 0 ? b : 0) % 16];
}


InstCh::InstCh(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::InstCh)
{
    ui->setupUi(this);


    // instrument name: slightly smaller font, fixed height so every strip stays aligned
    {
        QFont nf = ui->lbName->font();
        ui->lbName->setFixedHeight(QFontMetrics(nf).height() + 4);
        nf.setPointSizeF(8);
        ui->lbName->setFont(nf);
    }

    btnDefault      = "font: bold 10pt; border-radius: 2px;"
                      "border: 1px solid rgb(158, 158, 158);";

    btnMuteStyle    = "color: rgb(255, 255, 255);font: bold 10pt;"
                      "border-radius: 2px;"
                      "border: 1px solid rgb(158, 158, 158);"
                      "background-color: rgb(229, 57, 53);";

    btnSoloStyle    = "color: rgb(255, 255, 255);font: bold 10pt;"
                      "border-radius: 2px;"
                      "border: 1px solid rgb(158, 158, 158);"
                      "background-color: rgb(46, 184, 114);";

    QString fxListScroll = "QScrollBar:vertical {width: 8px;} ";

    QScrollBar *bar = ui->fxList->verticalScrollBar();
    bar->setStyleSheet("QScrollBar:vertical { width: 5px; margin: 7px 0 7px 0; background: transparent; border: none; }"
                       "QScrollBar::handle:vertical { background: palette(highlight); border-radius: 2px; min-height: 8px; }"
                       "QScrollBar::handle:vertical:hover, QScrollBar::handle:vertical:pressed { background: palette(link); }"
                       "QScrollBar[hot=\"true\"]::handle:vertical { background: palette(link); }"
                       "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { width: 0; height: 0; border: none; background: none; }"
                       "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }");

    // Whole FX box lights the scrollbar while the mouse is inside it (or dragging).
    ui->fxList->installEventFilter(this);
    ui->fxList->viewport()->installEventFilter(this);
    bar->installEventFilter(this);

    // Selecting / hovering an effect must not tint the row (it changed the colors of the FX label)
    ui->fxList->setFocusPolicy(Qt::NoFocus);
    ui->fxList->setStyleSheet("QListWidget::item, QListWidget::item:selected, QListWidget::item:hover,"
                              "QListWidget::item:selected:active, QListWidget::item:selected:!active"
                              " { background: transparent; border: none; }");


    ui->slider->setShowScale(true);
    ui->slider->setMaximumLevel(100);
    ui->slider->setLevel(50);
    ui->slider->setToolTip(levelToTipText(50));

    {
        // swap the designer label for one that draws the infinity sign itself
        QLabel *oldLb = ui->lbDb;
        DbLabel *nl = new DbLabel(oldLb->parentWidget());
        nl->setAlignment(Qt::AlignCenter);
        if (oldLb->parentWidget() && oldLb->parentWidget()->layout())
            oldLb->parentWidget()->layout()->replaceWidget(oldLb, nl);
        oldLb->hide();
        oldLb->deleteLater();
        ui->lbDb = nl;
    }
    {
        QFont df = ui->lbDb->font();
        df.setFamily(QStringLiteral("DejaVu Sans"));   // bundled font (Fonts/DejaVu) - has the infinity sign
        df.setPointSizeF(7.5);
        df.setBold(true);
        ui->lbDb->setFont(df);
        ui->lbDb->setText(levelToDbText(50));
    }

    // pan knob (L / R either side, value read-out below) and reverb knob (name below), under the dB read-out
    // - double-click on a knob = back to default
    {
        QWidget *row = new QWidget(this);
        QGridLayout *g = new QGridLayout(row);
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(1);
        g->setVerticalSpacing(0);

        panDial = new PanKnob(row, true, QColor(79, 195, 247));
        panDial->setToolTip(panText(0));
        revDial = new PanKnob(row, false, QColor(59, 140, 255));
        revDial->setToolTip(revText(0));

        auto cap = [row](const QString &s, double pt = 6.5) {
            QLabel *l = new QLabel(s, row);
            l->setAlignment(Qt::AlignCenter);
            QFont f = l->font();
            f.setPointSizeF(pt);
            f.setBold(true);
            l->setFont(f);
            l->setAttribute(Qt::WA_TransparentForMouseEvents);
            return l;
        };
        QLabel *lbL = cap(QStringLiteral("L"));
        QLabel *lbR = cap(QStringLiteral("R"));
        lbPanVal = cap(panValueText(0), 6.5);
        lbRevCap = cap(revCaptionText(0), 5.5);
        lbL->setFixedWidth(9);
        lbR->setFixedWidth(9);
        lbPanVal->setFixedHeight(12);
        lbRevCap->setFixedHeight(12);

        g->addWidget(lbL, 0, 0, Qt::AlignRight | Qt::AlignVCenter);
        g->addWidget(panDial, 0, 1, Qt::AlignHCenter);
        g->addWidget(lbR, 0, 2, Qt::AlignLeft | Qt::AlignVCenter);
        g->addWidget(lbPanVal, 1, 0, 1, 3);
        g->addWidget(revDial, 2, 0, 1, 3, Qt::AlignHCenter);
        g->addWidget(lbRevCap, 3, 0, 1, 3);

        // fine level trim of the strip, like the master's TRIM: - / + move it 0.1 dB (hold to repeat),
        // the middle button shows the value and resets it to 0 dB when clicked (range -12 .. +12 dB)
        {
            QWidget *tr = new QWidget(row);
            QHBoxLayout *th = new QHBoxLayout(tr);
            th->setContentsMargins(0, 2, 0, 0);
            th->setSpacing(1);
            QPushButton *dn = new QPushButton(QString(QChar(0x2212)), tr);   // true minus sign: as wide as the plus
            QPushButton *up = new QPushButton(QStringLiteral("+"), tr);
            lbTrim = new QPushButton(trimText(0.0), tr);
            for (QPushButton *b : { dn, up, lbTrim }) {
                b->setFocusPolicy(Qt::NoFocus);
                b->setCursor(Qt::PointingHandCursor);
            }
            dn->setFixedSize(16, 15);
            up->setFixedSize(16, 15);
            dn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            up->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            lbTrim->setFixedHeight(15);
            lbTrim->setMinimumWidth(0);
            lbTrim->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);   // takes what is left between - and +
            tr->setMinimumWidth(0);
            dn->setAutoRepeat(true);
            up->setAutoRepeat(true);
            dn->setAutoRepeatDelay(350);
            up->setAutoRepeatDelay(350);
            dn->setAutoRepeatInterval(60);
            up->setAutoRepeatInterval(60);
            dn->setToolTip(QString::fromUtf8("Trim ลดลง 0.1 dB (กดค้างเพื่อลดต่อเนื่อง)"));
            up->setToolTip(QString::fromUtf8("Trim เพิ่มขึ้น 0.1 dB (กดค้างเพื่อเพิ่มต่อเนื่อง)"));
            lbTrim->setToolTip(QString::fromUtf8("Trim: ปรับความดังละเอียดของช่องนี้ (หน่วย dB, -12 ถึง +12)\nคลิกที่ตัวเลข = กลับเป็น 0"));
            th->addWidget(dn);
            th->addWidget(lbTrim, 1);
            th->addWidget(up);
            g->addWidget(tr, 4, 0, 1, 3);
            connect(dn, &QPushButton::clicked, this, [this]() { setTrimInternal(trimVal - 0.1, true); });
            connect(up, &QPushButton::clicked, this, [this]() { setTrimInternal(trimVal + 0.1, true); });
            connect(lbTrim, &QPushButton::clicked, this, [this]() { setTrimInternal(0.0, true); });
            trimDn = dn;
            trimUp = up;
            applyTrimStyle();
        }

        knobRow = row;
        QVBoxLayout *vl = qobject_cast<QVBoxLayout *>(layout());
        int at = vl ? vl->indexOf(ui->lbDb) : -1;
        if (vl && at >= 0)
            vl->insertWidget(at + 1, row, 0, Qt::AlignHCenter);

        connect(panDial, &PanKnob::valueChanged, this, [this](int v) {
            panDial->setToolTip(panText(v));
            lbPanVal->setText(panValueText(v));
            QToolTip::showText(QCursor::pos(), panText(v));
            emit panChanged(instType, v);
        });
        connect(revDial, &PanKnob::valueChanged, this, [this](int v) {
            revDial->setToolTip(revText(v));
            lbRevCap->setText(revCaptionText(v));
            QToolTip::showText(QCursor::pos(), revText(v));
            emit reverbChanged(instType, v);
        });
    }

    ui->vuBar->setMaximumLevel(127);

    ui->btnMute->setStyleSheet(btnDefault);
    ui->btnSolo->setStyleSheet(btnDefault);

    setInstrumentType(InstrumentType::Piano);

    connect(ui->btnMute, SIGNAL(clicked()), this, SLOT(onBtnMuteClicked()));
    connect(ui->btnSolo, SIGNAL(clicked()), this, SLOT(onBtnSoloClicked()));
    connect(ui->slider, SIGNAL(userLevelChanged(int)),
            this, SLOT(onSliderUserLevelChanged(int)));
    connect(ui->slider, SIGNAL(mouseDoubleClicked()),
            this, SLOT(onSliderDoubleClicked()));

    connect(this, SIGNAL(customContextMenuRequested(QPoint)),
            this, SLOT(contextMenuRequested(QPoint)));
}

InstCh::~InstCh()
{
    delete ui;
}

int InstCh::setKnobCollapse(double p)
{
    if (!knobRow)
        return 0;
    if (baseHeight < 0)
        baseHeight = maximumHeight();            // the strip's normal (fixed) height
    if (knobRowHeight <= 0) {
        knobRowHeight = qMax(1, knobRow->sizeHint().height());
        knobSpacing = layout() ? qMax(0, layout()->spacing()) : 0;
    }
    p = qBound(0.0, p, 1.0);

    int drop = 0;
    if (p >= 1.0) {
        knobRow->setVisible(false);
        drop = knobRowHeight + knobSpacing;
    } else {
        knobRow->setVisible(true);
        knobRow->setMaximumHeight(p <= 0.0 ? QWIDGETSIZE_MAX : qMax(0, qRound(knobRowHeight * (1.0 - p))));
        drop = qRound(knobRowHeight * p);
    }
    const int h = baseHeight - drop;
    setMinimumHeight(h);
    setMaximumHeight(h);
    updateGeometry();
    return drop;
}

LEDVu *InstCh::vuBar()
{
    return ui->vuBar;
}

QString InstCh::instrumentName()
{
    return ui->lbName->text();
}

QWidget *InstCh::dbLabel() const
{
    return ui->lbDb;
}

QWidget *InstCh::nameTag() const
{
    return ui->lbName;
}

QString InstCh::fullInstrumentName()
{
    return ui->lbName->toolTip();
}

void InstCh::addFXLabel(const QString &label, int fxIndex, bool bypass)
{
    QListWidgetItem *item = new QListWidgetItem();

    VSTLabel *vstLabel = new VSTLabel(ui->fxList, label, fxIndex, bypass);

    connect(vstLabel, SIGNAL(byPassChanged(int,bool)),
            this, SLOT(onFxByPassChanged(int,bool)));
    connect(vstLabel, SIGNAL(doubleClicked(int)),
            this, SLOT(onFxDoubleClicked(int)));
    connect(vstLabel, SIGNAL(menuRequested(int,QPoint)),
            this, SLOT(onFxMenuRequested(int,QPoint)));

    // FX rows always stay above the "BusN" row
    item->setSizeHint(QSize(0, 16));   // 3 rows fit in the list; the widget is never clipped by a smaller default row
    ui->fxList->insertItem(fxCount(), item);
    ui->fxList->setItemWidget(item, vstLabel);

    // bring the new row into view, but never hide the first rows by jumping to the very bottom
    QTimer::singleShot(0, this, [this, item]() { ui->fxList->scrollToItem(item, QAbstractItemView::EnsureVisible); });
}

void InstCh::removeVSTLabel(int fxIndex)
{
    QListWidgetItem *item = ui->fxList->takeItem(fxIndex);
    QWidget *widget = ui->fxList->itemWidget(item);
    delete widget;
    delete item;

    for (int i=fxIndex; i<fxCount(); i++) {
        item = ui->fxList->item(i);
        widget = ui->fxList->itemWidget(item);
        VSTLabel *label = dynamic_cast<VSTLabel*>(widget);
        label->setFxIndex(i);
    }
}

void InstCh::setInstrumentType(InstrumentType t)
{
    instType = t;

    const QColor c = nameColorFor(t);
    const QString txt = c.lightness() > 120 ? "#10121a" : "#ffffff";
    ui->lbName->setStyleSheet(QString("QLabel { background-color: %1; color: %2; border-radius: 4px; }")
                              .arg(c.name(), txt));
}

void InstCh::setInstrumentName(const QString &name)
{
    ui->lbName->setText(name);
}

void InstCh::setFullInstrumentName(const QString &name)
{
    ui->lbName->setToolTip(name);
    ui->lbImage->setToolTip(name);
}

void InstCh::setInstrumentNames(const QString &name, const QString &tooltip)
{
    ui->lbName->setText(name);
    ui->lbName->setToolTip(tooltip);
    ui->lbImage->setToolTip(tooltip);
}

void InstCh::setInstrumentImage(const QImage &img)
{
    ui->lbImage->setPixmap(QPixmap::fromImage(img));
}

void InstCh::setMuteButton(bool m)
{
    if (m == mute)
        return;

    mute = m;
    if (mute)
        ui->btnMute->setStyleSheet(btnMuteStyle);
    else
        ui->btnMute->setStyleSheet(btnDefault);
}

void InstCh::setSoloButton(bool s)
{
    if (s == solo)
        return;

    solo = s;
    if (solo)
        ui->btnSolo->setStyleSheet(btnSoloStyle);
    else
        ui->btnSolo->setStyleSheet(btnDefault);
}

void InstCh::setSliderLevel(int v)
{
    ui->slider->setLevel(v);
    ui->slider->setToolTip(levelToTipText(v));
    ui->lbDb->setText(levelTrimToDbText(v, trimVal));
}

void InstCh::setPanValue(int pan)
{
    if (!panDial)
        return;
    QSignalBlocker b(panDial);
    panDial->setValue(pan);
    panDial->setToolTip(panText(pan));
    if (lbPanVal)
        lbPanVal->setText(panValueText(pan));
}

void InstCh::setReverbValue(int level)
{
    if (!revDial)
        return;
    QSignalBlocker b(revDial);
    revDial->setValue(level);
    revDial->setToolTip(revText(level));
    if (lbRevCap)
        lbRevCap->setText(revCaptionText(level));
}

void InstCh::setTrimValue(double db)
{
    setTrimInternal(db, false);
}

void InstCh::setTrimInternal(double db, bool notify)
{
    db = qBound(-12.0, qRound(db * 10.0) / 10.0, 12.0);     // 0.1 dB steps, no float drift
    if (db == trimVal && lbTrim && lbTrim->text() == trimText(db))
        return;                                                // nothing changed
    trimVal = db;
    if (lbTrim)
        lbTrim->setText(trimText(db));
    ui->slider->setTrimDb(db);                                       // the knob follows the trim, in fine steps
    ui->lbDb->setText(levelTrimToDbText(ui->slider->level(), db));  // read-out = fader + trim (matches the knob)
    if (notify)
        emit trimChanged(instType, db);
}

void InstCh::peak(int v)
{
    if (v <= ui->vuBar->level() )
        return;

    ui->vuBar->peak(v);
}

void InstCh::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && ui->lbImage->underMouse())
        emit imageDoubleClicked(instType);
}

void InstCh::onBtnMuteClicked()
{
    mute = !mute;

    if (mute) {
        ui->btnMute->setStyleSheet(btnMuteStyle);
    }
    else {
        ui->btnMute->setStyleSheet(btnDefault);
    }

    emit muteChanged(instType, mute);
}

void InstCh::onBtnSoloClicked()
{
    solo = !solo;

    if (solo) {
        ui->btnSolo->setStyleSheet(btnSoloStyle);
    }
    else {
        ui->btnSolo->setStyleSheet(btnDefault);
    }

    emit soloChanged(instType, solo);
}

void InstCh::onSliderUserLevelChanged(int v)
{
    ui->slider->setToolTip(levelToTipText(v));
    ui->lbDb->setText(levelTrimToDbText(v, trimVal));
    QToolTip::showText(QCursor::pos(), levelToTipText(v));
    emit sliderLevelChanged(instType, v);
}

void InstCh::onSliderDoubleClicked()
{
    emit sliderDoubleClicked(instType);
}

void InstCh::contextMenuRequested(const QPoint &pos)
{
    // right click on the instrument picture = choose its soundfont (strips 1..42 only)
    if (ui->lbImage->rect().contains(ui->lbImage->mapFromGlobal(QCursor::pos())) && (static_cast<int>(instType) < 42 || instType == InstrumentType::Canthai)) {
        emit imageMenuRequested(instType, QCursor::pos());
        return;
    }
    emit menuRequested(instType, pos);
}

void InstCh::onFxMenuRequested(int fxIndex, const QPoint &pos)
{
    //QListWidgetItem *item = ui->fxList->item(fxIndex);
    //QWidget *widget = ui->fxList->itemWidget(item);
    //emit fxRemoveMenuRequested(instType, fxIndex, widget->mapToGlobal(pos));
    emit fxRemoveMenuRequested(instType, fxIndex, pos);
}

void InstCh::onFxByPassChanged(int fxIndex, bool bypass)
{
    emit fxByPassChanged(instType, fxIndex, bypass);
}

void InstCh::onFxDoubleClicked(int fxIndex)
{
    emit fxDoubleClicked(instType, fxIndex);
}

// A "BusN" row, shown like an FX row (same size/font/green light) at the end of the FX list
void InstCh::setBusBadge(int bus)
{
    if (bus < 0) {
        if (busItem) {
            int row = ui->fxList->row(busItem);
            QListWidgetItem *it = ui->fxList->takeItem(row);
            delete ui->fxList->itemWidget(it);
            delete it;
            busItem = nullptr;
            busLabel = nullptr;
        }
        return;
    }

    const QString text = QString("Bus%1").arg(bus + 1);
    if (!busItem) {
        busItem = new QListWidgetItem();
        busLabel = new VSTLabel(ui->fxList, text, -1, false);
        busLabel->setIndicatorOnly(true);
        connect(busLabel, &VSTLabel::menuRequested, this, [this](int, const QPoint &pos) {
            emit busMenuRequested(instType, busLabel->mapToGlobal(pos));
        });
        busItem->setSizeHint(QSize(0, 16));
        ui->fxList->addItem(busItem);
        ui->fxList->setItemWidget(busItem, busLabel);
    } else {
        busLabel->setLabelText(text);
    }
    busLabel->setToolTip(tr("ส่งออกไปที่ Bus Group %1").arg(bus + 1));
}

int InstCh::fxCount() const
{
    return ui->fxList->count() - (busItem ? 1 : 0);
}


bool InstCh::eventFilter(QObject *obj, QEvent *e)
{

    if (e->type() == QEvent::Enter || e->type() == QEvent::Leave ||
        e->type() == QEvent::MouseButtonRelease)
    {
        QScrollBar *bar = ui->fxList->verticalScrollBar();
        bool hot = ui->fxList->rect().contains(ui->fxList->mapFromGlobal(QCursor::pos())) || bar->isSliderDown();
        if (bar->property("hot").toBool() != hot) {
            bar->setProperty("hot", hot);
            bar->style()->unpolish(bar);
            bar->style()->polish(bar);
            bar->update();
        }
    }
    return QWidget::eventFilter(obj, e);
}


// Trim row colours come from the current theme (the accent colour of the faders), pushed lighter or darker until it
// reads clearly on the window colour; small, whole-pixel font sizes keep the text crisp.
void InstCh::applyTrimStyle()
{
    if (!lbTrim || !trimDn || !trimUp)
        return;
    const QColor bg = palette().color(QPalette::Window);
    QColor c = palette().color(QPalette::Link);
    if (!c.isValid())
        c = palette().color(QPalette::Highlight);
    const bool dark = bg.lightness() < 128;
    for (int i = 0; i < 8 && qAbs(c.lightness() - bg.lightness()) < 110; i++)
        c = dark ? c.lighter(120) : c.darker(125);
    const QColor hover = dark ? c.lighter(125) : c.darker(125);
    const QString css = QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: 1px solid %1; border-radius: 3px;"
        " font-weight: bold; font-size: 9px; padding: 0px; margin: 0px; min-width: 0px; }"
        "QPushButton:hover { color: %2; border: 1px solid %2; }"
        "QPushButton:pressed { background: %1; color: %3; }")
            .arg(c.name(), hover.name(), bg.name());
    const QString signs = css + QStringLiteral("QPushButton { font-size: 12px; }");
    lbTrim->setStyleSheet(css);
    trimDn->setStyleSheet(signs);
    trimUp->setStyleSheet(signs);
}

void InstCh::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::ApplicationPaletteChange)
        applyTrimStyle();
}
