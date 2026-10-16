#ifndef DIALOGSKIN_H
#define DIALOGSKIN_H

// Gives every application dialog (Settings, EQ, Reverb, ...) the same themed
// neon title bar as the mixer window. Installed once as an application-wide
// event filter. Header-only, no Q_OBJECT (no moc step).

#include <QObject>
#include <QDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QAbstractButton>
#include <QMenu>
#include <QApplication>
#include <QHash>
#include <QSet>
#include <QEvent>
#include <QMouseEvent>
#include <QWindow>
#include <QByteArray>

#include "Widgets/TitleBar.h"

class DialogSkin : public QObject
{
public:
    explicit DialogSkin(QObject *parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        switch (e->type()) {
        case QEvent::Polish:
        case QEvent::Resize:
        case QEvent::WindowTitleChange:
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
            break;
        default:
            return false;
        }

        if (!o->isWidgetType())
            return false;

        if (e->type() == QEvent::Polish) {
            if (QMenu *m = qobject_cast<QMenu *>(o)) {
                if (m->isWindow() && !m->isVisible() && !m->property("buaiRounded").toBool()) {
                    m->setProperty("buaiRounded", true);
                    m->setWindowFlag(Qt::FramelessWindowHint, true);
                    m->setWindowFlag(Qt::NoDropShadowWindowHint, true);
                    m->setAttribute(Qt::WA_TranslucentBackground, true);
                }
                return false;
            }
        }

        QDialog *dlg = qobject_cast<QDialog *>(o);
        if (!dlg)
            return false;

        if (e->type() == QEvent::Polish) {
            inheritStayOnTop(dlg);
            skin(dlg);
            return false;
        }

        auto it = skins.find(dlg);
        if (it == skins.end())
            return false;

        switch (e->type()) {
        case QEvent::Resize:
            it->bar->setGeometry(0, 0, dlg->width(), H);
            it->bar->raise();
            it->frame->setGeometry(dlg->rect());
            it->frame->raise();
            break;
        case QEvent::WindowTitleChange:
            it->bar->setTitle(dlg->windowTitle());
            break;
        case QEvent::MouseMove: {
            Qt::Edges ed = edgesAt(dlg, static_cast<QMouseEvent *>(e)->pos());
            dlg->setCursor(edgeCursor(ed));
            break;
        }
        case QEvent::MouseButtonPress: {
            QMouseEvent *me = static_cast<QMouseEvent *>(e);
            if (me->button() == Qt::LeftButton) {
                Qt::Edges ed = edgesAt(dlg, me->pos());
                if (ed && dlg->windowHandle()) {
                    dlg->windowHandle()->startSystemResize(ed);
                    return true;
                }
            }
            break;
        }
        default:
            break;
        }
        return false;
    }

private:
    static const int H = 36;

    struct Skin {
        TitleBar *bar;
        FrameOverlay *frame;
    };
    QHash<QObject *, Skin> skins;
    QSet<QObject *> busy;

    static Qt::Edges edgesAt(QDialog *dlg, const QPoint &pos)
    {
        Qt::Edges ed;
        const int m = 6;
        const bool canW = dlg->minimumWidth() != dlg->maximumWidth();
        const bool canH = dlg->minimumHeight() != dlg->maximumHeight();
        if (canW && pos.x() < m)
            ed |= Qt::LeftEdge;
        else if (canW && pos.x() >= dlg->width() - m)
            ed |= Qt::RightEdge;
        if (canH && pos.y() >= dlg->height() - m)
            ed |= Qt::BottomEdge;
        return ed;
    }

    static Qt::CursorShape edgeCursor(Qt::Edges ed)
    {
        const bool h = ed & (Qt::LeftEdge | Qt::RightEdge);
        const bool v = ed & Qt::BottomEdge;
        if (h && v)
            return (ed & Qt::LeftEdge) ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor;
        if (h)
            return Qt::SizeHorCursor;
        if (v)
            return Qt::SizeVerCursor;
        return Qt::ArrowCursor;
    }

    // A dialog opened while a "stay on top" window (the mixer) is showing must not end up behind it.
    static void inheritStayOnTop(QDialog *dlg)
    {
        if (!dlg->isWindow() || dlg->isVisible())
            return;
        if (dlg->windowFlags() & Qt::WindowStaysOnTopHint)
            return;
        const QWidgetList tops = QApplication::topLevelWidgets();
        for (QWidget *w : tops) {
            // the mixer's "On Top" is switched natively (SetWindowPos), so its Qt flag alone is not reliable:
            // it also marks itself with the "kmTopmost" property
            if (w != dlg && w->isVisible()
                && ((w->windowFlags() & Qt::WindowStaysOnTopHint) || w->property("kmTopmost").toBool())) {
                dlg->setWindowFlag(Qt::WindowStaysOnTopHint, true);
                break;
            }
        }
    }

    void skin(QDialog *dlg)
    {
        if (skins.contains(dlg) || busy.contains(dlg))
            return;
        if (!dlg->isWindow() || dlg->isVisible())
            return;
        if (dlg->property("buaiNoSkin").toBool())
            return;
        // Qt's own dialogs (QMessageBox, QInputDialog, QColorDialog, QFileDialog) get the same skin.
        // Native file dialogs are not Qt widgets and are left alone.
        if (dlg->windowFlags() & Qt::FramelessWindowHint)
            return;

        busy.insert(dlg);

        // Yes / No / OK / Cancel buttons of the message and input boxes: a little bigger, easier to hit
        // (size only - the texts are untouched). Other dialogs get a slightly bigger OK / Cancel row too.
        const bool box = qobject_cast<QMessageBox *>(dlg) || qobject_cast<QInputDialog *>(dlg);
        for (QDialogButtonBox *bb : dlg->findChildren<QDialogButtonBox *>()) {
            for (QAbstractButton *b : bb->buttons()) {
                QFont bf = b->font();
                bf.setPointSizeF(bf.pointSizeF() > 0 ? bf.pointSizeF() + (box ? 1.5 : 1.0) : 10.0);
                bf.setBold(true);
                b->setFont(bf);
                b->setMinimumSize(box ? 104 : 92, box ? 44 : 38);
            }
        }

        Qt::WindowFlags f = dlg->windowFlags();
        f |= Qt::FramelessWindowHint;
        f &= ~Qt::WindowContextHelpButtonHint;
        dlg->setWindowFlags(f);

        // make room for the title bar (the layout respects contents margins)
        int l, t, r, b;
        dlg->getContentsMargins(&l, &t, &r, &b);
        dlg->setContentsMargins(l, t + H, r, b);

        QSize mn = dlg->minimumSize();
        QSize mx = dlg->maximumSize();
        if (mx.height() < QWIDGETSIZE_MAX)
            dlg->setMaximumHeight(mx.height() + H);
        if (mn.height() > 0)
            dlg->setMinimumHeight(mn.height() + H);
        dlg->resize(dlg->width(), dlg->height() + H);

        TitleBar *bar = new TitleBar(dlg->windowTitle(), dlg);
        bar->minimizeButton->hide();
        QObject::connect(bar->closeButton, &QAbstractButton::clicked, dlg, [dlg]() { dlg->reject(); });
        bar->setGeometry(0, 0, dlg->width(), H);
        bar->show();

        FrameOverlay *frame = new FrameOverlay(dlg);
        frame->setGeometry(dlg->rect());
        frame->show();
        frame->raise();

        dlg->setMouseTracking(true);

        skins.insert(dlg, Skin{ bar, frame });
        busy.remove(dlg);
        QObject::connect(dlg, &QObject::destroyed, this, [this](QObject *obj) { skins.remove(obj); });
    }
};

#endif // DIALOGSKIN_H
