#ifndef DEVICEPICKER_H
#define DEVICEPICKER_H

// Finds "the same" device on another computer.
//
// The config files store devices by name (MIDI-in, external MIDI out) or by number (sound card, ASIO driver).
// A config made on one computer and opened on another often names a device that does not exist there: the
// karaoke program on computer A sends through "CopperLan ..." while computer B has "loopMIDI Port", or the
// sound card that was number 2 on A is another card (or nothing) on B. Before, the mixer then simply stayed
// unconnected. These helpers pick the device this computer really has:
//
//  - exact name, then the same name written a little differently (Windows adds "2- " in front or " 1" behind
//    when a device is plugged in twice / into another USB port);
//  - the device this computer used the last time (remembered in a small file that is NOT part of the exported
//    .kmcfg, keyed by the computer's id, so copying the Config folder does not carry it over either);
//  - for MIDI-in only: a virtual MIDI cable (loopMIDI, CopperLan, LoopBe, MIDI Yoke ...), because that is what
//    the karaoke program sends through.
//
// The name in the config is never overwritten automatically: when that device shows up, it wins again.

#include <QString>
#include <QStringList>
#include <QSettings>
#include <QSysInfo>
#include <QRegularExpression>
#include <QCryptographicHash>

#include "Config.h"

namespace DevicePicker {

// "2- loopMIDI Port 1" -> "loopmidi port"
inline QString normalized(const QString &name)
{
    QString n = name.trimmed().toLower();
    n.remove(QRegularExpression("^\\d+\\s*-\\s*"));       // "2- USB MIDI"  (Windows: second device of that name)
    n.remove(QRegularExpression("\\s*[\\(\\[]?\\d+[\\)\\]]?$"));   // "loopMIDI Port 1", "Device (2)"
    n.replace(QRegularExpression("\\s+"), " ");
    return n.trimmed();
}

// index of `name` in `list`: exact first, then written a little differently; -1 = not there
inline int indexOfLoose(const QStringList &list, const QString &name)
{
    if (name.isEmpty())
        return -1;
    int i = list.indexOf(name);
    if (i >= 0)
        return i;
    for (i = 0; i < list.count(); i++)
        if (list[i].compare(name, Qt::CaseInsensitive) == 0)
            return i;
    const QString n = normalized(name);
    if (n.isEmpty())
        return -1;
    for (i = 0; i < list.count(); i++)
        if (normalized(list[i]) == n)
            return i;
    return -1;
}

// the virtual MIDI cables a karaoke program sends through
inline bool isVirtualCable(const QString &name)
{
    static const char *keys[] = {
        "loopmidi", "copperlan", "loopbe", "virtual", "yoke", "rtpmidi", "network",
        "bome", "springbeats", "loopback", "midi through", "iac", "vmidi", "tevirtualmidi", "karamixer",
        "midi-ox", "maple", "hubi"
    };
    const QString n = name.toLower();
    for (const char *k : keys)
        if (n.contains(QLatin1String(k)))
            return true;
    return false;
}

// ---- what THIS computer uses (not exported with the config)
inline QString machineKey()
{
    QByteArray id = QSysInfo::machineUniqueId();
    if (id.isEmpty())
        id = QSysInfo::machineHostName().toUtf8();
    return "PC-" + QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex().left(12));
}

inline QString localFile()
{
    return Config::CONFIG_DIR_PATH + "/ThisComputer.conf";
}

inline QString localValue(const QString &key)
{
    QSettings s(localFile(), QSettings::IniFormat);
    return s.value(machineKey() + "/" + key).toString();
}

inline void setLocalValue(const QString &key, const QString &value)
{
    QSettings s(localFile(), QSettings::IniFormat);
    if (s.value(machineKey() + "/" + key).toString() != value)
        s.setValue(machineKey() + "/" + key, value);
}

// MIDI-in to open on this computer for the name in the config.
//   saved     - "MidiInName" from the config ("" = the user chose None: nothing is opened)
//   available - the MIDI-in devices of this computer
//   mayGuess  - allow taking a virtual cable when nothing better is known (the caller holds this back for a few
//               seconds after the start, so a cable that starts together with the karaoke program can appear first)
// returns the device name to open, or "" (wait / nothing).
inline QString pickMidiIn(const QString &saved, const QStringList &available, bool mayGuess)
{
    if (saved.isEmpty() || available.isEmpty())
        return QString();

    int i = indexOfLoose(available, saved);
    if (i >= 0)
        return available[i];

    // the config names a device this computer does not have (now): the device this computer used last time
    // (config from another computer, or a cable this computer already replaced once)
    const QString local = localValue("MidiInName");
    if (!local.isEmpty() && indexOfLoose(QStringList{ local }, saved) < 0) {
        i = indexOfLoose(available, local);
        if (i >= 0)
            return available[i];
    }

    if (!mayGuess)
        return QString();

    // Nothing known: take a virtual cable, one of the same kind first ("loopMIDI Port 2" for "loopMIDI Port").
    // This also covers this computer's own cable when it was removed / renamed: before, the program waited for
    // it forever and the mixer stayed unconnected. If the config's device comes back it wins again (step 1).
    const QString fam = normalized(saved).section(' ', 0, 0);
    QStringList cables;
    for (const QString &d : available)
        if (isVirtualCable(d))
            cables << d;
    for (const QString &d : cables)
        if (!fam.isEmpty() && normalized(d).startsWith(fam))
            return d;
    if (!cables.isEmpty())
        return cables.first();
    if (available.count() == 1)
        return available.first();               // the only MIDI-in there is (a cable with an unknown name)
    return QString();
}

// ---- sound card / ASIO driver: the config stores a number, the name is stored next to it
inline void saveAudioDevice(QSettings *settings, int index, const QStringList &devices)
{
    settings->setValue("SynthDefaultDevice", index);
    settings->setValue("SynthDefaultDeviceName", (index >= 0 && index < devices.count()) ? devices[index] : QString());
}

inline void saveAsioDevice(QSettings *settings, int index, const QStringList &drivers)
{
    settings->setValue("AsioDevice", index);
    if (index >= 0 && index < drivers.count())
        settings->setValue("AsioDeviceName", drivers[index]);
    else
        settings->remove("AsioDeviceName");
}

// number of the stored device on this computer.
//   `index`/`name` from the config, `fallback` when the named device is not here.
//   `*learnt` = true when the device was found under another number (the caller stores the new number).
inline int resolveByName(int index, const QString &name, const QStringList &devices, int fallback, bool *learnt)
{
    if (learnt) *learnt = false;
    if (name.isEmpty())
        return index;                           // config from before names were stored: keep the number
    if (index >= 0 && index < devices.count() && devices[index] == name)
        return index;
    const int i = indexOfLoose(devices, name);
    if (i >= 0) {
        if (learnt) *learnt = (i != index);
        return i;
    }
    return fallback;
}

// sound card of one mixer strip / bus ("Device" in SynthMixer.conf, 0 = the main output, 1.. = another card).
// A card this computer does not have: the strip plays on the main output (the config keeps its choice).
inline int resolveStripDevice(int device, const QString &name, const QStringList &devices)
{
    if (device <= 0)
        return device;
    return resolveByName(device, name, devices, 0, nullptr);
}

inline QString stripDeviceName(int device, const QStringList &devices)
{
    return (device >= 1 && device < devices.count()) ? devices[device] : QString();
}

} // namespace DevicePicker

#endif // DEVICEPICKER_H
