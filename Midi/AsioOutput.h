#ifndef ASIOOUTPUT_H
#define ASIOOUTPUT_H

#include <QStringList>

// Optional ASIO output (Windows only).  The synth's main mixer is created as a
// decoding stream and BASSASIO pulls the audio from it, so the sound goes
// straight to the selected ASIO driver instead of a normal Windows device.
namespace AsioOutput
{
    QStringList devices();                    // ASIO driver names (empty if none installed)
    bool        start(unsigned long source, int device);   // source = BASS decoding mixer
    void        stop();
    bool        isActive();
    QString     lastError();
    void        setGain(float g);                      // master gain (volume x headroom) applied to the ASIO feed
    float       gain();
    void        setRate(int hz);                       // rate to ask the driver for (default 44100)

    // ---- audio device panel (Settings) ----
    void        setPairMask(unsigned mask);            // bit n = driver outputs 2n+1 / 2n+2 receive the main mix (default 1 = outputs 1+2)
    unsigned    pairMask();
    void        setBufferSize(int samples);            // ASIO buffer length in samples, 0 = driver default
    int         bufferSize();
    int         outputCount();                         // output channels of the running driver (0 when ASIO is not running)
    QString     outputName(int channel);               // channel name reported by the running driver
    bool        bufferInfo(int &minSamples, int &maxSamples, int &prefSamples, int &granularity);   // running driver only
    bool        rateSupported(int hz);                 // running driver only
    int         currentRate();                         // rate the running driver really uses (0 when not running)
}

#endif // ASIOOUTPUT_H
