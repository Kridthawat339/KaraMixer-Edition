#ifndef USBDONGLE_H
#define USBDONGLE_H

// USB "dongle" for the PRO licence: an ordinary USB flash drive is recognised by the hardware serial number its
// controller reports (not the volume serial, which changes when the drive is formatted). The licence key is made
// for that serial, so a copy of the key does not work with another drive or without the drive plugged in.
// Windows only; elsewhere no drive is ever reported. Header-only, no Q_OBJECT.

#include <QString>
#include <QStringList>
#include <QList>
#include <QByteArray>
#include <QCryptographicHash>
#include <QElapsedTimer>

#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <winioctl.h>
#endif

namespace UsbDongle {

struct Drive {
    QString letter;     // "E:"
    QString vendor;
    QString product;
    QString serial;     // hardware serial of the USB device (cleaned, upper case)
    QString id;         // 8 hex digits made from the serial; empty when the drive reports no serial
    QString label() const
    {
        QString n = (vendor + QLatin1Char(' ') + product).simplified();
        if (n.isEmpty())
            n = QStringLiteral("USB drive");
        return letter + QStringLiteral("  ") + n + (id.isEmpty() ? QStringLiteral("  (no serial number)")
                                                                : QStringLiteral("  -  ") + id);
    }
};

inline QString idFromSerial(const QString &serial)
{
    if (serial.isEmpty())
        return QString();
    const QByteArray h = QCryptographicHash::hash(QByteArray("KMUSB|") + serial.toLatin1(), QCryptographicHash::Sha256);
    return QString::fromLatin1(h.left(4).toHex().toUpper());
}

#ifdef _WIN32
// every USB-connected drive letter with its hardware serial
inline QList<Drive> scan()
{
    QList<Drive> out;
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i)))
            continue;
        const wchar_t root[4] = { wchar_t(L'A' + i), L':', L'\\', 0 };
        const UINT type = GetDriveTypeW(root);
        if (type != DRIVE_REMOVABLE && type != DRIVE_FIXED)
            continue;
        const wchar_t path[7] = { L'\\', L'\\', L'.', L'\\', wchar_t(L'A' + i), L':', 0 };
        HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        STORAGE_PROPERTY_QUERY q;
        ZeroMemory(&q, sizeof(q));
        q.PropertyId = StorageDeviceProperty;
        q.QueryType = PropertyStandardQuery;
        alignas(8) char buf[1024];
        DWORD got = 0;
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf, sizeof(buf), &got, nullptr)
            && got >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
            const STORAGE_DEVICE_DESCRIPTOR *d = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR *>(buf);
            if (d->BusType == BusTypeUsb) {
                auto str = [&](DWORD off) -> QString {
                    if (off == 0 || off >= sizeof(buf))
                        return QString();
                    return QString::fromLatin1(buf + off).trimmed();
                };
                Drive dr;
                dr.letter = QString(QChar(L'A' + i)) + QLatin1Char(':');
                dr.vendor = str(d->VendorIdOffset);
                dr.product = str(d->ProductIdOffset);
                QString ser;
                for (const QChar &c : str(d->SerialNumberOffset))
                    if (c.isLetterOrNumber())
                        ser += c.toUpper();
                dr.serial = ser;
                dr.id = idFromSerial(ser);
                out.append(dr);
            }
        }
        CloseHandle(h);
    }
    return out;
}
#else
inline QList<Drive> scan() { return QList<Drive>(); }
#endif

// drives, re-read at most every 2 seconds (the licence is asked for often)
inline QList<Drive> drives(bool force = false)
{
    static QList<Drive> cache;
    static QElapsedTimer age;
    if (force || !age.isValid() || age.elapsed() > 2000) {
        cache = scan();
        age.restart();
    }
    return cache;
}

inline QStringList connectedIds(bool force = false)
{
    QStringList ids;
    for (const Drive &d : drives(force))
        if (!d.id.isEmpty() && !ids.contains(d.id))
            ids << d.id;
    return ids;
}

} // namespace UsbDongle

#endif // USBDONGLE_H
