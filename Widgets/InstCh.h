#ifndef INSTCH_H
#define INSTCH_H

#include <QWidget>

#include "Widgets/LEDVu.h"
#include "Midi/MidiSynthesizer.h"


namespace Ui {
class InstCh;
}

class InstCh : public QWidget
{
    Q_OBJECT

public:
    explicit InstCh(QWidget *parent = 0);
    ~InstCh();

    LEDVu* vuBar();
    QString instrumentName();
    QString fullInstrumentName();
    QWidget *dbLabel() const;   // the visible "0.0 dB" read-out under the fader
    QWidget *nameTag() const;   // the coloured name label at the bottom of the strip

    void addFXLabel(const QString &label, int fxIndex, bool bypass = false);
    void removeVSTLabel(int fxIndex);

public slots:
    void setInstrumentType(InstrumentType t);
    void setInstrumentName(const QString &name);
    void setFullInstrumentName(const QString &name);
    void setInstrumentNames(const QString &name, const QString &tooltip = QString());
    void setInstrumentImage(const QImage &img);
    void setMuteButton(bool m);
    void setSoloButton(bool s);
    void setSliderLevel(int v);
    void setPanValue(int pan);   // -100..100, no signal
    void setReverbValue(int level);   // 0..100, no signal
    void setTrimValue(double db);     // -12..+12 dB, no signal
    void peak(int v);
    void setBusBadge(int bus);   // bus: -1 = Master (badge hidden), 0.. = Bus 1..
    // Fold the Pan / Reverb knobs away: p = 0 shown ... 1 hidden (animated by the mixer window).
    // Returns how many pixels shorter the strip is now.
    int setKnobCollapse(double p);

signals:
    void muteChanged(InstrumentType type, bool m);
    void soloChanged(InstrumentType type, bool s);
    void sliderLevelChanged(InstrumentType type, int v);
    void sliderDoubleClicked(InstrumentType type);
    void panChanged(InstrumentType type, int pan);
    void reverbChanged(InstrumentType type, int level);
    void trimChanged(InstrumentType type, double db);

    void menuRequested(InstrumentType type, const QPoint &pos);
    void busMenuRequested(InstrumentType type, const QPoint &globalPos);
    void fxRemoveMenuRequested(InstrumentType typr, int fxIndex, const QPoint &pos);
    void fxByPassChanged(InstrumentType type, int fxIndex, bool bypass);
    void fxDoubleClicked(InstrumentType type, int fxIndex);

    void imageDoubleClicked(InstrumentType type);
    void imageMenuRequested(InstrumentType type, const QPoint &globalPos);   // right click on the instrument icon

protected:
    void mouseDoubleClickEvent(QMouseEvent *event);
    bool eventFilter(QObject *obj, QEvent *e) override;

private slots:
    void onBtnMuteClicked();
    void onBtnSoloClicked();
    void onSliderUserLevelChanged(int v);
    void onSliderDoubleClicked();
    void contextMenuRequested(const QPoint &pos);

    void onFxMenuRequested(int fxIndex, const QPoint &pos);
    void onFxByPassChanged(int fxIndex, bool bypass);
    void onFxDoubleClicked(int fxIndex);

private:
    class QListWidgetItem *busItem = nullptr;
    class VSTLabel *busLabel = nullptr;
    int fxCount() const;
    Ui::InstCh *ui;
    class PanKnob *panDial = nullptr;
    class PanKnob *revDial = nullptr;
    class QLabel *lbPanVal = nullptr;
    class QLabel *lbRevCap = nullptr;
    class QPushButton *lbTrim = nullptr;
    double trimVal = 0.0;
    void setTrimInternal(double db, bool notify);
    QWidget *knobRow = nullptr;
    int baseHeight = -1;
    int knobRowHeight = 0;     // the knob block
    int knobSpacing = 0;       // layout spacing that goes away with it

    InstrumentType instType;
    bool mute = false;
    bool solo = false;

    QString btnDefault;
    QString btnMuteStyle;
    QString btnSoloStyle;
};

#endif // INSTCH_H
