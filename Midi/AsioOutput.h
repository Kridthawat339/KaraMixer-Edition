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
}

#endif // ASIOOUTPUT_H
