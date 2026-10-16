#ifndef FADERSLIDER_H
#define FADERSLIDER_H

#include <QFrame>
#include <QLabel>

class FaderSlider : public QFrame
{
    Q_OBJECT
public:
    explicit FaderSlider(QWidget *parent = nullptr);
    ~FaderSlider();

    int level() { return sLv; }
    int minimumLevel() { return sMinLv; }
    int maximumLevel() { return sMaxLv; }
    int changeStep() { return sChangStep; }
    int tickCount() { return sTickCount; }
    bool isEnableMousePress() { return sMousePress; }
    bool showScale() const { return sShowScale; }

signals:
    void levelChanged(int level);
    void levelChanged(QString level);
    void userLevelChanged(int level);
    void userLevelChanged(QString level);
    void mouseDoubleClicked();

public slots:
    void setLevel(int level);
    void setMinimumLevel(int minLevel);
    void setMaximumLevel(int maxLevel);
    void setChangeStep(int cstp);
    void setTickCount(int tc);
    void setEnableMousePress(bool mp);
    void setShowScale(bool show);   // dB ruler on the left of the track (linear 0-100 gain)

protected:
    void mouseDoubleClickEvent(QMouseEvent *event);
    void wheelEvent(QWheelEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void resizeEvent(QResizeEvent *event);
    void paintEvent(QPaintEvent *event);
    void changeEvent(QEvent *event);

private slots:
    void moveHandle();

private:
    void buildHandle();
    qreal trackX() const;
    qreal handleCenterY(int level) const;
    int levelForY(int y) const;

    QLabel *sHandle;

    qreal sTickCount = 20;

    bool sMousePress = false;
    bool sShowScale = false;
    int sChangStep = 2;
    int sMinLv = 0;
    int sMaxLv = 100;
    int sLv = 0;
};

#endif // FADERSLIDER_H
