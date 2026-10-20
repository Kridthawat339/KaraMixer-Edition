#ifndef YOUTUBERING_H
#define YOUTUBERING_H

// Jitter buffer between the capture (YouTube bridge) and the YouTube strip of the mixer.
// The strip's BASS stream pulls its samples through proc(); proc() NEVER returns 0 bytes (that would end the stream),
// it returns silence while the buffer is refilling, so a late capture block gives a tiny gap instead of a stalled stream.
// Float stereo, 48 kHz. write() is called by the capture thread, proc() by the mixer thread.

#include <bass.h>
#include <mutex>
#include <vector>
#include <cstring>

class YouTubeRing
{
public:
    static YouTubeRing &get() { static YouTubeRing r; return r; }

    static constexpr int kRate = 48000;

    void clear()
    {
        std::lock_guard<std::mutex> g(mx);
        rd = wr = fill = 0;
        primed = false;
    }

    // interleaved stereo float
    void write(const float *in, int frames)
    {
        std::lock_guard<std::mutex> g(mx);
        for (int i = 0; i < frames; i++) {
            if (fill >= kCap) {                       // full: drop the oldest frame
                rd = (rd + 1) % kCap;
                fill--;
            }
            buf[2 * wr] = in[2 * i];
            buf[2 * wr + 1] = in[2 * i + 1];
            wr = (wr + 1) % kCap;
            fill++;
        }
    }

    static DWORD CALLBACK proc(HSTREAM, void *buffer, DWORD length, void *user)
    {
        return static_cast<YouTubeRing *>(user)->read(static_cast<float *>(buffer), length);
    }

private:
    static constexpr int kCap = kRate;                // 1 s
    static constexpr int kStart = kRate / 1000 * 70;  // play once 70 ms are buffered
    static constexpr int kHigh = kRate / 1000 * 220;  // the two clocks drift: above 220 ms ...
    static constexpr int kKeep = kRate / 1000 * 110;  // ... drop back to 110 ms

    YouTubeRing() : buf(size_t(kCap) * 2, 0.0f) {}

    DWORD read(float *out, DWORD length)
    {
        const int want = int(length / (2 * sizeof(float)));
        std::lock_guard<std::mutex> g(mx);

        if (fill > kHigh) {                           // too much queued: skip the oldest part in one go
            const int skip = fill - kKeep;
            rd = (rd + skip) % kCap;
            fill -= skip;
        }
        if (!primed) {
            if (fill >= kStart) primed = true;
        }
        int done = 0;
        if (primed) {
            const int n = fill < want ? fill : want;
            for (int i = 0; i < n; i++) {
                out[2 * i] = buf[2 * rd];
                out[2 * i + 1] = buf[2 * rd + 1];
                rd = (rd + 1) % kCap;
            }
            fill -= n;
            done = n;
            if (n < want)
                primed = false;                       // ran dry: wait until some data is queued again
        }
        if (done < want)
            std::memset(out + 2 * done, 0, size_t(want - done) * 2 * sizeof(float));
        return length;
    }

    std::mutex mx;
    std::vector<float> buf;
    int rd = 0, wr = 0, fill = 0;
    bool primed = false;
};

#endif // YOUTUBERING_H
