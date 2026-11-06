#ifndef VSTFX_H
#define VSTFX_H

#ifndef __linux__

#include "FX.h"
#include <bass_vst.h>
#include "Vst3/BassVst3.h"
#include <memory>

struct VstRunner;


class VSTFX : public FX
{
public:
    VSTFX(const QString &vstFile, DWORD stream = 0, int priority = 1);
   ~VSTFX();

    static bool isVSTFile(const QString &vstPath, BASS_VST_INFO *info);

    BASS_VST_INFO VSTInfo();
    uint uids();
    // the plug-in's unique ID from the VST list: a plug-in created without a stream (strip / master not running yet)
    // could not read it from the plug-in, kept 0 - and uid 0 is the built-in Auto Wah, so it came back as Auto Wah
    void setKnownUid(uint uid) { if (_uids == 0) _uids = uid; }
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
    // The plug-in does not run on the strip's stream itself but on a private decoding "carrier" stream, processed by its
    // own thread: the strip's DSP hands each buffer over and waits a short time. A plug-in that stops answering (a
    // demo's "Limitations" message box, a licence check, a crash dialog ...) then only drops out of the sound (the
    // strip plays dry) instead of freezing the whole audio output and every window that touches it.
    void attach(DWORD stream);
    void detach();
    std::shared_ptr<VstRunner> runner;
    bool direct = false;         // carrier could not be made: the plug-in sits on the stream directly (old way)

    QString vstFile;

    int defaultProgramIndex = 0;
    QList<float> tempParams, defaultParams;
    QByteArray tempChunk;
};

#endif

#endif // VSTFX_H
