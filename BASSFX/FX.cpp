#include "FX.h"


FX::FX(int priority)
{
    this->priority = priority;
    _on = false;
}

FX::~FX()
{

}

uint FX::uids()
{
    return static_cast<unsigned int>(this->type);
}


#ifndef __linux__

QList<float> FX::getVSTParams(DWORD vstHandle)
{
    QList<float> params;
    int count = bv::GetParamCount(vstHandle);
    for (int i=0; i<count; i++) {
        params.append(bv::GetParam(vstHandle, i));
    }
    return params;
}

void FX::setVSTParams(DWORD fxHandle, const QList<float> &params)
{
    for (int i=0; i<params.count(); i++) {
        bv::SetParam(fxHandle, i, params[i]);
    }
}

#endif
