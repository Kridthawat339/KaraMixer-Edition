#ifndef VSTFX_H
#define VSTFX_H

#ifndef __linux__

#include "FX.h"
#include <bass_vst.h>
#include "Vst3/BassVst3.h"


class VSTFX : public FX
{
public:
    VSTFX(const QString &vstFile, DWORD stream = 0, int priority = 1);
   ~VSTFX();

    static bool isVSTFile(const QString &vstPath, BASS_VST_INFO *info);

    BASS_VST_INFO VSTInfo();
    uint uids();
    int program();
    void setProgram(int programIndex);

    QByteArray chunk() override;
    void setChunk(const QByteArray &cnk) override;

    QList<float> params();
    void setParams(const QList<float> &params);
    void setStreamHandle(DWORD stream);
    void setBypass(bool b);
    void reset();

private:
    // true when the plug-in is really running on a stream; when it could not be loaded (or the strip has no stream yet)
    // the saved state is kept in tempChunk / tempParams / programIndex so that saving the settings does not erase it
    bool live() const { return stream != 0 && fx != 0; }

    QString vstFile;

    int defaultProgramIndex = 0;
    QList<float> tempParams, defaultParams;
    QByteArray tempChunk;
};

#endif

#endif // VSTFX_H
