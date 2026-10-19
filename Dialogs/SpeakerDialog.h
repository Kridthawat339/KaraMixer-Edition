#ifndef SPEAKERDIALOG_H
#define SPEAKERDIALOG_H

#include <QDialog>
#include <QSignalMapper>

#include "MainWindow.h"
#include "Widgets/InstCh.h"

namespace Ui {
class SpeakerDialog;
}

class SpeakerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SpeakerDialog(QWidget *parent = 0, QMap<InstrumentType, InstCh*> *chInstMap = 0, MainWindow *mainWindow = 0);
    ~SpeakerDialog();

private slots:
    void on_btnClose_clicked();
    void showSpeakerMenu(const QPoint &pos);
    void setSpeaker(int number);
    void setItems(int row);

private:
    Ui::SpeakerDialog *ui;

    QMap<InstrumentType, InstCh*> *chInstMap;
    MainWindow *mainWindow;
    MidiPlayer *player;
    MidiSynthesizer *synth;

    QStringList deviceNames;

    // strip shown in each table row (same order as the mixer: Canthai sits between Pipe and Strings)
    QList<InstrumentType> rowTypes;
    InstrumentType typeAt(int row) const { return rowTypes.value(row, static_cast<InstrumentType>(row)); }
};

#endif // SPEAKERDIALOG_H
