#ifndef MIDIOUT_H
#define MIDIOUT_H

#ifdef __linux__
#include <rtmidi/RtMidi.h>
#else
#include <RtMidi.h>
#endif

class MidiOut : public RtMidiOut
{
public:
    MidiOut();
    ~MidiOut();

    float volume() { return oVolume; }
    void setVolume(float vol);
    void sendNoteOff(int ch, int note, int velocity);
    void sendNoteOn(int ch, int note, int velocity);
    void sendNoteAftertouch(int ch, int note, int value);
    void sendController(int ch, int number, int value);
    void sendProgramChange(int ch, int number);
    void sendChannelAftertouch(int ch, int value);
    void sendPitchBend(int ch, int value);
    void sendAllNotesOff(int ch);
    void sendAllNotesOff();
    void sendAllSoundOff(int ch);
    void sendAllSoundOff();
    void sendResetAllControllers(int ch);
    void sendResetAllControllers();

private:
    void send3(int status, int d1, int d2);
    void send2(int status, int d1);
    void sendSafe(std::vector<unsigned char> &m);
    float oVolume;
};

#endif // MIDIOUT_H
