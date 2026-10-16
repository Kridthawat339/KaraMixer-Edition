#include "VSTLabel.h"
#include "ui_VSTLabel.h"

#include <QMouseEvent>
#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QPainter>


VSTLabel::VSTLabel(QWidget *parent, const QString &label, int fxIndex, bool bypass) :
    QWidget(parent),
    ui(new Ui::VSTLabel)
{
    ui->setupUi(this);

    this->fxIndex = fxIndex;
    this->fxBypass = bypass;

    ui->label->setText(label);
    ui->label->setToolTip(label);

    // neon glow (colors follow the theme)
    btnGlow = new QGraphicsDropShadowEffect(ui->btn);
    btnGlow->setOffset(0, 0);
    btnGlow->setBlurRadius(9);
    ui->btn->setFixedSize(12, 12);
    ui->btn->installEventFilter(this);

    // small, crisp FX name
    {
        QFont f = ui->label->font();
        f.setPointSizeF(6.5);
        f.setBold(false);
        f.setWeight(QFont::Medium);
        f.setHintingPreference(QFont::PreferFullHinting);
        ui->label->setFont(f);
    }
    ui->btn->setGraphicsEffect(btnGlow);

    frameGlow = new QGraphicsDropShadowEffect(ui->frame);
    frameGlow->setOffset(0, 0);
    frameGlow->setBlurRadius(10);
    ui->frame->setGraphicsEffect(frameGlow);

    applyNeon();

    connect(this, SIGNAL(customContextMenuRequested(QPoint)),
            this, SLOT(contextMenuRequested(QPoint)));
}

VSTLabel::~VSTLabel()
{
    delete ui;
}

void VSTLabel::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        applyNeon();
    QWidget::changeEvent(event);
}

void VSTLabel::applyNeon()
{
    const QColor neon = palette().color(QPalette::Link);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;

    // setStyleSheet can itself trigger palette events: only act when the colors really changed
    const QString key = neon.name() + (dark ? "d" : "l");
    if (key == neonKey)
        return;
    neonKey = key;

    const QColor core = dark ? neon.lighter(150) : neon;

    setStyleSheet(QString("#frame { border: 1px solid %1; border-radius: 4px; background: rgba(%2, %3, %4, 30); }"
                          "#label { color: %5; background: transparent; }")
                  .arg(neon.name()).arg(neon.red()).arg(neon.green()).arg(neon.blue()).arg(core.name()));

    QColor g = neon;
    g.setAlpha(dark ? 150 : 110);
    frameGlow->setColor(g);

    updateBtnStyle();
}

void VSTLabel::updateBtnStyle()
{
    const bool dark = palette().color(QPalette::Window).lightness() < 128;

    // power light: neon green when the effect is on, dim ring when bypassed (painted in eventFilter)
    const QColor green = dark ? QColor("#39ff88") : QColor("#10c55a");

    QColor glow = green;
    glow.setAlpha(255);
    btnGlow->setColor(glow);
    btnGlow->setBlurRadius(8);
    btnGlow->setEnabled(!fxBypass);
    ui->btn->update();
}

bool VSTLabel::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == ui->btn && event->type() == QEvent::Paint)
    {
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        const QColor green = dark ? QColor("#39ff88") : QColor("#10c55a");

        QPainter p(ui->btn);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(ui->btn->rect()).adjusted(2.5, 2.5, -2.5, -2.5);  // 7px circle
        if (fxBypass)
        {
            p.setPen(QPen(palette().color(QPalette::Mid), 1.2));
            p.setBrush(Qt::NoBrush);
        }
        else
        {
            p.setPen(QPen(green.lighter(140), 1.0));
            p.setBrush(green);
        }
        p.drawEllipse(r);
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void VSTLabel::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    emit doubleClicked(fxIndex);
}

void VSTLabel::setLabelText(const QString &text)
{
    ui->label->setText(text);
    ui->label->setToolTip(text);
}

void VSTLabel::on_btn_clicked()
{
    if (indicatorOnly)
        return;

    fxBypass = !fxBypass;

    updateBtnStyle();

    emit byPassChanged(fxIndex, fxBypass);
}

void VSTLabel::contextMenuRequested(const QPoint &pos)
{
    emit menuRequested(fxIndex, pos);
}
