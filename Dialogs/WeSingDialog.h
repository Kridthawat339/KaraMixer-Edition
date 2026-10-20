#ifndef WESINGDIALOG_H
#define WESINGDIALOG_H

// PRO: bring the sound of a karaoke app (WeSing running in an Android emulator such as BlueStacks) into the mixer.
//   * the app / emulator sends its sound to a virtual audio cable (e.g. VB-Cable, "CABLE Input")
//   * KaraMixer records "CABLE Output" and feeds it into the main mixer, so it follows the master volume,
//     limiter, mastering, recording and the ASIO / Windows Audio output of the program
// Header-only, no Q_OBJECT (same style as RecordDialog.h / MasteringDialog.h).

#ifndef __linux__

#include <QDialog>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QProgressBar>
#include <QPushButton>
#include <QLineEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QApplication>
#include <QList>
#include <atomic>
#include <cmath>

#include <bass.h>
#include <bassmix.h>

#include "Config.h"
#include "Midi/MidiSynthesizer.h"
#include "Widgets/ProLicense.h"

// ------------------------------------------------------------------------------------------------
// The audio side: records a Windows capture device and pushes it into the main mixer.
// ------------------------------------------------------------------------------------------------
class WeSingBridge
{
public:
    struct Dev { int index; QString name; };

    static WeSingBridge &get() { static WeSingBridge b; return b; }

    QList<Dev> devices() const
    {
        QList<Dev> out;
        BASS_DEVICEINFO di;
        for (int i = 0; BASS_RecordGetDeviceInfo(i, &di); i++)
            if (di.flags & BASS_DEVICE_ENABLED)
                out.append({ i, QString::fromUtf8(di.name) });
        return out;
    }

    bool active() const { return push.load() != 0; }
    QString deviceName() const { return devName; }

    bool start(int dev, const QString &name, MidiSynthesizer *s, QString &err)
    {
        stop();
        synth = s;

        if (!BASS_RecordInit(dev) && BASS_ErrorGetCode() != BASS_ERROR_ALREADY) {
            err = QStringLiteral("BASS_RecordInit error %1").arg(BASS_ErrorGetCode());
            return false;
        }
        recDevice = dev;
        BASS_RecordSetDevice((DWORD)dev);

        DWORD p = BASS_StreamCreate(kRate, 2, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT, STREAMPROC_PUSH, nullptr);
        if (!p) {
            err = QStringLiteral("BASS_StreamCreate error %1").arg(BASS_ErrorGetCode());
            BASS_RecordFree();
            return false;
        }
        applyVolume(p);
        peak = 0.0f;
        push = p;

        // 10 ms capture period keeps the extra delay small
        rec = BASS_RecordStart(kRate, 2, MAKELONG(BASS_SAMPLE_FLOAT, 10), &WeSingBridge::recProc, this);
        if (!rec) {
            err = QStringLiteral("BASS_RecordStart error %1").arg(BASS_ErrorGetCode());
            push = 0;
            BASS_StreamFree(p);
            BASS_RecordFree();
            return false;
        }
        devName = name;

        attach();
        if (!timer) {
            timer = new QTimer(qApp);
            QObject::connect(timer, &QTimer::timeout, qApp, []() { WeSingBridge::get().attach(); });
        }
        timer->start(250);      // re-attach after the synth was re-opened (output change)
        return true;
    }

    void stop()
    {
        if (timer) timer->stop();
        const DWORD p = push.exchange(0);
        const DWORD r = rec;
        rec = 0;
        if (r) BASS_ChannelStop(r);
        if (p) {
            BASS_Mixer_ChannelRemove(p);
            BASS_StreamFree(p);
        }
        if (r || p) {
            BASS_RecordSetDevice((DWORD)recDevice);
            BASS_RecordFree();
        }
        peak = 0.0f;
    }

    void setVolume(int percent) { vol = percent; applyVolume(push.load()); }
    void setMuted(bool m) { muted = m; applyVolume(push.load()); }
    int volume() const { return vol; }
    bool isMuted() const { return muted; }

    float takePeak() { const float v = peak; peak = 0.0f; return v; }

    // start again at program start, when the user asked for it (and PRO is unlocked)
    static void autoStart(MidiSynthesizer *s)
    {
        if (!ProLicense::isUnlocked())
            return;
        QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        if (!st.value("WeSingAuto", false).toBool())
            return;
        const QString want = st.value("WeSingInput", "").toString();
        if (want.isEmpty())
            return;
        WeSingBridge &b = get();
        b.vol = st.value("WeSingVolume", 100).toInt();
        b.muted = st.value("WeSingMute", false).toBool();
        for (const Dev &d : b.devices())
            if (d.name == want) {
                QString err;
                b.start(d.index, d.name, s, err);
                return;
            }
    }

private:
    static constexpr DWORD kRate = 48000;

    static BOOL CALLBACK recProc(HRECORD, const void *buffer, DWORD length, void *user)
    {
        WeSingBridge *b = static_cast<WeSingBridge *>(user);
        const DWORD p = b->push.load();
        if (!p || !length)
            return TRUE;

        // peak meter
        const float *f = static_cast<const float *>(buffer);
        const DWORD n = length / sizeof(float);
        float m = 0.0f;
        for (DWORD i = 0; i < n; i++) {
            const float a = f[i] < 0 ? -f[i] : f[i];
            if (a > m) m = a;
        }
        if (m > b->peak) b->peak = m;

        // the capture clock and the output clock drift apart: never let more than ~120 ms pile up
        const DWORD keepBytes = kRate / 1000 * 120 * 2 * sizeof(float);
        const DWORD avail = BASS_ChannelGetData(p, nullptr, BASS_DATA_AVAILABLE);
        if (avail != (DWORD)-1 && avail > keepBytes) {
            char tmp[8192];
            DWORD drop = avail - keepBytes / 2;
            while (drop > 0) {
                const DWORD chunk = drop > sizeof(tmp) ? (DWORD)sizeof(tmp) : drop;
                const DWORD got = BASS_ChannelGetData(p, tmp, chunk);
                if (got == (DWORD)-1 || got == 0) break;
                drop -= got;
            }
        }
        BASS_StreamPutData(p, buffer, length);
        return TRUE;
    }

    void applyVolume(DWORD p)
    {
        if (p)
            BASS_ChannelSetAttribute(p, BASS_ATTRIB_VOL, muted ? 0.0f : vol / 100.0f);
    }

    void attach()
    {
        const DWORD p = push.load();
        if (!p || !synth)
            return;
        const DWORD m = synth->mainMixerHandle();
        if (!m)
            return;
        if (BASS_Mixer_ChannelGetMixer(p) == m)
            return;
        BASS_Mixer_ChannelRemove(p);
        BASS_Mixer_StreamAddChannel(m, p, BASS_MIXER_CHAN_NORAMPIN);
    }

    std::atomic<DWORD> push{0};
    DWORD rec = 0;
    int recDevice = -1;
    MidiSynthesizer *synth = nullptr;
    QTimer *timer = nullptr;
    QString devName;
    int vol = 100;
    bool muted = false;
    std::atomic<float> peak{0.0f};
};

// ------------------------------------------------------------------------------------------------
// The window: choose the input, start / stop, volume, open the emulator / WeSing.
// ------------------------------------------------------------------------------------------------
class WeSingDialog : public QDialog
{
public:
    WeSingDialog(QWidget *parent, MidiSynthesizer *synth) : QDialog(parent), synth(synth)
    {
        setWindowTitle(QString::fromUtf8("WeSing — ร้องคาราโอเกะผ่านมิกเซอร์"));
        setMinimumWidth(620);
        setAttribute(Qt::WA_DeleteOnClose);

        QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        WeSingBridge &br = WeSingBridge::get();
        if (!br.active()) {       // while it is running the bridge keeps its own values
            br.setVolume(st.value("WeSingVolume", 100).toInt());
            br.setMuted(st.value("WeSingMute", false).toBool());
        }

        QVBoxLayout *v = new QVBoxLayout(this);
        v->setSpacing(8);

        QLabel *help = new QLabel(QString::fromUtf8(
            "<b>วิธีใช้</b><br>"
            "1) ติดตั้งสายเสียงเสมือน (เช่น VB-Cable) และโปรแกรมจำลอง Android (เช่น BlueStacks) แล้วติดตั้งแอป WeSing ในโปรแกรมจำลอง<br>"
            "2) ตั้งให้เสียงของโปรแกรมจำลองออกที่ <b>CABLE Input</b> (กดปุ่ม \"ตั้งค่าเสียงของ Windows\" ด้านล่าง)<br>"
            "3) เลือกอินพุต <b>CABLE Output</b> ในช่องด้านล่าง แล้วกด \"เริ่มรับเสียง\"<br>"
            "4) กด \"เปิด WeSing\" แล้วร้องได้เลย เสียงของแอปจะผ่านมิกเซอร์ของ KaraMixer"), this);
        help->setWordWrap(true);
        v->addWidget(help);

        // ---- input
        QGroupBox *gi = new QGroupBox(QString::fromUtf8("รับเสียงจากแอปเข้ามิกเซอร์"), this);
        QGridLayout *g = new QGridLayout(gi);

        g->addWidget(new QLabel(QString::fromUtf8("อินพุต :"), gi), 0, 0);
        cbInput = new QComboBox(gi);
        g->addWidget(cbInput, 0, 1);
        QPushButton *btnRefresh = new QPushButton(QString::fromUtf8("โหลดรายการใหม่"), gi);
        g->addWidget(btnRefresh, 0, 2);

        btnStart = new QPushButton(gi);
        g->addWidget(btnStart, 1, 1);
        status = new QLabel(gi);
        status->setWordWrap(true);
        g->addWidget(status, 1, 2, 1, 1);

        g->addWidget(new QLabel(QString::fromUtf8("ระดับเสียง :"), gi), 2, 0);
        QHBoxLayout *hv = new QHBoxLayout();
        sl = new QSlider(Qt::Horizontal, gi);
        sl->setRange(0, 150);
        sl->setValue(br.volume());
        lbVol = new QLabel(gi);
        lbVol->setMinimumWidth(48);
        hv->addWidget(sl, 1);
        hv->addWidget(lbVol);
        g->addLayout(hv, 2, 1);
        chMute = new QCheckBox(QString::fromUtf8("ปิดเสียง"), gi);
        chMute->setChecked(br.isMuted());
        g->addWidget(chMute, 2, 2);

        g->addWidget(new QLabel(QString::fromUtf8("สัญญาณ :"), gi), 3, 0);
        meter = new QProgressBar(gi);
        meter->setRange(0, 100);
        meter->setTextVisible(false);
        meter->setFixedHeight(12);
        g->addWidget(meter, 3, 1, 1, 2);

        chAuto = new QCheckBox(QString::fromUtf8("เริ่มรับเสียงอัตโนมัติเมื่อเปิดโปรแกรม"), gi);
        chAuto->setChecked(st.value("WeSingAuto", false).toBool());
        g->addWidget(chAuto, 4, 1, 1, 2);
        v->addWidget(gi);

        // ---- emulator / app
        QGroupBox *ge = new QGroupBox(QString::fromUtf8("เปิดโปรแกรมจำลอง / แอป WeSing"), this);
        QGridLayout *e = new QGridLayout(ge);
        e->addWidget(new QLabel(QString::fromUtf8("โปรแกรมจำลอง :"), ge), 0, 0);
        edExe = new QLineEdit(st.value("WeSingEmuPath", "").toString(), ge);
        e->addWidget(edExe, 0, 1);
        QPushButton *btnBrowse = new QPushButton(QString::fromUtf8("เลือกไฟล์..."), ge);
        e->addWidget(btnBrowse, 0, 2);
        e->addWidget(new QLabel(QString::fromUtf8("คำสั่งเพิ่มเติม :"), ge), 1, 0);
        edArgs = new QLineEdit(st.value("WeSingEmuArgs", "--cmd launchApp --package com.tencent.wesing").toString(), ge);
        edArgs->setToolTip(QString::fromUtf8("ใช้เปิดแอป WeSing อัตโนมัติ (BlueStacks) ถ้าเปิดแอปไม่ขึ้น ให้ลบช่องนี้ให้ว่าง แล้วเปิดแอป WeSing เองในโปรแกรมจำลอง"));
        e->addWidget(edArgs, 1, 1, 1, 2);

        QHBoxLayout *hb = new QHBoxLayout();
        QPushButton *btnOpen = new QPushButton(QString::fromUtf8("เปิด WeSing"), ge);
        btnOpen->setMinimumHeight(34);
        QPushButton *btnSound = new QPushButton(QString::fromUtf8("ตั้งค่าเสียงของ Windows"), ge);
        QPushButton *btnWeb = new QPushButton(QString::fromUtf8("หน้าดาวน์โหลด BlueStacks / VB-Cable"), ge);
        hb->addWidget(btnOpen, 1);
        hb->addWidget(btnSound);
        hb->addWidget(btnWeb);
        e->addLayout(hb, 2, 0, 1, 3);
        v->addWidget(ge);

        QLabel *note = new QLabel(QString::fromUtf8(
            "หมายเหตุ: อย่าตั้งเอาต์พุตของ KaraMixer เป็น CABLE Input (เสียงจะวนกลับเข้ามาเอง) "
            "เสียงจากแอปจะมีความหน่วงเพิ่มเล็กน้อยจากการรับเสียงผ่านสายเสมือน"), this);
        note->setWordWrap(true);
        v->addWidget(note);

        QPushButton *btnClose = new QPushButton(QString::fromUtf8("ปิด"), this);
        QHBoxLayout *hc = new QHBoxLayout();
        hc->addStretch(1);
        hc->addWidget(btnClose);
        v->addLayout(hc);

        if (edExe->text().isEmpty())
            edExe->setText(findBlueStacks());

        fillInputs(st.value("WeSingInput", "").toString());
        refreshState();

        // ---- actions
        connect(btnRefresh, &QPushButton::clicked, this, [this]() { fillInputs(cbInput->currentText()); });
        connect(btnStart, &QPushButton::clicked, this, [this]() { toggle(); });
        connect(sl, &QSlider::valueChanged, this, [this](int val) {
            WeSingBridge::get().setVolume(val);
            QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
            s.setValue("WeSingVolume", val);
            lbVol->setText(QString::number(val) + QStringLiteral(" %"));
        });
        connect(chMute, &QCheckBox::toggled, this, [this](bool on) {
            WeSingBridge::get().setMuted(on);
            QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
            s.setValue("WeSingMute", on);
        });
        connect(chAuto, &QCheckBox::toggled, this, [this](bool on) {
            QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
            s.setValue("WeSingAuto", on);
            s.setValue("WeSingInput", cbInput->currentText());
        });
        connect(btnBrowse, &QPushButton::clicked, this, [this]() {
            const QString f = QFileDialog::getOpenFileName(this, QString::fromUtf8("เลือกโปรแกรมจำลอง (เช่น HD-Player.exe)"),
                                                           edExe->text(), QStringLiteral("Program (*.exe)"));
            if (!f.isEmpty())
                edExe->setText(f);
        });
        connect(btnOpen, &QPushButton::clicked, this, [this]() { openApp(); });
        connect(btnSound, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:apps-volume")));
        });
        connect(btnWeb, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.bluestacks.com/download.html")));
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://vb-audio.com/Cable/")));
        });
        connect(btnClose, &QPushButton::clicked, this, &QDialog::close);

        tm = new QTimer(this);
        connect(tm, &QTimer::timeout, this, [this]() {
            const float p = WeSingBridge::get().takePeak();
            int pct = 0;
            if (p > 0.001f)       // -60 dB .. 0 dB
                pct = qBound(0, int((20.0 * std::log10(double(p)) + 60.0) / 60.0 * 100.0 + 0.5), 100);
            meter->setValue(pct);
        });
        tm->start(60);
        lbVol->setText(QString::number(sl->value()) + QStringLiteral(" %"));
    }

    ~WeSingDialog() override
    {
        QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        s.setValue("WeSingEmuPath", edExe->text());
        s.setValue("WeSingEmuArgs", edArgs->text());
        if (!cbInput->currentText().isEmpty())
            s.setValue("WeSingInput", cbInput->currentText());
    }

private:
    void fillInputs(const QString &prefer)
    {
        cbInput->clear();
        int pick = -1;
        const QList<WeSingBridge::Dev> list = WeSingBridge::get().devices();
        for (int i = 0; i < list.count(); i++) {
            cbInput->addItem(list[i].name, list[i].index);
            if (pick < 0 && !prefer.isEmpty() && list[i].name == prefer)
                pick = i;
        }
        if (pick < 0)       // the virtual cable output is the usual choice
            for (int i = 0; i < list.count(); i++)
                if (list[i].name.contains(QStringLiteral("CABLE"), Qt::CaseInsensitive)) { pick = i; break; }
        if (pick >= 0)
            cbInput->setCurrentIndex(pick);
    }

    void refreshState()
    {
        const bool on = WeSingBridge::get().active();
        btnStart->setText(on ? QString::fromUtf8("หยุดรับเสียง") : QString::fromUtf8("เริ่มรับเสียง"));
        cbInput->setEnabled(!on);
        status->setText(on ? QString::fromUtf8("กำลังรับเสียงจาก: ") + WeSingBridge::get().deviceName()
                           : QString::fromUtf8("ยังไม่ได้รับเสียง"));
    }

    void toggle()
    {
        WeSingBridge &b = WeSingBridge::get();
        if (b.active()) {
            b.stop();
            refreshState();
            return;
        }
        if (cbInput->count() == 0) {
            QMessageBox::information(this, QStringLiteral("WeSing"),
                QString::fromUtf8("ไม่พบอุปกรณ์อินพุต: ติดตั้ง VB-Cable (สายเสียงเสมือน) แล้วกด \"โหลดรายการใหม่\""));
            return;
        }
        b.setVolume(sl->value());
        b.setMuted(chMute->isChecked());
        QString err;
        if (!b.start(cbInput->currentData().toInt(), cbInput->currentText(), synth, err)) {
            QMessageBox::warning(this, QStringLiteral("WeSing"),
                QString::fromUtf8("เริ่มรับเสียงไม่สำเร็จ") + QStringLiteral("\n") + err);
        } else {
            QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
            s.setValue("WeSingInput", cbInput->currentText());
        }
        refreshState();
    }

    void openApp()
    {
        const QString exe = edExe->text().trimmed();
        if (exe.isEmpty() || !QFileInfo::exists(exe)) {
            QMessageBox::information(this, QStringLiteral("WeSing"),
                QString::fromUtf8("ไม่พบโปรแกรมจำลอง: ติดตั้ง BlueStacks หรือกด \"เลือกไฟล์...\" เพื่อระบุไฟล์ที่ใช้เปิดโปรแกรมจำลอง"));
            return;
        }
        QStringList args;
        if (!edArgs->text().trimmed().isEmpty())
            args = QProcess::splitCommand(edArgs->text());
        QProcess::startDetached(exe, args, QFileInfo(exe).absolutePath());
        QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        s.setValue("WeSingEmuPath", exe);
        s.setValue("WeSingEmuArgs", edArgs->text());
    }

    static QString findBlueStacks()
    {
        const QString keys[] = { QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\BlueStacks_nxt"),
                                 QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\BlueStacks_nxt_cn") };
        for (const QString &k : keys) {
            QSettings reg(k, QSettings::NativeFormat);
            const QString dir = reg.value(QStringLiteral("InstallDir")).toString();
            if (!dir.isEmpty()) {
                const QString exe = QDir(dir).filePath(QStringLiteral("HD-Player.exe"));
                if (QFileInfo::exists(exe))
                    return QDir::toNativeSeparators(exe);
            }
        }
        const QString cand[] = { QStringLiteral("C:/Program Files/BlueStacks_nxt/HD-Player.exe"),
                                 QStringLiteral("C:/Program Files (x86)/BlueStacks_nxt/HD-Player.exe") };
        for (const QString &c : cand)
            if (QFileInfo::exists(c))
                return QDir::toNativeSeparators(c);
        return QString();
    }

    MidiSynthesizer *synth;
    QComboBox *cbInput = nullptr;
    QPushButton *btnStart = nullptr;
    QLabel *status = nullptr;
    QLabel *lbVol = nullptr;
    QSlider *sl = nullptr;
    QCheckBox *chMute = nullptr;
    QCheckBox *chAuto = nullptr;
    QProgressBar *meter = nullptr;
    QLineEdit *edExe = nullptr;
    QLineEdit *edArgs = nullptr;
    QTimer *tm = nullptr;
};

#endif // !__linux__
#endif // WESINGDIALOG_H
