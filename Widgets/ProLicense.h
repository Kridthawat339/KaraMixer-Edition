#ifndef PROLICENSE_H
#define PROLICENSE_H

// KaraMixer PRO licence, tied to the machine (hardware ID).
//   Hardware ID : 8 hex digits made from the machine's unique id (Windows MachineGuid) - shown to the user.
//   License     : XXXX-XXXX-XXXX-XXXX = first 8 bytes of HMAC-SHA256(secret, "KMPRO|" + hardware ID).
// The same licence works only on the machine it was made for. Licences are produced with tools/kmlicense.py
// (same passphrase as kPassphrase below - CHANGE IT before you give the program to anybody, in both places).
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
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QClipboard>

#include "Config.h"

namespace ProLicense {

// secret passphrase (must be identical in tools/kmlicense.py)
static const char kPassphrase[] = "Buai1984";

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
    const QByteArray secret = QCryptographicHash::hash(QByteArray(kPassphrase), QCryptographicHash::Sha256);
    const QByteArray mac = QMessageAuthenticationCode::hash(QByteArray("KMPRO|") + hwid.toUpper().toLatin1(), secret,
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

inline bool isValidLicense(const QString &text)
{
    return normalize(text) == normalize(licenseFor(hardwareId()));
}

inline bool isUnlocked()
{
    return isOwner() || isValidLicense(storedLicense());
}

// "PRO  XXXX-XXXX-XXXX-XXXX" / "PRO (owner)" / empty when not unlocked
inline QString statusText()
{
    if (isOwner())
        return QString::fromLatin1("PRO (owner)");
    if (isValidLicense(storedLicense()))
        return QString::fromLatin1("PRO  ") + licenseFor(hardwareId());
    return QString();
}

inline bool activate(const QString &text)
{
    if (!isValidLicense(text))
        return false;
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    st.setValue("ProLicense", licenseFor(hardwareId()));
    st.sync();
    // tell every open window (e.g. the title bar badge) so it refreshes at once
    const QList<QWidget *> all = QApplication::allWidgets();
    for (QWidget *w : all) {
        QEvent ev(QEvent::Type(QEvent::User + 77));
        QApplication::sendEvent(w, &ev);
    }
    return true;
}

// ---- dialog: shows the hardware ID and asks for the licence ----
class Dialog : public QDialog
{
public:
    explicit Dialog(QWidget *parent) : QDialog(parent)
    {
        setWindowTitle(QString::fromLatin1("KaraMixer PRO"));
        setMinimumWidth(460);

        QVBoxLayout *v = new QVBoxLayout(this);
        v->setSpacing(10);

        QLabel *help = new QLabel(QString::fromUtf8(
            "ฟังก์ชันนี้เป็นของเวอร์ชัน PRO\nส่งเลข Hardware ID ด้านล่างให้ผู้พัฒนา เพื่อขอ License แล้วกรอกด้านล่าง"), this);
        help->setWordWrap(true);
        v->addWidget(help);

        QLabel *hw = new QLabel(QString::fromLatin1("Hardware ID: ") + hardwareId(), this);
        QFont mono(QString::fromLatin1("Consolas"));
        mono.setStyleHint(QFont::Monospace);
        mono.setPointSize(13);
        mono.setBold(true);
        hw->setFont(mono);
        hw->setStyleSheet(QString::fromLatin1("QLabel { color: #ffc83d; }"));
        hw->setTextInteractionFlags(Qt::TextSelectableByMouse);
        QPushButton *copy = new QPushButton(QString::fromUtf8("คัดลอก"), this);
        QHBoxLayout *hr = new QHBoxLayout();
        hr->addWidget(hw, 1);
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
        for (QPushButton *b : { copy, ok, close }) {   // a little bigger (size only)
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

        connect(copy, &QPushButton::clicked, this, [this]() { QApplication::clipboard()->setText(hardwareId()); });
        connect(close, &QPushButton::clicked, this, &QDialog::reject);
        auto tryIt = [this]() {
            if (activate(edit->text())) {
                status->setStyleSheet(QString::fromLatin1("QLabel { color: #5be36b; font-weight: bold; }"));
                status->setText(QString::fromUtf8("ปลดล็อกสำเร็จ: PRO"));
                accept();
            } else {
                status->setStyleSheet(QString::fromLatin1("QLabel { color: #ff6b6b; }"));
                status->setText(QString::fromUtf8("License ไม่ถูกต้องสำหรับเครื่องนี้"));
            }
        };
        connect(ok, &QPushButton::clicked, this, tryIt);
        connect(edit, &QLineEdit::returnPressed, this, tryIt);
    }

private:
    QLineEdit *edit = nullptr;
    QLabel *status = nullptr;
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
