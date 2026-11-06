#include "MidiFile.h"

#include "MidiHelper.h"

#include <cstdlib>

// ========================================================
bool isGreaterThan(MidiEvent* e1, MidiEvent* e2)
{
    return (e1->tick() < e2->tick());
}
quint16 readUInt16(QFile *in) {
    unsigned char buffer[2] = { 0, 0 };   // short read (truncated file): 0, not stack garbage
    in->read((char*)&buffer, 2);
    return ((uint16_t)(buffer[0]) << 8) | (uint16_t)(buffer[1]);
}

quint32 readUInt32(QFile *in) {
    unsigned char buffer[4] = { 0, 0, 0, 0 };
    in->read((char*)&buffer, 4);
    return ((uint32_t)(buffer[0]) << 24) | ((uint32_t)(buffer[1]) << 16) |
           ((uint32_t)(buffer[2]) << 8) | (uint32_t)(buffer[3]);
}

quint32 readVariableLengthQuantity(QFile *in) {
    unsigned char b = 0;
    uint32_t value = 0;
    do {
        if (!in->getChar((char*)&b))
            break;                      // end of a truncated file: b would be left uninitialised
        value = (value << 7) | (b & 0x7F);
    } while ((b & 0x80) == 0x80 && !in->atEnd());

    return value;
}
// ========================================================

MidiFile::MidiFile()
{
    clear();
}

MidiFile::~MidiFile()
{
    clear();
}

QVector<MidiEvent *> MidiFile::controllerAndProgramEvents()
{
    QVector<MidiEvent*> evnts = fControllerEvents;
    for (MidiEvent *e : fProgramChangeEvents)
        evnts.append(e);

    qStableSort(evnts.begin(), evnts.end(), isGreaterThan);
    return evnts;
}

void MidiFile::clear()
{
    fFormatType = 1;
    fNumOfTracks = 0;
    fResolution = 0;
    fDivision = PPQ;

    fLyrics = "";
    fLyricscursor.clear();

    for (MidiEvent *e : fEvents)
        delete e;

    fEvents.clear();
    fTempoEvents.clear();
    fLyricsEvents.clear();
    fControllerEvents.clear();
    fProgramChangeEvents.clear();
    fTimeSignatureEvents.clear();
}

bool MidiFile::read(const QString &file, bool seekFileChunkID)
{
    if (!QFile::exists(file))
        return false;

    QFile in(file);

    return read(&in, seekFileChunkID);
}

bool MidiFile::read(QFile *in, bool seekFileChunkID)
{
    if (!in->exists() || !in->open(QFile::ReadOnly))
        return false;

    clear();

    char chunkID[4];
    qint64 chunkSize = 0, chunkStart = 0;

    in->read((char*)chunkID, 4);

    if (seekFileChunkID == false) {
        if (memcmp(chunkID, "MThd", 4) != 0){
            in->close();
            return false;
        }
    }

    chunkSize = readUInt32(in);
    chunkStart = in->pos();

    // the header is 6 bytes; the standard lets it grow (newer fields are to be skipped), so only a short one is broken
    if (chunkSize < 6) {
        in->close();
        return false;
    }

    fFormatType = readUInt16(in);
    fNumOfTracks = readUInt16(in);

    unsigned char divResolution[2];
    in->read((char*)divResolution, 2);

    switch ((signed char)(divResolution[0])) {
    case SMPTE24:
        fDivision = SMPTE24;
        fResolution = divResolution[1];
        break;
    case SMPTE25:
        fDivision = SMPTE25;
        fResolution = divResolution[1];
        break;
    case SMPTE30DROP:
        fDivision = SMPTE30DROP;
        fResolution = divResolution[1];
        break;
    case SMPTE30:
        fDivision = SMPTE30;
        fResolution = divResolution[1];
        break;
    default:
        fDivision = PPQ;
        fResolution = divResolution[1] | divResolution[0] << 8;
        if (fResolution <= 0)
            fResolution = 96;   // broken header: avoid dividing by zero in every tick <-> time conversion
        break;
    }

    in->seek(chunkStart + chunkSize);       // past the header, also when it is longer than 6 bytes

    for (int t=0; t<fNumOfTracks; t++) {

        if (in->read((char*)chunkID, 4) != 4)
            break;                              // fewer tracks than the header says: keep the ones read
        chunkSize = readUInt32(in);
        chunkStart = in->pos();

        if (memcmp(chunkID, "MTrk", 4) != 0) {
            // an unknown chunk (e.g. Yamaha XF "XFIH" / "XFKM", RIFF data ...): the standard says skip it.
            // Zero padding or a chunk running past the end of the file: no more tracks (keep the ones read; a file
            // without any track is not a MIDI file)
            if (chunkStart + chunkSize > in->size() || (chunkID[0] == 0 && chunkSize == 0)) {
                if (t == 0) {
                    clear();
                    in->close();
                    return false;
                }
                break;
            }
            in->seek(chunkStart + chunkSize);
            t--;
            continue;
        }

        uint32_t tick = 0, delta = 0;
        unsigned char status, runningStatus = 0;
        bool endOfTrack = false;

        while (in->pos() < (chunkStart + chunkSize) && !endOfTrack && !in->atEnd()) {

            delta = readVariableLengthQuantity(in);
            tick += delta;
            status = 0;
            if (!in->getChar((char*)&status))
                break;

            if ((status & 0x80) == 0) {
                if (runningStatus == 0)
                    continue;           // data byte without any status before it: skip it
                status = runningStatus;
                in->seek(in->pos() - 1);
            } else if (status < 0xF0) {
                runningStatus = status; // only channel messages set running status (not SysEx / meta)
            }

            switch (status & 0xF0) {
            case 0x80: {
                int ch = status & 0x0F;
                char d1, d2;
                in->getChar(&d1);
                in->getChar(&d2);
                createMidiEvent(t, tick, delta, MidiEventType::NoteOff, ch, d1, d2);
                break;
            }
            case 0x90: {
                int ch = status & 0x0F;
                char d1, d2;
                in->getChar(&d1);
                in->getChar(&d2);
                if (d2 != 0) {
                    createMidiEvent(t, tick, delta, MidiEventType::NoteOn, ch, d1, d2);
                } else {
                    createMidiEvent(t, tick, delta, MidiEventType::NoteOff, ch, d1, 0);
                }
                break;
            }
            case 0xA0: {
                int ch = status & 0x0F;
                char d1, d2;
                in->getChar(&d1);
                in->getChar(&d2);
                createMidiEvent(t, tick, delta, MidiEventType::NoteAftertouch, ch, d1, d2);
                break;
            }
            case 0xB0: {
                int ch = status & 0x0F;
                char d1, d2;
                in->getChar(&d1);
                in->getChar(&d2);
                MidiEvent *e = createMidiEvent(t, tick, delta, MidiEventType::Controller, ch, d1, d2);
                fControllerEvents.append(e);
                break;
            }
            case 0xC0: {
                int ch = status & 0x0F;
                char d1;
                in->getChar(&d1);
                MidiEvent *e = createMidiEvent(t, tick, delta, MidiEventType::ProgramChange, ch, d1, 0);
                fProgramChangeEvents.append(e);
                break;
            }
            case 0xD0: {
                int ch = status & 0x0F;
                char d1;
                in->getChar(&d1);
                createMidiEvent(t, tick, delta, MidiEventType::ChannelAftertouch, ch, d1, 0);
                break;
            }
            case 0xE0: {
                int ch = status & 0x0F;
                char d1, d2;
                in->getChar(&d1);
                in->getChar(&d2);
                int pitch = ((d2 & 0x7F) << 7) | (d1 & 0x7F);
                createMidiEvent(t, tick, delta, MidiEventType::PitchBend, ch, pitch, 0);
                break;
            }
            case 0xF0:
                QByteArray data;
                int lenght = 0;
                switch (status) {
                    case 0xF0:
                    case 0xF7:
                        lenght = readVariableLengthQuantity(in);
                        data[0] = status;
                        data += in->read(lenght);
                        createSysExEvent(t, tick, delta, data);
                        break;
                    case 0xFF:
                        char number;
                        in->getChar(&number);
                        lenght = readVariableLengthQuantity(in);
                        data = in->read(lenght);
                        createMetaEvent(t, tick, delta, number, data);
                        if (number == 0x2F) {
                            endOfTrack = true;
                        }
                        break;
                }
                break;
            } // End Switch

        } // End while

        /* forwards compatibility: skip over any unrecognized chunks, or extra
        * data at the end of tracks. */
        in->seek(chunkStart + chunkSize);

    } // For loop read tracks

    qStableSort(fEvents.begin(), fEvents.end(), isGreaterThan);
    qStableSort(fTempoEvents.begin(), fTempoEvents.end(), isGreaterThan);
    qStableSort(fLyricsEvents.begin(), fLyricsEvents.end(), isGreaterThan);
    qStableSort(fControllerEvents.begin(), fControllerEvents.end(), isGreaterThan);
    qStableSort(fProgramChangeEvents.begin(), fProgramChangeEvents.end(), isGreaterThan);
    qStableSort(fTimeSignatureEvents.begin(), fTimeSignatureEvents.end(), isGreaterThan);

    in->close();

    for (auto e : fLyricsEvents) {
        QString lyr = e->data();
        for (auto chr : lyr)
            fLyricscursor.append(e->tick());
        fLyrics += lyr;
    }

    return true;
}

MidiEvent *MidiFile::createMidiEvent(int track, uint32_t tick, uint32_t delta, MidiEventType evType, int ch, int data1, int data2)
{
    // data bytes are read as (signed) char: a broken file gave negative note / controller numbers, used later as
    // array indexes (played-note tables, channel state). MIDI data bytes are 7-bit.
    if (evType != MidiEventType::PitchBend) {
        data1 &= 0x7F;
        data2 &= 0x7F;
    }
    MidiEvent *e = new MidiEvent();
    e->setTrack(track);
    e->setTick(tick);
    e->setDelta(delta);
    e->setEventType(evType);
    e->setChannel(ch);
    e->setData1(data1);
    e->setData2(data2);

    fEvents.append(e);

    return e;
}

MidiEvent *MidiFile::createMetaEvent(int track, uint32_t tick, uint32_t delta, int number, QByteArray data)
{
    MidiEvent *me = new MidiEvent();
    me->setTrack(track);
    me->setTick(tick);
    me->setDelta(delta);
    me->setEventType(MidiEventType::Meta);
    me->setMetaType(number);
    me->setData(data);

    fEvents.append(me);

    if (me->metaEventType() == MidiMetaType::SetTempo)
        fTempoEvents.append(me);
    if (me->metaEventType() == MidiMetaType::Lyrics)
        fLyricsEvents.append(me);
    if (me->metaEventType() == MidiMetaType::TimeSignature)
        fTimeSignatureEvents.append(me);

    return me;
}

MidiEvent *MidiFile::createSysExEvent(int track, uint32_t tick, uint32_t delta, QByteArray data)
{
    MidiEvent *e = new MidiEvent();
    e->setTrack(track);
    e->setTick(tick);
    e->setDelta(delta);
    e->setEventType(MidiEventType::SysEx);
    e->setData(data);

    fEvents.append(e);

    return e;
}

float MidiFile::beatFromTick(uint32_t tick)
{
    switch (fDivision) {
    case PPQ:
        return (float)(tick) / fResolution;
    case SMPTE24:
        return (float)(tick) / 24.0;
    case SMPTE25:
        return (float)(tick) / 25.0;
    case SMPTE30DROP:
        return (float)(tick) / 29.97;
    case SMPTE30:
        return (float)(tick) / 30.0;
    default:
        return 0.0f;
    }
}

float MidiFile::timeFromTick(uint32_t tick, int bpmSpeed)
{
    switch (fDivision) {
    case PPQ: {
        float tempo_event_time = 0.0;
        uint32_t tempo_event_tick = 0;
        float tempo = 120.0 + bpmSpeed;

        for (MidiEvent* e : fTempoEvents) {
            if (e->tick() >= tick) {
                break;
            }
            tempo_event_time +=
                (((float)(e->tick() - tempo_event_tick)) / fResolution / (tempo / 60));
            tempo_event_tick = e->tick();
            tempo = e->bpm() + bpmSpeed;

            if (_singleTempo)
                break;
        }

        float time =
            tempo_event_time + (((float)(tick - tempo_event_tick)) / fResolution / (tempo / 60));
        return time;
    }
    case SMPTE24:
        return (float)(tick) / (fResolution * 24.0);
    case SMPTE25:
        return (float)(tick) / (fResolution * 25.0);
    case SMPTE30DROP:
        return (float)(tick) / (fResolution * 29.97);
    case SMPTE30:
        return (float)(tick) / (fResolution * 30.0);
    default:
        return 0.0f;
    }
}

uint32_t MidiFile::tickFromTime(float time, int bpmSpeed)
{
    switch (fDivision) {
    case PPQ: {
        float tempo_event_time = 0.0;
        uint32_t tempo_event_tick = 0;
        float tempo = 120.0 + bpmSpeed;

        for (MidiEvent* e : fTempoEvents) {
            float next_tempo_event_time =
                tempo_event_time +
                (((float)(e->tick() - tempo_event_tick)) / fResolution / (tempo / 60));
            if (next_tempo_event_time >= time) break;
            tempo_event_time = next_tempo_event_time;
            tempo_event_tick = e->tick();
            tempo = e->bpm() + bpmSpeed;

            if (_singleTempo)
                break;
        }

        return tempo_event_tick + (uint32_t)((time - tempo_event_time) * (tempo / 60) * fResolution);
    }
    case SMPTE24:
        return (uint32_t)(time * fResolution * 24.0);
    case SMPTE25:
        return (uint32_t)(time * fResolution * 25.0);
    case SMPTE30DROP:
        return (uint32_t)(time * fResolution * 29.97);
    case SMPTE30:
        return (uint32_t)(time * fResolution * 30.0);
    default:
        return 0;
    }
}

uint32_t MidiFile::tickFromTimeMs(long msTime, int bpmSpeed)
{
    switch (fDivision) {
    case PPQ: {
        float tempo_event_time = 0.0;
        uint32_t tempo_event_tick = 0;
        float tempo = 120.0 + bpmSpeed;

        for (MidiEvent* e : fTempoEvents) {
            float next_tempo_event_time =
                tempo_event_time +
                (((float)(e->tick() - tempo_event_tick)) / fResolution / (tempo / 60000));
            if (next_tempo_event_time >= msTime) break;
            tempo_event_time = next_tempo_event_time;
            tempo_event_tick = e->tick();
            tempo = e->bpm() + bpmSpeed;

            if (_singleTempo)
                break;
        }

        return tempo_event_tick + (uint32_t)((msTime - tempo_event_time) * (tempo / 60000) * fResolution);
    }
    case SMPTE24:
        return (uint32_t)(msTime / 1000.0 * fResolution * 24.0);
    case SMPTE25:
        return (uint32_t)(msTime / 1000.0 * fResolution * 25.0);
    case SMPTE30DROP:
        return (uint32_t)(msTime / 1000.0 * fResolution * 29.97);
    case SMPTE30:
        return (uint32_t)(msTime / 1000.0 * fResolution * 30.0);
    default:
        return 0;
    }
}

uint32_t MidiFile::tickFromBeat(float beat)
{
    switch (fDivision) {
    case PPQ:
        return (uint32_t)(beat * fResolution);
    case SMPTE24:
        return (uint32_t)(beat * 24.0);
    case SMPTE25:
        return (uint32_t)(beat * 25.0);
    case SMPTE30DROP:
        return (uint32_t)(beat * 29.97);
    case SMPTE30:
        return (uint32_t)(beat * 30);
    default:
        return -1;
    }
}

uint32_t MidiFile::tickFromBar(int barNumber)
{
    uint32_t result = 0, lastBar = 0, lastBeat = 0, lastBeatInBar = 4;   // no time signature yet = 4/4

    for (const SignatureBeat &sigBeat : MidiHelper::calculateBeats(this)) {
        uint32_t nBar = lastBar + ((sigBeat.nBeat - lastBeat) / sigBeat.nBeatInBar);
        if (nBar >= barNumber) {
            result = (barNumber - lastBar) * lastBeatInBar * fResolution;
            lastBar = barNumber;
            break;
        } else {
            lastBar = nBar;
            lastBeat = sigBeat.nBeat;
            lastBeatInBar = sigBeat.nBeatInBar;
        }
    }

    if (barNumber > lastBar)
        result = (barNumber - lastBar) * lastBeatInBar * fResolution;

    return result;
}

int MidiFile::barFromTick(uint32_t tick)
{
    if (tick == 0)
        return 0;

    int currentBeat = beatFromTick(tick);

    int lastBeat = 0;
    int lastBar = 0;
    int beatInbar = 4;
    for (const SignatureBeat &sigBeat : MidiHelper::calculateBeats(this)) {
        if (sigBeat.nBeat > currentBeat) {
            break;
        }
        lastBar += (sigBeat.nBeat - lastBeat) / beatInbar;
        lastBeat = sigBeat.nBeat;
        beatInbar = sigBeat.nBeatInBar;
    }

    return ((currentBeat - lastBeat) / beatInbar) + lastBar;;
}

int MidiFile::barCount()
{
    if (fEvents.isEmpty())
        return 0;
    int bCount = 0;
    int lastBeat = 0, lastBeatInBar = 4, tempBeatCount = 0;   // no time signature = 4/4 (0 divided by zero: crash)

    for (SignatureBeat sigBeat : MidiHelper::calculateBeats(this)) {
        lastBeat = sigBeat.nBeat;
        if (lastBeat > 0) {
            bCount += (lastBeat - tempBeatCount) / lastBeatInBar;
        }
        tempBeatCount = lastBeat;
        lastBeatInBar = sigBeat.nBeatInBar;
    }

    int beatCount = beatFromTick(fEvents.last()->tick());
    bCount += (beatCount - lastBeat) / lastBeatInBar;

    return bCount;
}

bool MidiFile::isSingleTempo()
{
    return _singleTempo;
}

void MidiFile::setSingleTempo(bool single)
{
    _singleTempo = single;
}

void MidiFile::insertEvent(int index, MidiEvent *event)
{
    fEvents.insert(index, event);
}

MidiEvent *MidiFile::takeEvent(int index)
{
    if (index < 0 || index >= fEvents.size())
        return nullptr;
    return fEvents.takeAt(index);
}

MidiEvent *MidiFile::takeEvent(MidiEvent *event)
{
    // indexOf(event, count - 1) only looked at the very last element: -1 (-> takeAt(-1), crash) for any other one
    const int i = fEvents.lastIndexOf(event);
    if (i < 0)
        return nullptr;
    return takeEvent(i);
}

int MidiFile::firstBpm(const QString &file)
{
    if (!QFile::exists(file))
        return 0;

    QFile in(file);

    int bpm = firstBpm(&in);
    return bpm;
}

int MidiFile::firstBpm(QFile *in)
{
    if (!in->exists() || !in->open(QFile::ReadOnly))
        return 0;

    uchar cId[4];
    in->read((char*)cId, 4);

    qint64 cSize = readUInt32(in);
    if (memcmp(cId, "MThd", 4) != 0 || cSize < 6) {     // same checks as read(): a longer header is allowed
        in->close();
        return 0;
    }
    const qint64 hdrEnd = in->pos() + cSize;

    int fType = readUInt16(in);
    int nTracks = readUInt16(in);

    if (fType == 2) {
        in->close();
        return 0;
    }

    unsigned char divResolution[2];
    in->read((char*)divResolution, 2);
    in->seek(hdrEnd);

    // at least one track; unknown chunks (Yamaha XF ...) are skipped as read() does
    int found = 0;
    for (int i=0; i<nTracks && found < nTracks; i++) {
        if (in->read((char*)cId, 4) != 4)
            break;
        cSize = readUInt32(in);
        if (memcmp(cId, "MTrk", 4) == 0)
            found++;                            // (a cut-off last track still counts: read() keeps what is there)
        else if (in->pos() + cSize > in->size() || (cId[0] == 0 && cSize == 0))
            break;
        else
            i--;
        if (in->pos() + cSize >= in->size())
            break;
        in->seek(in->pos() + cSize);
    }
    if (found == 0) {
        in->close();
        return 0;
    }

    // End check file

    in->seek(0);

    QByteArray buffer = in->readAll();
    // Set Tempo meta event: FF 51 03 tt tt tt (QByteArray::indexOf(0xFF51) searched for the single byte 0x51 ('Q'))
    int index = buffer.indexOf(QByteArray("\xFF\x51\x03", 3), 0);

    if (index == -1) {
        in->close();
        return 120;
    }
    else {
        in->seek(index + 1);
        char metaNumber;//, lenght;
        in->getChar(&metaNumber);
        //in->getChar(&lenght);
        int lenght = readVariableLengthQuantity(in);

        // wrong
        if (metaNumber != 0x51 || lenght != 3) {
            in->close();
            return 120;
        }

        unsigned char data[3];
        in->read((char*)data, 3);
        int32_t midi_tempo = (data[0] << 16) | (data[1] << 8) | data[2];

        in->close();

        if (midi_tempo <= 0)
            return 120;
        return 60000000 / midi_tempo;
    }
}


