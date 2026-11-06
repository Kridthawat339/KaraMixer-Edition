#include "MainWindow.h"
#include <QPainterPath>
#include <QRegion>

#include <QApplication>
#include <QSplashScreen>
#include <QDirIterator>
#include <QMetaType>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>
#include <QThread>
#include <QMessageBox>
#include <QFileInfo>
#ifndef __linux__
#endif
#include <QFontDatabase>
#include <QSettings>
#include <QDir>
#include <QTimer>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <cmath>
#include <atomic>
#include <bass.h>
#include <bassmidi.h>

#include "BASSFX/VSTFX.h"
#include "version.h"
#include "Config.h"
#include "Utils.h"
#include "Theme.h"
#include "Widgets/DialogSkin.h"
#include "Widgets/EventLog.h"
#include "Widgets/KaraokeLauncher.h"
#include "Widgets/PopupLifter.h"

#ifdef __linux__
#include <QFontDatabase>
#endif

void registerMetaType();

void checkDatabase(QSplashScreen *splash, SongDatabase *db);
void loadSoundfonts(QSplashScreen *splash, MidiSynthesizer *synth);

#ifndef __linux__
void loadVSTi(QSplashScreen *splash, MidiSynthesizer *synth);
void makeVSTList(QSplashScreen *splash, MidiSynthesizer *synth);
#endif

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    {
        // Windows' "Text size" (Settings > Accessibility > Text size) and a mixed 100 % / 150 % two-screen setup make the
        // system font Qt starts from much bigger (24 pt seen on a customer PC instead of 9), but the mixer's buttons
        // have fixed pixel sizes: the page tabs, DRUM / LIVE / BUS ... showed huge, cut-off text. The program uses the
        // standard 9 pt its layouts were made for, set before the first window (the splash) is drawn.
        QFont f = a.font();
        const qreal stdPt = 9.0;
        a.setProperty("kmSystemFontPt", f.pointSizeF());
        a.setProperty("kmSystemFontFamily", f.family());
        if (f.pointSizeF() > stdPt + 0.25) {
            f.setPointSizeF(stdPt);
            a.setFont(f);       // (also drops the per-class fonts of the system theme: menus / tooltips follow this one)
        }
    }
    PopupLifter::install();      // a plug-in's message box must not open hidden behind the (topmost) mixer

    // Started by "Open config": give the previous instance time to release audio/MIDI devices.
    if (a.arguments().contains("--delay"))
        QThread::msleep(1500);

    QCoreApplication::setApplicationName(VER_PRODUCTNAME_STR);
    QCoreApplication::setApplicationVersion(VER_FILEVERSION_STR);
    QCoreApplication::setOrganizationName(VER_COMPANYNAME_STR);
    QCoreApplication::setOrganizationDomain(VER_COMPANYDOMAIN_STR);

    registerMetaType();

    // problem journal (~/.KaraMixer/events.log): Qt warnings / errors, crash address, "last run did not close normally"
    EventLog::installHandlers();
    EventLog::sessionStart();
    EventLog::add(QStringLiteral("START"), QString::fromUtf8(VER_FILEVERSION_STR) + QStringLiteral("  ")
                  + QSysInfo::prettyProductName() + QStringLiteral("  ") + QSysInfo::currentCpuArchitecture());

    QPixmap *pixmap = new QPixmap(":/Icons/App/karamixer-splash.png");
    QSplashScreen *splash = new QSplashScreen(*pixmap);
    {   // rounded splash window
        QPainterPath sp;
        sp.addRoundedRect(QRectF(splash->rect()), 14, 14);
        splash->setMask(QRegion(sp.toFillPolygon().toPolygon()));
    }
    splash->show();
    qApp->processEvents();

    splash->showMessage("กำลังเริ่มโปรแกรม...", Qt::AlignBottom|Qt::AlignRight, QColor("#cfd3dc"));
    qApp->processEvents();

    { // Config Dir
        Config::initConfigDataPath();

        QDir dir(TEMP_DIR_PATH);
        if (!dir.exists())
            dir.mkpath(TEMP_DIR_PATH);

        dir.setPath(ALL_DATA_DIR_PATH);
        if (!dir.exists())
            dir.mkpath(ALL_DATA_DIR_PATH);

        dir.setPath(Config::CONFIG_DIR_PATH);
        if (!dir.exists())
            dir.mkpath(Config::CONFIG_DIR_PATH);
    }

    // Color theme (follow Windows / dark / light)
    Theme::init(a);
    a.installEventFilter(new DialogSkin(&a));

    // crisper text rendering
    {
        QFont f = a.font();
        f.setHintingPreference(QFont::PreferFullHinting);
        f.setStyleStrategy(QFont::PreferAntialias);
        a.setFont(f);
    }


    // Bundled font with the infinity sign used by the dB read-outs (all platforms)
    QFontDatabase::addApplicationFont(":/Fonts/DejaVu/DejaVuSans-Bold.ttf");

    // Add font for linux
    #ifdef __linux__
    QFontDatabase::addApplicationFont(":/Fonts/THSarabunNew/THSarabunNew Bold.ttf");
    #endif
    //----------------------------------

    // KaraMixer Edition edition: MainWindow only acts as the hidden audio/MIDI
    // back-end. The visible window of the program is the synth mixer.
    MainWindow w;
    qApp->setWindowIcon(QIcon(":/Icons/App/karamixer.png"));

    loadSoundfonts(splash, w.midiPlayer()->midiSynthesizer());

    #ifndef __linux__
    loadVSTi(splash, w.midiPlayer()->midiSynthesizer());
    makeVSTList(splash, w.midiPlayer()->midiSynthesizer());
    w.synthMixerDialog()->setVSTVendorMenu();
    #endif
    w.synthMixerDialog()->setFXToSynth();

    SynthMixerDialog *mixer = w.synthMixerDialog();
    mixer->setWindowTitle("KaraMixer Edition");
    w.hide();
    mixer->show();
    {   // auto-hide of the mixer while the karaoke program is used (Settings)
        QSettings ahs(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        w.setMixerAutoHide(ahs.value("MixerAutoHide", false).toBool(),
                           ahs.value("MixerAutoHideDelay", 3.0).toDouble(),
                           ahs.value("MixerAutoHideProgram", "").toString());
    }
    w.startRemoteAtLaunch();             // phone control that starts with the program (menu tick), if it is switched on

    if (KaraokeLauncher::autoLaunch())   // "open the karaoke program together with the mixer" (menu)
        QTimer::singleShot(1500, mixer, []() { KaraokeLauncher::launch(nullptr, false); });

    splash->finish(mixer);

    delete splash;
    delete pixmap;

    // "open / close the karaoke program together with the mixer": when the mixer closes, ask the karaoke program to close
    QObject::connect(&a, &QApplication::aboutToQuit, []() {
        if (KaraokeLauncher::autoLaunch())
            KaraokeLauncher::closeProgram();
    });

    const int exitCode = a.exec();
    EventLog::sessionEnd();   // normal close: the "did not close normally" flag is removed
    return exitCode;
}


void registerMetaType()
{
    qRegisterMetaType<MidiEvent>("MidiEvent");
    qRegisterMetaType<InstrumentType>("InstrumentType");

    //<QList<int>>("QList<int>");
    qRegisterMetaTypeStreamOperators<QList<int>>("QList<int>");

    //qRegisterMetaType<QList<uint>>("QList<uint>");
    qRegisterMetaTypeStreamOperators<QList<uint>>("QList<uint>");

    //qRegisterMetaType<QList<float>>("QList<float>");
    qRegisterMetaTypeStreamOperators<QList<float>>("QList<float>");

    //qRegisterMetaType<QList<bool>>("QList<bool>");
    qRegisterMetaTypeStreamOperators<QList<bool>>("QList<bool>");

    //qRegisterMetaType<QList<QList<float>>>("QList<QList<float>>");
    qRegisterMetaTypeStreamOperators<QList<QList<float>>>("QList<QList<float>>");

    qRegisterMetaTypeStreamOperators<QList<QByteArray>>("QList<QByteArray>");
}

void checkDatabase(QSplashScreen *splash, SongDatabase *db)
{
    if (db->isNewVersion())
        return;

    splash->showMessage("กำลังปรับปรุงฐานข้อมูลเพลง", Qt::AlignBottom|Qt::AlignRight, QColor("#cfd3dc"));
    qApp->processEvents();

    db->updateToNewVersion();
}

void loadSoundfonts(QSplashScreen *splash, MidiSynthesizer *synth)
{
    QSettings settings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);

    synth->setLoadAllSoundfont(settings.value("SynthSoundfontsLoadAll", false).toBool());

    // set soundfont to synth
    QStringList sfList = settings.value("SynthSoundfonts", QStringList()).toStringList();
    // saved index -> index in the synth. A soundfont that cannot be loaded (file moved, locked for another PC) is left
    // out, so every later one moves up: the saved volumes and instrument maps used to land on the wrong soundfont.
    QVector<int> newIndex(sfList.count(), -1);
    settings.beginReadArray("SynthSoundfontsVolume");
    for (int i=0; i<sfList.count(); i++)
    {
        settings.setArrayIndex(i);
        int volume = settings.value("SoundfontVolume", 100).toInt();

        splash->showMessage("กำลังโหลด : " + QFileInfo(sfList.at(i)).fileName(), Qt::AlignBottom|Qt::AlignRight, QColor("#cfd3dc"));
        qApp->processEvents();

        if (synth->addSoundfont(sfList.at(i))) {
            newIndex[i] = synth->soundfontFiles().count() - 1;
            synth->setSoundfontVolume(newIndex[i], volume / 100.0f);
        }
    }
    settings.endArray();
    auto remap = [&newIndex](QList<int> &m) {
        for (int &v : m)
            v = (v > 0 && v < newIndex.count() && newIndex[v] > 0) ? newIndex[v] : 0;   // missing -> default soundfont
    };


    // set Map soundfont
    for (int i = 0; i < SF_PRESET_COUNT; i++) {
        QString sfKey = "SynthSoundfontsMap";
        QString drKey = "SynthSoundfontsDrumMap";
        if (i > 0) {
            sfKey = sfKey + QString::number(i);
            drKey = drKey + QString::number(i);
        }
        QList<int> sfMap     = settings.value(sfKey).value<QList<int>>();
        QList<int> sfDrumMap = settings.value(drKey).value<QList<int>>();

        if (sfMap.count() == 0)
            sfMap = synth->getMapSoundfontIndex(i);
        else
            remap(sfMap);
        if (sfDrumMap.count() == 0)
            sfDrumMap = synth->getDrumMapSfIndex(i);
        else
            remap(sfDrumMap);

        synth->setMapSoundfontIndex(i, sfMap, sfDrumMap);
    }
}

#ifndef __linux__

void loadVSTi(QSplashScreen *splash, MidiSynthesizer *synth)
{
    QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);

    st.beginReadArray("VSTiGroup");

    for (int i=0; i<4; i++)
    {
        st.setArrayIndex(i);

        QString      filePath   = st.value("VstiFilePath", "").toString();
        int          program    = st.value("VstiPrograms", 0).toInt();
        QList<float> params     = st.value("VstiParams").value<QList<float>>();
        QByteArray   chunk      = st.value("VstiChunk", QByteArray()).toByteArray();

        if (filePath == "")
            continue;

        splash->showMessage("กำลังโหลด : " + QFileInfo(filePath).fileName(), Qt::AlignBottom|Qt::AlignRight, QColor("#cfd3dc"));
        qApp->processEvents();

        DWORD vsti = synth->setVSTiFile(i, filePath);

        if (vsti != 0)
        {
            if (chunk.length() > 0) {
                bv::SetChunk(vsti, false, chunk.constData(), chunk.length());
            } else {
                bv::SetProgram(vsti, program);
                FX::setVSTParams(vsti, params);
            }
        }
    }

    st.endArray();
}

void makeVSTList(QSplashScreen *splash, MidiSynthesizer *synth)
{
    QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
    QStringList dirs = st.value("VSTDirs", QStringList()).toStringList();

    QStringList vstDirs;
    vstDirs << QCoreApplication::applicationDirPath() + "/VST";
    vstDirs += dirs;

    QMap<uint, VSTNamePath> vstList;

    for (const QString &dir : vstDirs)
    {
        QDirIterator it(dir, QStringList() << "*.DLL" << "*.dll" << "*.vst3" << "*.VST3",
                        QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks, QDirIterator::Subdirectories);

        while(it.hasNext()) {

            it.next();

            // skip files inside a *.vst3 folder (the folder itself is scanned)
            if (it.filePath().contains(".vst3/", Qt::CaseInsensitive))
                continue;

            splash->showMessage("กำลังตรวจสอบ : " + it.fileName(), Qt::AlignBottom|Qt::AlignRight, QColor("#cfd3dc"));
            qApp->processEvents();

            QList<VSTNamePath> infos;

            if (!Utils::vstInfos(it.filePath(), &infos))
                continue;

            for (const VSTNamePath &info : infos)
                vstList[info.uniqueID] = info;
        }
    }

    synth->setVSTList(vstList);
}

#endif
