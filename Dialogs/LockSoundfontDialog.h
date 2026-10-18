#ifndef LOCKSOUNDFONTDIALOG_H
#define LOCKSOUNDFONTDIALOG_H

// "Lock Soundfont": pick a soundfont (one that is already loaded, or any .sf2 on disk), type the Hardware ID of the
// computer it is for, press Lock and a protected copy (.kmsf, plays only in KaraMixer AND only on the computer with
// that Hardware ID) is saved in the Kmsf folder. The ID has to be typed again for every lock.
// Header-only, no Q_OBJECT (all connections are lambdas).

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QLineEdit>
#include <QClipboard>
#include <QApplication>
#include <QSettings>

#include "Config.h"
#include "Utils.h"
#include "Midi/MidiSynthesizer.h"
#include "Midi/KmSoundfont.h"

class LockSoundfontDialog : public QDialog
{
public:
    LockSoundfontDialog(QWidget *parent, MidiSynthesizer *synth) : QDialog(parent), synth(synth)
    {
        setWindowTitle(QString::fromUtf8("Lock Soundfont"));
        setMinimumWidth(520);

        QVBoxLayout *v = new QVBoxLayout(this);
        v->setSpacing(8);

        QLabel *help = new QLabel(QString::fromUtf8(
            "เลือก Soundfont ที่จะล็อก กรอก Hardware ID ของเครื่องที่จะใช้งาน แล้วกด Lock\n"
            "ระบบจะสร้างไฟล์ .kmsf (เล่นได้เฉพาะใน KaraMixer และเฉพาะเครื่องที่มี ID ตรงกัน แก้ไขไม่ได้) "
            "บันทึกในโฟลเดอร์ Kmsf (อยู่ข้างโฟลเดอร์ Config) ถ้าไปใช้เครื่องอื่นต้องล็อกใหม่ด้วย ID ของเครื่องนั้น"), this);
        help->setWordWrap(true);
        v->addWidget(help);

        list = new QListWidget(this);
        list->setMinimumHeight(160);
        v->addWidget(list, 1);

        // Hardware ID of this computer (what the PRO licence uses too) and the ID the file is locked for
        {
            QLabel *mine = new QLabel(QString::fromUtf8("Hardware ID ของเครื่องนี้: ") + KmSf::thisMachineId(), this);
            QFont mono(QString::fromLatin1("Consolas"));
            mono.setStyleHint(QFont::Monospace);
            mono.setPointSize(12);
            mono.setBold(true);
            mine->setFont(mono);
            mine->setTextInteractionFlags(Qt::TextSelectableByMouse);
            QPushButton *copy = new QPushButton(QString::fromUtf8("คัดลอก"), this);
            QHBoxLayout *r1 = new QHBoxLayout();
            r1->addWidget(mine, 1);
            r1->addWidget(copy);
            v->addLayout(r1);
            connect(copy, &QPushButton::clicked, this, []() { QApplication::clipboard()->setText(KmSf::thisMachineId()); });

            idEdit = new QLineEdit(this);
            idEdit->setPlaceholderText(QString::fromUtf8("กรอก Hardware ID ของเครื่องปลายทาง (8 ตัว เช่น 556BAE3D)"));
            idEdit->setMaxLength(12);
            idEdit->setFont(mono);
            idEdit->setMinimumHeight(34);
            QPushButton *useMine = new QPushButton(QString::fromUtf8("ใช้ ID เครื่องนี้"), this);
            QHBoxLayout *r2 = new QHBoxLayout();
            r2->addWidget(idEdit, 1);
            r2->addWidget(useMine);
            v->addLayout(r2);
            connect(useMine, &QPushButton::clicked, this, [this]() { idEdit->setText(KmSf::thisMachineId()); });
        }

        QHBoxLayout *h = new QHBoxLayout();
        QPushButton *browse = new QPushButton(QString::fromUtf8("เลือกไฟล์ .sf2 จากเครื่อง..."), this);
        lockBtn = new QPushButton(QString::fromUtf8("Lock"), this);
        QPushButton *close = new QPushButton(QString::fromUtf8("ปิด"), this);
        for (QPushButton *b : { browse, lockBtn, close }) {   // a little bigger (size only)
            QFont bf = b->font();
            bf.setPointSizeF(bf.pointSizeF() > 0 ? bf.pointSizeF() + 1.0 : 10.0);
            b->setFont(bf);
            b->setMinimumHeight(38);
        }
        h->addWidget(browse);
        h->addStretch(1);
        h->addWidget(lockBtn);
        h->addWidget(close);
        v->addLayout(h);

        status = new QLabel(this);
        status->setWordWrap(true);
        v->addWidget(status);

        refill();

        connect(browse, &QPushButton::clicked, this, [this]() {
            const QString f = QFileDialog::getOpenFileName(this, QString::fromUtf8("Soundfont"), Utils::LAST_OPEN_DIR,
                                                           QString::fromUtf8("SoundFont (*.sf2 *.SF2)"));
            if (f.isEmpty()) return;
            Utils::LAST_OPEN_DIR = QFileInfo(f).dir().absolutePath();
            QListWidgetItem *it = new QListWidgetItem(QFileInfo(f).fileName() + QString::fromUtf8("   (") + f + QString::fromUtf8(")"));
            it->setData(Qt::UserRole, f);
            list->addItem(it);
            list->setCurrentItem(it);
        });
        connect(close, &QPushButton::clicked, this, &QDialog::accept);
        connect(lockBtn, &QPushButton::clicked, this, [this]() { doLock(); });
    }

private:
    MidiSynthesizer *synth;
    QListWidget *list = nullptr;
    QPushButton *lockBtn = nullptr;
    QLabel *status = nullptr;
    QLineEdit *idEdit = nullptr;

    void refill()
    {
        list->clear();
        for (const QString &f : synth->soundfontFiles()) {
            if (KmSf::isProtectedFile(f))
                continue;   // already locked
            if (!f.endsWith(QLatin1String(".sf2"), Qt::CaseInsensitive))
                continue;   // only .sf2 can be locked
            QListWidgetItem *it = new QListWidgetItem(QFileInfo(f).fileName() + QString::fromUtf8("   (") + f + QString::fromUtf8(")"));
            it->setData(Qt::UserRole, f);
            list->addItem(it);
        }
    }

    void doLock()
    {
        QListWidgetItem *it = list->currentItem();
        if (!it) {
            status->setText(QString::fromUtf8("กรุณาเลือก Soundfont ก่อน"));
            return;
        }
        const QString src = it->data(Qt::UserRole).toString();

        // the Hardware ID of the computer the soundfont is for: typed in every time
        const QString id = KmSf::normalizeId(idEdit->text());
        if (id.isEmpty()) {
            status->setText(QString::fromUtf8("กรุณากรอก Hardware ID ของเครื่องปลายทางให้ถูกต้อง (8 ตัว 0-9, A-F)"));
            idEdit->setFocus();
            return;
        }

        // <data folder>/Kmsf, next to the Config and Data folders; the ID is part of the name
        const QString outDir = QFileInfo(Config::CONFIG_DIR_PATH).dir().absoluteFilePath(QString::fromUtf8("Kmsf"));
        QDir().mkpath(outDir);
        const QString dst = QDir(outDir).absoluteFilePath(QFileInfo(src).completeBaseName() + QString::fromUtf8("-") + id
                                                          + QString::fromUtf8(".kmsf"));
        if (QFileInfo::exists(dst)
            && QMessageBox::question(this, QString::fromUtf8("Lock Soundfont"),
                   QFileInfo(dst).fileName() + QString::fromUtf8(" มีอยู่แล้ว ต้องการเขียนทับหรือไม่?")) != QMessageBox::Yes)
            return;

        lockBtn->setEnabled(false);
        status->setText(QString::fromUtf8("กำลังล็อก ..."));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        qApp->processEvents();
        QString err;
        const bool ok = KmSf::protectFile(src, dst, id, &err);
        QApplication::restoreOverrideCursor();
        lockBtn->setEnabled(true);

        if (!ok) {
            status->setText(QString::fromUtf8("ล็อกไม่สำเร็จ: ") + err);
            return;
        }

        idEdit->clear();     // the ID has to be typed again for the next lock

        if (id != KmSf::thisMachineId()) {
            // made for another computer: it cannot play here, so it is not added to this program's list
            status->setText(QString::fromUtf8("ล็อกแล้วสำหรับเครื่อง ID ") + id + QString::fromUtf8(" (ใช้ได้เฉพาะเครื่องนั้น): ")
                            + QDir::toNativeSeparators(dst));
            return;
        }

        // add the locked copy to the soundfont list (once)
        bool known = false;
        for (const QString &f : synth->soundfontFiles())
            if (QFileInfo(f) == QFileInfo(dst)) { known = true; break; }
        if (!known && synth->addSoundfont(dst)) {
            QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
            st.setValue("SynthSoundfonts", QVariant::fromValue(synth->soundfontFiles()));
        }
        status->setText(QString::fromUtf8("ล็อกแล้วสำหรับเครื่อง ID ") + id + QString::fromUtf8(": ") + QDir::toNativeSeparators(dst));
    }
};

#endif // LOCKSOUNDFONTDIALOG_H
