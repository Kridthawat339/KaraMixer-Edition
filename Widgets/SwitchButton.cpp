#include "SwitchButton.h"

#include <QPainter>
#include <QMouseEvent>


SwitchButton::SwitchButton(QWidget *parent) : QWidget(parent)
{
    animation = new QPropertyAnimation(this, "offset");
    animation->setDuration(120);

    QFont f = font();
    f.setBold(true);

    setFont(f);
}

SwitchButton::~SwitchButton()
{
    delete animation;
}

void SwitchButton::setOffset(int ost)
{
    _x = ost;
    update();
}

void SwitchButton::on()
{
    if (_on)
        return;

    _on = true;
    animation->setStartValue(_x);
    animation->setEndValue(width() - height()+2);
    animation->start();

    emit switchChanged(true);
}

void SwitchButton::off()
{
    if (!_on)
        return;

    _on = false;
    animation->setStartValue(_x);
    animation->setEndValue(2);
    animation->start();

    emit switchChanged(false);
}

void SwitchButton::setOnText(const QString &ot)
{
    _onText = ot;
    update();
}

void SwitchButton::setOffText(const QString &ot)
{
    _offText = ot;
    update();
}

void SwitchButton::enterEvent(QEvent *event)
{
    setCursor(Qt::PointingHandCursor);
    QWidget::enterEvent(event);
}

void SwitchButton::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (_on)
        off();
    else
        on();

    emit userSwitchChanged(_on);
    QWidget::mouseReleaseEvent(event);
}

void SwitchButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QColor neon = palette().color(QPalette::Link);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    const qreal rad = r.height() / 2.0;

    QColor track, border, text;
    if (_on)
    {
        track = dark ? QColor(0x39, 0xff, 0x88) : QColor(0x10, 0xc5, 0x5a);
        border = track.darker(120);
        text = dark ? QColor(0x06, 0x2b, 0x14) : QColor(Qt::white);
    }
    else
    {
        track = dark ? QColor(0x2c, 0x31, 0x3d) : QColor(0xd4, 0xd8, 0xe0);
        border = dark ? QColor(0x5a, 0x62, 0x75) : QColor(0x9a, 0xa1, 0xb0);
        text = dark ? QColor(0xc8, 0xcf, 0xdc) : QColor(0x4a, 0x50, 0x5e);
    }
    Q_UNUSED(neon);

    p.setPen(QPen(border, 1.2));
    p.setBrush(track);
    p.drawRoundedRect(r, rad, rad);

    QFont f = font();
    f.setBold(true);
    f.setPixelSize(qMax(9, int(height() * 0.45)));
    p.setFont(f);
    p.setPen(text);

    const int knob = height() - 4;
    if (_on)
        p.drawText(QRectF(r.left() + 2, r.top(), r.width() - knob - 6, r.height()), Qt::AlignCenter, _onText);
    else
        p.drawText(QRectF(r.left() + knob + 4, r.top(), r.width() - knob - 6, r.height()), Qt::AlignCenter, _offText);

    p.setPen(QPen(border, 1.2));
    p.setBrush(_on ? QColor(Qt::white) : (dark ? QColor(0xe6, 0xea, 0xf2) : QColor(Qt::white)));
    p.drawEllipse(QRectF(_x, 2, knob, knob));
}
