// Sends the sound of a program (Edge / Chrome) to a chosen Windows output device (e.g. "CABLE Input" of VB-Cable)
// - the same thing as Settings > System > Sound > Volume mixer > the program's "Output" - without opening Settings.
// Uses Windows' own (undocumented) per-app routing interface; works on Windows 10 21H2 and Windows 11.
#ifndef AUDIOROUTE_H
#define AUDIOROUTE_H

#ifdef _WIN32

#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <tlhelp32.h>
#include <set>
#include <vector>
#include <string>
#include <QString>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "ole32.lib")

// Windows 10 21H2+ / Windows 11 layout (only SetPersistedDefaultAudioEndpoint / Get... are used)
MIDL_INTERFACE("ab3d4648-e242-459f-b02f-541c70306324")
IKmAudioPolicyConfigFactory : public IInspectable
{
public:
    virtual HRESULT STDMETHODCALLTYPE m01() = 0;
    virtual HRESULT STDMETHODCALLTYPE m02() = 0;
    virtual HRESULT STDMETHODCALLTYPE m03() = 0;
    virtual HRESULT STDMETHODCALLTYPE m04() = 0;
    virtual HRESULT STDMETHODCALLTYPE m05() = 0;
    virtual HRESULT STDMETHODCALLTYPE m06() = 0;
    virtual HRESULT STDMETHODCALLTYPE m07() = 0;
    virtual HRESULT STDMETHODCALLTYPE m08() = 0;
    virtual HRESULT STDMETHODCALLTYPE m09() = 0;
    virtual HRESULT STDMETHODCALLTYPE m10() = 0;
    virtual HRESULT STDMETHODCALLTYPE m11() = 0;
    virtual HRESULT STDMETHODCALLTYPE m12() = 0;
    virtual HRESULT STDMETHODCALLTYPE m13() = 0;
    virtual HRESULT STDMETHODCALLTYPE m14() = 0;
    virtual HRESULT STDMETHODCALLTYPE m15() = 0;
    virtual HRESULT STDMETHODCALLTYPE m16() = 0;
    virtual HRESULT STDMETHODCALLTYPE m17() = 0;
    virtual HRESULT STDMETHODCALLTYPE m18() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPersistedDefaultAudioEndpoint(UINT32 processId, EDataFlow flow, ERole role, HSTRING deviceId) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPersistedDefaultAudioEndpoint(UINT32 processId, EDataFlow flow, ERole role, HSTRING *deviceId) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearAllPersistedApplicationDefaultEndpoints() = 0;
};

class AudioRoute
{
public:
    static void log(const QString &s)
    {
        QDir().mkpath(QDir::homePath() + QStringLiteral("/.KaraMixer"));
        QFile f(QDir::homePath() + QStringLiteral("/.KaraMixer/route.log"));
        if (f.size() > 200000) f.remove();
        if (!f.open(QIODevice::Append | QIODevice::Text)) return;
        QTextStream t(&f);
        t << QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz ")) << s << "\n";
    }

    // endpoint id of the first active playback device whose name contains `part` (e.g. "CABLE Input"); empty = none
    static QString findPlaybackDevice(const QString &part, QString *friendly = nullptr)
    {
        QString result;
        IMMDeviceEnumerator *en = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void **>(&en))) || !en)
            return result;
        IMMDeviceCollection *col = nullptr;
        if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
            UINT n = 0;
            col->GetCount(&n);
            for (UINT i = 0; i < n && result.isEmpty(); i++) {
                IMMDevice *d = nullptr;
                if (FAILED(col->Item(i, &d)) || !d) continue;
                IPropertyStore *ps = nullptr;
                if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps)) && ps) {
                    PROPVARIANT pv;
                    PropVariantInit(&pv);
                    if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) {
                        const QString name = QString::fromWCharArray(pv.pwszVal);
                        if (name.contains(part, Qt::CaseInsensitive)) {
                            LPWSTR id = nullptr;
                            if (SUCCEEDED(d->GetId(&id)) && id) {
                                result = QString::fromWCharArray(id);
                                CoTaskMemFree(id);
                                if (friendly) *friendly = name;
                            }
                        }
                    }
                    PropVariantClear(&pv);
                    ps->Release();
                }
                d->Release();
            }
            col->Release();
        }
        en->Release();
        return result;
    }

    static std::set<DWORD> tree(DWORD root)
    {
        std::set<DWORD> out;
        out.insert(root);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return out;
        std::vector<std::pair<DWORD, DWORD>> pp;
        PROCESSENTRY32W e;
        e.dwSize = sizeof(e);
        if (Process32FirstW(snap, &e))
            do { pp.push_back({ e.th32ProcessID, e.th32ParentProcessID }); } while (Process32NextW(snap, &e));
        CloseHandle(snap);
        for (bool grew = true; grew;) {
            grew = false;
            for (const auto &p : pp)
                if (!out.count(p.first) && out.count(p.second)) { out.insert(p.first); grew = true; }
        }
        return out;
    }

    // process id of the browser that owns the YouTube app window (title has no "Microsoft Edge" / "Google Chrome"
    // suffix, so an everyday browser tab is not taken); 0 = not found
    static DWORD findYouTubeWindowPid()
    {
        struct Ctx { DWORD pid = 0; } ctx;
        EnumWindows([](HWND h, LPARAM lp) -> BOOL {
            if (!IsWindowVisible(h)) return TRUE;
            wchar_t cls[64] = {};
            GetClassNameW(h, cls, 63);
            if (wcscmp(cls, L"Chrome_WidgetWin_1") != 0) return TRUE;
            wchar_t title[512] = {};
            GetWindowTextW(h, title, 511);
            const QString t = QString::fromWCharArray(title);
            if (!t.contains(QStringLiteral("YouTube"), Qt::CaseInsensitive)) return TRUE;
            if (t.contains(QStringLiteral("Microsoft")) || t.contains(QStringLiteral("Google Chrome"))) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (!pid) return TRUE;
            reinterpret_cast<Ctx *>(lp)->pid = pid;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&ctx));
        return ctx.pid;
    }

    // endpointId empty = back to the Windows default output. Returns the number of processes that accepted it.
    static int setOutputForTree(DWORD rootPid, const QString &endpointId, QString *error = nullptr)
    {
        IKmAudioPolicyConfigFactory *f = nullptr;
        HSTRING cls = nullptr;
        const wchar_t name[] = L"Windows.Media.Internal.AudioPolicyConfig";
        HRESULT hr = WindowsCreateString(name, UINT32(wcslen(name)), &cls);
        if (SUCCEEDED(hr)) {
            hr = RoGetActivationFactory(cls, __uuidof(IKmAudioPolicyConfigFactory), reinterpret_cast<void **>(&f));
            if (FAILED(hr) || !f) {          // older Windows 10 builds: same layout, other interface id
                static const GUID kOld = { 0x2a59116d, 0x6c4f, 0x45e0, { 0xa7, 0x4f, 0x70, 0x7e, 0x3f, 0xef, 0x92, 0x58 } };
                hr = RoGetActivationFactory(cls, kOld, reinterpret_cast<void **>(&f));
            }
            WindowsDeleteString(cls);
        }
        if (FAILED(hr) || !f) {
            if (error) *error = QStringLiteral("this Windows has no per-app routing interface (0x%1)").arg(quint32(hr), 8, 16, QLatin1Char('0'));
            log(error ? *error : QStringLiteral("no policy factory"));
            return 0;
        }

        HSTRING dev = nullptr;
        if (!endpointId.isEmpty()) {
            const QString full = QStringLiteral("\\\\?\\SWD#MMDEVAPI#") + endpointId +
                                 QStringLiteral("#{e6327cad-dcec-4949-ae8a-991e976a79d4}");
            const std::wstring w = full.toStdWString();
            WindowsCreateString(w.c_str(), UINT32(w.size()), &dev);
        }

        int ok = 0;
        for (DWORD pid : tree(rootPid)) {
            const HRESULT h1 = f->SetPersistedDefaultAudioEndpoint(pid, eRender, eConsole, dev);
            const HRESULT h2 = f->SetPersistedDefaultAudioEndpoint(pid, eRender, eMultimedia, dev);
            if (SUCCEEDED(h1) && SUCCEEDED(h2)) ok++;
            else log(QStringLiteral("pid %1: Set failed 0x%2 / 0x%3").arg(pid).arg(quint32(h1), 8, 16, QLatin1Char('0')).arg(quint32(h2), 8, 16, QLatin1Char('0')));
        }
        if (dev) WindowsDeleteString(dev);
        f->Release();
        log(QStringLiteral("route root pid %1 -> %2 : %3 processes").arg(rootPid).arg(endpointId.isEmpty() ? QStringLiteral("(default)") : endpointId).arg(ok));
        if (!ok && error) *error = QStringLiteral("Windows refused the routing");
        // read it back: does Windows keep what was set?
        if (ok && !endpointId.isEmpty()) {
            IKmAudioPolicyConfigFactory *g2 = nullptr;
            HSTRING c2 = nullptr;
            if (SUCCEEDED(WindowsCreateString(name, UINT32(wcslen(name)), &c2))) {
                if (FAILED(RoGetActivationFactory(c2, __uuidof(IKmAudioPolicyConfigFactory), reinterpret_cast<void **>(&g2))) || !g2) {
                    static const GUID kOld2 = { 0x2a59116d, 0x6c4f, 0x45e0, { 0xa7, 0x4f, 0x70, 0x7e, 0x3f, 0xef, 0x92, 0x58 } };
                    RoGetActivationFactory(c2, kOld2, reinterpret_cast<void **>(&g2));
                }
                WindowsDeleteString(c2);
            }
            if (g2) {
                HSTRING got = nullptr;
                const HRESULT hg = g2->GetPersistedDefaultAudioEndpoint(rootPid, eRender, eMultimedia, &got);
                const wchar_t *gp = got ? WindowsGetStringRawBuffer(got, nullptr) : nullptr;
                log(QStringLiteral("read back: hr=0x%1 value=%2").arg(quint32(hg), 8, 16, QLatin1Char('0')).arg(gp ? QString::fromWCharArray(gp) : QStringLiteral("(empty)")));
                if (got) WindowsDeleteString(got);
                g2->Release();
            }
        }
        return ok;
    }
};

#endif // _WIN32
#endif // AUDIOROUTE_H
