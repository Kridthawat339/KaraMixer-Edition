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
#include "Midi/KmSoundfont.h"
#include <QFontDatabase>

#include "BASSFX/VSTFX.h"
#include "version.h"
#include "Config.h"
#include "Utils.h"
#include "Theme.h"
#include "Widgets/DialogSkin.h"

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

    // Started by "Open config": give the previous instance time to release audio/MIDI devices.
    if (a.arguments().contains("--delay"))
        QThread::msleep(1500);

    QCoreApplication::setApplicationName(VER_PRODUCTNAME_STR);
    QCoreApplication::setApplicationVersion(VER_FILEVERSION_STR);
    QCoreApplication::setOrganizationName(VER_COMPANYNAME_STR);
    QCoreApplication::setOrganizationDomain(VER_COMPANYDOMAIN_STR);

    registerMetaType();

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

    splash->finish(mixer);

    delete splash;
    delete pixmap;

    // locked soundfonts made for another computer (or in the old format) were skipped: tell the user what to do
    if (!KmSf::foreignFiles().isEmpty()) {
        QStringList names;
        for (const QString &f : KmSf::foreignFiles())
            names << QFileInfo(f).fileName();
        QMessageBox box(QMessageBox::NoIcon, QString::fromUtf8("Soundfont ที่ล็อกไว้"),
                        QString::fromUtf8("ซาวด์ฟอนต์ต่อไปนี้ถูกล็อกสำหรับเครื่องอื่น (หรือเป็นไฟล์ล็อกรุ่นเก่า) จึงใช้กับเครื่องนี้ไม่ได้:\n\n")
                            + names.join("\n")
                            + QString::fromUtf8("\n\nต้องล็อกใหม่ด้วย Hardware ID ของเครื่องนี้: ") + KmSf::thisMachineId(),
                        QMessageBox::Ok, mixer);
        box.exec();
    }

    return a.exec();
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

    qRegisterMetaTypeStreamOperators<QList<QByteArray>>("QList<QByteArray>>");
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
    settings.beginReadArray("SynthSoundfontsVolume");
    for (int i=0; i<sfList.count(); i++)
    {
        settings.setArrayIndex(i);
        int volume = settings.value("SoundfontVolume", 100).toInt();

        splash->showMessage("กำลังโหลด : " + QFileInfo(sfList.at(i)).fileName(), Qt::AlignBottom|Qt::AlignRight, QColor("#cfd3dc"));
        qApp->processEvents();

        if (synth->addSoundfont(sfList.at(i)))
            synth->setSoundfontVolume(i, volume / 100.0f);
    }
    settings.endArray();


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
        if (sfDrumMap.count() == 0)
            sfDrumMap = synth->getDrumMapSfIndex(i);

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
    vstDirs << QDir::currentPath() + "/VST";
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
