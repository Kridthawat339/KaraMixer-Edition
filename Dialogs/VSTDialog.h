#ifndef VSTDIALOG_H
#define VSTDIALOG_H

#include <QDialog>

#include <bass.h>
#ifndef __linux__
#include <bass_vst.h>
#include "Vst3/BassVst3.h"
#endif

class VSTDialog : public QDialog
{
    Q_OBJECT
public:
    explicit VSTDialog(QWidget *parent = nullptr, DWORD fxHandle = 0, const QString &instName = QString());

    ~VSTDialog();

    DWORD getFxHandle() { return fxHandle; }
    bool isCanOpen() { return canOpen; }

public slots:

protected:
    void showEvent(QShowEvent *event);
    void closeEvent(QCloseEvent *event);
    void hideEvent(QHideEvent *event);

private:
    void detachEditor();
    void embedNow();
    void verifyEmbed(int attempt);
    bool attached = false;
    int edW = 0, edH = 0;
    DWORD fxHandle;
    bool canOpen = false;
};

#endif // VSTDIALOG_H
