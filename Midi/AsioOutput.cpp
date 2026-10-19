#include "AsioOutput.h"

#ifdef _WIN32

#include <windows.h>
#include <bass.h>
#include <bassmix.h>
#include <bassasio.h>

#include <QString>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QTextStream>
#include <atomic>

namespace {
    DWORD  g_mix    = 0;      // stereo float mixer fed to the ASIO driver
    bool   g_active = false;
    int    g_rate   = 44100;  // sample rate asked from the driver
    DWORD  g_stopTick = 0;    // GetTickCount() of the last BASS_ASIO_Free (drivers need a moment before a re-init)
    std::atomic<float> g_gainTarget{1.0f};   // master volume x headroom (a decoding mixer ignores BASS_ATTRIB_VOL)
    float  g_gainCur = 1.0f;

    // BASSASIO pulls the audio from a decoding mixer, where the mixer's own volume attribute is not applied:
    // apply the master gain here (smoothed, so moving the master fader does not click)
    void CALLBACK gainDsp(HDSP, DWORD, void *buffer, DWORD length, void *)
    {
        float *p = static_cast<float *>(buffer);
        const size_t frames = length / (2 * sizeof(float));
        const float target = g_gainTarget.load();
        float g = g_gainCur;
        for (size_t i = 0; i < frames; i++) {
            g += (target - g) * 0.002f;
            p[2 * i] *= g;
            p[2 * i + 1] *= g;
        }
        g_gainCur = g;
    }
    QString g_error;
}

namespace {
    void asioLog(const QString &msg)   // ~/.KaraMixer/asio.log : what the driver reported (for support)
    {
        QFile f(QDir::homePath() + "/.KaraMixer/asio.log");
        if (f.size() > 200000) f.remove();
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream(&f) << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "  " << msg << "\n";
        }
    }
}

QStringList AsioOutput::devices()
{
    QStringList list;
    BASS_ASIO_DEVICEINFO info;
    for (DWORD i = 0; BASS_ASIO_GetDeviceInfo(i, &info); i++)
        list << QString::fromLocal8Bit(info.name);
    return list;
}

bool AsioOutput::start(unsigned long source, int device)
{
    stop();
    g_error.clear();

    if (device < 0 || source == 0)
        return false;

    // many ASIO drivers stay silent (or fail) when re-initialised right after being freed: wait for the driver to settle
    if (g_stopTick != 0) {
        const DWORD since = GetTickCount() - g_stopTick;
        if (since < 600)
            Sleep(600 - since);
    }

    if (!BASS_ASIO_Init(device, BASS_ASIO_THREAD)) {
        asioLog("FAIL"); g_error = QString("ASIO init failed (error %1)").arg(BASS_ASIO_ErrorGetCode());
        return false;
    }

    // the driver must offer at least two output channels (ASIO4ALL has none until a device is switched on in its panel)
    {
        BASS_ASIO_INFO ai;
        if (BASS_ASIO_GetInfo(&ai) && ai.outputs < 2) {
            asioLog("FAIL"); g_error = QString("This ASIO driver has no usable stereo output (%1 output channel(s)). "
                              "For ASIO4ALL: open its panel from the tray icon and switch the output device on.").arg(ai.outputs);
            BASS_ASIO_Free();
            return false;
        }
    }

    // ask for the chosen rate (44.1 kHz free, up to 96 kHz PRO); if the driver refuses, keep its own rate (BASSmix resamples)
    BASS_ASIO_SetRate((double)g_rate);
    double rate = BASS_ASIO_GetRate();
    if (rate < 8000.0)
        rate = 44100.0;

    // multi-channel main mixer -> stereo for ASIO outputs 1+2
    g_mix = BASS_Mixer_StreamCreate((DWORD)rate, 2, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT);
    if (!g_mix || !BASS_Mixer_StreamAddChannel(g_mix, (DWORD)source, BASS_MIXER_CHAN_DOWNMIX)) {
        asioLog("FAIL"); g_error = QString("ASIO mixer failed (error %1)").arg(BASS_ErrorGetCode());
        if (g_mix) BASS_StreamFree(g_mix);
        g_mix = 0;
        BASS_ASIO_Free();
        return false;
    }

    {
        BASS_ASIO_INFO ai2; BASS_ASIO_GetInfo(&ai2);
        BASS_CHANNELINFO ci2; BASS_ChannelGetInfo((DWORD)source, &ci2);
        asioLog(QString("init ok: driver=%1 outputs=%2 rate=%3 bufmin=%4 | source chans=%5 freq=%6 flags=0x%7 | gain=%8")
                .arg(QString::fromLocal8Bit(ai2.name)).arg(ai2.outputs).arg(rate).arg(ai2.bufmin)
                .arg(ci2.chans).arg(ci2.freq).arg(ci2.flags, 0, 16).arg(g_gainTarget.load()));
    }
    g_gainCur = g_gainTarget.load();
    BASS_ChannelSetDSP(g_mix, gainDsp, nullptr, 0);

    if (!BASS_ASIO_ChannelEnableBASS(FALSE, 0, g_mix, TRUE)) {
        asioLog("FAIL"); g_error = QString("ASIO channel failed (error %1)").arg(BASS_ASIO_ErrorGetCode());
        BASS_StreamFree(g_mix);
        g_mix = 0;
        BASS_ASIO_Free();
        return false;
    }

    if (!BASS_ASIO_Start(0, 0)) {
        asioLog("FAIL"); g_error = QString("ASIO start failed (error %1)").arg(BASS_ASIO_ErrorGetCode());
        BASS_ASIO_Free();
        BASS_StreamFree(g_mix);
        g_mix = 0;
        return false;
    }

    asioLog(QString("started: asio rate=%1 cpu=%2").arg(BASS_ASIO_GetRate()).arg(BASS_ASIO_GetCPU()));
    g_active = true;
    return true;
}

void AsioOutput::stop()
{
    if (g_active) {
        BASS_ASIO_Stop();
        BASS_ASIO_Free();
        g_stopTick = GetTickCount();
        g_active = false;
    }
    if (g_mix) {
        BASS_StreamFree(g_mix);
        g_mix = 0;
    }
}

bool AsioOutput::isActive()   { return g_active; }
QString AsioOutput::lastError() { return g_error; }
void AsioOutput::setRate(int hz) { g_rate = hz > 0 ? hz : 44100; }
void AsioOutput::setGain(float g) { g_gainTarget = g; }
float AsioOutput::gain() { return g_gainTarget.load(); }

#else   // non-Windows: ASIO not available

QStringList AsioOutput::devices() { return QStringList(); }
bool AsioOutput::start(unsigned long, int) { return false; }
void AsioOutput::stop() {}
bool AsioOutput::isActive() { return false; }
QString AsioOutput::lastError() { return QString(); }
void AsioOutput::setRate(int) {}
void AsioOutput::setGain(float) {}
float AsioOutput::gain() { return 1.0f; }

#endif
