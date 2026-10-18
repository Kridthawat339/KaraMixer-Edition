#ifndef MIXERAUTOHIDE_H
#define MIXERAUTOHIDE_H

// Auto-hide of the mixer window while the karaoke program (another program) is being used.
//   * karaoke program in front and the user is typing / clicking in it  -> the mixer hides itself
//   * no keyboard / mouse input for <delay> seconds, or another program comes to the front for <delay> seconds
//     -> the mixer comes back (without stealing the focus)
// The karaoke program is recognised by the program (.exe) name or a part of its window title typed in Settings;
// when that is left empty, any other program counts. Windows only (does nothing elsewhere). Header-only.

#include <QObject>
#include <QTimer>
#include <QWidget>
#include <QRect>
#include <QCursor>
#include <QApplication>
#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QElapsedTimer>

#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

class MixerAutoHide : public QObject
{
public:
    explicit MixerAutoHide(QWidget *mixer, QObject *parent = nullptr) : QObject(parent), mixer(mixer)
    {
        timer.setInterval(150);
        QObject::connect(&timer, &QTimer::timeout, this, [this]() { tick(); });
    }

    void configure(bool on, double delaySec, const QString &program)
    {
        enabled = on;
        delayMs = qBound(300, int(delaySec * 1000.0), 120000);
        names.clear();
        for (const QString &s : program.split(QRegExp("[;,]"), QString::SkipEmptyParts))
            if (!s.trimmed().isEmpty())
                names << s.trimmed().toLower();
#ifdef _WIN32
        if (enabled) {
            if (!timer.isActive())
                timer.start();
        } else {
            timer.stop();
            restore();
        }
#endif
    }

    bool isEnabled() const { return enabled; }

private:
    QWidget *mixer;
    QTimer timer;
    bool enabled = false;
    bool hiddenByUs = false;
    int delayMs = 3000;
    QStringList names;
    QElapsedTimer awayClock;     // how long the karaoke program has not been in front
    bool awayRunning = false;

    void restore()
    {
        awayRunning = false;
        if (hiddenByUs && mixer) {
            hiddenByUs = false;
            mixer->setAttribute(Qt::WA_ShowWithoutActivating, true);
            mixer->show();
        }
    }

#ifdef _WIN32
    static QString exeNameOf(DWORD pid)
    {
        QString res;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h) {
            wchar_t buf[MAX_PATH * 2];
            DWORD n = DWORD(sizeof(buf) / sizeof(buf[0]));
            if (QueryFullProcessImageNameW(h, 0, buf, &n))
                res = QFileInfo(QString::fromWCharArray(buf, int(n))).fileName();
            CloseHandle(h);
        }
        return res;
    }

    // is the window in front the karaoke program?
    bool karaokeInFront() const
    {
        HWND fg = GetForegroundWindow();
        if (!fg)
            return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid == GetCurrentProcessId())
            return false;                       // our own windows are never "the karaoke program"
        wchar_t cls[128] = {0};
        GetClassNameW(fg, cls, 127);
        const QString c = QString::fromWCharArray(cls);
        if (c == QLatin1String("Shell_TrayWnd") || c == QLatin1String("Shell_SecondaryTrayWnd")
            || c == QLatin1String("Progman") || c == QLatin1String("WorkerW"))
            return false;                       // taskbar / desktop
        if (names.isEmpty())
            return true;                        // any other program
        const QString exe = exeNameOf(pid).toLower();
        wchar_t title[256] = {0};
        GetWindowTextW(fg, title, 255);
        const QString t = QString::fromWCharArray(title).toLower();
        for (const QString &n : names)
            if ((!exe.isEmpty() && exe.contains(n)) || t.contains(n))
                return true;
        return false;
    }

    void tick()
    {
        if (!enabled || !mixer)
            return;
        if (QApplication::activeModalWidget())  // never hide the mixer under an open settings / message dialog
            return;

        if (!karaokeInFront()) {
            // karaoke program not in front: bring the mixer back after the delay
            if (hiddenByUs) {
                if (!awayRunning) { awayClock.start(); awayRunning = true; }
                else if (awayClock.elapsed() >= delayMs) restore();
            }
            return;
        }
        awayRunning = false;

        LASTINPUTINFO li;
        li.cbSize = sizeof(li);
        if (!GetLastInputInfo(&li))
            return;
        const DWORD idle = GetTickCount() - li.dwTime;
        const bool active = idle < DWORD(delayMs);

        if (active) {
            // the mouse resting over the mixer is not "using the karaoke program"
            if (!hiddenByUs && mixer->isVisible() && mixer->frameGeometry().contains(QCursor::pos()))
                return;
            if (!hiddenByUs && mixer->isVisible()) {
                hiddenByUs = true;
                mixer->hide();
            }
        } else if (hiddenByUs) {
            restore();                          // idle long enough: back again
        }
    }
#else
    void tick() {}
#endif
};

#endif // MIXERAUTOHIDE_H
