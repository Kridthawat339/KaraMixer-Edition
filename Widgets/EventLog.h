#ifndef EVENTLOG_H
#define EVENTLOG_H

// Problem journal: ~/.KaraMixer/events.log. Keeps a short, timestamped line for every event that helps to find a
// customer's problem later (output clipped, audio device could not play, program warnings / errors, a previous run
// that did not close normally, a crash with its address). Shown at the end of "ข้อมูลระบบ / Diagnostics".
// Nothing is sent anywhere: it is a plain text file on the PC. Header-only; add() may be called from any thread.

#include <QString>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <QSysInfo>
#include <QtGlobal>
#include <QCoreApplication>

#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <cstdio>
#endif

namespace EventLog {

inline QString dir()  { return QDir::homePath() + QStringLiteral("/.KaraMixer"); }
inline QString path() { return dir() + QStringLiteral("/events.log"); }

// minGapMs > 0: the same kind of event is written at most once per that time (a clip that lasts 30 s must not
// write 600 lines)
inline void add(const QString &kind, const QString &msg, int minGapMs = 0)
{
    // a Qt warning raised by the journal's own file calls must not come back in here (the mutex is not recursive)
    static thread_local bool busy = false;
    if (busy)
        return;
    struct Guard { bool &b; explicit Guard(bool &x) : b(x) { b = true; } ~Guard() { b = false; } } guard(busy);
    static QMutex mx;
    static QHash<QString, qint64> last;
    QMutexLocker lk(&mx);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (minGapMs > 0) {
        auto it = last.find(kind);
        if (it != last.end() && now - it.value() < minGapMs)
            return;
        last[kind] = now;
    }
    QDir().mkpath(dir());
    QFile f(path());
    if (f.size() > 128 * 1024 && f.open(QIODevice::ReadOnly)) {   // keep the newest half
        QByteArray all = f.readAll();
        f.close();
        all = all.right(64 * 1024);
        const int nl = all.indexOf('\n');
        if (nl >= 0)
            all = all.mid(nl + 1);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(all);
            f.close();
        }
    }
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        const QString line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                           + QStringLiteral("  [") + kind + QStringLiteral("] ") + msg + QLatin1Char('\n');
        f.write(line.toUtf8());
    }
}

inline QString flagPath() { return dir() + QStringLiteral("/running.flag"); }

// at start: if the flag of the last run is still there, that run did not close normally (crash, power cut, killed)
inline void sessionStart()
{
    QDir().mkpath(dir());
    if (QFile::exists(flagPath()))
        add(QStringLiteral("ABNORMAL"), QStringLiteral("the previous run did not close normally (crash, power cut or force-closed)"));
    QFile f(flagPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
}

inline void sessionEnd() { QFile::remove(flagPath()); }

namespace detail {
inline QtMessageHandler &prevHandler() { static QtMessageHandler h = nullptr; return h; }

inline void qtHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    static int count = 0;
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        if (count < 200 || type != QtWarningMsg) {   // a flood of warnings must not fill the journal
            count++;
            const char *k = type == QtWarningMsg ? "QT-WARN" : (type == QtCriticalMsg ? "QT-ERROR" : "QT-FATAL");
            QString where = ctx.file ? QStringLiteral(" (%1:%2)").arg(QString::fromUtf8(ctx.file).section(QLatin1Char('\\'), -1)
                                                                      .section(QLatin1Char('/'), -1)).arg(ctx.line) : QString();
            add(QString::fromLatin1(k), msg.left(300) + where, type == QtWarningMsg ? 1500 : 0);
        }
    }
    if (prevHandler())
        prevHandler()(type, ctx, msg);
}

#ifdef _WIN32
inline wchar_t *crashPath() { static wchar_t p[MAX_PATH * 2] = {0}; return p; }

// "module.dll+0x1234" for a code address (which DLL / where in it), for the crash line
inline int codeWhere(unsigned long long pc, char *out, int cap)
{
    HMODULE hm = nullptr;
    wchar_t w[MAX_PATH] = {0};
    if (pc && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 reinterpret_cast<LPCWSTR>(pc), &hm) && hm && GetModuleFileNameW(hm, w, MAX_PATH)) {
        const wchar_t *base = wcsrchr(w, L'\\');
        char m[MAX_PATH] = "?";
        WideCharToMultiByte(CP_UTF8, 0, base ? base + 1 : w, -1, m, MAX_PATH, nullptr, nullptr);
        return _snprintf_s(out, size_t(cap), _TRUNCATE, "%s+0x%llx", m, pc - reinterpret_cast<unsigned long long>(hm));
    }
    return _snprintf_s(out, size_t(cap), _TRUNCATE, "0x%llx", pc);
}

inline LONG WINAPI crashFilter(EXCEPTION_POINTERS *ep)
{
    // no Qt in here: the program state is not trusted. Plain Win32 calls only.
    char line[700];
    char mod[MAX_PATH] = "?";
    wchar_t wmod[MAX_PATH] = {0};
    HMODULE hm = nullptr;
    const void *addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    if (addr && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   static_cast<LPCWSTR>(addr), &hm) && hm
        && GetModuleFileNameW(hm, wmod, MAX_PATH))
        WideCharToMultiByte(CP_UTF8, 0, wmod, -1, mod, MAX_PATH, nullptr, nullptr);
    SYSTEMTIME st;
    GetLocalTime(&st);
    const unsigned long code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    const int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
                              "%04d-%02d-%02d %02d:%02d:%02d  [CRASH] exception 0x%08lX at %p in %s\r\n",
                              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, code, addr, mod);
    // second line: the call stack (which DLL called which), e.g. ucrtbase.dll (memcpy) < bassmidi.dll < KaraMixer.exe.
    // "where it crashed" alone said only ucrtbase.dll, i.e. some memory copy - not whose.
    char stack[1400];
    int sn = _snprintf_s(stack, sizeof(stack), _TRUNCATE, "  stack:");
#ifdef _WIN64
    if (ep && ep->ContextRecord) {
        CONTEXT c = *ep->ContextRecord;
        const NT_TIB *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
        const unsigned long long lo = reinterpret_cast<unsigned long long>(tib->StackLimit),
                                 hi = reinterpret_cast<unsigned long long>(tib->StackBase);
        for (int i = 0; i < 14 && c.Rip && sn > 0 && sn < int(sizeof(stack)) - 80; i++) {
            char one[160];
            codeWhere(c.Rip, one, sizeof(one));
            sn += _snprintf_s(stack + sn, sizeof(stack) - size_t(sn), _TRUNCATE, "%s %s", i ? " <" : "", one);
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(c.Rip, &imageBase, nullptr);
            if (!f) {                                    // leaf function: the return address is on top of the stack
                if (c.Rsp < lo || c.Rsp + 8 > hi)
                    break;
                c.Rip = *reinterpret_cast<const DWORD64 *>(c.Rsp);
                c.Rsp += 8;
            } else {
                void *hd = nullptr;
                DWORD64 est = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, f, &c, &hd, &est, nullptr);
            }
            if (c.Rsp < lo || c.Rsp > hi)
                break;
        }
    }
#endif
    if (ep && ep->ExceptionRecord && code == 0xC0000005 && ep->ExceptionRecord->NumberParameters >= 2)
        sn += _snprintf_s(stack + (sn > 0 ? sn : 0), sizeof(stack) - size_t(sn > 0 ? sn : 0), _TRUNCATE, "   (%s 0x%llx)",
                          ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "writing" :
                          (ep->ExceptionRecord->ExceptionInformation[0] == 8 ? "executing" : "reading"),
                          static_cast<unsigned long long>(ep->ExceptionRecord->ExceptionInformation[1]));
    if (sn > 0 && sn < int(sizeof(stack)) - 3)
        sn += _snprintf_s(stack + sn, sizeof(stack) - size_t(sn), _TRUNCATE, "\r\n");

    HANDLE h = CreateFileW(crashPath(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(h, line, DWORD(n > 0 ? n : 0), &w, nullptr);
        if (sn > 0)
            WriteFile(h, stack, DWORD(sn), &w, nullptr);
        CloseHandle(h);
    }
    return EXCEPTION_CONTINUE_SEARCH;   // then Windows does what it always did (error window / report)
}
#endif
} // namespace detail

// call once, right after the QApplication has been created
inline void installHandlers()
{
    detail::prevHandler() = qInstallMessageHandler(detail::qtHandler);
#ifdef _WIN32
    QDir().mkpath(dir());
    const QString p = QDir::toNativeSeparators(path());
    const int n = qMin(int(p.length()), MAX_PATH * 2 - 1);
    p.left(n).toWCharArray(detail::crashPath());
    detail::crashPath()[n] = 0;
    SetUnhandledExceptionFilter(detail::crashFilter);
#endif
}

} // namespace EventLog

#endif // EVENTLOG_H
