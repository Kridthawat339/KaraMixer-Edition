#include "MainWindow.h"
#include "ui_MainWindow.h"

#include <QTime>
#include <QTimer>
#include <QCursor>
#include <QMenu>
#include <QActionGroup>
#include <QCloseEvent>
#include <QMessageBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QDataStream>
#include <QSaveFile>
#include <QProcess>
#include <QSettings>
#include <QWindow>

#include "Config.h"
#include "Theme.h"
#include "Utils.h"
#include "MedleyLoader.h"
#include "DrumPadsKey.h"
#include "SettingsDialog.h"
#include "Widgets/MixerAutoHide.h"
#include "Midi/MidiFile.h"
#include "Midi/HNKFile.h"
#include "Midi/AsioOutput.h"
#include "Dialogs/AboutDialog.h"
#include "Dialogs/MapSoundfontDialog.h"
#include "Dialogs/MapChannelDialog.h"
#include "Dialogs/BusDialog.h"
#include "Dialogs/SpeakerDialog.h"
#include "Dialogs/LockSoundfontDialog.h"
#include "Dialogs/RecordDialog.h"
#include <QToolTip>
#include <QCursor>
#include "Dialogs/MasteringDialog.h"
#ifndef __linux__
#include "BASSFX/VSTFX.h"
#include "Vst3/BassVst3.h"
#include <QScreen>
#include <QGuiApplication>
#endif
#include "Widgets/ProMenuBadge.h"
#include "Widgets/ProLicense.h"
#include "Dialogs/Equalizer31BandDialog.h"
#include "Dialogs/Chorus2Dialog.h"
#include "Widgets/MenuIcons.h"
#include "Dialogs/Reverb2Dialog.h"
#include "Dialogs/DeleteSongDialog.h"
#include "Dialogs/MedleyDialog.h"
#include "Remote/RemoteServer.h"
#include "Remote/RemoteDialog.h"

#ifndef __linux__
#include "Dialogs/VSTDirsDialog.h"
#endif


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{

    // List audio device and Init BASS
    {
        QMap<int, QString> dvs;
        int a, count=0;
        BASS_DEVICEINFO info;
        for (a=0; BASS_GetDeviceInfo(a, &info); a++)
        {
            #ifdef __linux__
            if (info.flags&BASS_DEVICE_ENABLED && BASS_Init(a, 44100, BASS_DEVICE_SPEAKERS, NULL, NULL)) { // device is enabled
                dvs[a] =  QString(info.name);
                count++; // count it
            }
            #else
            if (info.flags&BASS_DEVICE_ENABLED) { // device is enabled
                BASS_Init(a, 44100, BASS_DEVICE_SPEAKERS, NULL, NULL);
                dvs[a] =  QString(info.name);
                count++; // count it
            }
            #endif
        }
        MidiSynthesizer::audioDevices(dvs);
    }

    BASS_SetDevice(1);
    BASS_FX_GetVersion();

    auto concurentThreadsSupported = Utils::concurentThreadsSupported();
    float nVoices = (concurentThreadsSupported > 1) ? 500 : 256;

    BASS_SetConfig(BASS_CONFIG_BUFFER, 100);
    BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD, 10);
    BASS_SetConfig(BASS_CONFIG_MIDI_VOICES, nVoices);
    BASS_SetConfig(BASS_CONFIG_MIDI_COMPACT, true);
    // End Init BASS


    bgWidget = new Background(this);
    lyrWidget = new LyricsWidget(this);
    updateDetail = new Detail(this);

    ui->setupUi(this);

    #ifdef _WIN32
    taskbarButton = new QWinTaskbarButton();
    #endif

    settings = new QSettings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);

    // lang
    QString lang = settings->value("Language", "th").toString();
    if (lang == "en")
        setEngLang();

    QString ncn = settings->value("NCNPath", QDir::currentPath() + "/Songs/NCN").toString();
    QString hnk = settings->value("HNKPath", QDir::currentPath() + "/Songs/HNK").toString();
    QString kar = settings->value("KARPath", QDir::currentPath() + "/Songs/KAR").toString();

    db = new SongDatabase();
    db->setNcnPath(ncn);
    db->setHNKPath(hnk);
    db->setKarPath(kar);


    timer1 = new QTimer();
    timer2 = new QTimer();
    timer2->setSingleShot(true);

    positionTimer = new QTimer(this);
    positionTimer->setInterval(30);

    lyricsTimer = new QTimer(0);
    lyricsTimer->setInterval(30);

    detailTimer = new QTimer();
    detailTimer->setSingleShot(true);

    player = new MidiPlayer();

    locale = QLocale(QLocale::English, QLocale::UnitedStates);

    { // Channel Mixer
        bool lck = settings->value("ChMixLock", false).toBool();

        ui->chMix->setLock(lck);
        ui->chMix->setPlayer(player);

        if (lck) {
            ui->chMix->setLock(true);
            ui->chMix->show();
            ui->expandChMix->show();
        }
        else {
            ui->chMix->setLock(false);
            ui->chMix->hide();
            ui->expandChMix->hide();
        }

        connect(ui->chMix, SIGNAL(lockChanged(bool)), this, SLOT(onChMixLockChanged(bool)));
        connect(ui->chMix, SIGNAL(mouseLeaved()), this, SLOT(onChMixMouseLeaved()));
    }


    { // UI window size
        int sTime = settings->value("SearchTimeout", 5).toInt();
        int pTime = settings->value("PlaylistTimeout", 5).toInt();
        setSearchTimeout(sTime);
        setPlaylistTimeout(pTime);

        bool removeList  = settings->value("RemoveFromPlaylist", true).toBool();
        bool aPlayNext   = settings->value("AutoPlayNext", true).toBool();
        remove_playlist = removeList;
        auto_playnext = aPlayNext;

        int bg = settings->value("BackgroundType", 0).toInt();
        QString bgColor = settings->value("BackgroundColor", "#515151").toString();
        QString bgImg = settings->value("BackgroundImage", "").toString();
        bgWidget->setBackgroundType((Background::BackgroundType)bg);
        bgWidget->setBackgroundColor(bgColor);
        bgWidget->setBackgroundImage(bgImg);

        int w       = settings->value("WindowWidth", this->minimumWidth()).toInt();
        int h       = settings->value("WindowHeight", this->minimumHeight()).toInt();
        bool maximum= settings->value("WindowMaximized", false).toBool();
        bool full   = settings->value("WindowFullScreen", false).toBool();

        this->resize(w, h);
        // Mixer-only edition: this window is just the hidden back-end and must
        // never be shown (old saved maximized/fullscreen states are ignored).
        Q_UNUSED(maximum)
        Q_UNUSED(full)
    }


    { // Player
        // Mixer edition: output is always the SoundFont synthesizer
        // (an external MIDI out such as "Microsoft GS Wavetable" lost the sf2).
        int oPort   = -1;
        settings->setValue("MidiOut", -1);
        int iPort   = settings->value("MidiIn", -1).toInt();
        // MIDI-in is remembered by device name, because port numbers shift
        // depending on which program/virtual cable was started first.
        QString inName = settings->value("MidiInName", "").toString();
        if (!inName.isEmpty()) {
            int idx = MidiPlayer::midiInDevices().indexOf(inName);
            if (idx >= 0)
                iPort = idx;
        }
        int vl      = settings->value("MidiVolume", 50).toInt();
        bool lDrum  = settings->value("MidiLockDrum", false).toBool();
        bool lSnare = settings->value("MidiLockSnare", false).toBool();
        bool lKick  = settings->value("MidiLockKick", false).toBool();
        bool lBass  = settings->value("MidiLockBass", false).toBool();
        int synBuf  = settings->value("SynthBuffer", 100).toInt();
        if (!ProLicense::isUnlocked() && synBuf < 50)
            synBuf = 50;   // low latency (below 50 ms) is a PRO option

        BASS_SetConfig(BASS_CONFIG_BUFFER, synBuf);
        player->setMidiOut(oPort);
        player->setMidiIn(iPort);
        player->setVolume(vl);
        player->setMasterMute(settings->value("MasterMute", false).toBool());
        player->midiSynthesizer()->setIgnoreDrumPitch(settings->value("IgnoreDrumPitch", true).toBool());

        // Auto (re)connect the MIDI-in device by name, so the mixer works no
        // matter whether the karaoke program / virtual MIDI cable starts first.
        QTimer *midiInWatcher = new QTimer(this);
        midiInWatcher->setInterval(2000);
        connect(midiInWatcher, &QTimer::timeout, this, [this]() {
            QString name = settings->value("MidiInName", "").toString();
            if (name.isEmpty())
                return;
            int idx = MidiPlayer::midiInDevices().indexOf(name);
            if (idx < 0) {
                if (player->midiInPortNumber() != -1)
                    player->setMidiIn(-1);
            } else if (idx != player->midiInPortNumber()) {
                player->setMidiIn(idx);
            }
        });
        midiInWatcher->start();

        if (lDrum) {
            int ldNum = settings->value("MidiLockDrumNumber", 0).toInt();
            player->setLockDrum(true, ldNum);
        }
        if (lSnare) {
            int lsNum = settings->value("MidiLockSnareNumber", 38).toInt();
            player->setLockSnare(true, lsNum);
        }
        if (lKick) {
            int lkNum = settings->value("MidiLockKickNumber", 36).toInt();
            player->setLockKick(true, lkNum);
        }
        if (lBass) {
            int lbNum = settings->value("MidiLockBassNumber", 32).toInt();
            player->setLockBass(true, lbNum);
        }

        // Midi Channel Mapper
        QList<int> ports = settings->value("MidiChannelMapper").value<QList<int>>();
        if (ports.count() == 16) {
            for (int i=0; i<16; i++) {
                // Mixer edition: every channel always goes to the SoundFont synth
                Q_UNUSED(ports)
                player->setMapChannelOutput(i, -1);
            }
        }

        connect(player, SIGNAL(finished()), this, SLOT(onPlayerThreadFinished()));
        connect(player, SIGNAL(bpmChanged(int)), ui->rhmWidget, SLOT(setBpm(int)));

        connect(player, SIGNAL(nextMedleyStarted()), lyricsTimer, SLOT(stop()));
        connect(player, SIGNAL(nextMedleyStarted()), this, SLOT(onNextMedleyStarted()));
        connect(player, SIGNAL(nextMedleyStarted()), this, SLOT(switchMedleyLyrics()));
        connect(player, SIGNAL(nextMedleyStarted()), this, SLOT(switchMedleyLyrics2()));
        connect(player, SIGNAL(nextMedleyAfterStarted()), lyricsTimer, SLOT(start()));
        connect(player, SIGNAL(nextMedleyAfterStarted()), this, SLOT(onNextMedleyAfterStarted()));
    }


    { // Synth
        MidiSynthesizer *synth = player->midiSynthesizer();

        // Audio out
        int aout = settings->value("SynthDefaultDevice", 1).toInt();
        synth->setDefaultDevice(aout);

        // ASIO output (optional, -1 = normal Windows device)
        int asio = settings->value("AsioDevice", -1).toInt();
        if (asio >= AsioOutput::devices().count())
            asio = -1;
        if (AsioOutput::crashedLastStart()) {          // the driver hung / crashed the last time: start on the normal device
            asio = -1;
            settings->setValue("AsioDevice", -1);
        }
        AsioOutput::setPairMask(settings->value("AsioPairMask", 1).toUInt());
        AsioOutput::setSpeakerSplit(ProLicense::isUnlocked() && settings->value("AsioSpeakerSplit", false).toBool());   // PRO function
        AsioOutput::setBufferSize(settings->value("AsioBufferSamples", 0).toInt());
        synth->setAsioDevice(asio);

        // audio quality: the higher settings are PRO options (Free = 44.1 kHz, linear interpolation, 256 voices)
        {
            const bool pro = ProLicense::isUnlocked();
            synth->setVoices(pro ? settings->value("SynthVoices", 500).toInt() : 256);
            synth->setSampleRate(pro ? settings->value("SynthSampleRate", 44100).toInt() : 44100);
            synth->setSincInterpolation(pro ? settings->value("SynthSinc", true).toBool() : false);
            synth->setReverbStrong(settings->value("ReverbStrong", false).toBool());
        }

        // floating and fx
        bool useFloat = settings->value("SynthFloatPoint", true).toBool();
        bool useFX = settings->value("SynthUseFXRC", false).toBool();
        synth->setUsetFloattingPoint(useFloat);
        synth->setUseFXRC(useFX);

        // PRO mastering (limiter / chain / auto level): saved settings, only with a licence
        MasteringDialog::applySaved(synth);

        // Soundfonts and Soundfonts map
        // move to setup in main function (main.cpp)

        // Synth EQ -> move to SynthMixer
        // Synth chorus -> move to SynthMixer
        // Synth reverb -> move to SynthMixer

        // Create synth mixer
        synthMix = new SynthMixerDialog(nullptr, this);
    }


    { // Lyrics
        QString family  = settings->value("LyricsFamily", font().family()).toString();
        int size        = settings->value("LyricsSize", 40).toInt();
        int weight      = settings->value("LyricsWeight", 75).toInt();
        bool italic     = settings->value("LyricsItalic", false).toBool();
        bool strikeOut  = settings->value("LyricsStrikeOut", false).toBool();
        bool underline  = settings->value("LyricsUnderline", false).toBool();

        QString tColor  = settings->value("LyricsTextColor", "#f3f378").toString();
        QString tbColor = settings->value("LyricsTextBorderColor", "#000000").toString();
        QString toColor = settings->value("LyricsTextBorderOutColor", "#000000").toString();
        int     tbWidth = settings->value("LyricsTextBorderWidth", 3).toInt();
        int     toWidth = settings->value("LyricsTextBorderOutWidth", 2).toInt();

        QString cColor  = settings->value("LyricsCurColor", "#0000ff").toString();
        QString cbColor = settings->value("LyricsCurBorderColor", "#ffffff").toString();
        QString coColor = settings->value("LyricsCurBorderOutColor", "#000000").toString();
        int     cbWidth = settings->value("LyricsCurBorderWidth", 4).toInt();
        int     coWidth = settings->value("LyricsCurBorderOutWidth", 2).toInt();

        int     line1X  = settings->value("LyricsLine1X", 0).toInt();
        int     line2X  = settings->value("LyricsLine2X", 0).toInt();
        int     line1Y  = settings->value("LyricsLine1Y", 320).toInt();
        int     line2Y  = settings->value("LyricsLine2Y", 170).toInt();
        int     aTime   = settings->value("LyricsAnimationTime", 250).toInt();
        bool   autosize = settings->value("LyricsAutoFontSize", true).toBool();

        QFont f;
        f.setFamily(family);
        f.setPointSize(size);
        f.setWeight(weight);
        f.setItalic(italic);
        f.setStrikeOut(strikeOut);
        f.setUnderline(underline);

        lyrWidget->setTextFont(f);
        lyrWidget->setTextColor(QColor(tColor));
        lyrWidget->setTextBorderColor(QColor(tbColor));
        lyrWidget->setTextBorderOutColor(QColor(toColor));
        lyrWidget->setTextBorderWidth(tbWidth);
        lyrWidget->setTextBorderOutWidth(toWidth);

        lyrWidget->setCurColor(QColor(cColor));
        lyrWidget->setCurBorderColor(QColor(cbColor));
        lyrWidget->setCurBorderOutColor(QColor(coColor));
        lyrWidget->setCurBorderWidth(cbWidth);
        lyrWidget->setCurBorderOutWidth(coWidth);

        lyrWidget->setLine1Position(static_cast<LinePosition>(line1X));
        lyrWidget->setLine2Position(static_cast<LinePosition>(line2X));
        lyrWidget->setLine1Y(line1Y);
        lyrWidget->setLine2Y(line2Y);
        lyrWidget->setAnimationTime(aTime);
        lyrWidget->setAutoFontSize(autosize);
    }


    { // Init UI
        this->setWindowTitle(QApplication::applicationName() + "  " + QApplication::applicationVersion());

        Utils::LAST_OPEN_DIR = settings->value("LastOpenDir", QDir::homePath()).toString();

        updateDetail->hide();
        updateDetail->resize(250, 60);
        updateDetail->setText("กำลังปรับปรุงฐานข้อมูล");

        ui->detail->hide();
        ui->frameSearch->hide();
        ui->playlistWidget->hide();
        ui->songDetail->hide();
        ui->songMedley->hide();
        ui->sliderVolume->setValue(player->volume());

        // 249
        ui->playlistWidget->setMinimumHeight(305);
        ui->playlistWidget->setMaximumHeight(305);

        { // Playback control
            ui->btnPlay->setIconFiles(":/Icons/play-white", ":/Icons/play-blue");
            ui->btnPause->setIconFiles(":/Icons/pause-white", ":/Icons/pause-blue");
            ui->btnStop->setIconFiles(":/Icons/stop-white", ":/Icons/stop-blue");
            ui->btnPrevious->setIconFiles(":/Icons/previous-white", ":/Icons/previous-blue");
            ui->btnNext->setIconFiles(":/Icons/next-white", ":/Icons/next-blue");

            ui->btnVolumeMute->setIconFiles(":/Icons/volume-white", ":/Icons/volume-blue");
        }

        #ifdef __linux__
        { // UI Font in linux
            QString fontName = "TH Sarabun New Bold";
            QFont f = ui->lbSearch->font();
            f.setBold(true);
            f.setFamily(fontName);
            f.setPointSize(f.pointSize() - 12);

            ui->lbSearch->setFont(f);
            ui->lbArtist->setFont(f);
            ui->lbBtw->setFont(f);
            ui->lbId->setFont(f);
            ui->lbName->setFont(f);
            ui->lbTempoKey->setFont(f);
            ui->lbType->setFont(f);

            f.setPointSize(f.pointSize() - 4);
            ui->lbLyrics->setFont(f);
        }
        #endif

        connect(db, SIGNAL(started()), updateDetail, SLOT(show()));
        connect(db, SIGNAL(finished()), updateDetail, SLOT(hide()));
        connect(db, SIGNAL(updatePositionChanged(int)), this, SLOT(onDbUpdateChanged(int)));

        connect(timer1, SIGNAL(timeout()), this, SLOT(showCurrentTime()));
        connect(timer2, SIGNAL(timeout()), this, SLOT(hideUIFrame()));

        connect(positionTimer, SIGNAL(timeout()), this, SLOT(onPositiomTimerTimeOut()));
        connect(lyricsTimer, SIGNAL(timeout()), this, SLOT(onLyricsTimerTimeOut()));

        connect(detailTimer, SIGNAL(timeout()), this, SLOT(onDetailTimerTimeout()));

        connect(ui->btnPause, SIGNAL(clicked()), this, SLOT(pause()));
        connect(ui->btnStop, SIGNAL(clicked()), this, SLOT(stop()));
        connect(ui->btnNext, SIGNAL(clicked()), this, SLOT(playNext()));
        connect(ui->btnPrevious, SIGNAL(clicked()), this, SLOT(playPrevious()));
        connect(ui->sliderPosition, SIGNAL(sliderPressed()), this, SLOT(onSliderPositionPressed()));
        connect(ui->sliderPosition, SIGNAL(sliderReleased()), this, SLOT(onSliderPositionReleased()));
        connect(ui->sliderVolume, SIGNAL(valueChanged(int)), this, SLOT(onSliderVolumeValueChanged(int)));

        timer1->start(1000);
    }


    // Menu
    connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
}

MainWindow::~MainWindow()
{
    stop();


    delete synthMix;

    if (!skipSaveOnExit) {
        settings->setValue("LastOpenDir", Utils::LAST_OPEN_DIR);
        settings->setValue("MidiVolume", player->volume());
    }
    if (this->isFullScreen()) {
        settings->setValue("WindowFullScreen", true);
        settings->setValue("WindowMaximized", false);
    } else if (this->isMaximized()) {
        settings->setValue("WindowFullScreen", false);
        settings->setValue("WindowMaximized", true);
    } else {
        settings->setValue("WindowFullScreen", false);
        settings->setValue("WindowMaximized", false);
        settings->setValue("WindowWidth", this->width());
        settings->setValue("WindowHeight", this->height());
    }

    foreach (Song *s, playlist) {
        delete s;
    }

    if (medleyLoader != nullptr) {
        medleyLoader->wait();
    }

    delete player;

    delete detailTimer;

    delete lyricsTimer;
    delete positionTimer;
    delete timer2;
    delete timer1;

    delete db;
    delete settings;

    #ifdef _WIN32
    delete taskbarButton;
    #endif

    delete ui;

    delete updateDetail;
    delete lyrWidget;
    delete bgWidget;

    BASS_Free();
}

void MainWindow::play(int index, int position)
{
    if (player->isUseMedley() && player->isPlayerPlaying()) {
        loadNextMedley(playlist[index]);
        connect(medleyLoader, &MedleyLoader::finished, [this, index](){
            MidiSequencer *seq = player->midiSequencer();
            seq->setEndTick(seq->midiFile()->tickFromBar(seq->currentBar() + 1));
            playingIndex = index - 1;
        });
        return;
    }

    stop();

    if (index == -1 && playingSong.id() != "")
    {
        lyrWidget->reset();
        lyrWidget->show();
        if (secondLyr != nullptr)
        {
            secondLyr->reset();
            secondLyr->show();
        }
        if (position > 0)
        {
            lyrWidget->setSeekPositionCursor(position);
            if (secondLyr != nullptr)
                secondLyr->setSeekPositionCursor(position);
        }

        #ifdef _WIN32
        taskbarButton->progress()->resume();
        taskbarButton->progress()->show();
        #endif

        if (position > 0)
            player->setPositionTick(position);

        player->play();
        positionTimer->start();
        lyricsTimer->start();
        return;
    }

    Song *s = playlist[index];
    playingSong = *s;
    playingIndex = index;

    if (remove_playlist) {
        delete s; // delete playlist in "index"
        playlist.removeAt(index);
        ui->playlistWidget->removeRow(index);
        playingIndex = -1;
    }


    // NCN File
    if (playingSong.songType() == "NCN")
    {
        QString p = db->ncnPath() + playingSong.path();
        if (!player->load(p, true)) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไม่มีไฟล์ ") + p +
                                 tr("\nหรือไฟล์อาจเสียหายไม่สามารถอ่านได้"), QMessageBox::Ok);
            return;
        }

        QString curPath = db->getCurFilePath(p);
        if (curPath == "" || !QFile::exists(curPath)) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไม่มีไฟล์ Cursor รหัส ") + playingSong.id() +
                                 tr("\nหรือไฟล์อาจเสียหายไม่สามารถอ่านได้"), QMessageBox::Ok);
            return;
        }

        QString lyrPath = db->getLyrFilePath(p);
        if (lyrPath == "" || !QFile::exists(lyrPath)) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไม่มีไฟล์ Lyrics รหัส ") + playingSong.id() +
                                 tr("\nหรือไฟล์อาจเสียหายไม่สามารถอ่านได้"), QMessageBox::Ok);
            return;
        }

        lyrWidget->setLyrics(Utils::readLyrics(lyrPath),
            Utils::readCurFile(curPath, player->midiFile()->resorution()));

    }
    else if (playingSong.songType() == "HNK")
    {
        // HNK File
        QString p = db->hnkPath() + playingSong.path();
        if (!QFile::exists(p)) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไม่มีไฟล์ ") + p, QMessageBox::Ok);
            return;
        }

        QFile mid(TEMP_MIDI_DIR_PATH);
        if (mid.exists())
            mid.remove();

        mid.open(QFile::ReadWrite);
        mid.write(HNKFile::midData(p));
        mid.close();

        if (!player->load(TEMP_MIDI_DIR_PATH, true)) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไฟล์อาจเสียหายไม่สามารถอ่านได้"), QMessageBox::Ok);
            mid.remove();
            return;
        }

        mid.remove();

        lyrWidget->setLyrics(Utils::readLyrics(HNKFile::lyrData(p)),
            Utils::readCurFile(HNKFile::curData(p), player->midiFile()->resorution()));

    }
    else if (playingSong.songType() == "KAR")
    {
        // KAR file
        QString p = db->karPath() + playingSong.path();
        if (!QFile::exists(p)) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไม่มีไฟล์ ") + p, QMessageBox::Ok);
            return;
        }

        if (!player->load(p, false)) {
            QMessageBox::warning(this, tr("ไม่สามารถเล่นเพลงได้"),
                                 tr("ไฟล์อาจเสียหายไม่สามารถอ่านได้"), QMessageBox::Ok);
            return;
        }

        lyrWidget->setLyrics(player->midiFile()->lyrics(), player->midiFile()->lyricsCursor());
    }
    else
    {
        return;
    }

    if (secondLyr != nullptr)
        secondLyr->setLyrics(lyrWidget->lyrData(), lyrWidget->curData());

    onPlayerDurationTickChanged(player->durationTick());
    onPlayerDurationMSChanged(player->durationMs());

    #ifdef _WIN32
    taskbarButton->progress()->setValue(0);
    taskbarButton->progress()->setMaximum(player->durationTick());
    taskbarButton->progress()->resume();
    taskbarButton->progress()->show();
    #endif

    // RHM
    ui->rhmWidget->setBeat(MidiHelper::calculateBeats(player->midiFile()), player->beatCount());

    ui->frameSearch->hide();
    ui->playlistWidget->hide();
    ui->chMix->hide();
    ui->expandChMix->hide();

    // SongDetail
    ui->songDetail->setDetail(&playingSong);
    ui->songDetail->adjustSize();
    ui->songDetail->show();
    timer2->start(songDetail_timeout);

    player->setBpmSpeed(playingSong.bpmSpeed());
    player->setTranspose(playingSong.transpose());

    if (player->isUseMedley()) {
        player->midiSequencer()->setCutStartBar(playingSong.cutStartBar());
        player->midiSequencer()->setCutEndBar(playingSong.cutEndBar());

        lyrWidget->setSeekPositionCursor(player->positionTick());
        if (secondLyr != nullptr)
            secondLyr->setSeekPositionCursor(player->positionTick());
    }

    player->play();

    lyrWidget->show();

    if (secondLyr != nullptr)
        secondLyr->show();

    positionTimer->start();
    lyricsTimer->start();

    if (player->isUseMedley() && (playlist.size() > (playingIndex + 1))) {
        loadNextMedley(playlist[playingIndex + 1]);
    }
}

void MainWindow::pause()
{
    positionTimer->stop();
    lyricsTimer->stop();
    lyrWidget->stopAnimation();
    if (secondLyr != nullptr)
        secondLyr->stopAnimation();
    player->stop();

    #ifdef _WIN32
    taskbarButton->progress()->pause();
    #endif
}

void MainWindow::resume()
{
    player->play();
    positionTimer->start();
    lyricsTimer->start();

    #ifdef _WIN32
    taskbarButton->progress()->resume();
    #endif
}

void MainWindow::stop()
{
    positionTimer->stop();
    lyricsTimer->stop();
    player->stop(true);

    ui->sliderPosition->setValue(0);
    lyrWidget->hide();
    lyrWidget->reset();
    if (secondLyr != nullptr) {
        secondLyr->hide();
        secondLyr->reset();
    }
    ui->sliderPosition->setValue(0);
    onPlayerPositionMSChanged(0);

    ui->rhmWidget->reset();

    #ifdef _WIN32
    taskbarButton->progress()->hide();
    taskbarButton->progress()->setValue(0);
    #endif
}

void MainWindow::playNext()
{
    if (playlist.count() > 0 && playingIndex < playlist.count()-1) {
        play(playingIndex+1);
    } else {
        stop();
    }
}

void MainWindow::playPrevious()
{
    if (playingIndex > 0 && playlist.count() > 0) {
        play(playingIndex - 1);
    }
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    #ifdef _WIN32
    taskbarButton->setWindow(this->windowHandle());
    #endif

    if (firstShow)
    {
        firstShow = false;
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (ui->chMix->isLock())
        return;

    if (event->pos().y() < 70 && event->pos().x() < 700) {
        showChMix();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    bgWidget->resize(ui->centralWidget->size());
    lyrWidget->resize(ui->centralWidget->size());
    updateDetail->move(width() - 260, 70);
    emit resized(event->size());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    synthMix->close();

    QMessageBox::StandardButton resBtn = QMessageBox::question(
                                            this, tr("ออกจากโปรแกรม"),
                                            tr("ท่านต้องการออกจากโปรแกรม?"),
                                            QMessageBox::Yes|QMessageBox::No);
    if (resBtn != QMessageBox::Yes) {
        event->ignore();
    } else {
        if (secondLyr != nullptr) {
            secondMonitor->close();
            delete secondMonitor;
            secondLyr = nullptr;
        }
        event->accept();
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (ui->songMedley->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Escape: {
            ui->songMedley->hide();
            timer2->start(playlist_timeout);
            break;
        }
        case Qt::Key_Enter:
        case Qt::Key_Return: {
            Song *s = playlist[ui->playlistWidget->currentRow()];
            s->setCutStartBar(ui->songMedley->cutStartBar());
            s->setCutEndBar(ui->songMedley->cutEndBar());
            this->setFocus();
            ui->songMedley->hide();
            timer2->start(playlist_timeout);

            if (ui->playlistWidget->currentRow() == playingIndex + 1) {
                MidiSequencer *seq = player->midiSequencerTemp();
                seq->setCutStartBar(s->cutStartBar());
                seq->setCutEndBar(s->cutEndBar());
            }
            break;
        }
        default:
            break;
        }
        return;
    }

    if (event->modifiers() == Qt::ControlModifier) {
        switch (event->key()) {
            case Qt::Key_Up:
                if (ui->frameSearch->isVisible())
                {
                    preSetTranspose(db->currentSong()->transpose() + 1);
                }
                else if (ui->playlistWidget->isVisible())
                {
                    int i = ui->playlistWidget->currentRow();
                    if (i < 0)
                        break;
                    Song *s = playlist[i];
                    s->setTranspose(s->transpose() + 1);
                    ui->playlistWidget->updateDetail(i, s);
                    timer2->start(playlist_timeout);
                }
                else
                {
                    ui->sliderVolume->setValue(ui->sliderVolume->value() + 5);
                }
                break;
            case Qt::Key_Down:
                if (ui->frameSearch->isVisible())
                {
                    preSetTranspose(db->currentSong()->transpose() - 1);
                }
                else if (ui->playlistWidget->isVisible())
                {
                    int i = ui->playlistWidget->currentRow();
                    if (i < 0)
                        break;
                    Song *s = playlist[i];
                    s->setTranspose(s->transpose() - 1);
                    ui->playlistWidget->updateDetail(i, s);
                    timer2->start(playlist_timeout);
                }
                else
                {
                    ui->sliderVolume->setValue(ui->sliderVolume->value() - 5);
                }
                break;
            case Qt::Key_Right:
                if (ui->frameSearch->isVisible())
                {
                    preSetBpmSpeed(db->currentSong()->bpmSpeed() + 1);
                }
                else if (ui->playlistWidget->isVisible())
                {
                    int i = ui->playlistWidget->currentRow();
                    if (i < 0)
                        break;
                    Song *s = playlist[i];
                    s->setBpmSpeed(s->bpmSpeed() + 1);
                    ui->playlistWidget->updateDetail(i, s);
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_Left:
                if (ui->frameSearch->isVisible())
                {
                    preSetBpmSpeed(db->currentSong()->bpmSpeed() - 1);
                }
                else if (ui->playlistWidget->isVisible())
                {
                    int i = ui->playlistWidget->currentRow();
                    if (i < 0)
                        break;
                    Song *s = playlist[i];
                    s->setBpmSpeed(s->bpmSpeed() - 1);
                    ui->playlistWidget->updateDetail(i, s);
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_X:
                if (ui->frameSearch->isVisible()) {
                    setFrameSearch( db->search("") );
                    ui->lbSearch->setText("_");
                    ui->frameSearch->show();
                    timer2->start(search_timeout);
                }
                break;
            case Qt::Key_Equal:
                if (ui->playlistWidget->isVisible()) {
                    int i = ui->playlistWidget->currentRow();
                    if (i >= ui->playlistWidget->rowCount() -1)
                        break;

                    swapInPlaylist(i, i+1);
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_Minus:
                if (ui->playlistWidget->isVisible()) {
                    int i = ui->playlistWidget->currentRow();
                    if (i == 0)
                        break;

                    swapInPlaylist(i, i-1);
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_S:
                if (ui->playlistWidget->isVisible())
                {
                    timer2->stop();
                    savePlaylist();
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_O:
                if (ui->playlistWidget->isVisible())
                {
                    timer2->stop();
                    loadPlaylist();
                    timer2->start(playlist_timeout);
                }
                else
                    this->sendDrumPads(event, true);
                break;
        case Qt::Key_Delete:
            if (ui->frameSearch->isVisible()) {
                DeleteSongDialog dlg(this);
                dlg.setModal(true);
                dlg.adjustSize();
                dlg.setFixedSize(dlg.size());
                dlg.exec();

                if (dlg.removeConfirmed()) {
                    db->removeCurrentSong(dlg.removeFromStorage());
                    setFrameSearch( db->searchNext() );
                    if (ui->frameSearch->isVisible()) {
                        timer2->start(search_timeout);
                    }
                }
            }
            break;
            default:
                this->sendDrumPads(event, true);
                break;
        }
        return;
    }


    switch (event->key()) {
    case Qt::Key_Escape: {
        hideUIFrame();
        break;
    }
    case Qt::Key_F4: {
        if (player->isUseMedley()) {
            QMessageBox::StandardButton resBtn = QMessageBox::question(
                                                    this, tr("ปิดเมลเลย์"),
                                                    tr("ท่านต้องการปิดโหมดเมลเลย์?"),
                                                    QMessageBox::Yes|QMessageBox::No);
            if (resBtn == QMessageBox::Yes) {
                player->setUseMedley(false);
                ui->detail->setDetail(tr("เมดเลย์ "), tr("ปิด"));
                ui->detail->show();
                detailTimer->start(3000);
                if (this->width() < 1160 && ui->chMix->isVisible())
                    ui->lcdTime->hide();
            }
        } else {
            MedleyDialog dlg(this, player);
            dlg.adjustSize();
            dlg.setFixedSize(dlg.size());
            dlg.setModal(true);
            dlg.exec();

            if (player->isUseMedley()) {
                ui->detail->setDetail(tr("เมดเลย์ "), tr("เปิด"));
                ui->detail->show();
                detailTimer->start(3000);
                if (this->width() < 1160 && ui->chMix->isVisible())
                    ui->lcdTime->hide();
            }
        }
        break;
    }
    case Qt::Key_F5: {
        if (player->isPlayerPaused())
            resume();
        else
            play(-1);
        break;
    }
    case Qt::Key_F6: {
        pause();
        break;
    }
    case Qt::Key_F7: {
        stop();
        break;
    }
    case Qt::Key_F8: {
        playPrevious();
        break;
    }
    case Qt::Key_F9: {
        playNext();
        break;
    }
    case Qt::Key_Asterisk: {
        if (ui->chMix->isMuteVoice()) {
            ui->chMix->setMuteVoice(false);
            ui->detail->setDetail(tr("เสียงร้อง "), tr("เปิด"));
        } else {
            ui->chMix->setMuteVoice(true);
            ui->detail->setDetail(tr("เสียงร้อง "), tr("ปิด"));
        }
        ui->detail->show();
        detailTimer->start(3000);
        if (this->width() < 1160 && ui->chMix->isVisible()) {
            ui->lcdTime->hide();
        }
        break;
    }
    case Qt::Key_Insert: {
        if (ui->frameSearch->isVisible()) {
            preSetTranspose(db->currentSong()->transpose() + 1);
            break;
        }
        playingSong.setTranspose(player->transpose() + 1);
        player->setTranspose(player->transpose() + 1);
        int trp = player->transpose();
        QString t;
        if (trp > 0) t = "+" + QString::number(trp);
        else t = QString::number(trp);
        ui->songDetail->setDetail(&playingSong);
        ui->songDetail->adjustSize();
        ui->detail->setDetail(tr("คีย์เพลง "), t);
        ui->detail->show();
        detailTimer->start(3000);
        if (this->width() < 1160 && ui->chMix->isVisible()) {
            ui->lcdTime->hide();
        }
        break;
    }
    case Qt::Key_Delete: {
        if (ui->frameSearch->isVisible()) {
            preSetTranspose(db->currentSong()->transpose() - 1);
            break;
        }
        if (ui->playlistWidget->isVisible()) {
            int i = ui->playlistWidget->currentRow();
            if (i<0)
                break;
            removeFromPlaylist(i);
            showPlaylist();
            timer2->start(playlist_timeout);
            break;
        }
        playingSong.setTranspose(player->transpose() - 1);
        player->setTranspose(player->transpose() - 1);
        int trp = player->transpose();
        QString t;
        if (trp > 0) t = "+" + QString::number(trp);
        else t = QString::number(trp);
        ui->songDetail->setDetail(&playingSong);
        ui->songDetail->adjustSize();
        ui->detail->setDetail(tr("คีย์เพลง "), t);
        ui->detail->show();
        detailTimer->start(3000);
        if (this->width() < 1160 && ui->chMix->isVisible()) {
            ui->lcdTime->hide();
        }
        break;
    }
    case Qt::Key_PageUp:
        if (ui->frameSearch->isVisible()) {
            preSetBpmSpeed(db->currentSong()->bpmSpeed() + 1);
        } else if (ui->playlistWidget->isVisible()) {
            int i = ui->playlistWidget->currentRow();
            if (i < 0)
                break;
            Song *s = playlist[i];
            s->setBpmSpeed(s->bpmSpeed() + 1);
            ui->playlistWidget->updateDetail(i, s);
            timer2->start(playlist_timeout);
        } else {
            addBpmSpeed(1);
        }
        break;
    case Qt::Key_PageDown:
        if (ui->frameSearch->isVisible()) {
            preSetBpmSpeed(db->currentSong()->bpmSpeed() - 1);
        } else if (ui->playlistWidget->isVisible()) {
            int i = ui->playlistWidget->currentRow();
            if (i < 0)
                break;
            Song *s = playlist[i];
            s->setBpmSpeed(s->bpmSpeed() - 1);
            ui->playlistWidget->updateDetail(i, s);
            timer2->start(playlist_timeout);
        } else {
            addBpmSpeed(-1);
        }
        break;
    case Qt::Key_Home:
        if (playingSong.id() != "") {
            play(-1);
        }
        break;
    case Qt::Key_End:
        if (auto_playnext) {
            playNext();
        } else {
            stop();
        }
        break;
    case Qt::Key_Up:
        if (ui->playlistWidget->isVisible()) {
            if (ui->playlistWidget->currentRow() > 0 )
                ui->playlistWidget->setCurrentRow( ui->playlistWidget->currentRow() - 1 );
            timer2->start(playlist_timeout);
        } else {
            ui->frameSearch->hide();
            if (ui->playlistWidget->currentRow() == -1)
                ui->playlistWidget->setCurrentRow(0);
            showPlaylist();
            ui->chMix->hide();
            ui->expandChMix->hide();
            timer2->start(playlist_timeout);
        }
        break;
    case Qt::Key_Down:
        if (ui->playlistWidget->isVisible()) {
            if (ui->playlistWidget->currentRow() < ui->playlistWidget->rowCount() - 1 )
                ui->playlistWidget->setCurrentRow( ui->playlistWidget->currentRow() + 1 );
            timer2->start(playlist_timeout);
        } else {
            ui->frameSearch->hide();
            if (ui->playlistWidget->currentRow() == -1)
                ui->playlistWidget->setCurrentRow(0);
            showPlaylist();
            ui->chMix->hide();
            ui->expandChMix->hide();
            timer2->start(playlist_timeout);
        }
        break;
    case Qt::Key_Right:
        if (ui->frameSearch->isVisible()) {
            if (searchBoxChangeBpm) {
                searchBoxChangeBpm = false;
                ui->lbSearch->setText(db->searchText() + "_");
            }
            setFrameSearch( db->searchNext() );
            timer2->start(search_timeout);
        } else {
            searchBoxChangeBpm = false;
            db->currentSong()->setBpmSpeed(0);
            db->currentSong()->setTranspose(0);
            ui->playlistWidget->hide();
            if (ui->lbId->text() == "")
                setFrameSearch( db->search("") );
            ui->lbSearch->setText("_");
            showFrameSearch();
            ui->chMix->hide();
            ui->expandChMix->hide();
            timer2->start(search_timeout);
        }
        break;
    case Qt::Key_Left:
        if (ui->frameSearch->isVisible()) {
            if (searchBoxChangeBpm) {
                searchBoxChangeBpm = false;
                ui->lbSearch->setText(db->searchText() + "_");
            }
            setFrameSearch( db->searchPrevious() );
            timer2->start(search_timeout);
        } else {
            searchBoxChangeBpm = false;
            db->currentSong()->setBpmSpeed(0);
            db->currentSong()->setTranspose(0);
            ui->playlistWidget->hide();
            if (ui->lbId->text() == "")
                setFrameSearch( db->search("") );
            ui->lbSearch->setText("_");
            showFrameSearch();
            ui->chMix->hide();
            ui->expandChMix->hide();
            timer2->start(search_timeout);
        }
        break;
    case Qt::Key_Tab:
        if (ui->frameSearch->isVisible()) {
            QString s = ui->lbSearch->text();
            s = s.replace(s.length() - 1, 1, "");
            setFrameSearch( db->nextType(s) );
            timer2->start(search_timeout);
        }
        break;
    case Qt::Key_Backspace:
        if (ui->frameSearch->isVisible()) {
            if (ui->lbSearch->text() == "_") {
                timer2->start(search_timeout);
                break;
            }
            QString s = ui->lbSearch->text();
            s = s.replace(s.length() - 2, 2, "");
            setFrameSearch( db->search(s) );
            ui->lbSearch->setText(s + "_");
            timer2->start(search_timeout);
        }
        break;
    case Qt::Key_Enter:
    case Qt::Key_Return:
        if (ui->frameSearch->isVisible()) {
            Song *s = db->currentSong();
            addToPlaylist(s);

            if (auto_playnext && playlist.count() == 1 && player->isPlayerStopped()) {
                play(0);
            } else {
                hideUIFrame();
            }
        }
        if (ui->playlistWidget->isVisible() && ui->playlistWidget->rowCount() > 0) {
            ui->playlistWidget->hide();
            play(ui->playlistWidget->currentRow());
        }
        break;
    case Qt::Key_Space:
        if (player->isUseMedley() && ui->playlistWidget->isVisible()) {
            timer2->stop();
            Song *s = playlist[ui->playlistWidget->currentRow()];
            ui->songMedley->setup(s->cutStartBar(), s->cutEndBar());
            ui->songMedley->show();
            break;
        }

        if (ui->frameSearch->isHidden()) {
            showSongDetail();
            timer2->start(songDetail_timeout);
            break;
        }
    case Qt::Key_Plus:
        if (ui->playlistWidget->isVisible()) {
            int i = ui->playlistWidget->currentRow();
            if (i >= ui->playlistWidget->rowCount() -1)
                break;

            swapInPlaylist(i, i+1);
            timer2->start(playlist_timeout);
            break;
        }
    case Qt::Key_Minus:
        if (ui->playlistWidget->isVisible()) {
            int i = ui->playlistWidget->currentRow();
            if (i == 0)
                break;

            swapInPlaylist(i, i-1);
            timer2->start(playlist_timeout);
            break;
        }
    default:
        if (searchBoxChangeBpm) {
            ui->lbSearch->setText("_");
            searchBoxChangeBpm = false;
        }
        if (ui->frameSearch->isVisible()) {
            QString s = ui->lbSearch->text();
            s = s.replace(s.length() - 1, 1, "");
            ui->lbSearch->setText(s + event->text() + "_");
            setFrameSearch( db->search(s + event->text()) );
            timer2->start(search_timeout);
        } else {
            db->setSearchType(SearchType::ByAll);
            ui->lbSearch->setText(event->text() + "_");
            setFrameSearch( db->search(event->text() ));
            showFrameSearch();
            ui->chMix->hide();
            ui->expandChMix->hide();
            timer2->start(search_timeout);
        }
        break;
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (event->modifiers() == Qt::ControlModifier) {
        this->sendDrumPads(event, false);
    }
}

void MainWindow::loadNextMedley(Song *song)
{
    if ((medleyLoader != nullptr) && medleyLoader->isRunning())
        medleyLoader->wait();

    medleyLoader = new MedleyLoader(this, song, db, player, lyrWidget, secondLyr);
    connect(medleyLoader, &MedleyLoader::finished, [this](){
        medleyLoader->deleteLater();
        medleyLoader = nullptr;
    });
    medleyLoader->start();
}

void MainWindow::addToPlaylist(Song *song)
{
    Song *songToAdd = new Song();
    *songToAdd = *song;

    if (player->isUseMedley()) {
        int medleySpeed = player->medleyBPM() - songToAdd->tempo();
        songToAdd->setBpmSpeed(songToAdd->bpmSpeed() + medleySpeed);
    }

    playlist.append(songToAdd);
    ui->playlistWidget->addSong(songToAdd);

    if (player->isUseMedley() && !player->isPlayerStopped() && ((playlist.size() - 1 - playingIndex) == 1)) {
        loadNextMedley(songToAdd);
    }
}

void MainWindow::removeFromPlaylist(int index)
{
    delete playlist.takeAt(index);
    ui->playlistWidget->removeRow(index);

    if (playlist.size() == 0)
        player->unloadNextMedley();

    if (player->isUseMedley() && (index == playingIndex + 1) && (playlist.size() > playingIndex + 1))
        loadNextMedley(playlist[playingIndex + 1]);
}

void MainWindow::swapInPlaylist(int index, int toIndex)
{
    playlist.swap(index, toIndex);
    ui->playlistWidget->swapRow(index, toIndex);
    ui->playlistWidget->setCurrentRow(toIndex);

    bool isNextSong = (index == playingIndex + 1) || (toIndex == playingIndex + 1);

    if (player->isUseMedley() && isNextSong && (playlist.size() > playingIndex + 1)) {
        loadNextMedley(playlist[playingIndex + 1]);
    }
}

void MainWindow::updateShutdownRequest()
{
    qApp->quit();
}

void MainWindow::showFrameSearch()
{
    ui->chMix->hide();
    ui->expandChMix->hide();
    ui->songDetail->hide();
    ui->playlistWidget->hide();

    ui->frameSearch->show();
}

void MainWindow::showPlaylist()
{
    ui->chMix->hide();
    ui->expandChMix->hide();
    ui->songDetail->hide();
    ui->frameSearch->hide();

    ui->playlistWidget->show();
}

void MainWindow::showSongDetail()
{
    ui->chMix->hide();
    ui->expandChMix->hide();
    ui->frameSearch->hide();
    ui->playlistWidget->hide();

    ui->songDetail->show();
}


void MainWindow::showChMix()
{
    if (ui->playlistWidget->isVisible()
            || ui->frameSearch->isVisible()
            || ui->songDetail->isVisible())
    {
        return;
    }

    ui->chMix->show();
    ui->expandChMix->show();

    if (ui->detail->isVisible() && this->width() < 1160) {
        ui->lcdTime->hide();
    }
}

void MainWindow::onChMixMouseLeaved()
{
    if (ui->chMix->isLock())
        return;

    ui->chMix->hide();
    ui->expandChMix->hide();
}

void MainWindow::onChMixLockChanged(bool lock)
{
    if (lock) {
        settings->setValue("ChMixLock", true);
    } else {
        settings->setValue("ChMixLock", false);
    }
}

void MainWindow::hideUIFrame()
{
    ui->frameSearch->hide();
    ui->playlistWidget->hide();
    ui->songDetail->hide();

    if (ui->chMix->isLock()) {
        ui->chMix->show();
        ui->expandChMix->show();
    }
}

void MainWindow::showCurrentTime()
{
    QTime time = QTime::currentTime();
    QString currentTime = locale.toString(time, "hh:mm:ss");
    ui->lcdTime->display(currentTime);
}

void MainWindow::setFrameSearch(Song *s)
{
    ui->lbId->setText(s->id());
    ui->lbName->setText(" " + s->name());
    ui->lbBtw->setText("-");
    ui->lbArtist->setText(s->artist());
    if (s->key() != "")
        ui->lbTempoKey->setText(" (" + QString::number(s->tempo()) + "-" + s->key() + ")");
    else
        ui->lbTempoKey->setText(" (" + QString::number(s->tempo()) + ")");
    ui->lbType->setText("[" + s->songType() + "]");
    ui->lbLyrics->setText(s->lyrics().replace("\r\n", " "));

    if (s->artist().length() == 0) {
        ui->lbBtw->hide();
    }
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    showContextMenuAt(mapToGlobal(pos));
}

static const char *CFG_APP_PREFIX   = "App/";
static const char *CFG_SYNTH_PREFIX = "Synth/";

// New .kmcfg format: the two settings files stored byte for byte. Copying key by key through QVariant changed some
// values (lists written as text, "a,b,c" strings turned into string lists ...) and the sound came back wrong.
static const quint32 CFG_RAW_MAGIC = 0x4B4D4352;   // "KMCR"

static QByteArray readWholeFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

static bool writeWholeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    return f.commit();
}

void MainWindow::saveConfigFile()
{
    // make sure the file on disk holds the current mixer state first
    synthMix->settingValues();
    settings->sync();

    QDir().mkpath(Config::CONFIG_DIR_PATH);   // dialog opens in the config folder itself
    QString file = QFileDialog::getSaveFileName(synthMix, tr("บันทึกคอนฟิก"),
                        QDir(Config::CONFIG_DIR_PATH).absoluteFilePath("KaraMixer.kmcfg"),
                        tr("KaraMixer config (*.kmcfg)"), nullptr,
                        QFileDialog::DontConfirmOverwrite);   // no overwrite prompt (it plays a warning sound over the music)
    if (file.isEmpty())
        return;
    if (!file.endsWith(".kmcfg", Qt::CaseInsensitive))
        file += ".kmcfg";

    if (QFileInfo::exists(file)) {   // ask before overwriting, with NoIcon so Windows plays no sound over the music
        QMessageBox box(QMessageBox::NoIcon, tr("บันทึกคอนฟิก"),
                        tr("มีไฟล์นี้อยู่แล้ว ต้องการเขียนทับหรือไม่?\n") + file,
                        QMessageBox::Yes | QMessageBox::No, synthMix);
        box.setDefaultButton(QMessageBox::No);
        if (box.exec() != QMessageBox::Yes)
            return;
    }

    {
        QSettings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat).sync();
        QSettings(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat).sync();
        QByteArray blob;
        QDataStream ds(&blob, QIODevice::WriteOnly);
        ds.setVersion(QDataStream::Qt_5_12);
        ds << CFG_RAW_MAGIC << quint32(1)
           << readWholeFile(Config::CONFIG_APP_FILE_PATH)
           << readWholeFile(Config::CONFIG_SYNTH_FILE_PATH);
        if (!writeWholeFile(file, blob)) {
            QMessageBox::warning(synthMix, tr("บันทึกคอนฟิก"), tr("บันทึกไฟล์ไม่สำเร็จ\n") + file);
            return;
        }
    }

    // silent confirmation: a message box would play the system sound in the middle of the music
    QToolTip::showText(QCursor::pos(), tr("บันทึกคอนฟิกเรียบร้อยแล้ว\n") + file, synthMix, QRect(), 4000);
}

void MainWindow::openConfigFile()
{
    QString file = QFileDialog::getOpenFileName(synthMix, tr("เปิดคอนฟิก"),
                        Config::CONFIG_DIR_PATH, tr("KaraMixer config (*.kmcfg)"));
    if (file.isEmpty())
        return;

    // new format (raw copy of both settings files)?
    QByteArray rawApp, rawSyn;
    bool isRaw = false;
    {
        const QByteArray all = readWholeFile(file);
        QDataStream ds(all);
        ds.setVersion(QDataStream::Qt_5_12);
        quint32 magic = 0, ver = 0;
        ds >> magic >> ver;
        if (ds.status() == QDataStream::Ok && magic == CFG_RAW_MAGIC) {
            ds >> rawApp >> rawSyn;
            if (ds.status() != QDataStream::Ok) {
                QMessageBox::warning(synthMix, tr("เปิดคอนฟิก"), tr("ไฟล์คอนฟิกเสียหาย"));
                return;
            }
            isRaw = true;
        }
    }

    QSettings in(file, QSettings::IniFormat);
    QStringList keys = isRaw ? QStringList() : in.allKeys();

    bool valid = isRaw;
    for (const QString &k : keys) {
        if (k.startsWith(CFG_APP_PREFIX) || k.startsWith(CFG_SYNTH_PREFIX)) {
            valid = true;
            break;
        }
    }
    if (!valid) {
        QMessageBox::warning(synthMix, tr("เปิดคอนฟิก"), tr("ไฟล์นี้ไม่ใช่ไฟล์คอนฟิกของ KaraMixer Edition"));
        return;
    }

    if (QMessageBox::question(synthMix, tr("เปิดคอนฟิก"),
            tr("โปรแกรมจะโหลดคอนฟิกนี้แทนค่าปัจจุบัน แล้วเปิดโปรแกรมใหม่อัตโนมัติ\nต้องการดำเนินการต่อ?"),
            QMessageBox::Yes|QMessageBox::No) != QMessageBox::Yes)
        return;

    // Do not let the exit routine overwrite what we are about to import.
    skipSaveOnExit = true;
    synthMix->setSkipSave(true);

    if (isRaw) {
        if (!writeWholeFile(Config::CONFIG_APP_FILE_PATH, rawApp) ||
            !writeWholeFile(Config::CONFIG_SYNTH_FILE_PATH, rawSyn)) {
            skipSaveOnExit = false;
            synthMix->setSkipSave(false);
            QMessageBox::warning(synthMix, tr("เปิดคอนฟิก"), tr("เขียนไฟล์คอนฟิกไม่สำเร็จ"));
            return;
        }
    } else {
        QSettings app(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        QSettings syn(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
        app.clear();
        syn.clear();

        for (const QString &k : keys) {
            if (k.startsWith(CFG_APP_PREFIX))
                app.setValue(k.mid(QString(CFG_APP_PREFIX).length()), in.value(k));
            else if (k.startsWith(CFG_SYNTH_PREFIX))
                syn.setValue(k.mid(QString(CFG_SYNTH_PREFIX).length()), in.value(k));
        }
        app.sync();
        syn.sync();
    }

    // the files on disk are the new config: apply it to the running program (the music keeps playing)
    skipSaveOnExit = false;
    synthMix->setSkipSave(false);
    applyConfigLive();
    QToolTip::showText(QCursor::pos(), tr("โหลดคอนฟิกเรียบร้อยแล้ว (ไม่ต้องรีสตาร์ท)"), synthMix, QRect(), 4000);
}

void MainWindow::applyConfigLive()
{
    MidiSynthesizer *synth = player->midiSynthesizer();
    synthMix->setSkipSave(true);          // no auto-save of the old state over the new files while this runs

    settings->sync();                     // MainWindow's own QSettings re-reads the replaced file
    QSettings app(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    app.sync();

    // ---- 1. audio engine options (the setters rebuild the audio only when the value really changes)
    {
        const bool pro = ProLicense::isUnlocked();
        int synBuf = app.value("SynthBuffer", 100).toInt();
        if (!pro && synBuf < 50)
            synBuf = 50;
        BASS_SetConfig(BASS_CONFIG_BUFFER, synBuf);

        synth->setVoices(pro ? app.value("SynthVoices", 500).toInt() : 256);
        synth->setSincInterpolation(pro ? app.value("SynthSinc", true).toBool() : false);
        synth->setReverbStrong(app.value("ReverbStrong", false).toBool());

        int aout = app.value("SynthDefaultDevice", 1).toInt();
        int asio = app.value("AsioDevice", -1).toInt();
        if (asio >= AsioOutput::devices().count())
            asio = -1;
        if (asio != synth->asioDevice())
            synth->setAsioDeviceKeepState(asio);
        if (asio < 0 && aout != synth->defaultDevice())
            synth->setDefaultDevice(aout);

        synth->setSampleRate(pro ? app.value("SynthSampleRate", 44100).toInt() : 44100);
        synth->setUsetFloattingPoint(app.value("SynthFloatPoint", true).toBool());
        synth->setUseFXRC(app.value("SynthUseFXRC", false).toBool());
    }

    // ---- 2. soundfonts (list, volumes, preset maps)
    {
        synth->setLoadAllSoundfont(app.value("SynthSoundfontsLoadAll", false).toBool());

        const QStringList want = app.value("SynthSoundfonts", QStringList()).toStringList();
        if (synth->soundfontFiles() != want) {
            int guard = 512;
            while (guard-- > 0 && !synth->soundfontFiles().isEmpty())
                synth->removeSoundfont(0);
            for (const QString &f : want)
                synth->addSoundfont(f);
        }
        app.beginReadArray("SynthSoundfontsVolume");
        for (int i = 0; i < want.count(); i++) {
            app.setArrayIndex(i);
            synth->setSoundfontVolume(i, app.value("SoundfontVolume", 100).toInt() / 100.0f);
        }
        app.endArray();

        for (int i = 0; i < SF_PRESET_COUNT; i++) {
            QString sfKey = "SynthSoundfontsMap";
            QString drKey = "SynthSoundfontsDrumMap";
            if (i > 0) {
                sfKey += QString::number(i);
                drKey += QString::number(i);
            }
            QList<int> sfMap     = app.value(sfKey).value<QList<int>>();
            QList<int> sfDrumMap = app.value(drKey).value<QList<int>>();
            if (sfMap.count() == 0)
                sfMap = synth->getMapSoundfontIndex(i);
            if (sfDrumMap.count() == 0)
                sfDrumMap = synth->getDrumMapSfIndex(i);
            synth->setMapSoundfontIndex(i, sfMap, sfDrumMap);
        }
    }

    #ifndef __linux__
    // ---- 3. VSTi instruments
    {
        QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
        st.sync();
        st.beginReadArray("VSTiGroup");
        for (int i = 0; i < synth->HANDLE_VSTI_COUNT; i++) {
            st.setArrayIndex(i);
            const QString filePath = st.value("VstiFilePath", "").toString();
            const int program = st.value("VstiPrograms", 0).toInt();
            const QList<float> params = st.value("VstiParams").value<QList<float>>();
            const QByteArray chunk = st.value("VstiChunk", QByteArray()).toByteArray();

            if (filePath.isEmpty()) {
                if (!synth->vstiFile(i).isEmpty())
                    synth->removeVSTiFile(i);
                continue;
            }
            const DWORD vsti = synth->setVSTiFile(i, filePath);   // same file = nothing is reloaded
            if (vsti == 0)
                continue;
            if (chunk.length() > 0) {
                bv::SetChunk(vsti, false, chunk.constData(), chunk.length());
            } else {
                bv::SetProgram(vsti, program);
                FX::setVSTParams(vsti, params);
            }
        }
        st.endArray();
    }
    #endif

    // ---- 4. player options (drum locks)
    {
        auto lock = [&](const char *key, const char *numKey, int def, void (MidiPlayer::*fn)(bool, int)) {
            const bool on = app.value(key, false).toBool();
            (player->*fn)(on, on ? app.value(numKey, def).toInt() : 0);
        };
        lock("MidiLockDrum",  "MidiLockDrumNumber",  0,  &MidiPlayer::setLockDrum);
        lock("MidiLockSnare", "MidiLockSnareNumber", 38, &MidiPlayer::setLockSnare);
        lock("MidiLockKick",  "MidiLockKickNumber",  36, &MidiPlayer::setLockKick);
        lock("MidiLockBass",  "MidiLockBassNumber",  32, &MidiPlayer::setLockBass);
    }

    // ---- 5. the mixer: strips, effects, routing, master volume ...
    synthMix->reloadFromSettings();

    // ---- 6. PRO mastering
    MasteringDialog::applySaved(synth);

    ui->sliderVolume->setValue(player->volume());
    synthMix->setSkipSave(false);
}

void MainWindow::setMixerAutoHide(bool on, double delaySec, const QString &program)
{
    if (!autoHide)
        autoHide = new MixerAutoHide(synthMix, this);
    autoHide->configure(on, delaySec, program);
}

QWidget* MainWindow::dialogParent()
{
    // The mixer is the only visible window in this edition.
    return synthMix;
}

void MainWindow::showContextMenuAt(const QPoint &globalPos)
{
    QMenu menu(tr("Context menu"), synthMix);
    menu.setMinimumWidth(230);
    QAction *lockSfAction = nullptr;
    QAction *recordAction = nullptr;
    QAction *masteringAction = nullptr;

    QAction actionSettings(tr("ตั้งค่า..."), this);
    QAction actionMappChanel(tr("แยกช่องสัญญาณ..."), this);
    QAction actionShowEqDlg(tr("อีควอไลเซอร์"), this);
    QAction actionShowChorusDlg(tr("เอฟเฟ็กต์เสียงประสาน"), this);
    QAction actionShowReverbDlg(tr("เอฟเฟ็กต์เสียงก้อง"), this);
    QAction actionMapSF(tr("ตารางเลือกใช้ซาวด์ฟ้อนท์..."), this);
    QAction actionAbout(tr("เกี่ยวกับ..."), this);
    QAction actionAboutQt(tr("เกี่ยวกับ Qt..."), this);
    QAction actionExit(tr("ออกจากโปรแกรม"), this);

    connect(&actionSettings, SIGNAL(triggered()), this, SLOT(showSettingsDialog()));
    connect(&actionMappChanel, SIGNAL(triggered()), this, SLOT(showMapMidiChannelDialog()));
    connect(&actionShowEqDlg, SIGNAL(triggered()), this, SLOT(showEqDialog()));
    connect(&actionShowReverbDlg, SIGNAL(triggered()), this, SLOT(showReverbDialog()));
    connect(&actionShowChorusDlg, SIGNAL(triggered()), this, SLOT(showChorusDialog()));
    connect(&actionMapSF, SIGNAL(triggered()), this, SLOT(showMapSFDialog()));
    connect(&actionAbout, SIGNAL(triggered()), this, SLOT(showAboutDialog()));
    connect(&actionAboutQt, SIGNAL(triggered()), this, SLOT(showAboutQtDialog()));
    connect(&actionExit, SIGNAL(triggered()), synthMix, SLOT(close()));

    menu.addAction(&actionSettings);
    menu.addSeparator();
    menu.addAction(&actionMappChanel);
    menu.addSeparator();

    { // mixer tools (all former Menu-button items live here now)
        actionSettings.setIcon(MenuIcons::make(MenuIcons::Settings));
        actionMappChanel.setIcon(MenuIcons::make(MenuIcons::Channel));
        actionMapSF.setIcon(MenuIcons::make(MenuIcons::Soundfont));
        actionAbout.setIcon(MenuIcons::make(MenuIcons::About));
        actionExit.setIcon(MenuIcons::make(MenuIcons::Exit));
        MidiSynthesizer *syn = player->midiSynthesizer();

        QAction *act = menu.addAction(MenuIcons::make(MenuIcons::Bus), tr("บัสกรุ๊ป..."));
        connect(act, SIGNAL(triggered()), this, SLOT(showBusGroupDialog()));

        act = menu.addAction(MenuIcons::make(MenuIcons::Speaker), tr("แยกอุปกรณ์เสียง/ลำโพง..."));
        connect(act, SIGNAL(triggered()), this, SLOT(showSpeakerDialog()));

        act = menu.addAction(MenuIcons::make(MenuIcons::Soundfont), tr("Lock Soundfont..."));
        lockSfAction = act;   // gets a coloured "Pro" badge right before the menu opens
        connect(act, &QAction::triggered, this, [this]() {
            if (!ProLicense::ensureUnlocked(dialogParent()))   // PRO only: shows the hardware ID and asks for the licence
                return;
            LockSoundfontDialog dlg(dialogParent(), midiPlayer()->midiSynthesizer());
            dlg.exec();
        });

        act = menu.addAction(MenuIcons::make(MenuIcons::Save), tr("Record Output..."));
        recordAction = act;   // PRO: record the main output to a WAV file
        connect(act, &QAction::triggered, this, [this]() {
            if (!ProLicense::ensureUnlocked(dialogParent()))
                return;
            RecordDialog dlg(dialogParent(), midiPlayer()->midiSynthesizer());
            dlg.exec();
        });

        act = menu.addAction(MenuIcons::make(MenuIcons::Meter), tr("Mastering (Limiter / LUFS / Chain)..."));
        masteringAction = act;   // PRO: master limiter, mastering chain, loudness meter + auto level
        connect(act, &QAction::triggered, this, [this]() {
            if (!ProLicense::ensureUnlocked(dialogParent()))
                return;
            MasteringDialog dlg(dialogParent(), midiPlayer()->midiSynthesizer());
            dlg.exec();
        });

        #ifndef __linux__
        act = menu.addAction(MenuIcons::make(MenuIcons::Vst), tr("จัดการ VST && VSTi..."));
        connect(act, SIGNAL(triggered()), this, SLOT(showVSTDirDialog()));
        #endif

        act = menu.addAction(MenuIcons::make(MenuIcons::Meter), tr("ตั้งค่า LED Meter..."));
        connect(act, &QAction::triggered, this, [this]() {
            QMetaObject::invokeMethod(synthMix, "showVuDlg");
        });

        act = menu.addAction(MenuIcons::make(MenuIcons::Phone), tr("ควบคุมผ่านมือถือ (QR Code)..."));
        connect(act, SIGNAL(triggered()), this, SLOT(showRemoteDialog()));

        menu.addSeparator();

        // effect pictograms: neon green when the effect is on, dimmed when off
        auto fxIcon = [](MenuIcons::Kind k, bool on) {
            return MenuIcons::make(k, on ? MenuIcons::onColor() : MenuIcons::dimColor());
        };
        actionShowEqDlg.setIcon(fxIcon(MenuIcons::Eq, syn->equalizer31BandFXs()[0]->isOn()));
        actionShowChorusDlg.setIcon(fxIcon(MenuIcons::Chorus, syn->chorusFXs()[0]->isOn()));
        actionShowReverbDlg.setIcon(fxIcon(MenuIcons::Reverb, syn->reverbFXs()[0]->isOn()));
        menu.addAction(&actionShowEqDlg);
        menu.addAction(&actionShowChorusDlg);
        menu.addAction(&actionShowReverbDlg);
        menu.addAction(&actionMapSF);
        menu.addSeparator();

        act = menu.addAction(MenuIcons::make(MenuIcons::Reset), tr("รีเซ็ต"));
        connect(act, &QAction::triggered, this, [this]() {
            QMetaObject::invokeMethod(synthMix, "resetChannel");
        });

        menu.addSeparator();

        // the one and only "On Top" switch
        QAction *topAct = menu.addAction(tr("อยู่บนสุดตลอดเวลา (On Top)"));
        topAct->setCheckable(true);
        topAct->setChecked(synthMix->isStaysOnTop());
        connect(topAct, &QAction::triggered, this, [this](bool on) {
            QMetaObject::invokeMethod(synthMix, "setStaysOnTop", Q_ARG(bool, on));
        });

        QAction *drumAct = menu.addAction(tr("ตัดคำสั่งจูนเสียงกลอง/สแนร์ (ช่อง 10)"));
        drumAct->setCheckable(true);
        drumAct->setChecked(syn->isIgnoreDrumPitch());
        connect(drumAct, &QAction::triggered, this, [this](bool on) {
            player->midiSynthesizer()->setIgnoreDrumPitch(on);
            settings->setValue("IgnoreDrumPitch", on);
        });

        menu.addSeparator();

        QAction *saveCfg = menu.addAction(MenuIcons::make(MenuIcons::Save), tr("บันทึกคอนฟิก (Save config)..."));
        connect(saveCfg, &QAction::triggered, this, [this]() { saveConfigFile(); });
        QAction *openCfg = menu.addAction(MenuIcons::make(MenuIcons::Open), tr("เปิดคอนฟิก (Open config)..."));
        connect(openCfg, &QAction::triggered, this, [this]() { openConfigFile(); });

        menu.addSeparator();
    }

    { // lang menu
        QMenu *m = menu.addMenu(MenuIcons::make(MenuIcons::Language), tr("Language"));

        QAction *act = m->addAction("ไทย");
        act->setCheckable(true);
        act->setChecked(currentLang == "th");
        connect(act, SIGNAL(triggered()), this, SLOT(setThaiLang()));

        act = m->addAction("English");
        act->setCheckable(true);
        act->setChecked(currentLang == "en");
        connect(act, SIGNAL(triggered()), this, SLOT(setEngLang()));

        menu.addSeparator();
    }

    { // theme menu
        QMenu *m = menu.addMenu(MenuIcons::make(MenuIcons::Theme), tr("ธีม / Theme"));
        QActionGroup *grp = new QActionGroup(m);
        QAction *aSys   = m->addAction(tr("ตามธีม Windows"));
        QAction *aDark  = m->addAction(tr("มืด (Dark)"));
        QAction *aLight = m->addAction(tr("สว่าง (Light)"));
        QAction *aOrange = m->addAction(tr("ดาร์กส้ม (Dark Orange)"));
        QList<QAction*> acts;
        acts << aSys << aDark << aLight << aOrange;
        for (int i = 0; i < acts.count(); i++) {
            acts[i]->setCheckable(true);
            grp->addAction(acts[i]);
        }
        aSys->setChecked(Theme::mode() == Theme::System);
        aDark->setChecked(Theme::mode() == Theme::Dark);
        aLight->setChecked(Theme::mode() == Theme::Light);
        aOrange->setChecked(Theme::mode() == Theme::DarkOrange);
        connect(aSys,   &QAction::triggered, this, []() { Theme::setMode(Theme::System); });
        connect(aDark,  &QAction::triggered, this, []() { Theme::setMode(Theme::Dark); });
        connect(aLight, &QAction::triggered, this, []() { Theme::setMode(Theme::Light); });
        connect(aOrange, &QAction::triggered, this, []() { Theme::setMode(Theme::DarkOrange); });

        menu.addSeparator();
    }

    menu.addAction(&actionAbout);
    menu.addAction(&actionAboutQt);
    menu.addAction(&actionExit);

    // The menu closes by itself when the mouse leaves it (sub-menus count as part of it)
    QTimer leaveTimer;
    leaveTimer.setInterval(120);
    bool entered = false;
    int outsideMs = 0;
    connect(&leaveTimer, &QTimer::timeout, &menu, [&]() {
        QRect area;
        const QWidgetList tops = QApplication::topLevelWidgets();
        for (QWidget *w : tops) {
            if (w->isVisible() && qobject_cast<QMenu *>(w))
                area = area.united(w->frameGeometry());
        }
        if (area.isNull())
            return;
        const QPoint pos = QCursor::pos();
        if (area.contains(pos)) {
            entered = true;
            outsideMs = 0;
            return;
        }
        // not entered yet: leave time to travel from the Menu button into the menu
        if (!entered && area.adjusted(-40, -40, 40, 40).contains(pos)) {
            outsideMs = 0;
            return;
        }
        outsideMs += leaveTimer.interval();
        if (outsideMs >= (entered ? 350 : 1500)) {
            if (QWidget *p = QApplication::activePopupWidget())
                p->close();     // innermost sub-menu first, then the menu itself on the next tick
        }
    });
    leaveTimer.start();

    if (lockSfAction)
        ProMenuBadge::attach(&menu, lockSfAction);
    if (recordAction)
        ProMenuBadge::attach(&menu, recordAction);
    if (masteringAction)
        ProMenuBadge::attach(&menu, masteringAction);
    menu.exec(globalPos);
    QMetaObject::invokeMethod(synthMix, "syncQuickChecks");
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog d(dialogParent(), this);
    d.setModal(true);
    d.adjustSize();
    {   // never taller / wider than the screen it opens on (the device tab scrolls inside)
        QScreen *sc = dialogParent() ? dialogParent()->screen() : QGuiApplication::primaryScreen();
        if (sc) {
            const QRect av = sc->availableGeometry();
            const int maxH = qMax(300, av.height() - 100);   // 36 px title bar + window border + taskbar margin
            const int maxW = qMax(400, av.width() - 20);
            d.setMaximumHeight(maxH);
            d.resize(qMin(d.width(), maxW), qMin(d.height(), maxH));
        }
    }
    d.setMinimumSize(d.size());
    d.exec();
}

void MainWindow::showRemoteDialog()
{
    if (remoteDialog)
    {
        remoteDialog->raise();
        remoteDialog->activateWindow();
        return;
    }

    if (!remoteServer)
        remoteServer = new RemoteServer(synthMix, this, this);

    RemoteDialog *dlg = new RemoteDialog(remoteServer, dialogParent());
    dlg->adjustSize();
    dlg->setFixedSize(dlg->size());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    remoteDialog = dlg;
    dlg->show();
}

void MainWindow::showEqDialog()
{
    if (Equalizer31BandDialog::isOpenned())
        return;

    Equalizer31BandDialog *dlg = new Equalizer31BandDialog(
                dialogParent(), player->midiSynthesizer()->equalizer31BandFXs());
    dlg->adjustSize();
    dlg->setFixedSize(dlg->size());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::showChorusDialog()
{
    if (Chorus2Dialog::isOpenned())
        return;

    Chorus2Dialog *dlg = new Chorus2Dialog(dialogParent(), player->midiSynthesizer()->chorusFXs());
    dlg->adjustSize();
    dlg->setFixedSize(dlg->size());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::showReverbDialog()
{
    if (Reverb2Dialog::isOpenned())
        return;

    Reverb2Dialog *dlg = new Reverb2Dialog(dialogParent(), player->midiSynthesizer()->reverbFXs());
    dlg->adjustSize();
    dlg->setFixedSize(dlg->size());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::showMapMidiChannelDialog()
{
    MapChannelDialog dlg(dialogParent(), player);
    dlg.setModal(true);
    dlg.adjustSize();
    dlg.setMinimumSize(dlg.size());
    dlg.exec();
}

void MainWindow::showBusGroupDialog()
{
    BusDialog dlg(dialogParent(), synthMix->mixChannelMapPtr(), player->midiSynthesizer());
    dlg.setModal(true);
    dlg.adjustSize();
    dlg.setMinimumSize(dlg.size());
    dlg.exec();
}

void MainWindow::showSpeakerDialog()
{
    SpeakerDialog dlg(dialogParent(), synthMix->mixChannelMapPtr(), this);
    dlg.setModal(true);
    dlg.adjustSize();
    dlg.setMinimumSize(dlg.size());
    dlg.exec();
}

void MainWindow::showVSTDirDialog()
{
    #ifndef __linux__
    VSTDirsDialog dlg(dialogParent(), this);
    dlg.setModal(true);
    dlg.adjustSize();
    dlg.setMinimumSize(dlg.size());
    dlg.exec();
#endif
}

void MainWindow::minimizeWindow()
{
    setWindowState(Qt::WindowMinimized);
}

void MainWindow::showMapSFDialog()
{
    MapSoundfontDialog msfDlg(dialogParent(), player->midiSynthesizer());
    msfDlg.setModal(true);
    msfDlg.setMinimumSize(msfDlg.size());
    msfDlg.exec();
}

void MainWindow::showSecondMonitor()
{
    if (secondLyr == nullptr) {
        this->stop();
        secondMonitor = new SecondMonitorDialog(this, lyrWidget);
        secondMonitor->setWindowTitle("KaraMixer Edition");
        #ifdef _WIN32
        secondMonitor->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint);
        #else
        secondMonitor->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
        #endif

        Background *bg = secondMonitor->backgroundWidget();
        bg->setBackgroundType(bgWidget->backgroundType());
        bg->setBackgroundColor(bgWidget->backgroundColor());
        bg->setBackgroundImage(bgWidget->backgroundImage());

        secondLyr = secondMonitor->lyrWidget();
        secondMonitor->show();

        if (qApp->screens().count() > 1) {
            secondMonitor->windowHandle()->setScreen(qApp->screens()[1]);
            secondMonitor->showFullScreen();
        }
    } else {
        secondLyr = nullptr;
        secondMonitor->close();
        delete secondMonitor;
        secondMonitor = nullptr;
    }
}

void MainWindow::showFullScreenOrNormal()
{
    if (this->isFullScreen()) {
        this->showNormal();
    } else {
        this->showFullScreen();
    }
}

void MainWindow::setThaiLang()
{
    if (currentLang == "th")
        return;

    if (QApplication::removeTranslator(&translator)) {
        QApplication::processEvents();
        currentLang = "th";
        settings->setValue("Language", currentLang);
    }
}

void MainWindow::setEngLang()
{
    if (currentLang == "en")
        return;

    QString path = QApplication::applicationDirPath() + "/languages/en.qm";

    if (translator.load(path)) {
        QApplication::installTranslator(&translator);
        QApplication::processEvents();
        currentLang = "en";
        settings->setValue("Language", currentLang);
    }
}

void MainWindow::showCheckUpdateDialog()
{
}

void MainWindow::showAboutDialog()
{
    AboutDialog d(dialogParent());
    d.setModal(true);
    d.adjustSize();
    d.setFixedSize(d.size());
    d.exec();
}

void MainWindow::showAboutQtDialog()
{
    QMessageBox::aboutQt(dialogParent());
}

void MainWindow::onPositiomTimerTimeOut()
{
    int tick = player->positionTick();
    ui->sliderPosition->setValue(tick);
    #ifdef _WIN32
    taskbarButton->progress()->setValue(tick);
    #endif
    onPlayerPositionMSChanged(player->positionMs());
    ui->rhmWidget->setCurrentBeat( player->currentBeat() );
}

void MainWindow::onLyricsTimerTimeOut()
{
    lyrWidget->setPositionCursor(player->positionTick() + 25);
    if (secondLyr != nullptr)
        secondLyr->setPositionCursor(player->positionTick() + 25);
}

void MainWindow::onPlayerDurationMSChanged(qint64 d)
{
    QDateTime t = QDateTime::fromMSecsSinceEpoch(d);
    ui->lbDuration->setText( locale.toString(t, "mm:ss") );
}

void MainWindow::onPlayerPositionMSChanged(qint64 p)
{
    QDateTime t = QDateTime::fromMSecsSinceEpoch(p);
    ui->lbPosition->setText( locale.toString(t, "mm:ss") );
}

void MainWindow::onPlayerDurationTickChanged(int d)
{
    ui->sliderPosition->setMaximum(d);
}

void MainWindow::onSliderPositionPressed()
{
    playAfterSeek = player->isPlayerPlaying();
    pause();
}

void MainWindow::onSliderPositionReleased()
{
    if (player->isPlayerStopped()) {
        ui->sliderPosition->setValue(0);
        return;
    }

    #ifdef _WIN32
    taskbarButton->progress()->setValue(ui->sliderPosition->value());
    #endif

    player->setPositionTick(ui->sliderPosition->value());
    lyrWidget->setSeekPositionCursor(ui->sliderPosition->value());
    if (secondLyr != nullptr)
        secondLyr->setSeekPositionCursor(ui->sliderPosition->value());
    onPlayerPositionMSChanged(player->positionMs());

    // Seek beat
    ui->rhmWidget->setCurrentBeat(player->currentBeat());

    if (playAfterSeek) resume();
}

void MainWindow::on_btnVolumeMute_clicked()
{
    if (ui->sliderVolume->isEnabled()) {
        ui->sliderVolume->setEnabled(false);
        ui->btnVolumeMute->setIconFiles(":/Icons/volume-mute-white", ":/Icons/volume-mute-blue");
        player->setVolume(0);
    } else {
        ui->sliderVolume->setEnabled(true);
        ui->btnVolumeMute->setIconFiles(":/Icons/volume-white", ":/Icons/volume-blue");
        player->setVolume(ui->sliderVolume->value());
    }
}

void MainWindow::on_btnPlay_clicked()
{
    if (player->isPlayerPaused()) {
        resume();
    } else {
        if (playingSong.id() != "")
            play(-1);
    }
}


void MainWindow::onSliderVolumeValueChanged(int value)
{
    player->setVolume(value);

    ui->detail->setDetail(tr("ระดับเสียง"), QString::number(value));
    ui->detail->show();
    detailTimer->start(3000);
    if (this->width() < 1160 && ui->chMix->isVisible()) {
        ui->lcdTime->hide();
    }
}

void MainWindow::switchMedleyLyrics()
{
    lyrWidget->hide();
    lyrWidget->switchToLyricsTemp();
    lyrWidget->setSeekPositionCursor(player->positionTick());
    lyrWidget->show();
}

void MainWindow::switchMedleyLyrics2()
{
    if (secondLyr != nullptr) {
        secondLyr->hide();
        secondLyr->switchToLyricsTemp();
        secondLyr->setSeekPositionCursor(player->positionTick());
        secondLyr->show();
    }
}

void MainWindow::onNextMedleyStarted()
{
    int index = playingIndex + 1;

    playingSong = *playlist[index];

    if (remove_playlist && playlist.size() > 0) {
        delete playlist.takeAt(index);
        ui->playlistWidget->removeRow(index);
        playingIndex = -1;
    }

    positionTimer->stop();

    onPlayerDurationTickChanged(player->durationTick());
    onPlayerDurationMSChanged(player->durationMs());

    #ifdef _WIN32
    taskbarButton->progress()->setValue(0);
    taskbarButton->progress()->setMaximum(player->durationTick());
    taskbarButton->progress()->resume();
    taskbarButton->progress()->show();
    #endif

    ui->rhmWidget->setBeat(MidiHelper::calculateBeats(player->midiFile()), player->beatCount());

    positionTimer->start();

    ui->frameSearch->hide();
    ui->playlistWidget->hide();
    ui->chMix->hide();
    ui->expandChMix->hide();

    // SongDetail
    ui->songDetail->setDetail(&playingSong);
    ui->songDetail->adjustSize();
    ui->songDetail->show();
    timer2->start(songDetail_timeout);
}

void MainWindow::onNextMedleyAfterStarted()
{
    if (playlist.size() > 0)
        loadNextMedley(playlist[playingIndex + 1]);
}

void MainWindow::onPlayerThreadFinished()
{
    if (player->isPlayerFinished()) {
        playNext();
    }
}

void MainWindow::onDbUpdateChanged(int v)
{
    int p = (100 * v / db->updateCount());
    if (p < 0) p = 0;
    updateDetail->setValue(QString::number(p) + "%");
}

void MainWindow::onDetailTimerTimeout()
{
     ui->detail->hide();
     ui->lcdTime->show();
}

void MainWindow::addBpmSpeed(int speed)
{
    int bpm = player->currentBpm() + speed;
    int bpmSp = player->bpmSpeed() + speed;

    player->setBpmSpeed(bpmSp);

    if (playingSong.id() != "")
        playingSong.setBpmSpeed(bpmSp);
    ui->songDetail->setDetail(&playingSong);
    ui->songDetail->adjustSize();

    QString value = QString::number(bpm);
    if (bpmSp != 0) {
        value += bpmSp > 0 ? " (+" : " (";
        value += QString::number(bpmSp) + ")";
    }

    ui->detail->setDetail(tr("ความเร็ว"), value);
    ui->detail->show();
    detailTimer->start(3000);
    if (this->width() < 1160 && ui->chMix->isVisible()) {
        ui->lcdTime->hide();
    }
}

void MainWindow::preSetBpmSpeed(int speed)
{
    Song *song = db->currentSong();
    song->setBpmSpeed(speed);
    searchBoxChangeBpm = true;

    QString s;
    if (speed > 0)
        s = "+" + QString::number(speed);
    else
        s = QString::number(speed);

    ui->lbSearch->setText(tr("ความเร็ว : ") + QString::number(song->tempo() + speed) + " (" + s + ")");
    timer2->start(search_timeout);
}

void MainWindow::preSetTranspose(int transpose)
{
    Song *song = db->currentSong();
    song->setTranspose(transpose);
    searchBoxChangeBpm = true;

    QString s;
    if (transpose > 0)
        s = "+" + QString::number(transpose);
    else
        s = QString::number(transpose);


    ui->lbSearch->setText(tr("คีย์เพลง : ") + s );
    timer2->start(search_timeout);
}

void MainWindow::sendDrumPads(QKeyEvent *key, bool noteOn)
{
    MidiEvent e;
    e.setChannel(9);

    if (noteOn) {
        e.setData2(100);
        e.setEventType(MidiEventType::NoteOn);
    } else {
        e.setData2(0);
        e.setEventType(MidiEventType::NoteOff);
    }

    switch (key->key()) {
        case DrumPadsKey::bassDrum:
            e.setData1(36);
            player->sendEvent(e);
            break;
        case DrumPadsKey::snare:
            e.setData1(38);
            player->sendEvent(e);
            break;
        case DrumPadsKey::hihatClose:
            e.setData1(42);
            player->sendEvent(e);
            break;
        case DrumPadsKey::hihatOpen:
            e.setData1(46);
            player->sendEvent(e);
            break;
        case DrumPadsKey::cowbell:
            e.setData1(56);
            player->sendEvent(e);
            break;
        case DrumPadsKey::tambourine:
            e.setData1(54);
            player->sendEvent(e);
            break;
        case DrumPadsKey::tom1:
            e.setData1(50);
            player->sendEvent(e);
            break;
        case DrumPadsKey::tom2:
            e.setData1(48);
            player->sendEvent(e);
            break;
        case DrumPadsKey::crashCymbal:
            e.setData1(49);
            player->sendEvent(e);
            break;
        default:
            break;
    }
}

void MainWindow::savePlaylist()
{
    if (playlist.count() == 0)
        return;

    QString fileName = QFileDialog::getSaveFileName(this, tr("บันทึกรายการเล่น"),
                                                    Utils::LAST_OPEN_DIR,
                                                    APP_PLAYLIST_FILTER_TEXT);

    if (fileName == "")
        return;

    Utils::LAST_OPEN_DIR = QFileInfo(fileName).dir().absolutePath();

    if (!Utils::savePlaylist(fileName, playlist))
    {
        QMessageBox::critical(this, tr("บันทึกรายการเล่นล้มเหลว"),
                              tr("ไม่สามารถบันทึกรายการเล่นได้\nโปรดลองอีกครั้ง"));
    }
}

void MainWindow::loadPlaylist()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("เปิดรายการเล่น"),
                                                    Utils::LAST_OPEN_DIR,
                                                    APP_PLAYLIST_FILTER_TEXT);

    if (fileName == "")
        return;

    Utils::LAST_OPEN_DIR = QFileInfo(fileName).dir().absolutePath();

    QList<Song*> songs;
    if (Utils::loadPlaylist(fileName, songs))
    {
        for (Song *s : playlist)
            delete s;
        playlist.clear();
        playlist = songs;
        ui->playlistWidget->setPlaylist(playlist);
        playingIndex = -1;
    }
    else
    {
        QMessageBox::critical(this, tr("เปิดรายการเล่นล้มเหลว"),
                              tr("ไม่สามารถเปิดรายการเล่นได้\nโปรดลองอีกครั้ง"));
    }
}
