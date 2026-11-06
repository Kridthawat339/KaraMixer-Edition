#include "MidiOut.h"

MidiOut::MidiOut()
{
    oVolume = 1.0f;
}

MidiOut::~MidiOut()
{
}

// Every message is built in its own buffer: the functions are called from several threads at once (the song
// sequencer, the MIDI-in callback, the MIDI delay queue, the GUI), and the one shared member buffer they used got
// cleared / filled by two threads at the same time (wrong or broken messages, hanging notes, crashes).
void MidiOut::send3(int status, int d1, int d2)
{
    std::vector<unsigned char> m { (unsigned char)status, (unsigned char)(d1 & 0x7F), (unsigned char)(d2 & 0x7F) };
    sendSafe(m);
}

void MidiOut::send2(int status, int d1)
{
    std::vector<unsigned char> m { (unsigned char)status, (unsigned char)(d1 & 0x7F) };
    sendSafe(m);
}

void MidiOut::sendSafe(std::vector<unsigned char> &m)
{
    try {
        sendMessage(&m);
    } catch (...) {
        // driver error (device unplugged ...): drop the message instead of taking the audio / MIDI thread down
    }
}

void MidiOut::setVolume(float vol)
{
    if (vol < 0.0f) oVolume = 0.0f;
    else if (vol > 1.0f) oVolume = 1.0f;
    else oVolume = vol;

    int vol_14bits = (int)(oVolume * 16383);

    std::vector<unsigned char> m { 0xF0, 0x7F, 0x7F, 0x04, 0x01,
                                   (unsigned char)(vol_14bits & 0x7f), (unsigned char)((vol_14bits >> 7) & 0x7f), 0xF7 };
    sendSafe(m);
}

void MidiOut::sendNoteOff(int ch, int note, int velocity)
{
    if (note < 0 || note > 127)
        return;
    send3(0x80 | (ch & 0x0F), note, velocity);
}

void MidiOut::sendNoteOn(int ch, int note, int velocity)
{
    if (note < 0 || note > 127)
        return;
    send3(0x90 | (ch & 0x0F), note, velocity);
}

void MidiOut::sendNoteAftertouch(int ch, int note, int value)
{
    if (note < 0 || note > 127)
        return;
    send3(0xA0 | (ch & 0x0F), note, value);
}

void MidiOut::sendController(int ch, int number, int value)
{
    send3(0xB0 | (ch & 0x0F), number, value);
}

void MidiOut::sendProgramChange(int ch, int number)
{
    send2(0xC0 | (ch & 0x0F), number);
}

void MidiOut::sendChannelAftertouch(int ch, int value)
{
    send2(0xD0 | (ch & 0x0F), value);
}

void MidiOut::sendPitchBend(int ch, int value)
{
    value = value < 0 ? 0 : (value > 16383 ? 16383 : value);
    send3(0xE0 | (ch & 0x0F), value & 0x7F, value >> 7);
}

void MidiOut::sendAllNotesOff(int ch)
{
    sendController(ch, 123, 0);
}

void MidiOut::sendAllNotesOff()
{
    for (int i=0; i<16; i++) {
        sendAllNotesOff(i);
    }
}

void MidiOut::sendAllSoundOff(int ch)
{
    sendController(ch, 120, 0);
}

void MidiOut::sendAllSoundOff()
{
    for (int i=0; i<16; i++) {
        sendAllSoundOff(i);
    }
}

void MidiOut::sendResetAllControllers(int ch)
{
    sendController(ch, 100, 0);
    sendController(ch, 101, 0);
    sendController(ch, 6, 2);

    sendController(ch, 121, 0); 
}

void MidiOut::sendResetAllControllers()
{
    for (int i=0; i<16; i++) {
        sendResetAllControllers(i);
    }
}
