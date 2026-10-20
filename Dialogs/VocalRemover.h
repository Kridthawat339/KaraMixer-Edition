#ifndef VOCALREMOVER_H
#define VOCALREMOVER_H

// Vocal remover for a stereo float stream (48 kHz): frequency-domain "centre channel" removal.
//
// How it works (per FFT bin, 4096-point STFT, 75 % overlap, Hann analysis + synthesis windows):
//   M = L + R (what both channels share = centre: lead vocal, but also kick / snare / bass)
//   S = L - R (what differs = stereo instruments, reverb, backing instruments)
//   m = |L-R|^2 / (|L|^2 + |R|^2), clamped to 0..1   0 = pure centre, 1 = one side only (or out of phase)
//   gain = 1 - strength * band(f) * (1 - m^sharp)      -> bins that sit in the centre are turned down,
//   bins that are wide (guitars, keys, strings, cymbals, room) are left alone
//   band(f): 1 inside [keepLow .. keepHigh], fading to 0 outside it, so the bass / kick (below keepLow)
//   and the air / cymbals (above keepHigh) are not touched.
// The gain is smoothed over time and over neighbouring bins to avoid "musical noise".
// It is NOT a 100 % vocal remover: a mixed song cannot be un-mixed exactly. It works best on stereo songs
// whose lead vocal is panned dead centre; reverb of the voice, doubled / wide backing vocals and a mono
// recording stay. Latency: about 64 ms.
//
// Header-only, thread-safe setters (atomics); process() must be called from one thread only.

#include <vector>
#include <complex>
#include <cmath>
#include <atomic>
#include <algorithm>

class VocalRemover
{
public:
    static constexpr int N = 4096;
    static constexpr int HOP = N / 4;
    static constexpr int BINS = N / 2 + 1;

    VocalRemover()
    {
        win.resize(N);
        for (int i = 0; i < N; i++)
            win[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979f * i / N));
        rev.resize(N);
        int bits = 0;
        while ((1 << bits) < N) bits++;
        for (int i = 0; i < N; i++) {
            int r = 0;
            for (int b = 0; b < bits; b++)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[i] = r;
        }
        tw.resize(N / 2);
        for (int i = 0; i < N / 2; i++) {
            const float a = -2.0f * 3.14159265358979f * i / N;
            tw[i] = std::complex<float>(std::cos(a), std::sin(a));
        }
        reset();
    }

    void setEnabled(bool on)      { enabledFlag = on; }
    bool enabled() const          { return enabledFlag; }
    void setStrength(int percent) { strength = (std::min)(100, (std::max)(0, percent)); }       // 0..100
    void setSharpness(int percent){ sharp = (std::min)(100, (std::max)(0, percent)); }          // 0..100
    void setKeepLow(int hz)       { lowHz = (std::min)(600, (std::max)(40, hz)); }              // bass / kick kept below
    void setKeepHigh(int hz)      { highHz = (std::min)(20000, (std::max)(3000, hz)); }         // air / cymbals kept above
    void setSampleRate(int hz)    { rate = hz; }

    // clears the history (call when the processing is switched on)
    void reset()
    {
        inL.assign(0, 0.0f); inR.assign(0, 0.0f);
        accL.assign(N, 0.0f); accR.assign(N, 0.0f);
        gainPrev.assign(BINS, 1.0f);
        primed = false;
    }

    // interleaved stereo float in, the processed samples are appended to out (about 64 ms later)
    void process(const float *in, int frames, std::vector<float> &out)
    {
        for (int i = 0; i < frames; i++) {
            inL.push_back(in[2 * i]);
            inR.push_back(in[2 * i + 1]);
        }
        // first output: pad HOP*... so that the stream never runs dry before the first frame is ready
        while ((int)inL.size() >= N) {
            doFrame();
            for (int i = 0; i < HOP; i++) {
                out.push_back(accL[i]);
                out.push_back(accR[i]);
            }
            std::copy(accL.begin() + HOP, accL.end(), accL.begin());
            std::copy(accR.begin() + HOP, accR.end(), accR.begin());
            std::fill(accL.end() - HOP, accL.end(), 0.0f);
            std::fill(accR.end() - HOP, accR.end(), 0.0f);
            inL.erase(inL.begin(), inL.begin() + HOP);
            inR.erase(inR.begin(), inR.begin() + HOP);
        }
    }

private:
    typedef std::complex<float> cf;

    static float smooth(float x, float a, float b)       // 0 below a, 1 above b
    {
        if (x <= a) return 0.0f;
        if (x >= b) return 1.0f;
        const float t = (x - a) / (b - a);
        return t * t * (3.0f - 2.0f * t);
    }

    void fft(std::vector<cf> &a, bool inverse)
    {
        for (int i = 0; i < N; i++)
            if (i < rev[i]) std::swap(a[i], a[rev[i]]);
        for (int len = 2; len <= N; len <<= 1) {
            const int half = len >> 1, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int j = 0; j < half; j++) {
                    cf w = tw[j * step];
                    if (inverse) w = std::conj(w);
                    const cf u = a[i + j], v = a[i + j + half] * w;
                    a[i + j] = u + v;
                    a[i + j + half] = u - v;
                }
        }
    }

    void doFrame()
    {
        const float s = strength.load() / 100.0f;
        const float sharpExp = 0.6f + 2.4f * (sharp.load() / 100.0f);       // 0.6 .. 3.0
        const float lo = (float)lowHz.load(), hi = (float)highHz.load();
        const float binHz = (float)rate.load() / N;

        std::vector<cf> L(N), R(N);
        for (int i = 0; i < N; i++) {
            L[i] = cf(inL[i] * win[i], 0.0f);
            R[i] = cf(inR[i] * win[i], 0.0f);
        }
        fft(L, false);
        fft(R, false);

        std::vector<float> g(BINS);
        for (int k = 0; k < BINS; k++) {
            const cf sum = L[k] + R[k], dif = L[k] - R[k];
            const float ps = std::norm(sum), pd = std::norm(dif);
            (void)ps;
            const float m = (std::min)(1.0f, pd / (std::norm(L[k]) + std::norm(R[k]) + 1e-12f));   // 0 = same in both channels (centre) .. 1 = one side only / opposite phase
            const float f = k * binHz;
            const float band = smooth(f, lo * 0.7f, lo * 1.3f) * (1.0f - smooth(f, hi * 0.8f, hi * 1.2f));
            float target = 1.0f - s * band * (1.0f - std::pow(m, sharpExp));
            if (target < 0.0f) target = 0.0f;
            g[k] = target;
        }
        // smooth over frequency (3 taps) then over time (fast down, slower up)
        for (int k = 0; k < BINS; k++) {
            const float a = g[k > 0 ? k - 1 : 0], b = g[k], c = g[k < BINS - 1 ? k + 1 : BINS - 1];
            float t = 0.25f * a + 0.5f * b + 0.25f * c;
            const float prev = gainPrev[k];
            t = (t < prev) ? (0.65f * t + 0.35f * prev) : (0.35f * t + 0.65f * prev);
            gainPrev[k] = t;
            g[k] = t;
        }
        for (int k = 0; k < BINS; k++) {
            L[k] *= g[k];
            R[k] *= g[k];
            if (k > 0 && k < N / 2) {
                L[N - k] *= g[k];
                R[N - k] *= g[k];
            }
        }
        fft(L, true);
        fft(R, true);
        const float norm = (2.0f / 3.0f) / N;       // Hann x Hann at 75 % overlap sums to 1.5
        for (int i = 0; i < N; i++) {
            accL[i] += L[i].real() * win[i] * norm;
            accR[i] += R[i].real() * win[i] * norm;
        }
    }

    std::vector<float> win;
    std::vector<std::complex<float>> tw;
    std::vector<int> rev;
    std::vector<float> inL, inR, accL, accR, gainPrev;
    bool primed = false;
    std::atomic<bool> enabledFlag{false};
    std::atomic<int> strength{100}, sharp{60}, lowHz{160}, highHz{12000}, rate{48000};
};

#endif // VOCALREMOVER_H
