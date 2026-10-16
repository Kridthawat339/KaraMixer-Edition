#ifndef VST3HOST_H
#define VST3HOST_H

// Minimal VST3 host (no BASS / Qt dependency).
// Loads a .vst3 module, creates one audio-module class, processes 32-bit float
// audio, takes MIDI (notes / CC / pitch bend ...) and exposes parameters, state
// and the plug-in editor view.

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <set>
#include <functional>

namespace vst3host {

struct ClassEntry
{
    int         index = 0;          // index inside the factory (all classes)
    std::string name;
    std::string vendor;
    std::string subCategories;
    uint32_t    uid = 0;            // 32-bit hash of the class id (stable)
    bool        isInstrument = false;
};

struct Info
{
    std::string name;
    std::string vendor;
    uint32_t    uid = 0;
    bool        isInstrument = false;
    int         chansIn = 0;
    int         chansOut = 0;
    bool        hasEditor = false;
    int         editorWidth = 0;
    int         editorHeight = 0;
};

// "C:/x/Foo.vst3|3" -> path "C:/x/Foo.vst3", class index 3 (-1 when no suffix)
void splitPath(const std::string &full, std::string &path, int &classIndex);
bool isVst3Path(const std::string &full);

// List the audio-module classes of a file (factory only, nothing is instantiated).
bool scan(const std::string &fileUtf8, std::vector<ClassEntry> &out, std::string *err = nullptr);

class Impl;

class Plugin
{
public:
    // wantInstrument: used to pick a class when the path has no "|index" suffix.
    static Plugin *load(const std::string &fullPathUtf8, bool wantInstrument,
                        double sampleRate, std::string *err = nullptr);
    ~Plugin();

    const Info &info() const;
    bool isInstrument() const;

    // ---- audio (audio thread) ----
    // in may be null (instruments). Interleaved float, hostChans = 1 or 2.
    void process(const float *in, float *out, int frames, int hostChans);

    // ---- midi (any thread) ----
    void noteOn(int ch, int note, int velocity);
    void noteOff(int ch, int note, int velocity);
    void polyPressure(int ch, int note, int value);
    void controller(int ch, int cc, int value);          // 0..127
    void channelPressure(int ch, int value);             // 0..127
    void pitchBend(int ch, int value14);                 // 0..16383
    void programChange(int ch, int program);
    void allNotesOff(int ch);                            // ch < 0: all channels
    void reset();

    // ---- parameters / state ----
    int   paramCount() const;
    float param(int index) const;
    void  setParam(int index, float normalized);
    std::vector<char> saveState();
    bool  loadState(const char *data, size_t size);

    void setBypass(bool b);
    bool bypass() const;

    // ---- editor ----
    // parent: HWND on Windows. null detaches. Returns true when attached/detached.
    bool embedEditor(void *parent);
    void editorSize(int &w, int &h) const;

private:
    Plugin();
    Impl *d;
};

} // namespace vst3host

#endif // VST3HOST_H
