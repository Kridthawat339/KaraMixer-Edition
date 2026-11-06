#include "MidiPlayer.h"
#include "Midi/FaderTaper.h"
#include <algorithm>

#include <QtMath>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif


MidiPlayer::MidiPlayer(QObject *parent) : QObject(parent)
{
    for (int c = 0; c < 16; c++)
        for (int n = 0; n < 128; n++) {
            _playedNote[c][n] = -1;
            _playedPort[c][n] = -1;
        }

    _midiSeq = new MidiSequencer();

    _midiSynth  = new MidiSynthesizer();

    if (midiDevices().size() > 0)
    {
        setMidiOut(0);
    }
}

MidiPlayer::~MidiPlayer()
{
    delete _midiSeq;

    if (_midiSeqTemp != nullptr)
        delete _midiSeqTemp;

    for (MidiOut *out : _midiOuts.values()) {
        if (out) {
            out->closePort();
            delete out;
        }
    }

    delete _midiSynth;
}

QStringList MidiPlayer::midiDevices()
{
    QStringList outName;

    #ifdef _WIN32
    UINT outCount = midiOutGetNumDevs();
    for (UINT i=0; i<outCount; i++)
    {
        MIDIOUTCAPS outCaps;
        if (midiOutGetDevCaps(i, &outCaps, sizeof(MIDIOUTCAPS))  != MMSYSERR_NOERROR)
            continue;
        outName.append(QString::fromStdWString(outCaps.szPname));
    }
    #else
    MidiOut o;
    for (int i=0; i<o.getPortCount(); i++) {
        outName.append(QString::fromStdString(o.getPortName(i)));
    }
    #endif

    return outName;
}

QStringList MidiPlayer::midiInDevices()
{
    QStringList inName;

    #ifdef _WIN32
    UINT inCount = midiInGetNumDevs();
    for (UINT i=0; i<inCount; i++)
    {
        MIDIINCAPS inCaps;
        if (midiInGetDevCaps(i, &inCaps, sizeof(MIDIINCAPS))  != MMSYSERR_NOERROR)
            continue;
        inName.append(QString::fromStdWString(inCaps.szPname));
    }
    #else
    RtMidiIn in;
    for (int i=0; i<in.getPortCount(); i++) {
        inName.append(QString::fromStdString(in.getPortName(i)));
    }
    #endif

    return inName;
}

bool MidiPlayer::isSnareNumber(int num)
{
    if ( (num == 31) || (num == 34) || (num == 38) || (num == 40) )
        return true;
    else
        return false;
}

bool MidiPlayer::isKickNumber(int num)
{
    return (num == 35) || (num == 36);
}

bool MidiPlayer::isBassInstrument(int ints)
{
    if ( (ints < 32) || (ints > 39) )
        return false;
    else
        return true;
}

MidiFile *MidiPlayer::midiFile()
{
    return _midiSeq->midiFile();
}

MidiFile *MidiPlayer::midiFileTemp()
{
    if (_midiSeqTemp == nullptr)
        return nullptr;
    else
        return _midiSeqTemp->midiFile();
}

bool MidiPlayer::isUsedMidiSynthesizer()
{
    for (int i=0; i<16; i++)
    {
        if (_midiChannels[i].port() == -1)
            return true;
    }

    return false;
}

bool MidiPlayer::isPlayerPlaying()
{
    return _midiSeq->isSeqPlaying();
}

bool MidiPlayer::isPlayerStopped()
{
    return _midiSeq->isSeqStopped();
}

bool MidiPlayer::isPlayerPaused()
{
    return _midiSeq->isSeqPaused();
}

bool MidiPlayer::isPlayerFinished()
{
    return _midiSeq->isSeqFinished();
}

PlayerState MidiPlayer::playerState()
{
    if (isPlayerFinished())
        return PlayerState::Finished;
    else if (isPlayerPlaying())
        return PlayerState::Playing;
    else if (isPlayerPaused())
        return PlayerState::Paused;
    else
        return PlayerState::Stopped;
}

long MidiPlayer::durationMs()
{
    return _midiSeq->durationMs();
}

long MidiPlayer::positionMs()
{
    return _midiSeq->positionMs();
}

int MidiPlayer::durationTick()
{
    return _midiSeq->durationTick();
}

int MidiPlayer::positionTick()
{
    return _midiSeq->positionTick();
}

int MidiPlayer::bpmSpeed()
{
    return _midiSeq->bpmSpeed();
}

int MidiPlayer::currentBpm()
{
    return _midiSeq->currentBpm();
}

int MidiPlayer::currentBeat()
{
    return _midiSeq->currentBeat();
}

int MidiPlayer::beatCount()
{
    return _midiSeq->beatCount();
}

// opens (once) the MIDI out port; nullptr when the port cannot be opened. RtMidi throws on a busy / missing port
// (e.g. held by the karaoke program): uncaught, that ended the program
MidiOut *MidiPlayer::openOut(int port)
{
    MidiOut *out = _midiOuts.value(port, nullptr);
    if (out)
        return out;
    try {
        out = new MidiOut();
        out->openPort(port);
    } catch (...) {
        delete out;
        return nullptr;
    }
    if (!out->isPortOpen()) {
        delete out;
        return nullptr;
    }
    out->setVolume(qMin(1.0f, effectiveGain()));
    _midiOuts[port] = out;
    return out;
}

bool MidiPlayer::setMidiOut(int portNumber)
{
    if (portNumber < -1 || (portNumber != -1 && portNumber >= midiDevices().size()))
        return false;

    if (!isPlayerStopped())
        stop(true);

    int oldPort = _midiPortNum;
    bool result = false;

    if (portNumber == -1) {
        _midiSynth->open();
        _midiSynth->setVolume(effectiveGain());
        _midiPortNum = -1;
        result = true;
    } else {
        MidiOut *out = openOut(portNumber);
        result = out != nullptr && out->isPortOpen();
        if (out)
            out->setVolume(qMin(1.0f, effectiveGain()));
        _midiPortNum = result ? portNumber : _midiPortNum;
    }

    for (int i=0; i<16; i++) {
        if (_midiChannels[i].port() != oldPort)
            continue;
        _midiChannels[i].setPort(_midiPortNum);
    }

    calculateUsedPort();

    return result;
}

bool MidiPlayer::setMidiIn(int portNumber)
{
    if (portNumber != -1 && portNumber >= midiInDevices().size())
        return false;

    if (portNumber == _midiPortInNum)
        return true;

    _midiPortInNum = portNumber;

    if (portNumber == -1) {
        if (_midiIn != nullptr) {
            _midiIn->closePort();
            delete _midiIn;
            _midiIn = nullptr;
        }
    } else {
        try {
            if (_midiIn == nullptr) {
                _midiIn = new RtMidiIn();
                _midiIn->openPort(portNumber);
                _midiIn->setCallback(&midiIncallback, this);
            } else {
                _midiIn->closePort();
                _midiIn->openPort(portNumber);
            }
        } catch (...) {
            // port busy (e.g. held by the karaoke program): keep the UI alive
            if (_midiIn != nullptr) {
                try { _midiIn->closePort(); } catch (...) {}
                delete _midiIn;
                _midiIn = nullptr;
            }
            _midiPortInNum = -1;
            return false;
        }
    }

    return true;
}

bool MidiPlayer::load(const QString &file, bool seekFileChunkID)
{
    if (!isPlayerStopped())
        stop(true);

    _midiSeq->deleteLater();
    _midiSeq = new MidiSequencer();

    connect(_midiSeq, SIGNAL(playingEvent(MidiEvent)),
            this, SLOT(sendEvent(MidiEvent)), Qt::DirectConnection);
    connect(_midiSeq, SIGNAL(bpmChanged(int)),
            this, SLOT(onSeqBpmChanged(int)), Qt::DirectConnection);
    connect(_midiSeq, SIGNAL(finished()),
            this, SLOT(onSeqFinished()), Qt::DirectConnection);

    if (!_midiSeq->load(file, seekFileChunkID))
        return false;

    if (_useMedley)
        _midiSeq->midiFile()->setSingleTempo(true);

    _midiTranspose = 0;

    for (int i=0; i<16; i++) {
        bool isLockVol = _midiChannels[i].isLockVol();
        int lockVol = _midiChannels[i].volume();
        _midiChannels[i].setInstrument(0);
        _midiChannels[i].setVolume(isLockVol ? lockVol : 100);
        _midiChannels[i].setPan(64);
        _midiChannels[i].setReverb(0);
        _midiChannels[i].setChorus(0);
        _midiChannels[i].setInstrumentType(InstrumentType::Piano);
    }
    _midiChannels[9].setInstrumentType(InstrumentType::PercussionEtc);

    _midiSynth->compactSoundfont();

    emit loaded();

    return true;
}

void MidiPlayer::play()
{
    if (isPlayerStopped()) {
        sendResetAllControllers();
        MidiEvent ev;
        ev.setEventType(MidiEventType::ProgramChange);
        ev.setChannel(9);
        if (_lockDrum) {
            ev.setData1(_lockDrumNumber);
        } else {
            ev.setData1(0);
        }
        _songDrumProgram = 0;
        sendEvent(ev);
        emit sendedEvent(ev);
    }
    _midiSeq->start();
}

void MidiPlayer::stop(bool resetPos)
{
    if (isPlayerStopped())
        return;

    _midiSeq->stop(resetPos);

    sendAllNotesOff();
    releaseSustain();       // all-notes-off keeps the notes held by a pedal that is still down
}

void MidiPlayer::setVolume(int v)
{
    if (v < 0) _volume = 0;
    else if (v > 100) _volume = 100;
    else _volume = v;

    _midiSynth->setVolume(effectiveGain());
    for (MidiOut *out : _midiOuts.values()) {
        if (!out) continue;
        out->setVolume(qMin(1.0f, effectiveGain()));
    }
}

void MidiPlayer::setVolume(int ch, int v)
{
    if (ch < 0 || ch > 15)
        return;

    int vl = 0;
    if (v > 127) vl = 127;
    else if (v < 0) vl = 0;
    else vl = v;

    MidiEvent evt;
    evt.setChannel(ch);
    evt.setEventType(MidiEventType::Controller);
    evt.setData1(7);
    evt.setData2(vl);

    // when lock ch volume ch info is not set
    _midiChannels[ch].setVolume(vl);

    sendEvent(evt);
}

void MidiPlayer::setInstrument(int ch, int i)
{
    if (ch < 0 || ch > 15)
        return;

    int v = 0;
    if (i > 127) v = 127;
    else if (i < 0) v = 0;
    else v = i;

    _midiSynth->sendProgramChange(ch, v);
    for (MidiOut *out : _midiOuts.values()) {
        if (!out) continue;
        out->sendProgramChange(ch, v);
    }

    _midiChannels[ch].setInstrument(v);
    _midiChannels[ch].setInstrumentType(MidiHelper::getInstrumentType(v));
}

void MidiPlayer::setMute(int ch, bool mute)
{
    if (ch < 0 || ch > 15)
        return;

    if (mute == _midiChannels[ch].isMute())
        return;

    _midiChannels[ch].setMute(mute);

    if (mute)
        sendAllNotesOff(ch);
}

void MidiPlayer::setSolo(int ch, bool solo)
{
    if (ch < 0 || ch > 15)
        return;

    if (solo == _midiChannels[ch].isSolo())
        return;

    _midiChannels[ch].setSolo(solo);

    bool us = false;
    for (int i=0; i<16; i++) {
        if (_midiChannels[i].isSolo()) {
            us = true;
        } else {
            sendAllNotesOff(i);
        }
    }

    _useSolo = us;

    if (isPlayerPlaying()) {
        for (int i=0; i<16; i++) {
            if (_midiChannels[i].isSolo()) {
                continue;
            }
            sendAllNotesOff(i);
        }
    }
}

void MidiPlayer::setPan(int ch, int v)
{
    if (ch < 0 || ch > 15)
        return;

    int vl = 0;
    if (v > 127) vl = 127;
    else if (v < 0) vl = 0;
    else vl = v;

    MidiEvent evt;
    evt.setChannel(ch);
    evt.setEventType(MidiEventType::Controller);
    evt.setData1(10);
    evt.setData2(vl);

    sendEvent(evt);
}

void MidiPlayer::setReverb(int ch, int v)
{
    if (ch < 0 || ch > 15)
        return;

    int vl = 0;
    if (v > 127) vl = 127;
    else if (v < 0) vl = 0;
    else vl = v;

    MidiEvent evt;
    evt.setChannel(ch);
    evt.setEventType(MidiEventType::Controller);
    evt.setData1(91);
    evt.setData2(vl);

    sendEvent(evt);
}

void MidiPlayer::setChorus(int ch, int v)
{
    if (ch < 0 || ch > 15)
        return;

    int vl = 0;
    if (v > 127) vl = 127;
    else if (v < 0) vl = 0;
    else vl = v;

    MidiEvent evt;
    evt.setChannel(ch);
    evt.setEventType(MidiEventType::Controller);
    evt.setData1(93);
    evt.setData2(vl);

    sendEvent(evt);
}

void MidiPlayer::setLockVolume(int ch, bool lock)
{
    if (ch < 0 || ch > 15)
        return;

    _midiChannels[ch].setLockVol(lock);
}

void MidiPlayer::setPositionTick(int t)
{
    // seeking while playing: the notes sounding at the old position must stop (they never get their note-off)
    const bool wasPlaying = isPlayerPlaying();
    if (wasPlaying) {
        _midiSeq->stop();
        sendAllNotesOff();
        releaseSustain();
    }
    _midiSeq->setPositionTick(t);
    if (wasPlaying)
        _midiSeq->start();
}

void MidiPlayer::setTranspose(int t)
{
    // limit the NEW value (checking the current one locked the key for good once it reached +-12)
    if (t < -12) t = -12;
    if (t > 12)  t = 12;
    if (t == _midiTranspose)
        return;

    _midiTranspose = t;

    if (isPlayerPlaying()) {
        for (int i=0; i<16; i++) {
            if (i == 9)
                continue;
            sendAllNotesOff(i);
        }
    }
}

void MidiPlayer::setBpmSpeed(int sp)
{
    _midiSeq->setBpmSpeed(sp);
}

void MidiPlayer::setLockDrum(bool lock, int number)
{
    _lockDrum = lock;
    _lockDrumNumber = number;
    if (_midiSynth)
        _midiSynth->setLockedDrumKit(lock ? number : -1);

    if (!isPlayerStopped()) {
        // locked: switch to the chosen kit now; unlocked: go back to the kit the song asked for
        const int songKit = _songDrumProgram;   // (the event below must not overwrite it)
        MidiEvent ev;
        ev.setEventType(MidiEventType::ProgramChange);
        ev.setChannel(9);
        ev.setData1(lock ? number : songKit);
        sendEvent(ev);
        emit sendedEvent(ev);
        _songDrumProgram = songKit;
    }
}

void MidiPlayer::setLockSnare(bool lock, int number)
{
    if ( !isSnareNumber(number) )
        return;

    _lockSnare = lock;
    _lockSnareNumber = number;

    if (isPlayerPlaying()) {
        sendAllNotesOff(9);
    }
}

void MidiPlayer::setLockKick(bool lock, int number)
{
    if ( !isKickNumber(number) )
        return;

    _lockKick = lock;
    _lockKickNumber = number;

    if (isPlayerPlaying()) {
        sendAllNotesOff(9);
    }
}

void MidiPlayer::setLockBass(bool lock, int number)
{
    if ( !isBassInstrument(number) )
        return;

    _lockBass = lock;
    _lockBassBumber = number;

    if (isPlayerStopped())
        return;

    if (lock) {
        for (int i=0; i<16; i++) {
            if (i == 9 || !isBassInstrument(_midiChannels[i].instrument()))
                continue;   // channel 10 is the drums: program 32-39 there is a drum kit, not a bass
            MidiEvent ev;
            ev.setEventType(MidiEventType::ProgramChange);
            ev.setChannel(i);
            ev.setData1(number);
            sendEvent(ev);
            emit sendedEvent(ev);
        }
    }
}

void MidiPlayer::setMapChannelOutput(int ch, int port)
{
    if (ch < 0 || ch > 15)
        return;
    if (port < -1 || (port != -1 && port >= midiDevices().size()))
        return;

    if (port == _midiChannels[ch].port())
        return;

    if (port == -1)
    {
        if (!_midiSynth->isOpened()) {
            _midiSynth->open();
            _midiSynth->setVolume(effectiveGain());
        }

        if (!this->isPlayerStopped()) {
            _midiSynth->sendProgramChange(ch, _midiChannels[ch].instrument());
            _midiSynth->sendController(ch, 7, _midiChannels[ch].volume());
            _midiSynth->sendController(ch, 10, _midiChannels[ch].pan());
            _midiSynth->sendController(ch, 91, _midiChannels[ch].reverb());
            _midiSynth->sendController(ch, 93, _midiChannels[ch].chorus());
        }
    }
    else
    {
        MidiOut *out = openOut(port);
        if (!out)
            return;             // port busy / gone: the channel stays where it was

        if (!this->isPlayerStopped()) {
            out->sendProgramChange(ch, _midiChannels[ch].instrument());
            out->sendController(ch, 7, _midiChannels[ch].volume());
            out->sendController(ch, 10, _midiChannels[ch].pan());
            out->sendController(ch, 91, _midiChannels[ch].reverb());
            out->sendController(ch, 93, _midiChannels[ch].chorus());
        }
    }

    if (isPlayerPlaying()) {
        _midiChannels[ch].setChangingPort(true);
        sendAllNotesOff(ch);
        _midiChannels[ch].setPort(port);
        _midiChannels[ch].setChangingPort(false);
    } else {
        _midiChannels[ch].setPort(port);
    }

    this->calculateUsedPort();
}

void MidiPlayer::receiveMidiIn(std::vector<unsigned char> *message)
{
    if (_ignoreExtMixer && message->size() >= 3 && ((*message)[0] & 0xF0) == 0xB0) {
        // the karaoke program's own mixer: its volume / pan / expression / reverb / chorus and "reset all controllers"
        // would overwrite what was set here
        switch ((*message)[1]) {
        case 7: case 10: case 11: case 91: case 93:
            return;
        case 121: {
            // "reset all controllers" would also reset this mixer's levels, so it is not passed on - but the pedals,
            // modulation and pitch bend it resets must still be let go of, otherwise the next song's notes hang
            // (sustain stuck down) or play out of tune / with vibrato
            const unsigned char st = (*message)[0];
            const unsigned char ch = st & 0x0F;
            deliverMidiIn({ st, 64, 0 });
            deliverMidiIn({ st, 66, 0 });
            deliverMidiIn({ st, 1, 0 });
            deliverMidiIn({ (unsigned char)(0xE0 | ch), 0, 64 });
            return;
        }
        default:
            break;
        }
    }
    deliverMidiIn(*message);
}

void MidiPlayer::deliverMidiIn(const std::vector<unsigned char> &msg)
{
    std::vector<unsigned char> m = msg;
    MidiEvent ev;
    ev.setMessage(&m);
    this->sendEvent(ev);
}

void MidiPlayer::releaseSustain()
{
    for (int ch = 0; ch < 16; ch++) {
        const int port = _midiChannels[ch].port();
        if (port == -1) {
            _midiSynth->sendController(ch, 64, 0);      // sustain pedal released
            _midiSynth->sendController(ch, 66, 0);      // sostenuto
        } else if (MidiOut *out = _midiOuts.value(port, nullptr)) {
            out->sendController(ch, 64, 0);
            out->sendController(ch, 66, 0);
        }
    }
}

void MidiPlayer::setUseMedley(bool use)
{
    if (use == _useMedley)
        return;

    _useMedley = use;

    if (!_useMedley)
        _medleyId = "";
}

void MidiPlayer::setMedleyBPM(int bpm)
{
    if (bpm == _medleyBPM)
        return;

    _medleyBPM = bpm;
}

bool MidiPlayer::loadNextMedley(const QString &file, int cutStartBar, int cutEndBar, int midiSpeed, int transpose)
{
    if (!_useMedley)
        return false;

    if (_midiSeqTemp != nullptr)
        delete _midiSeqTemp;

    _midiSeqTemp = new MidiSequencer();
    if (!_midiSeqTemp->load(file, true)) {
        delete _midiSeqTemp;        // an empty sequencer would otherwise be switched in as "the next song"
        _midiSeqTemp = nullptr;
        return false;
    }

    _midiSeqTemp->midiFile()->setSingleTempo(true);

    _midiSeqTemp->setBpmSpeed(midiSpeed);
    _midiTransposeTemp = transpose;

    _midiSeqTemp->setCutStartBar(cutStartBar);
    _midiSeqTemp->setCutEndBar(cutEndBar);

    return true;
}

void MidiPlayer::unloadNextMedley()
{
    if (_midiSeqTemp != nullptr) {
        delete _midiSeqTemp;
        _midiSeqTemp = nullptr;
        _midiTransposeTemp = 0;
    }
}

void MidiPlayer::sendEvent(MidiEvent e)
{
    MidiEvent sent;
    {
        std::lock_guard<std::recursive_mutex> lock(_sendMx);
        _playingEventPtr = &e;

        // note-offs always pass: a note that started before the channel was muted / another one soloed must stop
        if (e.eventType() == MidiEventType::Controller
            || e.eventType() == MidiEventType::ProgramChange
            || e.eventType() == MidiEventType::NoteOff) {
            sendEventToDevices(e);
        } else {
            if (_midiChannels[e.channel()].isMute() == false) {
                if (_useSolo) {
                    if (_midiChannels[e.channel()].isSolo()) {
                        sendEventToDevices(e);
                    }
                } else {
                    sendEventToDevices(e);
                }
            }
        }

        sent = *_playingEventPtr;
        _playingEventPtr = nullptr;
    }

    emit sendedEvent(sent);
}

void MidiPlayer::onSeqFinished()
{
    if (_useMedley) {
        if (_midiSeq->isSeqFinished() && (_midiSeqTemp != nullptr)) {
            _midiSeq->deleteLater();
            _midiSeq = _midiSeqTemp;
            _midiSeqTemp = nullptr;

            connect(_midiSeq, SIGNAL(playingEvent(MidiEvent)),
                    this, SLOT(sendEvent(MidiEvent)), Qt::DirectConnection);
            connect(_midiSeq, SIGNAL(bpmChanged(int)),
                    this, SLOT(onSeqBpmChanged(int)), Qt::DirectConnection);
            connect(_midiSeq, SIGNAL(finished()),
                    this, SLOT(onSeqFinished()), Qt::DirectConnection);

            _midiTranspose = _midiTransposeTemp;
            _midiTransposeTemp = 0;

            _midiSynth->compactSoundfont();
            _midiSeq->start();

            emit nextMedleyStarted();
            emit nextMedleyAfterStarted();
        } else if (_midiSeq->isSeqFinished() && (_midiSeqTemp == nullptr)) {
            emit finished();
        }
    } else {
        emit finished();
    }
}

void MidiPlayer::onSeqBpmChanged(int bpm)
{
    emit bpmChanged(bpm);
}

void MidiPlayer::sendEventToDevices(MidiEvent evt)
{
    MidiEvent *e = &evt;
    int ch = e->channel();

    switch (e->eventType()) {
        case MidiEventType::NoteOff: {
            const int key = e->data1() & 0x7F;
            int n, port;
            if (_playedNote[ch][key] >= 0) {
                // the note number / port it was really played with (transpose, lock kick-snare ... may have changed)
                n = _playedNote[ch][key];
                port = _playedPort[ch][key];
                _playedNote[ch][key] = -1;
            } else {
                n = getNoteNumberToPlay(ch, key);
                port = _midiChannels[ch].port();
            }
            if (port == -1) {
                _midiSynth->sendNoteOff(ch, n, e->data2());
            } else if (MidiOut *out = _midiOuts.value(port, nullptr)) {
                out->sendNoteOff(ch, n, e->data2());
            }
            break;
        }
        case MidiEventType::NoteOn: {
            if (_midiChannels[ch].isChangingPort())
                break;
            const int key = e->data1() & 0x7F;
            int n = getNoteNumberToPlay(ch, key);
            const int port = _midiChannels[ch].port();
            const int oldN = _playedNote[ch][key];
            const int oldPort = _playedPort[ch][key];
            if (oldN >= 0 && (oldN != n || oldPort != port)) {
                // struck again before its note-off, but now as another note / on another port: stop the old one
                if (oldPort == -1)
                    _midiSynth->sendNoteOff(ch, oldN, 0);
                else if (MidiOut *out = _midiOuts.value(oldPort, nullptr))
                    out->sendNoteOff(ch, oldN, 0);
            }
            _playedNote[ch][key] = n;
            _playedPort[ch][key] = port;
            if (port == -1) {
                _midiSynth->sendNoteOn(ch, n, e->data2());
            } else if (MidiOut *out = _midiOuts.value(port, nullptr)) {
                out->sendNoteOn(ch, n, e->data2());
            }
            break;
        }
        case MidiEventType::NoteAftertouch: {
            int n = getNoteNumberToPlay(ch, e->data1());
            if (_midiChannels[ch].port() == -1) {
                _midiSynth->sendNoteAftertouch(ch, n, e->data2());
            } else {
                if (MidiOut *o = _midiOuts.value(_midiChannels[ch].port(), nullptr)) o->sendNoteAftertouch(ch, n, e->data2());
            }
            break;
        }
        case MidiEventType::Controller: {
            switch (e->data1()) {
            case 7: {
                if (_midiChannels[ch].isLockVol()) {
                    _tempEvent = *e;
                    _tempEvent.setData2(_midiChannels[ch].volume());
                    _playingEventPtr = &_tempEvent;
                    e = &_tempEvent;
                } else {
                    _midiChannels[ch].setVolume(e->data2());
                }
                break;
            }
            case 10: _midiChannels[ch].setPan(e->data2()); break;
            case 91: _midiChannels[ch].setReverb(e->data2()); break;
            case 93: _midiChannels[ch].setChorus(e->data2()); break;
            default: break;
            }

            if (_midiChannels[ch].port() == -1) {
                _midiSynth->sendController(ch, e->data1(), e->data2());
            } else {
                if (MidiOut *o = _midiOuts.value(_midiChannels[ch].port(), nullptr)) o->sendController(ch, e->data1(), e->data2());
            }
            break;
        }
        case MidiEventType::ProgramChange: {
            int programe = e->data1();
            if (ch == 9)
                _songDrumProgram = programe;
            if (ch == 9 && _lockDrum) {
                programe = _lockDrumNumber;
                _tempEvent = *e;
                _tempEvent.setData1(programe);
                _playingEventPtr = &_tempEvent;
            }
            if (ch != 9 && isBassInstrument(e->data1()) && _lockBass) {
                programe = _lockBassBumber;
                _tempEvent = *e;
                _tempEvent.setData1(programe);
                _playingEventPtr = &_tempEvent;
            }

            _midiChannels[ch].setInstrument(programe);
            if (ch != 9) {
                _midiChannels[ch].setInstrumentType(MidiHelper::getInstrumentType(programe));
            }

            if (_midiChannels[ch].port() == -1) {
                _midiSynth->sendProgramChange(ch, programe);
            } else {
                if (MidiOut *o = _midiOuts.value(_midiChannels[ch].port(), nullptr)) o->sendProgramChange(ch, programe);
            }
            break;
        }
        case MidiEventType::ChannelAftertouch: {
            if (_midiChannels[ch].port() == -1) {
                _midiSynth->sendChannelAftertouch(ch, e->data1());
            } else {
                if (MidiOut *o = _midiOuts.value(_midiChannels[ch].port(), nullptr)) o->sendChannelAftertouch(ch, e->data1());
            }
            break;
        }
        case MidiEventType::PitchBend: {
            if (_midiChannels[ch].port() == -1) {
                _midiSynth->sendPitchBend(ch, e->data1());
            } else {
                if (MidiOut *o = _midiOuts.value(_midiChannels[ch].port(), nullptr)) o->sendPitchBend(ch, e->data1());
            }
            break;
        }
        default:
            break;
    }
}

void MidiPlayer::sendAllNotesOff(int ch)
{
    if (ch < 0 || ch > 15)
        return;

    std::lock_guard<std::recursive_mutex> lock(_sendMx);
    if (_midiChannels[ch].port() == -1) {
        _midiSynth->sendAllNotesOff(ch);
    } else if (MidiOut *out = _midiOuts.value(_midiChannels[ch].port(), nullptr)) {
        out->sendAllNotesOff(ch);
    }
    for (int n = 0; n < 128; n++)
        _playedNote[ch][n] = -1;
}

void MidiPlayer::sendAllNotesOff()
{
    for (int i=0; i<16; i++) {
        this->sendAllNotesOff(i);
    }
}

void MidiPlayer::sendAllSoundOff(int ch)
{
    if (ch < 0 || ch > 15)
        return;
    if (_midiChannels[ch].port() == -1) {
        _midiSynth->sendController(ch, 120, 0);
    } else {
        if (MidiOut *o = _midiOuts.value(_midiChannels[ch].port(), nullptr)) o->sendController(ch, 120, 0);
    }
}

void MidiPlayer::sendAllSoundOff()
{
    for (int i=0; i<16; i++) {
        this->sendAllSoundOff(i);
    }
}

void MidiPlayer::sendResetAllControllers(int ch)
{
    if (ch < 0 || ch > 15)
        return;
    if (_midiChannels[ch].port() == -1) {
            _midiSynth->sendResetAllControllers(ch);
    } else {
        if (MidiOut *o = _midiOuts.value(_midiChannels[ch].port(), nullptr)) o->sendResetAllControllers(ch);
    }
}

void MidiPlayer::sendResetAllControllers()
{
    for (int i=0; i<16; i++) {
        sendResetAllControllers(i);
    }
}

int MidiPlayer::getNoteNumberToPlay(int ch, int defaultNote)
{
    int n = 0;
    if (ch == 9) {
        if (isSnareNumber(defaultNote) && _lockSnare)
            n = _lockSnareNumber;
        else if (isKickNumber(defaultNote) && _lockKick)
            n = _lockKickNumber;
        else
            n = defaultNote;
    }
    else {
        n = defaultNote + _midiTranspose;
        // transposing down can push bass notes below the lowest note the soundfont's bass has samples for
        // (silent bass in some songs): bring them back up by whole octaves
        if (_midiTranspose < 0 && defaultNote >= 28 && isBassInstrument(_midiChannels[ch].instrument())) {
            while (n < 28)
                n += 12;
        }
    }
    return n;
}

void MidiPlayer::calculateUsedPort()
{
    // calculate used port
    // list port all channel
    bool usedSynth = false;
    QList<int> usedPort;
    for (int i=0; i<16; i++) {
        int p = _midiChannels[i].port();
        usedPort.append(p);
        if (p == -1)
            usedSynth = true;
    }

    // remove port not used
    if (!usedSynth) {
        _midiSynth->close();
    }
    for (int pNumber : _midiOuts.keys()) {

        if (usedPort.indexOf(pNumber) != -1)
            continue;

        MidiOut *o = _midiOuts.take(pNumber);   // (an entry can be null: operator[] used to insert one)
        if (o) {
            try { o->closePort(); } catch (...) {}
            delete o;
        }
    }
}

void midiIncallback(double deltatime, std::vector<unsigned char> *message, void *userData)
{
    if (message->size() < 2)
        return;

    if ((message->at(0) & 0xF0) != 0xF0) {

        ((MidiPlayer*)(userData))->receiveMidiIn(message);

    }
}
