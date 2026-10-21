#ifndef YOUTUBEDIALOG_H
#define YOUTUBEDIALOG_H

// PRO: YouTube through the mixer, with a vocal remover.
//   * the button opens YouTube in its own browser window (Microsoft Edge / Chrome, own profile: log in once)
//   * Windows sends that browser's sound to a virtual audio cable ("CABLE Input", e.g. VB-Cable)
//   * KaraMixer records "CABLE Output", optionally removes the lead vocal (VocalRemover.h) and feeds it into the
//     YouTube strip of the mixer (first strip of the Bus group), so it follows the master volume, limiter,
//     mastering, recording and the ASIO / Windows Audio output of the program
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
#include "Dialogs/VocalRemover.h"
#include "Midi/YouTubeRing.h"
#include <vector>

// ------------------------------------------------------------------------------------------------
// The audio side: records a Windows capture device and pushes it into the main mixer.
// ------------------------------------------------------------------------------------------------
class YouTubeBridge
{
public:
    struct Dev { int index; QString name; };

    static YouTubeBridge &get() { static YouTubeBridge b; return b; }

    QList<Dev> devices() const
    {
        QList<Dev> out;
        // WASAPI loopback devices ("what you hear" of an output device): need no virtual cable and no per-app routing
        BASS_SetConfig(BASS_CONFIG_REC_LOOPBACK, 1);
        BASS_DEVICEINFO di;
        for (int i = 0; BASS_RecordGetDeviceInfo(i, &di); i++)
            if (di.flags & BASS_DEVICE_ENABLED) {
                QString n = QString::fromUtf8(di.name);
                if (di.flags & BASS_DEVICE_LOOPBACK)
                    n = QString::fromUtf8("[ทั้งระบบ] ") + n;
                out.append({ i, n });
            }
        return out;
    }

    bool active() const { return running; }
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

        // the YouTube strip of the mixer reads this capture through a jitter buffer (YouTubeRing)
        // (fader / mute / solo / Master are the strip's)
        YouTubeRing::get().clear();
        peak = 0.0f;

        // 20 ms capture period: steady, small extra delay
        rec = BASS_RecordStart(kRate, 2, MAKELONG(BASS_SAMPLE_FLOAT, 20), &YouTubeBridge::recProc, this);
        if (!rec) {
            err = QStringLiteral("BASS_RecordStart error %1").arg(BASS_ErrorGetCode());
            BASS_RecordFree();
            return false;
        }
        devName = name;
        running = true;

        attach();
        if (!timer) {
            timer = new QTimer(qApp);
            QObject::connect(timer, &QTimer::timeout, qApp, []() { YouTubeBridge::get().attach(); });
        }
        timer->start(250);      // re-attach after the synth was re-opened (output change)
        return true;
    }

    void stop()
    {
        if (timer) timer->stop();
        running = false;
        const DWORD r = rec;
        rec = 0;
        if (r) BASS_ChannelStop(r);
        YouTubeRing::get().clear();                       // the strip keeps playing silence
        if (r) {
            BASS_RecordSetDevice((DWORD)recDevice);
            BASS_RecordFree();
        }
        peak = 0.0f;
    }

    VocalRemover &vocal() { return vr; }

    // vocal remover settings are saved with the other YouTube keys
    static void loadVocal(VocalRemover &v)
    {
        QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        v.setStrength(st.value("YouTubeVocalStrength", 100).toInt());
        v.setSharpness(st.value("YouTubeVocalSharp", 60).toInt());
        v.setKeepLow(st.value("YouTubeVocalLow", 160).toInt());
        v.setKeepHigh(st.value("YouTubeVocalHigh", 12000).toInt());
        v.setKeepDrums(st.value("YouTubeVocalDrums", 60).toInt());
        v.setEnabled(st.value("YouTubeVocalOn", false).toBool());
    }

    // level for the mixer's YouTube strip / master meters (separate from the window's meter)
    float takeStripPeak() { const float v = stripPeak; stripPeak = 0.0f; return v; }

    float takePeak() { const float v = peak; peak = 0.0f; return v; }

    // diagnostics for the window: how much data the capture device delivers, and how loud it is
    unsigned long long bytesReceived() const { return bytesIn.load(); }
    float takeDiagPeak() { return diagPeak.exchange(0.0f); }

    // ---- used by the mixer's YouTube button (no window): start the capture quietly and open YouTube
    static QString findBrowser()
    {
        QStringList roots;
        roots << qEnvironmentVariable("ProgramFiles(x86)") << qEnvironmentVariable("ProgramFiles")
              << QStringLiteral("C:/Program Files (x86)") << QStringLiteral("C:/Program Files");
        const QString rel[] = { QStringLiteral("/Microsoft/Edge/Application/msedge.exe"),
                                QStringLiteral("/Google/Chrome/Application/chrome.exe") };
        for (const QString &r : rel)
            for (const QString &root : roots)
                if (!root.isEmpty() && QFileInfo::exists(root + r))
                    return QDir::toNativeSeparators(root + r);
        return QString();
    }

    // exe empty -> the saved / found browser
    static void openYouTube(QString exe = QString())
    {
        const QUrl url(QStringLiteral("https://www.youtube.com/"));
        QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        exe = exe.trimmed();
        if (exe.isEmpty())
            exe = s.value("YouTubeBrowser", "").toString();
        if (exe.isEmpty() || !QFileInfo::exists(exe))
            exe = findBrowser();
        if (exe.isEmpty() || !QFileInfo::exists(exe)) {
            QDesktopServices::openUrl(url);       // no Edge / Chrome found: the default browser (its sound may need routing too)
            return;
        }
        // own profile folder: the login is kept, and this browser window is separate from the everyday one
        const QString profile = QDir::homePath() + QStringLiteral("/.KaraMixer/youtube-profile");
        QDir().mkpath(profile);
        QStringList args;
        args << QStringLiteral("--app=") + url.toString()
             << QStringLiteral("--user-data-dir=") + QDir::toNativeSeparators(profile)
             << QStringLiteral("--window-size=1100,720");
        QProcess::startDetached(exe, args, QFileInfo(exe).absolutePath());
        s.setValue("YouTubeBrowser", exe);
    }

    // starts the capture with the saved input (or the first "CABLE" input) when it is not running; no message boxes
    static void startQuiet(MidiSynthesizer *syn)
    {
        YouTubeBridge &b = get();
        if (b.running)
            return;
        loadVocal(b.vr);
        QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        const QString want = st.value("YouTubeInput", "").toString();
        const QList<Dev> list = b.devices();
        int pick = -1;
        for (int i = 0; i < list.count() && pick < 0; i++)
            if (!want.isEmpty() && list[i].name == want) pick = i;
        for (int i = 0; i < list.count() && pick < 0; i++)
            if (list[i].name.contains(QStringLiteral("CABLE"), Qt::CaseInsensitive)) pick = i;
        if (pick < 0)
            return;
        QString err;
        if (b.start(list[pick].index, list[pick].name, syn, err))
            st.setValue("YouTubeInput", list[pick].name);
    }

    // start again at program start, when the user asked for it (and PRO is unlocked)
    static void autoStart(MidiSynthesizer *s)
    {
        if (!ProLicense::isUnlocked())
            return;
        QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        if (!st.value("YouTubeAuto", false).toBool())
            return;
        loadVocal(get().vr);
        const QString want = st.value("YouTubeInput", "").toString();
        if (want.isEmpty())
            return;
        YouTubeBridge &b = get();
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
        YouTubeBridge *b = static_cast<YouTubeBridge *>(user);
        if (!b->running || !length)
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
        b->bytesIn += length;
        if (m > b->diagPeak.load()) b->diagPeak = m;
        if (m > b->stripPeak) b->stripPeak = m;

        const void *data = buffer;
        DWORD len = length;
        if (b->vr.enabled()) {                 // vocal remover (about 64 ms later than the input)
            if (!b->vrWasOn) { b->vr.reset(); b->vrWasOn = true; }
            b->tmp.clear();
            b->vr.process(f, int(n / 2), b->tmp);
            data = b->tmp.data();
            len = DWORD(b->tmp.size() * sizeof(float));
        } else {
            b->vrWasOn = false;
        }
        if (len)
            YouTubeRing::get().write(static_cast<const float *>(data), int(len / (2 * sizeof(float))));
        return TRUE;
    }

    void attach()
    {
        if (!running || !synth)
            return;
        const DWORD h = synth->getChannelHandle(InstrumentType::YouTube);
        // the mixer drops a stream that stalls: put the strip's stream back into its mixer when it is gone
        if (h && BASS_Mixer_ChannelGetMixer(h) == 0) {
            const InstrumentType t = InstrumentType::YouTube;
            synth->setBusGroup(t, synth->instrument(t).bus);
        }
    }

    VocalRemover vr;
    std::vector<float> tmp;
    bool vrWasOn = false;
    bool running = false;
    DWORD rec = 0;
    int recDevice = -1;
    MidiSynthesizer *synth = nullptr;
    QTimer *timer = nullptr;
    QString devName;
    std::atomic<float> peak{0.0f};
    std::atomic<float> stripPeak{0.0f};
    std::atomic<unsigned long long> bytesIn{0};
    std::atomic<float> diagPeak{0.0f};
};

// ------------------------------------------------------------------------------------------------
// The window: choose the input, start / stop, vocal remover, open YouTube.
// ------------------------------------------------------------------------------------------------
class YouTubeDialog : public QDialog
{
public:
    YouTubeDialog(QWidget *parent, MidiSynthesizer *synth) : QDialog(parent), synth(synth)
    {
        setWindowTitle(QString::fromUtf8("YouTube — เล่นผ่านมิกเซอร์ + ตัดเสียงร้อง"));
        setMinimumWidth(620);
        setAttribute(Qt::WA_DeleteOnClose);

        QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        YouTubeBridge &br = YouTubeBridge::get();
        if (!br.active())
            YouTubeBridge::loadVocal(br.vocal());      // while it is running the bridge keeps its own values

        QVBoxLayout *v = new QVBoxLayout(this);
        v->setSpacing(8);

        QLabel *help = new QLabel(QString::fromUtf8(
            "<b>ตั้งค่าครั้งแรก (ทำครั้งเดียว)</b><br>"
            "1) ติดตั้งสายเสียงเสมือน <b>VB-Cable</b> (ปุ่ม \"ดาวน์โหลด VB-Cable\" ด้านล่าง) แล้วรีสตาร์ตเครื่อง<br>"
            "2) กดปุ่ม <b>YouTube</b> ในมิกเซอร์ จะเปิดหน้า YouTube ให้ล็อกอินได้เลย (ล็อกอินครั้งเดียว ระบบจำให้)<br>"
            "3) ตั้งให้เสียงของ <b>Microsoft Edge</b> ออกที่ <b>CABLE Input</b>: Windows Settings &gt; System &gt; Sound &gt; Volume mixer "
            "(ต้องเปิดเสียงใน YouTube ก่อน โปรแกรมถึงจะขึ้นในรายการ)<br>"
            "4) เลือกอินพุต <b>CABLE Output</b> ด้านล่าง กด \"เริ่มรับเสียง\" เสียงจะออกที่แทรค <b>YouTube</b> และ Master"), this);
        help->setWordWrap(true);
        v->addWidget(help);

        // ---- input
        QGroupBox *gi = new QGroupBox(QString::fromUtf8("รับเสียงจาก YouTube เข้ามิกเซอร์"), this);
        QGridLayout *g = new QGridLayout(gi);

        g->addWidget(new QLabel(QString::fromUtf8("อินพุต :"), gi), 0, 0);
        cbInput = new QComboBox(gi);
        g->addWidget(cbInput, 0, 1);
        QPushButton *btnRefresh = new QPushButton(QString::fromUtf8("โหลดรายการใหม่"), gi);
        g->addWidget(btnRefresh, 0, 2);
        QPushButton *btnLoop = new QPushButton(QString::fromUtf8("ใช้เสียงทั้งระบบ (ไม่ต้องใช้ VB-Cable)"), gi);
        btnLoop->setToolTip(QString::fromUtf8("ดักเสียงที่ออกลำโพงของ Windows ทั้งหมด ไม่ต้องตั้ง Volume mixer "
            "แนะนำให้ตั้งเอาต์พุตของ KaraMixer เป็น ASIO หรืออุปกรณ์อื่นที่ไม่ใช่ลำโพงที่ดัก ไม่งั้นเสียงจะวนซ้อน"));
        g->addWidget(btnLoop, 5, 0, 1, 3);

        btnStart = new QPushButton(gi);
        g->addWidget(btnStart, 1, 1);
        status = new QLabel(gi);
        status->setWordWrap(true);
        g->addWidget(status, 1, 2, 1, 1);

        QLabel *tip = new QLabel(QString::fromUtf8("ระดับเสียง / ปิดเสียง / โซโล ปรับที่แทรค <b>YouTube</b> (แทรคแรกของกลุ่ม Bus) ในมิกเซอร์ เสียงผ่าน Master ตามปกติ"), gi);
        tip->setWordWrap(true);
        g->addWidget(tip, 2, 0, 1, 3);

        g->addWidget(new QLabel(QString::fromUtf8("สัญญาณ :"), gi), 3, 0);
        meter = new QProgressBar(gi);
        meter->setRange(0, 100);
        meter->setTextVisible(false);
        meter->setFixedHeight(12);
        g->addWidget(meter, 3, 1, 1, 2);

        chAuto = new QCheckBox(QString::fromUtf8("เริ่มรับเสียงอัตโนมัติเมื่อเปิดโปรแกรม"), gi);
        chAuto->setChecked(st.value("YouTubeAuto", false).toBool());
        g->addWidget(chAuto, 4, 1, 1, 2);
        v->addWidget(gi);

        // ---- vocal remover
        QGroupBox *gv = new QGroupBox(QString::fromUtf8("ตัดเสียงร้อง"), this);
        QGridLayout *vg = new QGridLayout(gv);
        VocalRemover &vr = br.vocal();

        chVocal = new QCheckBox(QString::fromUtf8("เปิดตัดเสียงร้อง (ตัดเสียงที่อยู่ตรงกลางภาพเสียง)"), gv);
        chVocal->setChecked(vr.enabled());
        vg->addWidget(chVocal, 0, 0, 1, 3);

        auto addSlider = [&](int row, const QString &name, int lo, int hi, int val, QLabel *&lb, QSlider *&sl) {
            vg->addWidget(new QLabel(name, gv), row, 0);
            sl = new QSlider(Qt::Horizontal, gv);
            sl->setRange(lo, hi);
            sl->setValue(val);
            lb = new QLabel(gv);
            lb->setMinimumWidth(64);
            vg->addWidget(sl, row, 1);
            vg->addWidget(lb, row, 2);
        };
        addSlider(1, QString::fromUtf8("ความแรง :"), 0, 100, st.value("YouTubeVocalStrength", 100).toInt(), lbStr, slStr);
        addSlider(2, QString::fromUtf8("ความคม :"), 0, 100, st.value("YouTubeVocalSharp", 60).toInt(), lbSharp, slSharp);
        addSlider(3, QString::fromUtf8("เก็บเบส/กลองต่ำกว่า :"), 60, 400, st.value("YouTubeVocalLow", 160).toInt(), lbLow, slLow);
        addSlider(4, QString::fromUtf8("เก็บเสียงสูงเหนือ :"), 4000, 16000, st.value("YouTubeVocalHigh", 12000).toInt(), lbHigh, slHigh);
        addSlider(5, QString::fromUtf8("รักษากลอง/เสียงแตก :"), 0, 100, st.value("YouTubeVocalDrums", 60).toInt(), lbDrum, slDrum);

        QLabel *vnote = new QLabel(QString::fromUtf8(
            "เคล็ดลับ: ความแรงสูงสุด 100 = ตัดเต็มที่ ถ้าเสียงดนตรีโดนตัดตามไปด้วยให้ลดความแรงหรือเพิ่มค่า \"เก็บเบส/กลอง\" "
            "ถ้ายังมีเสียงร้องเหลือให้เพิ่ม \"ความคม\" ส่วน \"รักษากลอง/เสียงแตก\" ยิ่งสูงยิ่งเก็บกลอง ฉาบ และเสียงซ่ามากขึ้น (ตัดเฉพาะเสียงที่เป็นโทนของเสียงร้อง) แต่เสียงร้องอาจเหลือเล็กน้อย<br>"
            "<span style='color:#aaa'>ข้อจำกัด: เพลงที่มิกซ์รวมกันแล้วแยกเสียงร้องออก 100% ไม่ได้ในทางเทคนิค วิธีนี้ได้ผลดีกับเพลงสเตอริโอที่เสียงร้องหลักอยู่ตรงกลาง "
            "เสียงก้อง/คอรัสกว้างหรือเพลงโมโนจะยังเหลืออยู่ ถ้าต้องการสะอาดที่สุด ให้ค้นหาคลิปแบบ \"karaoke / instrumental / ดนตรีล้วน\" ใน YouTube แล้วปิดตัดเสียงร้อง</span>"), gv);
        vnote->setWordWrap(true);
        vg->addWidget(vnote, 6, 0, 1, 3);
        v->addWidget(gv);

        // ---- browser
        QGroupBox *ge = new QGroupBox(QString::fromUtf8("เปิด YouTube"), this);
        QGridLayout *e = new QGridLayout(ge);
        e->addWidget(new QLabel(QString::fromUtf8("เบราว์เซอร์ :"), ge), 0, 0);
        edExe = new QLineEdit(st.value("YouTubeBrowser", "").toString(), ge);
        e->addWidget(edExe, 0, 1);
        QPushButton *btnBrowse = new QPushButton(QString::fromUtf8("เลือกไฟล์..."), ge);
        e->addWidget(btnBrowse, 0, 2);

        QHBoxLayout *hb = new QHBoxLayout();
        QPushButton *btnOpen = new QPushButton(QString::fromUtf8("เปิด YouTube"), ge);
        btnOpen->setMinimumHeight(34);
        QPushButton *btnSound = new QPushButton(QString::fromUtf8("ตั้งค่าเสียงของ Windows"), ge);
        QPushButton *btnWeb = new QPushButton(QString::fromUtf8("ดาวน์โหลด VB-Cable"), ge);
        hb->addWidget(btnOpen, 1);
        hb->addWidget(btnSound);
        hb->addWidget(btnWeb);
        e->addLayout(hb, 1, 0, 1, 3);
        v->addWidget(ge);

        QLabel *note = new QLabel(QString::fromUtf8(
            "หมายเหตุ: อย่าตั้งเอาต์พุตของ KaraMixer เป็น CABLE Input (เสียงจะวนกลับเข้ามาเอง) "
            "เสียงจะหน่วงเพิ่ม (ประมาณ 0.15–0.2 วินาที เมื่อเปิดตัดเสียงร้อง) ไม่กระทบเพลงที่เล่นในมิกเซอร์ "
            "การล็อกอินใช้หน้าล็อกอินของ Google ในเบราว์เซอร์โดยตรง KaraMixer ไม่เห็นและไม่เก็บรหัสผ่าน"), this);
        note->setWordWrap(true);
        v->addWidget(note);

        QPushButton *btnClose = new QPushButton(QString::fromUtf8("ปิด"), this);
        QHBoxLayout *hc = new QHBoxLayout();
        hc->addStretch(1);
        hc->addWidget(btnClose);
        v->addLayout(hc);

        if (edExe->text().isEmpty())
            edExe->setText(findBrowser());

        fillInputs(st.value("YouTubeInput", "").toString());
        refreshState();
        updateLabels();

        // ---- actions
        connect(btnRefresh, &QPushButton::clicked, this, [this]() { fillInputs(cbInput->currentText()); });
        connect(btnStart, &QPushButton::clicked, this, [this]() { toggle(true); });
        connect(btnLoop, &QPushButton::clicked, this, [this]() {
            YouTubeBridge &b = YouTubeBridge::get();
            if (b.active()) { b.stop(); refreshState(); }
            int pick = -1;
            for (int i = 0; i < cbInput->count(); i++)
                if (cbInput->itemText(i).startsWith(QString::fromUtf8("[ทั้งระบบ]"))) { pick = i; break; }
            if (pick < 0) {
                QMessageBox::information(this, QStringLiteral("YouTube"),
                    QString::fromUtf8("ไม่พบอุปกรณ์ดักเสียงทั้งระบบในเครื่องนี้"));
                return;
            }
            cbInput->setCurrentIndex(pick);
            QMessageBox::information(this, QStringLiteral("YouTube"),
                QString::fromUtf8("โหมดเสียงทั้งระบบดักเสียงทุกอย่างที่ออกลำโพงของ Windows\n"
                                  "ควรตั้งเอาต์พุตของ KaraMixer เป็น ASIO หรืออุปกรณ์อื่น ไม่ใช่ลำโพงตัวเดียวกันนี้ "
                                  "มิฉะนั้นเสียงจะวนซ้อน"));
            toggle(true);
        });
        connect(chVocal, &QCheckBox::toggled, this, [this](bool) { applyVocal(); });
        for (QSlider *sl : { slStr, slSharp, slLow, slHigh, slDrum })
            connect(sl, &QSlider::valueChanged, this, [this](int) { applyVocal(); });
        connect(chAuto, &QCheckBox::toggled, this, [this](bool on) {
            QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
            s.setValue("YouTubeAuto", on);
            s.setValue("YouTubeInput", cbInput->currentText());
        });
        connect(btnBrowse, &QPushButton::clicked, this, [this]() {
            const QString f = QFileDialog::getOpenFileName(this, QString::fromUtf8("เลือกเบราว์เซอร์ (เช่น msedge.exe, chrome.exe)"),
                                                           edExe->text(), QStringLiteral("Program (*.exe)"));
            if (!f.isEmpty())
                edExe->setText(f);
        });
        connect(btnOpen, &QPushButton::clicked, this, [this]() { launchYouTube(); });
        connect(btnSound, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:apps-volume")));
        });
        connect(btnWeb, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://vb-audio.com/Cable/")));
        });
        connect(btnClose, &QPushButton::clicked, this, &QDialog::close);

        tm = new QTimer(this);
        connect(tm, &QTimer::timeout, this, [this]() {
            const float p = YouTubeBridge::get().takePeak();
            int pct = 0;
            if (p > 0.001f)       // -60 dB .. 0 dB
                pct = qBound(0, int((20.0 * std::log10(double(p)) + 60.0) / 60.0 * 100.0 + 0.5), 100);
            meter->setValue(pct);
        });
        tm->start(60);

        // what is really arriving from the capture device (so it is clear whether the cable is silent or not delivering)
        diag = new QLabel(this);
        diag->setWordWrap(true);
        v->insertWidget(2, diag);
        diagTimer = new QTimer(this);
        connect(diagTimer, &QTimer::timeout, this, [this]() {
            YouTubeBridge &b = YouTubeBridge::get();
            if (!b.active()) {
                diag->setText(QString());
                lastBytes = b.bytesReceived();
                return;
            }
            const unsigned long long now = b.bytesReceived();
            const double kbps = double(now - lastBytes) / 1024.0 * 2.0;   // timer = 500 ms
            lastBytes = now;
            const float p = b.takeDiagPeak();
            QString t;
            if (kbps < 1.0)
                t = QString::fromUtf8("⚠ อุปกรณ์รับเสียงไม่ส่งข้อมูลมาเลย: CABLE Output อาจถูกโปรแกรมอื่นใช้อยู่/ถูกปิด/ถูกตั้งแบบเอกสิทธิ์ "
                                      "(ลองกด \"หยุดรับเสียง\" แล้ว \"เริ่มรับเสียง\" ใหม่ หรือปิดโปรแกรมที่ใช้ CABLE Output)");
            else if (p < 0.0005f)
                t = QString::fromUtf8("⚠ ได้รับข้อมูลจากสาย (%1 KB/วินาที) แต่เป็นความเงียบ: เสียงของ Edge ยังไม่เข้า CABLE Input (ถ้าตั้ง Volume mixer ไม่ได้ผล ให้กดปุ่ม \"ใช้เสียงทั้งระบบ\") — "
                                      "เช็กว่า YouTube กำลังเล่นอยู่, ใน Volume mixer ช่อง Output ของ Microsoft Edge เป็น CABLE Input "
                                      "(ตั้งหลังจากเปิดวิดีโอแล้ว และอาจต้องปิดแล้วเปิดหน้าต่าง YouTube ใหม่), และ CABLE ไม่ได้ถูก Mute")
                        .arg(int(kbps));
            else
                t = QString::fromUtf8("✔ มีสัญญาณเข้ามิกเซอร์ (%1 KB/วินาที)").arg(int(kbps));
            diag->setText(t);
        });
        diagTimer->start(500);
    }

    ~YouTubeDialog() override
    {
        QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        s.setValue("YouTubeBrowser", edExe->text());
        if (!cbInput->currentText().isEmpty())
            s.setValue("YouTubeInput", cbInput->currentText());
    }

    // the mixer's YouTube button: start the capture (silently, when the virtual cable is there) and open YouTube
    void launchYouTube()
    {
        if (!YouTubeBridge::get().active() && cbInput->count() > 0)
            toggle(false);
        openBrowser();
    }

private:
    void applyVocal()
    {
        VocalRemover &vr = YouTubeBridge::get().vocal();
        vr.setStrength(slStr->value());
        vr.setSharpness(slSharp->value());
        vr.setKeepLow(slLow->value());
        vr.setKeepHigh(slHigh->value());
        vr.setKeepDrums(slDrum->value());
        vr.setEnabled(chVocal->isChecked());
        QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        s.setValue("YouTubeVocalOn", chVocal->isChecked());
        s.setValue("YouTubeVocalStrength", slStr->value());
        s.setValue("YouTubeVocalSharp", slSharp->value());
        s.setValue("YouTubeVocalLow", slLow->value());
        s.setValue("YouTubeVocalHigh", slHigh->value());
        s.setValue("YouTubeVocalDrums", slDrum->value());
        updateLabels();
    }

    void updateLabels()
    {
        lbStr->setText(QString::number(slStr->value()) + QStringLiteral(" %"));
        lbSharp->setText(QString::number(slSharp->value()) + QStringLiteral(" %"));
        lbLow->setText(QString::number(slLow->value()) + QStringLiteral(" Hz"));
        lbDrum->setText(QString::number(slDrum->value()) + QStringLiteral(" %"));
        lbHigh->setText(QString::number(slHigh->value() / 1000.0, 'f', 1) + QStringLiteral(" kHz"));
    }

    void fillInputs(const QString &prefer)
    {
        cbInput->clear();
        int pick = -1;
        const QList<YouTubeBridge::Dev> list = YouTubeBridge::get().devices();
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
        const bool on = YouTubeBridge::get().active();
        btnStart->setText(on ? QString::fromUtf8("หยุดรับเสียง") : QString::fromUtf8("เริ่มรับเสียง"));
        cbInput->setEnabled(!on);
        status->setText(on ? QString::fromUtf8("กำลังรับเสียงจาก: ") + YouTubeBridge::get().deviceName()
                           : QString::fromUtf8("ยังไม่ได้รับเสียง"));
    }

    // asked = the user pressed the start button (messages are allowed); otherwise only the status line changes
    void toggle(bool asked)
    {
        YouTubeBridge &b = YouTubeBridge::get();
        if (b.active()) {
            b.stop();
            refreshState();
            return;
        }
        if (cbInput->count() == 0) {
            if (asked)
                QMessageBox::information(this, QStringLiteral("YouTube"),
                    QString::fromUtf8("ไม่พบอุปกรณ์อินพุต: ติดตั้ง VB-Cable (สายเสียงเสมือน) แล้วกด \"โหลดรายการใหม่\""));
            return;
        }
        QString err;
        if (!b.start(cbInput->currentData().toInt(), cbInput->currentText(), synth, err)) {
            if (asked)
                QMessageBox::warning(this, QStringLiteral("YouTube"),
                    QString::fromUtf8("เริ่มรับเสียงไม่สำเร็จ") + QStringLiteral("\n") + err);
            else
                status->setText(QString::fromUtf8("เริ่มรับเสียงไม่สำเร็จ: ") + err);
        } else {
            QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
            s.setValue("YouTubeInput", cbInput->currentText());
        }
        refreshState();
    }

    void openBrowser() { YouTubeBridge::openYouTube(edExe->text()); }

    static QString findBrowser() { return YouTubeBridge::findBrowser(); }

    MidiSynthesizer *synth;
    QComboBox *cbInput = nullptr;
    QPushButton *btnStart = nullptr;
    QLabel *status = nullptr;
    QCheckBox *chAuto = nullptr;
    QCheckBox *chVocal = nullptr;
    QSlider *slStr = nullptr, *slSharp = nullptr, *slLow = nullptr, *slHigh = nullptr, *slDrum = nullptr;
    QLabel *lbStr = nullptr, *lbSharp = nullptr, *lbLow = nullptr, *lbHigh = nullptr, *lbDrum = nullptr;
    QLabel *diag = nullptr;
    QTimer *diagTimer = nullptr;
    unsigned long long lastBytes = 0;
    QProgressBar *meter = nullptr;
    QLineEdit *edExe = nullptr;
    QTimer *tm = nullptr;
};

#endif // !__linux__
#endif // YOUTUBEDIALOG_H
