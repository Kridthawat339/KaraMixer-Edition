#include "CustomFXList.h"

#include <QScrollBar>

CustomFXList::CustomFXList(QWidget *parent) : QListWidget(parent)
{
    // the slim scroll bar stays visible whenever there are more effects than fit in the box
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void CustomFXList::enterEvent(QEvent *event)
{
    QListWidget::enterEvent(event);
}

void CustomFXList::leaveEvent(QEvent *event)
{
    QListWidget::leaveEvent(event);
}
