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
    if (stream != 0)
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
    if (stream == 0)
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
    if (stream ==0)
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
    if (stream == 0)
    {
        return tempChunk;
    }
    else
    {
        DWORD length = 0;
        char *cnk = bv::GetChunk(fx, false, &length);
        return QByteArray(cnk, length);
    }
}

void VSTFX::setChunk(const QByteArray &cnk)
{
    if (stream == 0)
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
    if (stream == 0)
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
    if (stream == 0)
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
    tempChunk = chunk();
    tempParams = params();
    programIndex = program();
    bv::ChannelRemoveDSP(this->stream, fx);
    this->stream = stream;

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

    if (stream == 0)
        return;

    bv::SetBypass(fx, b);
}

void VSTFX::reset()
{
    setProgram(defaultProgramIndex);
    setParams(defaultParams);
}
