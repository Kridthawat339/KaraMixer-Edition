#ifndef PROLICENSE_H
#define PROLICENSE_H

// KaraMixer PRO licence, tied to a USB flash drive (the "dongle"); older licences tied to the machine still work.
//   USB ID      : 8 hex digits made from the hardware serial number of the flash drive (see UsbDongle.h).
//   USB licence : HMAC-SHA256(secret, "KMPRO|USB|" + USB ID), shown as XXXX-XXXX-XXXX-XXXX. It unlocks PRO only
//                 while the drive it was made for is plugged in, so copying the key gives nothing.
//
// (previous scheme, still accepted while kAllowMachineLicense is true)
//   Hardware ID : 8 hex digits made from the machine's unique id (Windows MachineGuid) - shown to the user.
//   License     : XXXX-XXXX-XXXX-XXXX = first 8 bytes of HMAC-SHA256(secret, "KMPRO|" + hardware ID).
// The same licence works only on the machine it was made for. Licences are produced with kmlicense.py, which is
// NOT part of the source tree.
// The secret is NOT in the source code: the build script generates Widgets/km_secret.h from the KM_LIC_SECRET
// (GitHub Actions secret / environment variable), storing it scrambled (XOR with a per-build random key). Without
// that header the program builds as a free version only (no licence can be accepted).
// Machines listed in kOwnerHwids (the owner's own PCs) are always unlocked.
// Header-only, no Q_OBJECT.

#include <QString>
#include <QByteArray>
#include <QStringList>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QSysInfo>
#include <QSettings>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QClipboard>

#include "Config.h"
#include "UsbDongle.h"

// generated at build time (never committed): KM_SECRET_LEN, kmSecretEnc[], kmSecretKey[]
#if defined(__has_include)
#  if __has_include("km_secret.h")
#    include "km_secret.h"
#    define KM_HAVE_SECRET 1
#  endif
#endif

namespace ProLicense {

// licences made for a machine's Hardware ID (the old scheme) keep working; set to false for USB-only
constexpr bool kAllowMachineLicense = true;

// the secret, unscrambled only for the moment it is needed (never stored in clear in the program)
inline QByteArray secretBytes()
{
    QByteArray b;
#ifdef KM_HAVE_SECRET
    const int kl = int(sizeof(kmSecretKey));
    for (int i = 0; i < int(KM_SECRET_LEN); i++)
        b.append(char(kmSecretEnc[i] ^ kmSecretKey[i % kl] ^ static_cast<unsigned char>((i * 31 + 7) & 0xFF)));
#endif
    return b;
}

// hardware IDs of the owner's machines: never asked for a licence (e.g. "556BAE3D")
inline QStringList ownerHwids()
{
    return QStringList() /* << "XXXXXXXX" */;
}

inline QString hardwareId()
{
    QByteArray id = QSysInfo::machineUniqueId();
    if (id.isEmpty())
        id = (QSysInfo::machineHostName() + QSysInfo::currentCpuArchitecture()).toUtf8();
    const QByteArray h = QCryptographicHash::hash(QByteArray("KMHW|") + id, QCryptographicHash::Sha256);
    return QString::fromLatin1(h.left(4).toHex().toUpper());
}

inline QString normalize(const QString &s)
{
    QString r;
    for (const QChar &c : s)
        if (c.isLetterOrNumber())
            r += c.toUpper();
    return r;
}

inline QString licenseFor(const QString &hwid)
{
    const QByteArray passphrase = secretBytes();
    if (passphrase.isEmpty())
        return QString();                        // built without a secret: no licence exists
    const QByteArray secret = QCryptographicHash::hash(passphrase, QCryptographicHash::Sha256);
    const QByteArray mac = QMessageAuthenticationCode::hash(QByteArray("KMPRO|") + hwid.toUpper().toLatin1(), secret,
                                                            QCryptographicHash::Sha256);
    const QString hex = QString::fromLatin1(mac.left(8).toHex().toUpper());
    return hex.mid(0, 4) + QLatin1Char('-') + hex.mid(4, 4) + QLatin1Char('-') + hex.mid(8, 4) + QLatin1Char('-') + hex.mid(12, 4);
}

// licence for a USB drive (USB ID from UsbDongle)
inline QString licenseForUsb(const QString &usbId)
{
    const QByteArray passphrase = secretBytes();
    if (passphrase.isEmpty())
        return QString();
    const QByteArray secret = QCryptographicHash::hash(passphrase, QCryptographicHash::Sha256);
    const QByteArray mac = QMessageAuthenticationCode::hash(QByteArray("KMPRO|USB|") + usbId.toUpper().toLatin1(), secret,
                                                            QCryptographicHash::Sha256);
    const QString hex = QString::fromLatin1(mac.left(8).toHex().toUpper());
    return hex.mid(0, 4) + QLatin1Char('-') + hex.mid(4, 4) + QLatin1Char('-') + hex.mid(8, 4) + QLatin1Char('-') + hex.mid(12, 4);
}

inline bool isOwner()
{
    return ownerHwids().contains(hardwareId(), Qt::CaseInsensitive);
}

inline QString storedLicense()
{
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    return st.value("ProLicense").toString();
}

inline QString storedUsbLicense()
{
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    return st.value("ProLicenseUsb").toString();
}

// the USB ID (of a drive plugged in right now) this licence text belongs to, or empty
inline QString usbIdForLicense(const QString &text, bool force = false)
{
    const QString got = normalize(text);
    if (got.isEmpty())
        return QString();
    for (const QString &id : UsbDongle::connectedIds(force)) {
        const QString want = normalize(licenseForUsb(id));
        if (want.isEmpty() || got.size() != want.size())
            continue;
        int diff = 0;
        for (int i = 0; i < want.size(); i++)
            diff |= (want.at(i).unicode() ^ got.at(i).unicode());
        if (diff == 0)
            return id;
    }
    return QString();
}

inline bool isValidUsbLicense(const QString &text, bool force = false)
{
    return !usbIdForLicense(text, force).isEmpty();
}

inline bool isValidLicense(const QString &text)
{
    if (!kAllowMachineLicense)
        return false;
    const QString want = normalize(licenseFor(hardwareId()));
    const QString got = normalize(text);
    if (want.isEmpty() || got.size() != want.size())
        return false;
    int diff = 0;                                // compare without stopping at the first difference
    for (int i = 0; i < want.size(); i++)
        diff |= (want.at(i).unicode() ^ got.at(i).unicode());
    return diff == 0;
}

inline bool isUnlocked()
{
    return isOwner() || isValidLicense(storedLicense()) || isValidUsbLicense(storedUsbLicense());
}

// "PRO  XXXX-XXXX-XXXX-XXXX" / "PRO (owner)" / empty when not unlocked
inline QString statusText()
{
    if (isOwner())
        return QString::fromLatin1("PRO (owner)");
    if (isValidLicense(storedLicense()))
        return QString::fromLatin1("PRO  ") + licenseFor(hardwareId());
    if (isValidUsbLicense(storedUsbLicense()))
        return QString::fromLatin1("PRO  USB ") + storedUsbLicense();
    return QString();
}

inline bool activate(const QString &text)
{
    const bool machine = isValidLicense(text);
    const QString usb = machine ? QString() : usbIdForLicense(text, true);
    if (!machine && usb.isEmpty())
        return false;
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    if (machine)
        st.setValue("ProLicense", licenseFor(hardwareId()));
    else
        st.setValue("ProLicenseUsb", licenseForUsb(usb));
    st.sync();
    // tell every open window (e.g. the title bar badge) so it refreshes at once
    const QList<QWidget *> all = QApplication::allWidgets();
    for (QWidget *w : all) {
        QEvent ev(QEvent::Type(QEvent::User + 77));
        QApplication::sendEvent(w, &ev);
    }
    return true;
}

// ---- dialog: pick the USB flash drive, show its USB ID and ask for the licence ----
class Dialog : public QDialog
{
public:
    explicit Dialog(QWidget *parent) : QDialog(parent)
    {
        setWindowTitle(QString::fromLatin1("KaraMixer PRO"));
        setMinimumWidth(500);

        QVBoxLayout *v = new QVBoxLayout(this);
        v->setSpacing(10);

        QLabel *help = new QLabel(QString::fromUtf8(
            "ฟังก์ชันนี้เป็นของเวอร์ชัน PRO\n"
            "1) เสียบแฟลชไดร์ที่จะใช้เป็นกุญแจ  2) ส่ง USB ID ด้านล่างให้ผู้พัฒนาเพื่อขอ License  "
            "3) กรอก License แล้วกดปลดล็อก\nต้องเสียบแฟลชไดร์ตัวนี้ทุกครั้งที่เปิดโปรแกรม"), this);
        help->setWordWrap(true);
        v->addWidget(help);

        drivesBox = new QComboBox(this);
        QPushButton *refresh = new QPushButton(QString::fromUtf8("ค้นหาใหม่"), this);
        QHBoxLayout *dr = new QHBoxLayout();
        dr->addWidget(drivesBox, 1);
        dr->addWidget(refresh);
        v->addLayout(dr);

        idLabel = new QLabel(this);
        QFont mono(QString::fromLatin1("Consolas"));
        mono.setStyleHint(QFont::Monospace);
        mono.setPointSize(13);
        mono.setBold(true);
        idLabel->setFont(mono);
        idLabel->setStyleSheet(QString::fromLatin1("QLabel { color: #ffc83d; }"));
        idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        QPushButton *copy = new QPushButton(QString::fromUtf8("คัดลอก"), this);
        QHBoxLayout *hr = new QHBoxLayout();
        hr->addWidget(idLabel, 1);
        hr->addWidget(copy);
        v->addLayout(hr);

        edit = new QLineEdit(this);
        edit->setPlaceholderText(QString::fromLatin1("XXXX-XXXX-XXXX-XXXX"));
        edit->setMaxLength(24);
        edit->setFont(mono);
        edit->setMinimumHeight(34);
        v->addWidget(edit);

        status = new QLabel(this);
        status->setWordWrap(true);
        v->addWidget(status);

        QPushButton *ok = new QPushButton(QString::fromUtf8("ปลดล็อก"), this);
        QPushButton *close = new QPushButton(QString::fromUtf8("ปิด"), this);
        for (QPushButton *b : { refresh, copy, ok, close }) {   // a little bigger (size only)
            QFont bf = b->font();
            bf.setPointSizeF(bf.pointSizeF() > 0 ? bf.pointSizeF() + 1.0 : 10.0);
            b->setFont(bf);
            b->setMinimumHeight(38);
        }
        QHBoxLayout *br = new QHBoxLayout();
        br->addStretch(1);
        br->addWidget(ok);
        br->addWidget(close);
        v->addLayout(br);

        fill(true);
        connect(refresh, &QPushButton::clicked, this, [this]() { fill(true); });
        connect(drivesBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { showId(); });
        connect(copy, &QPushButton::clicked, this, [this]() { QApplication::clipboard()->setText(currentId()); });
        connect(close, &QPushButton::clicked, this, &QDialog::reject);
        auto tryIt = [this]() {
            if (activate(edit->text())) {
                status->setStyleSheet(QString::fromLatin1("QLabel { color: #5be36b; font-weight: bold; }"));
                status->setText(QString::fromUtf8("ปลดล็อกสำเร็จ: PRO (ผูกกับแฟลชไดร์)"));
                accept();
            } else {
                status->setStyleSheet(QString::fromLatin1("QLabel { color: #ff6b6b; }"));
                status->setText(UsbDongle::connectedIds().isEmpty()
                    ? QString::fromUtf8("ไม่พบแฟลชไดร์ USB - เสียบแฟลชไดร์ที่ใช้ขอ License แล้วกด \"ค้นหาใหม่\"")
                    : QString::fromUtf8("License นี้ไม่ตรงกับแฟลชไดร์ที่เสียบอยู่"));
            }
        };
        connect(ok, &QPushButton::clicked, this, tryIt);
        connect(edit, &QLineEdit::returnPressed, this, tryIt);
    }

private:
    QComboBox *drivesBox = nullptr;
    QLabel *idLabel = nullptr;
    QLineEdit *edit = nullptr;
    QLabel *status = nullptr;

    QString currentId() const { return drivesBox->currentData().toString(); }

    void showId()
    {
        const QString id = currentId();
        idLabel->setText(id.isEmpty() ? QString::fromUtf8("USB ID: -") : QString::fromLatin1("USB ID: ") + id);
    }

    void fill(bool force)
    {
        drivesBox->blockSignals(true);
        drivesBox->clear();
        const QList<UsbDongle::Drive> ds = UsbDongle::drives(force);
        for (const UsbDongle::Drive &d : ds)
            drivesBox->addItem(d.label(), d.id);
        drivesBox->blockSignals(false);
        if (ds.isEmpty()) {
            status->setStyleSheet(QString::fromLatin1("QLabel { color: #ff6b6b; }"));
            status->setText(QString::fromUtf8("ไม่พบแฟลชไดร์ USB - เสียบแฟลชไดร์แล้วกด \"ค้นหาใหม่\""));
        } else {
            status->setText(QString());
        }
        showId();
    }
};

// true when PRO is unlocked (asks for the licence first if it is not)
inline bool ensureUnlocked(QWidget *parent)
{
    if (isUnlocked())
        return true;
    Dialog dlg(parent);
    dlg.exec();
    return isUnlocked();
}

} // namespace ProLicense

#endif // PROLICENSE_H
