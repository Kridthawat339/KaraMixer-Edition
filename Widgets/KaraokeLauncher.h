#ifndef KARAOKELAUNCHER_H
#define KARAOKELAUNCHER_H

// Opens the karaoke program from the mixer, like the YouTube button opens YouTube. The program (.exe or shortcut .lnk)
// is chosen once and remembered (Config app file, key KaraokeProgramPath). It is started with its own folder as the
// working directory (many karaoke programs look for their song data there) and through the Windows shell, so a
// program that asks for administrator rights shows the normal Windows question instead of failing silently.
// Optional (key KaraokeAutoLaunch): open it together with the mixer AND close it when the mixer is closed (closeProgram:
// a normal "close window" request to the program's windows, so it can still ask to save; nothing is force-killed). Header-only.

#include <QString>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QStandardPaths>
#include "Config.h"
#include "Widgets/EventLog.h"

#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <shellapi.h>
  #include <tlhelp32.h>
#endif

namespace KaraokeLauncher {

inline QString path()
{
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    return st.value("KaraokeProgramPath", "").toString();
}

inline void setPath(const QString &p)
{
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    st.setValue("KaraokeProgramPath", p);
}

inline bool autoLaunch()
{
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    return st.value("KaraokeAutoLaunch", false).toBool();
}

inline void setAutoLaunch(bool on)
{
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    st.setValue("KaraokeAutoLaunch", on);
}

// choose the karaoke program; returns false when cancelled
inline bool choose(QWidget *parent)
{
    const QString cur = path();
    // first time: start on the Desktop (that is where most people keep the karaoke program's shortcut)
    const QString start = cur.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                                        : QFileInfo(cur).absolutePath();
    const QString p = QFileDialog::getOpenFileName(parent, QString::fromUtf8("เลือกโปรแกรมคาราโอเกะ"), start,
                                                   QString::fromUtf8("โปรแกรม (*.exe *.lnk);;ทุกไฟล์ (*.*)"));
    if (p.isEmpty())
        return false;
    setPath(p);
    return true;
}

// start the program; asks for it first when none is chosen yet (or the file is gone)
inline bool launch(QWidget *parent, bool askIfMissing = true)
{
    QString p = path();
    if ((p.isEmpty() || !QFileInfo::exists(p))) {
        if (!askIfMissing || !choose(parent))
            return false;
        p = path();
    }
    const QString dir = QFileInfo(p).absolutePath();
    bool ok = false;
#ifdef _WIN32
    const QString nativeFile = QDir::toNativeSeparators(p);
    const QString nativeDir = QDir::toNativeSeparators(dir);
    const HINSTANCE r = ShellExecuteW(nullptr, L"open", reinterpret_cast<const wchar_t *>(nativeFile.utf16()), nullptr,
                                      reinterpret_cast<const wchar_t *>(nativeDir.utf16()), SW_SHOWNORMAL);
    ok = reinterpret_cast<quintptr>(r) > 32;
#else
    ok = QProcess::startDetached(p, QStringList(), dir);
#endif
    if (!ok) {
        EventLog::add(QStringLiteral("KARAOKE"), QStringLiteral("cannot start ") + p);
        if (parent)
            QMessageBox::warning(parent, QString::fromUtf8("เปิดโปรแกรมคาราโอเกะไม่ได้"),
                                 QString::fromUtf8("เปิดไม่ได้:\n") + p
                                 + QString::fromUtf8("\n\nเลือกโปรแกรมใหม่ได้จากเมนู > เปลี่ยนโปรแกรมคาราโอเกะ..."));
    } else {
        EventLog::add(QStringLiteral("KARAOKE"), QStringLiteral("started ") + p);
    }
    return ok;
}

#ifdef _WIN32
namespace detail {
struct CloseCtx { DWORD pid; int sent; bool hiddenToo; };
inline BOOL CALLBACK closeEnum(HWND h, LPARAM lp)
{
    CloseCtx *c = reinterpret_cast<CloseCtx *>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    // top-level program windows only; the second pass also takes hidden ones (a program sitting in the tray / with its
    // main window hidden has no visible window: the request reached nothing and the program kept running)
    if (pid == c->pid && (c->hiddenToo || IsWindowVisible(h)) && GetWindow(h, GW_OWNER) == nullptr) {
        PostMessageW(h, WM_CLOSE, 0, 0);
        c->sent++;
    }
    return TRUE;
}
}
#endif

// ask the karaoke program (every running copy of the chosen program) to close, like clicking the X of its window.
inline void closeProgram()
{
#ifdef _WIN32
    const QString p = path();
    if (p.isEmpty())
        return;
    QString target = p;
    if (p.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive))
        target = QFileInfo(p).symLinkTarget();   // Qt resolves the shortcut
    if (target.isEmpty())
        return;
    target = QDir::toNativeSeparators(QFileInfo(target).absoluteFilePath());

    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    int sent = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == GetCurrentProcessId())
                continue;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!h)
                continue;
            wchar_t buf[MAX_PATH * 2];
            DWORD n = DWORD(sizeof(buf) / sizeof(buf[0]));
            const bool got = QueryFullProcessImageNameW(h, 0, buf, &n) != FALSE;
            CloseHandle(h);
            if (got && QString::fromWCharArray(buf, int(n)).compare(target, Qt::CaseInsensitive) == 0) {
                detail::CloseCtx c{ pe.th32ProcessID, 0, false };
                EnumWindows(detail::closeEnum, reinterpret_cast<LPARAM>(&c));
                if (c.sent == 0) {
                    c.hiddenToo = true;
                    EnumWindows(detail::closeEnum, reinterpret_cast<LPARAM>(&c));
                }
                sent += c.sent;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    EventLog::add(QStringLiteral("KARAOKE"), QStringLiteral("close requested for ") + target
                  + QStringLiteral(" (%1 window(s))").arg(sent));
#endif
}

} // namespace KaraokeLauncher

#endif // KARAOKELAUNCHER_H
