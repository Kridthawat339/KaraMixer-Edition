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
#include <cstring>
#include <thread>

namespace {
    DWORD  g_mix    = 0;      // stereo float mixer fed to the ASIO driver
    bool   g_active = false;
    int    g_rate   = 44100;  // sample rate asked from the driver
    std::atomic<int> g_gen{0};  // generation of the running ASIO session (stops the level probe)
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

    // ---- device panel state ----
    unsigned g_pairMask   = 1;      // enabled stereo pairs (bit n = outputs 2n, 2n+1)
    int      g_bufLen     = 0;      // requested ASIO buffer length (samples), 0 = driver default
    int      g_outputs    = 0;      // output channels of the running driver
    QStringList g_chanNames;        // their names
    int      g_bmin = 0, g_bmax = 0, g_bpref = 0, g_bgran = 0;
    // several pairs get the same stereo mix: the first (lowest) pair pulls it from the mixer, the others copy it
    float    g_pairBuf[16384];
    DWORD    g_pairBytes = 0;
    DWORD    g_firstPair = 0;

    DWORD CALLBACK pairProc(BOOL, DWORD, void *buffer, DWORD length, void *user)
    {
        const DWORD pair = (DWORD)(size_t)user;
        if (pair == g_firstPair) {
            DWORD n = length;
            if (n > sizeof(g_pairBuf)) n = sizeof(g_pairBuf);
            DWORD got = g_mix ? BASS_ChannelGetData(g_mix, g_pairBuf, n) : 0;
            if (got == (DWORD)-1) got = 0;
            if (got < n) memset((char *)g_pairBuf + got, 0, n - got);
            g_pairBytes = n;
            memcpy(buffer, g_pairBuf, n);
            if (length > n) memset((char *)buffer + n, 0, length - n);
        } else {
            const DWORD n = length < g_pairBytes ? length : g_pairBytes;
            memcpy(buffer, g_pairBuf, n);
            if (length > n) memset((char *)buffer + n, 0, length - n);
        }
        return length;
    }
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

    {
        BASS_ASIO_INFO ai;
        if (BASS_ASIO_GetInfo(&ai)) {
            g_outputs = (int)ai.outputs;
            g_bmin = (int)ai.bufmin; g_bmax = (int)ai.bufmax; g_bpref = (int)ai.bufpref; g_bgran = ai.bufgran;
        }
        g_chanNames.clear();
        for (int c = 0; c < g_outputs; c++) {
            BASS_ASIO_CHANNELINFO ci;
            g_chanNames << (BASS_ASIO_ChannelGetInfo(FALSE, (DWORD)c, &ci) ? QString::fromLocal8Bit(ci.name) : QString());
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

    // which stereo pairs get the main mix (default outputs 1+2); pairs the driver does not have are dropped
    unsigned mask = g_pairMask;
    for (int pr = 0; pr < 16; pr++)
        if ((mask >> pr) & 1u) { if (2 * pr + 1 >= g_outputs) mask &= ~(1u << pr); }
    if (mask == 0) mask = 1;
    int firstPair = 0;
    while (!((mask >> firstPair) & 1u)) firstPair++;
    g_firstPair = (DWORD)firstPair;
    bool multi = (mask & ~(1u << firstPair)) != 0;

    bool chOk = false;
    if (!multi) {
        chOk = BASS_ASIO_ChannelEnableBASS(FALSE, (DWORD)(2 * firstPair), g_mix, TRUE) != FALSE;   // one pair: BASSASIO feeds it itself
    } else {
        chOk = true;
        for (int pr = 0; pr < 16 && chOk; pr++) {
            if (!((mask >> pr) & 1u)) continue;
            const DWORD c = (DWORD)(2 * pr);
            chOk = BASS_ASIO_ChannelEnable(FALSE, c, pairProc, (void *)(size_t)pr)
                && BASS_ASIO_ChannelJoin(FALSE, c + 1, (int)c)
                && BASS_ASIO_ChannelSetFormat(FALSE, c, BASS_ASIO_FORMAT_FLOAT);
        }
    }
    asioLog(QString("channels: pairMask=0x%1 used=0x%2 multi=%3 buffer=%4").arg(g_pairMask, 0, 16).arg(mask, 0, 16).arg(multi).arg(g_bufLen));
    if (!chOk) {
        asioLog("FAIL"); g_error = QString("ASIO channel failed (error %1)").arg(BASS_ASIO_ErrorGetCode());
        BASS_StreamFree(g_mix);
        g_mix = 0;
        BASS_ASIO_Free();
        return false;
    }

    if (!BASS_ASIO_Start((DWORD)g_bufLen, 0)) {
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
    ++g_gen;
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
    g_outputs = 0;
    g_chanNames.clear();
}

bool AsioOutput::isActive()   { return g_active; }
QString AsioOutput::lastError() { return g_error; }
void AsioOutput::setRate(int hz) { g_rate = hz > 0 ? hz : 44100; }
void AsioOutput::setGain(float g) { g_gainTarget = g; }
float AsioOutput::gain() { return g_gainTarget.load(); }

void AsioOutput::setPairMask(unsigned mask) { g_pairMask = mask ? mask : 1u; }
unsigned AsioOutput::pairMask() { return g_pairMask; }
void AsioOutput::setBufferSize(int samples) { g_bufLen = samples > 0 ? samples : 0; }
int AsioOutput::bufferSize() { return g_bufLen; }
int AsioOutput::outputCount() { return g_active ? g_outputs : 0; }
QString AsioOutput::outputName(int channel) { return (g_active && channel >= 0 && channel < g_chanNames.count()) ? g_chanNames[channel] : QString(); }
bool AsioOutput::bufferInfo(int &mn, int &mx, int &pref, int &gran)
{
    if (!g_active) return false;
    mn = g_bmin; mx = g_bmax; pref = g_bpref; gran = g_bgran;
    return true;
}
bool AsioOutput::rateSupported(int hz) { return g_active && BASS_ASIO_CheckRate((double)hz) != FALSE; }
int AsioOutput::currentRate() { return g_active ? (int)(BASS_ASIO_GetRate() + 0.5) : 0; }
void AsioOutput::waitSettle(int ms)
{
    if (g_stopTick == 0) return;
    const DWORD since = GetTickCount() - g_stopTick;
    if ((int)since < ms) Sleep((DWORD)(ms - (int)since));
}
bool AsioOutput::recentlyStopped(int ms) { return g_stopTick != 0 && (int)(GetTickCount() - g_stopTick) < ms; }
void AsioOutput::logLine(const QString &msg) { asioLog(msg); }

#else   // non-Windows: ASIO not available

QStringList AsioOutput::devices() { return QStringList(); }
bool AsioOutput::start(unsigned long, int) { return false; }
void AsioOutput::stop() {}
bool AsioOutput::isActive() { return false; }
QString AsioOutput::lastError() { return QString(); }
void AsioOutput::setRate(int) {}
void AsioOutput::setGain(float) {}
float AsioOutput::gain() { return 1.0f; }
void AsioOutput::setPairMask(unsigned) {}
unsigned AsioOutput::pairMask() { return 1; }
void AsioOutput::setBufferSize(int) {}
int AsioOutput::bufferSize() { return 0; }
int AsioOutput::outputCount() { return 0; }
QString AsioOutput::outputName(int) { return QString(); }
bool AsioOutput::bufferInfo(int &, int &, int &, int &) { return false; }
bool AsioOutput::rateSupported(int) { return false; }
int AsioOutput::currentRate() { return 0; }
void AsioOutput::waitSettle(int) {}
bool AsioOutput::recentlyStopped(int) { return false; }
void AsioOutput::logLine(const QString &) {}

#endif
