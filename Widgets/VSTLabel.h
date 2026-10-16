#ifndef VSTLABEL_H
#define VSTLABEL_H

#include <QWidget>

class QGraphicsDropShadowEffect;

namespace Ui {
class VSTLabel;
}

class VSTLabel : public QWidget
{
    Q_OBJECT

public:
    explicit VSTLabel(QWidget *parent = 0, const QString &label = QString(),
                      int fxIndex = 0, bool bypass = false);
    ~VSTLabel();

    int getFxIndex() { return fxIndex; }
    void setFxIndex(int i) { fxIndex = i; }
    void setLabelText(const QString &text);
    void setIndicatorOnly(bool only) { indicatorOnly = only; }

signals:
    void byPassChanged(int index, bool bypass);
    void doubleClicked(int index);
    void menuRequested(int index, const QPoint &pos);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event);
    void changeEvent(QEvent *event);
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void on_btn_clicked();
    void contextMenuRequested(const QPoint &pos);

private:
    Ui::VSTLabel *ui;
    void applyNeon();
    void updateBtnStyle();

    QString neonKey;
    QGraphicsDropShadowEffect *btnGlow = nullptr;
    QGraphicsDropShadowEffect *frameGlow = nullptr;
    bool fxBypass = false;
    bool indicatorOnly = false;
    int fxIndex = 0;
};

#endif // VSTLABEL_H
