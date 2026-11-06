#ifndef MIDIPLAYER_H
#define MIDIPLAYER_H

#include "MidiOut.h"
#include "Channel.h"
#include "MidiSequencer.h"
#include "MidiSynthesizer.h"

#include <QObject>
#include <atomic>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

enum class PlayerState
{
    Playing,
    Stopped,
    Paused,
    Finished
};

#include "Midi/FaderTaper.h"

class MidiPlayer : public QObject
{
    Q_OBJECT
public:
    explicit MidiPlayer(QObject *parent = nullptr);
    ~MidiPlayer();

    static QStringList midiDevices();
    static QStringList midiInDevices();
    static bool isSnareNumber(int num);
    static bool isKickNumber(int num);
    static bool isBassInstrument(int ints);

    MidiSequencer *midiSequencer() { return _midiSeq; }
    MidiSequencer *midiSequencerTemp() { return _midiSeqTemp; }
    MidiSynthesizer *midiSynthesizer() { return _midiSynth; }
    Channel *midiChannel() { return _midiChannels; }
    int midiOutPortNumber() { return _midiPortNum; }
    int midiInPortNumber() { return _midiPortInNum; }
    int volume() { return _volume; }
    // master mute: output gain forced to 0 while the level (_volume) is kept
    bool isMasterMute() { return _masterMute; }
    void setMasterMute(bool on) { _masterMute = on; setVolume(_volume); }
    float effectiveGain() { return _masterMute ? 0.0f : FaderTaper::gain(_volume); }
    int transpose() { return _midiTranspose; }

    int  lockDrumNumber()  { return _lockDrumNumber; }
    int  lockSnareNumber() { return _lockSnareNumber; }
    int  lockKickNumber()  { return _lockKickNumber; }
    int  lockBassNumber()  { return _lockBassBumber; }
    bool isLockDrum()   { return _lockDrum; }
    // do not take volume / pan / reverb / chorus / reset-controllers from the MIDI-in (a karaoke program with its own mixer)
    void setIgnoreExtMixer(bool v) { _ignoreExtMixer = v; }
    bool ignoreExtMixer() const    { return _ignoreExtMixer; }
    bool isLockSnare()  { return _lockSnare; }
    bool isLockKick()   { return _lockKick; }
    bool isLockBass()   { return _lockBass; }

    MidiFile *midiFile();
    MidiFile *midiFileTemp();

    bool isUsedMidiSynthesizer();

    bool isPlayerPlaying();
    bool isPlayerStopped();
    bool isPlayerPaused();
    bool isPlayerFinished();
    PlayerState playerState();

    long durationMs();
    long positionMs();
    int durationTick();
    int positionTick();
    int bpmSpeed();
    int currentBpm();
    int currentBeat();
    int beatCount();

    bool setMidiOut(int portNumber);
    bool setMidiIn(int portNumber);
    bool load(const QString &file, bool seekFileChunkID = false);
    void play();
    void stop(bool resetPos = false);
    void setVolume(int v);
    void setVolume(int ch, int v);
    void setInstrument(int ch, int i);
    void setMute(int ch, bool mute);
    void setSolo(int ch, bool solo);
    void setPan(int ch, int v);
    void setReverb(int ch, int v);
    void setChorus(int ch, int v);
    void setLockVolume(int ch, bool lock);

    void setPositionTick(int t);
    void setTranspose(int t);
    void setBpmSpeed(int sp);

    void setLockDrum(bool lock, int number = 0);
    void setLockSnare(bool lock, int number = 38);
    void setLockKick(bool lock, int number = 36);
    void setLockBass(bool lock, int number = 32);

    void setMapChannelOutput(int ch, int port);
    void receiveMidiIn(std::vector< unsigned char > *message);

    bool isUseMedley() { return _useMedley; }
    void setUseMedley(bool use);

    int medleyBPM() { return _medleyBPM; }
    void setMedleyBPM(int bpm);

    bool loadNextMedley(const QString &file, int cutStartBar, int cutEndBar, int midiSpeed, int transpose);
    void unloadNextMedley();

public slots:
    void sendEvent(MidiEvent e);

signals:
    void loaded();
    void finished();
    void sendedEvent(MidiEvent e);
    void bpmChanged(int bpm);
    void nextMedleyStarted();
    void nextMedleyAfterStarted();

private slots:
    void onSeqFinished();
    void onSeqBpmChanged(int bpm);

private:
    void sendEventToDevices(MidiEvent evt);
    void sendAllNotesOff(int ch);
    void sendAllNotesOff();
    void sendAllSoundOff(int ch);
    void sendAllSoundOff();
    void sendResetAllControllers(int ch);
    void sendResetAllControllers();
    void releaseSustain();                                        // sustain / sostenuto pedal up on every channel
    void deliverMidiIn(const std::vector<unsigned char> &msg);    // MIDI-in after the mixer filter (delayed or direct)

    int getNoteNumberToPlay(int ch, int defaultNote);
    void calculateUsedPort();

private:
    MidiSequencer *_midiSeq;
    MidiSequencer *_midiSeqTemp = nullptr;
    QMap<int, MidiOut*> _midiOuts;
    MidiOut *openOut(int port);   // opened once; nullptr if the port cannot be opened
    MidiSynthesizer     *_midiSynth;
    RtMidiIn            *_midiIn = nullptr;
    Channel             _midiChannels[16];
    int                 _midiPortNum = 0;
    int                 _midiPortInNum = -1;
    int                 _volume = 50;
    bool                _masterMute = false;
    int                 _midiTranspose = 0;
    int                 _midiTransposeTemp = 0;
    int                 _seqIndex = 0;
    int                 _medleyBPM = 120;
    bool                _useMedley = false;
    bool                _useSolo = false;

    QString _medleyId = "";

    MidiEvent   _tempEvent;
    MidiEvent*  _playingEventPtr = nullptr;
    // sendEvent() is called from the sequencer, the MIDI-in callback, the delay queue and the UI: one at a time
    std::recursive_mutex _sendMx;
    // note number / port each sounding note was really played with (after transpose / lock kick-snare / bass octave),
    // so its note-off matches even if those settings or the channel's program changed in between; -1 = not sounding
    int _playedNote[16][128];
    int _playedPort[16][128];

    bool    _ignoreExtMixer = true;
    bool    _lockDrum  = false;
    bool    _lockSnare = false;
    bool    _lockKick  = false;
    bool    _lockBass  = false;
    int     _lockDrumNumber  = 0;
    int     _songDrumProgram = 0;   // drum kit the song itself asked for (restored when the lock is turned off)
    int     _lockSnareNumber = 38;
    int     _lockKickNumber  = 36;
    int     _lockBassBumber  = 32;
};

void midiIncallback( double deltatime, std::vector< unsigned char > *message, void *userData );

#endif // MIDIPLAYER_H
