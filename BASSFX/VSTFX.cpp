#include "VSTFX.h"

VSTFX::VSTFX(const QString &vstFile, DWORD stream, int priority) : FX(priority)
{
    this->vstFile = vstFile;
    this->stream = stream;
    this->type = FXType::VSTEffects;

    if (stream != 0)
    {
        #ifdef _WIN32
        fx = bv::ChannelSetDSP(stream, vstFile.toStdWString().c_str(),
                                         BASS_VST_KEEP_CHANS|BASS_UNICODE, priority);
        #elif __APPLE__
        fx = bv::ChannelSetDSP(stream, vstFile.vstPath.toStdString().c_str(),
                                         BASS_VST_KEEP_CHANS, priority);
        #endif

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
    if (live())
    {
        bv::ChannelRemoveDSP(stream, fx);
    }
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
    if (live())
        bv::ChannelRemoveDSP(this->stream, fx);
    this->stream = stream;
    fx = 0;

    if (stream != 0)
    {
        #ifdef _WIN32
        fx = bv::ChannelSetDSP(stream, vstFile.toStdWString().c_str(),
                                         BASS_VST_KEEP_CHANS|BASS_UNICODE, priority);
        #elif __APPLE__
        fx = bv::ChannelSetDSP(stream, vstFile.vstPath.toStdString().c_str(),
                                         BASS_VST_KEEP_CHANS, priority);
        #endif

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

    if (!live())
        return;

    bv::SetBypass(fx, b);
}

void VSTFX::reset()
{
    setProgram(defaultProgramIndex);
    setParams(defaultParams);
}
