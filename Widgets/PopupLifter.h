#ifndef POPUPLIFTER_H
#define POPUPLIFTER_H

// Message boxes / dialogs that are not ours (a VST plug-in's "demo version" / licence box, a driver's error box ...)
// open behind the mixer when the mixer stays on top: the plug-in then waits for an "OK" nobody can see. Every plain
// Windows dialog window (class "#32770") this process shows is lifted above everything and given the focus.
// Windows only, header-only; install() once after the QApplication exists.

#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

namespace PopupLifter
{
#ifdef _WIN32
    inline void CALLBACK onShow(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD)
    {
        if (!hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
            return;
        if (GetAncestor(hwnd, GA_ROOT) != hwnd)
            return;                                      // a control inside a window, not a window
        wchar_t cls[64] = { 0 };
        if (!GetClassNameW(hwnd, cls, 63) || lstrcmpW(cls, L"#32770") != 0)
            return;                                      // Qt's own windows are "Qt5..." classes: they handle this themselves
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
    }

    // the hook is called on the installing (GUI) thread whenever it pumps messages - also inside a modal box's loop
    inline void install()
    {
        static HWINEVENTHOOK hook = nullptr;
        if (!hook)
            hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, onShow,
                                   GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    }
#else
    inline void install() {}
#endif
}

#endif // POPUPLIFTER_H
