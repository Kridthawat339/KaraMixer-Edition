#include "VSTFX.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

// shared by the strip's DSP (audio thread), the plug-in's worker thread and VSTFX (GUI thread)
struct VstRunner
{
    std::mutex mx;
    std::condition_variable cvJob, cvDone;
    std::vector<char> in, out;   // the buffer being processed: dry copy / processed result
    DWORD len = 0;
    DWORD readPos = 0;           // carrierProc's position in `in`
    bool job = false;            // a buffer was handed to the worker and not taken back yet
    bool done = false;           // the worker finished it
    bool quit = false;
    bool exited = false;
    std::atomic<bool> bypass{false};
    DWORD bytesPerSec = 0;
    HSTREAM carrier = 0;
    DWORD fx = 0;                // bv handle of the plug-in, on the carrier
    HDSP dsp = 0;                // hostDsp on the strip's stream
    DWORD stream = 0;
    std::thread th;
};

namespace {

// the carrier's source: the dry buffer the strip handed over
DWORD CALLBACK carrierProc(HSTREAM, void *buffer, DWORD length, void *user)
{
    VstRunner *r = static_cast<VstRunner *>(user);
    DWORD n = 0;
    if (r->readPos < r->len) {
        n = qMin(length, r->len - r->readPos);
        memcpy(buffer, r->in.data() + r->readPos, n);
        r->readPos += n;
    }
    if (n < length)
        memset(static_cast<char *>(buffer) + n, 0, length - n);
    return length;
}

void workerLoop(std::shared_ptr<VstRunner> r)
{
#ifdef _WIN32
    // the audio thread waits for this one: at normal priority any busy program delayed it (crackles, ASIO most of all)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
    std::unique_lock<std::mutex> lk(r->mx);
    for (;;) {
        r->cvJob.wait(lk, [&] { return r->quit || (r->job && !r->done); });
        if (r->quit)
            break;
        const DWORD len = r->len;
        lk.unlock();
        // the plug-in runs here (it may block: the audio thread does not wait for it longer than a moment)
        DWORD got = BASS_ChannelGetData(r->carrier, r->out.data(), len);
        if (got == DWORD(-1) || got > len)
            got = 0;
        if (got < len)
            memcpy(r->out.data() + got, r->in.data() + got, len - got);
        lk.lock();
        r->done = true;
        r->cvDone.notify_all();
    }
    lk.unlock();
    bv::ChannelRemoveDSP(r->carrier, r->fx);
    BASS_StreamFree(r->carrier);
    lk.lock();
    r->exited = true;
    r->cvDone.notify_all();
}

// on the strip's stream, where the plug-in used to sit
void CALLBACK hostDsp(HDSP, DWORD, void *buffer, DWORD length, void *user)
{
    VstRunner *r = static_cast<VstRunner *>(user);
    if (length == 0 || r->bypass.load())
        return;
    std::unique_lock<std::mutex> lk(r->mx);
    if (r->job) {
        if (!r->done)
            return;                          // the plug-in is still stuck on an earlier buffer: play dry
        r->job = r->done = false;            // late result of a buffer that timed out: too late, drop it
    }
    if (r->in.size() < length) {
        r->in.resize(length);
        r->out.resize(length);
    }
    memcpy(r->in.data(), buffer, length);
    r->len = length;
    r->readPos = 0;
    r->job = true;
    r->done = false;
    r->cvJob.notify_one();
    // normally the plug-in answers in well under a millisecond; give it up to 80 % of the buffer's own duration
    // (at least 30 ms, at most 250 ms) before this buffer goes out dry
    int ms = 30;
    if (r->bytesPerSec)
        ms = qBound(30, int(qint64(length) * 800 / r->bytesPerSec), 250);
    if (r->cvDone.wait_for(lk, std::chrono::milliseconds(ms), [&] { return r->done; })) {
        memcpy(buffer, r->out.data(), length);
        r->job = r->done = false;
    }
}

}   // namespace

void VSTFX::attach(DWORD s)
{
    direct = false;
    fx = 0;
    BASS_CHANNELINFO ci;
    std::shared_ptr<VstRunner> r = std::make_shared<VstRunner>();
    if (BASS_ChannelGetInfo(s, &ci) && ci.chans > 0 && ci.freq > 0) {
        const bool isFloat = (ci.flags & BASS_SAMPLE_FLOAT) != 0;
        r->carrier = BASS_StreamCreate(ci.freq, ci.chans, BASS_STREAM_DECODE | (isFloat ? BASS_SAMPLE_FLOAT : 0),
                                       carrierProc, r.get());
        r->bytesPerSec = ci.freq * ci.chans * (isFloat ? 4 : 2);
    }
    if (r->carrier) {
        #ifdef _WIN32
        r->fx = bv::ChannelSetDSP(r->carrier, vstFile.toStdWString().c_str(), BASS_VST_KEEP_CHANS | BASS_UNICODE, 0);
        #else
        r->fx = bv::ChannelSetDSP(r->carrier, vstFile.toStdString().c_str(), BASS_VST_KEEP_CHANS, 0);
        #endif
        if (!r->fx) {
            BASS_StreamFree(r->carrier);
            return;                          // not loadable: same as before (fx stays 0)
        }
        r->stream = s;
        r->bypass = !_on;
        r->th = std::thread(workerLoop, r);
        r->dsp = BASS_ChannelSetDSP(s, hostDsp, r.get(), priority);
        runner = r;
        fx = r->fx;
        return;
    }
    // no carrier (should not happen): the plug-in goes on the stream itself
    direct = true;
    #ifdef _WIN32
    fx = bv::ChannelSetDSP(s, vstFile.toStdWString().c_str(), BASS_VST_KEEP_CHANS | BASS_UNICODE, priority);
    #else
    fx = bv::ChannelSetDSP(s, vstFile.toStdString().c_str(), BASS_VST_KEEP_CHANS, priority);
    #endif
}

void VSTFX::detach()
{
    if (direct) {
        if (fx)
            bv::ChannelRemoveDSP(stream, fx);
        fx = 0;
        direct = false;
        return;
    }
    if (!runner)
        return;
    std::shared_ptr<VstRunner> r = runner;
    runner.reset();
    fx = 0;
    if (r->dsp)
        BASS_ChannelRemoveDSP(r->stream, r->dsp);     // the audio thread stops handing buffers over
    std::unique_lock<std::mutex> lk(r->mx);
    r->quit = true;
    r->cvJob.notify_all();
    // the worker frees the plug-in and the carrier itself; a plug-in stuck in a message box must not hang the program
    const bool ended = r->cvDone.wait_for(lk, std::chrono::milliseconds(1500), [&] { return r->exited; });
    lk.unlock();
    if (ended)
        r->th.join();
    else
        r->th.detach();                 // it finishes (and cleans up) once the plug-in lets go
}

VSTFX::VSTFX(const QString &vstFile, DWORD stream, int priority) : FX(priority)
{
    this->vstFile = vstFile;
    this->stream = stream;
    this->type = FXType::VSTEffects;
    this->_on = true;

    if (stream != 0)
    {
        attach(stream);

        defaultProgramIndex = program();
        defaultParams = params();

        BASS_VST_INFO info;
        if (bv::GetInfo(fx, &info))
        {
            _uids = info.uniqueID;
        }
    }

    this->_on = true;
}

VSTFX::~VSTFX()
{
    detach();
}

bool VSTFX::isVSTFile(const QString &vstPath, BASS_VST_INFO *info)
{
    HSTREAM stream = BASS_StreamCreate(44100, 2, 0, NULL, NULL);

    #ifdef _WIN32
    DWORD h = bv::ChannelSetDSP(stream, vstPath.toStdWString().c_str(),
                                     BASS_VST_KEEP_CHANS|BASS_UNICODE, 0);
    #else
    DWORD h = bv::ChannelSetDSP(stream, vstPath.toStdString().c_str(),
                                     BASS_VST_KEEP_CHANS, 0);
    #endif

    bool result = false;

    if (bv::GetInfo(h, info) && !info->isInstrument)
        result = true;
    else
        result = false;

    bv::ChannelRemoveDSP(stream, h);
    BASS_StreamFree(stream);

    return result;
}

BASS_VST_INFO VSTFX::VSTInfo()
{
    BASS_VST_INFO info;
    bv::GetInfo(fx, &info);

    return info;
}

uint VSTFX::uids()
{
    return _uids;
}

int VSTFX::program()
{
    if (!live())
    {
        return programIndex;
    }
    else
    {
        return bv::GetProgram(fx);
    }
}

void VSTFX::setProgram(int programIndex)
{
    if (!live())
    {
        this->programIndex = programIndex;
    }
    else
    {
        bv::SetProgram(fx, programIndex);
    }
}

QByteArray VSTFX::chunk()
{
    if (!live())
    {
        return tempChunk;
    }
    else
    {
        DWORD length = 0;
        char *cnk = bv::GetChunk(fx, false, &length);
        if (!cnk || length == 0)
            return tempChunk;        // this plug-in gave nothing: keep the last good state
        return QByteArray(cnk, int(length));
    }
}

void VSTFX::setChunk(const QByteArray &cnk)
{
    if (!live())
    {
        this->tempChunk = cnk;
    }
    else
    {
        if (cnk.length() > 0) {
            bv::SetChunk(fx, false, cnk.constData(), cnk.length());
        }
    }
}

QList<float> VSTFX::params()
{
    if (!live())
    {
        return tempParams;
    }
    else
    {
        QList<float> params;
        int count = bv::GetParamCount(fx);
        for (int i=0; i<count; i++) {
            params.append(bv::GetParam(fx, i));
        }
        return params;
    }
}

void VSTFX::setParams(const QList<float> &params)
{
    if (!live())
    {
        tempParams.clear();
        tempParams = params;
    }
    else
    {
        for (int i=0; i<params.count(); i++) {
            bv::SetParam(fx, i, params[i]);
        }
    }
}

void VSTFX::setStreamHandle(DWORD stream)
{
    // keep what the running plug-in holds now (if it is not running, the state already kept stays as it is)
    tempChunk = chunk();
    tempParams = params();
    programIndex = program();
    detach();
    this->stream = stream;

    if (stream != 0)
    {
        attach(stream);

        this->setBypass(this->isBypass());
        // The chunk holds the complete plugin state (incl. edits made in the
        // plugin GUI). Re-selecting the program / re-applying old parameter
        // values afterwards would revert it, so use them only as a fallback.
        if (tempChunk.length() > 0) {
            this->setChunk(tempChunk);
        } else {
            this->setProgram(programIndex);
            this->setParams(tempParams);
        }
    }
}

void VSTFX::setBypass(bool b)
{
    _on = !b;
    if (runner)
        runner->bypass = b;      // bypassed: the strip does not even wait for the plug-in

    if (!live())
        return;

    bv::SetBypass(fx, b);
}

void VSTFX::reset()
{
    setProgram(defaultProgramIndex);
    setParams(defaultParams);
}
