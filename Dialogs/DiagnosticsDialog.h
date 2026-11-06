#ifndef DIAGNOSTICSDIALOG_H
#define DIAGNOSTICSDIALOG_H

// "ข้อมูลระบบ / Diagnostics": one window that collects what is needed to find a problem on a customer's PC
// (Windows version, screen / DPI, sound device and output settings, VST scan results, layout log, licence state)
// and lets the user copy it or save it as a text file to send to the developer. Header-only, read-only: it changes nothing.

#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QMessageBox>
#include <QGuiApplication>
#include <QScreen>
#include <QSysInfo>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QFont>
#include <bass.h>

#include "version.h"
#include "Midi/MidiSynthesizer.h"
#include "Midi/AsioOutput.h"

class DiagnosticsDialog
{
public:
    static void run(QWidget *parent, MidiSynthesizer *synth)
    {
        QDialog d(parent);
        d.setWindowTitle(QString::fromUtf8("ข้อมูลระบบ / Diagnostics"));
        d.resize(680, 560);
        QVBoxLayout *v = new QVBoxLayout(&d);

        QPlainTextEdit *te = new QPlainTextEdit(&d);
        te->setReadOnly(true);
        te->setLineWrapMode(QPlainTextEdit::NoWrap);
        QFont mono(QStringLiteral("Consolas"));
        mono.setStyleHint(QFont::Monospace);
        mono.setPointSize(9);
        te->setFont(mono);
        const QString text = report(synth);
        te->setPlainText(text);
        v->addWidget(te, 1);

        QHBoxLayout *h = new QHBoxLayout();
        QPushButton *bCopy = new QPushButton(QString::fromUtf8("คัดลอก"), &d);
        QPushButton *bSave = new QPushButton(QString::fromUtf8("บันทึกเป็นไฟล์..."), &d);
        QPushButton *bDir = new QPushButton(QString::fromUtf8("เปิดโฟลเดอร์ล็อก"), &d);
        QPushButton *bClose = new QPushButton(QString::fromUtf8("ปิด"), &d);
        h->addWidget(bCopy);
        h->addWidget(bSave);
        h->addWidget(bDir);
        h->addStretch(1);
        h->addWidget(bClose);
        v->addLayout(h);

        QObject::connect(bCopy, &QPushButton::clicked, &d, [te]() {
            QApplication::clipboard()->setText(te->toPlainText());
        });
        QObject::connect(bSave, &QPushButton::clicked, &d, [&d, te]() {
            const QString def = QDir::homePath() + QStringLiteral("/KaraMixer-diagnostics.txt");
            const QString path = QFileDialog::getSaveFileName(&d, QString::fromUtf8("บันทึกข้อมูลระบบ"), def,
                                                              QStringLiteral("Text (*.txt)"));
            if (path.isEmpty())
                return;
            QFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                f.write(te->toPlainText().toUtf8());
                f.close();
            } else {
                QMessageBox::warning(&d, QString::fromUtf8("บันทึกไม่ได้"), path);
            }
        });
        QObject::connect(bDir, &QPushButton::clicked, &d, []() {
            const QString dir = QDir::homePath() + QStringLiteral("/.KaraMixer");
            QDir().mkpath(dir);
            QProcessOpen(dir);
        });
        QObject::connect(bClose, &QPushButton::clicked, &d, &QDialog::accept);
        d.exec();
    }

    static QString report(MidiSynthesizer *synth)
    {
        QString r;
        QTextStream t(&r);
        t.setCodec("UTF-8");
        t << "KaraMixer diagnostics  " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        t << "=========================================================\n";
        t << "Program   : " << QString::fromUtf8(VER_FILEDESCRIPTION_STR) << " " << QString::fromUtf8(VER_FILEVERSION_STR) << "\n";
        t << "Edition   : Free (open source)\n";
        t << "Qt        : " << QT_VERSION_STR << " (run time " << qVersion() << ")\n";
        t << "Windows   : " << QSysInfo::prettyProductName() << "  kernel " << QSysInfo::kernelVersion()
          << "  " << QSysInfo::currentCpuArchitecture() << "  (program " << (sizeof(void *) == 8 ? "64" : "32") << "-bit)\n";

        t << "\n-- Screens --\n";
        int n = 0;
        for (QScreen *s : QGuiApplication::screens()) {
            t << "#" << n++ << "  " << s->name() << "  " << s->geometry().width() << "x" << s->geometry().height()
              << "  scale " << s->devicePixelRatio() << "  dpi " << int(s->logicalDotsPerInch()) << "\n";
        }

        t << "\n-- Sound --\n";
        t << "BASS      : " << QString::number(BASS_GetVersion(), 16) << "\n";
        const DWORD dev = BASS_GetDevice();
        BASS_DEVICEINFO di;
        if (dev != DWORD(-1) && BASS_GetDeviceInfo(dev, &di))
            t << "Device    : #" << dev << "  " << QString::fromUtf8(di.name ? di.name : "") << "\n";
        else
            t << "Device    : (none)\n";
        BASS_INFO bi;
        if (BASS_GetInfo(&bi))
            t << "Driver    : speakers " << bi.speakers << "  device rate " << bi.freq << " Hz  latency " << bi.latency << " ms\n";
        t << "ASIO      : " << (AsioOutput::isActive() ? "active" : "not used") << "\n";
        if (synth) {
            const DWORD mix = synth->mainMixerHandle();
            BASS_CHANNELINFO ci;
            if (mix && BASS_ChannelGetInfo(mix, &ci))
                t << "Mixer     : " << ci.freq << " Hz  " << ci.chans << " ch  "
                  << ((ci.flags & BASS_SAMPLE_FLOAT) ? "32-bit float" : "16-bit")
                  << ((ci.flags & BASS_STREAM_DECODE) ? "  (decode / ASIO)" : "") << "\n";
            t << "CPU (BASS): " << QString::number(double(BASS_GetCPU()), 'f', 1) << " %\n";
        }

        const QString dir = QDir::homePath() + QStringLiteral("/.KaraMixer/");
        t << "\n-- events.log (problem journal: CLIP / AUDIO / QT-ERROR / CRASH / ABNORMAL, last lines) --\n" << tail(dir + "events.log", 60);
        t << "\n-- asio.log (last lines) --\n" << tail(dir + "asio.log", 20);
        t << "\n-- vst-skip.txt (plug-ins skipped because they crashed the scan) --\n" << tail(dir + "vst-skip.txt", 40);
        t << "\n-- vstscan.log (last lines) --\n" << tail(dir + "vstscan.log", 25);
        t << "\n-- vst3.log (last lines) --\n" << tail(dir + "vst3.log", 25);
        t << "\n-- layout.log (last lines) --\n" << tail(dir + "layout.log", 25);
        t << "\n-- autohide.log (last lines) --\n" << tail(dir + "autohide.log", 15);
        return r;
    }

private:
    static QString tail(const QString &path, int lines)
    {
        QFile f(path);
        if (!f.exists())
            return QStringLiteral("(no file)\n");
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return QStringLiteral("(cannot read)\n");
        const QStringList all = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
        QStringList out = all.mid(qMax(0, all.size() - lines));
        const QString s = out.join(QLatin1Char('\n')).trimmed();
        return s.isEmpty() ? QStringLiteral("(empty)\n") : s + QLatin1Char('\n');
    }

    static void QProcessOpen(const QString &dir);
};

#include <QDesktopServices>
#include <QUrl>
inline void DiagnosticsDialog::QProcessOpen(const QString &dir)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

#endif // DIAGNOSTICSDIALOG_H
