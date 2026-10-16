#ifndef FADERTAPER_H
#define FADERTAPER_H

#include <cmath>

// Fader law shared by the mixer UI and the audio engine.
// A fader level is an int 0..100:
//   50  = 0 dB (unity, the middle of the travel)
//   100 = +12 dB (top)
//   0   = -inf (silence); between 0 and 50 the scale is linear in dB down to -60 dB.
namespace FaderTaper {

const int kUnityLevel = 50;
const int kMaxLevel   = 100;
const double kTopDb   = 12.0;
const double kFloorDb = -60.0;

// Fixed headroom (-6 dB) taken off the main output mixer. The faders now give 0 dB at the middle (the old
// law gave only half gain there), so with every strip and the master at their default position the sum was
// 6 dB hotter than before and could run past full scale (clipping when recording, or when a loud strip is soloed).
// The 0 dB marks on the faders are unchanged; this is the console's built-in headroom.
const float kOutputHeadroom = 0.5f;

inline bool isSilent(int level) { return level <= 0; }

// dB for a level (only meaningful when !isSilent)
inline double db(int level)
{
    if (level >= kMaxLevel) return kTopDb;
    if (level >= kUnityLevel)
        return (level - kUnityLevel) * kTopDb / double(kMaxLevel - kUnityLevel);
    return kFloorDb * (kUnityLevel - level) / double(kUnityLevel);
}

// linear gain for BASS_ATTRIB_VOL
inline float gain(int level)
{
    if (isSilent(level)) return 0.0f;
    return float(std::pow(10.0, db(level) / 20.0));
}

// fader position (0..100, fractional) for a dB value, used to place the ruler marks
inline double levelForDb(double d)
{
    if (d >= 0.0)
        return kUnityLevel + d / kTopDb * (kMaxLevel - kUnityLevel);
    return kUnityLevel + d / (-kFloorDb) * kUnityLevel;   // d is negative
}

}

#endif // FADERTAPER_H
