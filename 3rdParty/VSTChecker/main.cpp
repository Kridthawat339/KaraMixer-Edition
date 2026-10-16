#include <bass.h>
#include <bass_vst.h>

#include <iostream>
#include <string>
#include <vector>

#include "Vst3/Vst3Host.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>

static std::string toUtf8(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
#endif

// VST3: prints, for every audio-module class of the file, 4 lines
// (uid, name, vendor, "path|classIndex"). Output is UTF-8.
static int checkVst3(const std::string &path)
{
    std::vector<vst3host::ClassEntry> classes;
    std::string err;
    if (!vst3host::scan(path, classes, &err) || classes.empty())
    {
        std::cout << path << " is not VST3 file" << std::endl;
        return 2;
    }
    for (size_t i = 0; i < classes.size(); i++)
    {
        const vst3host::ClassEntry &c = classes[i];
        std::cout << c.uid << "\n" << c.name << "\n" << c.vendor << "\n"
                  << path << "|" << c.index << "\n";
    }
    std::cout.flush();
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cout << "Invalid parameter, argc is " << argc << std::endl;
        return 3;
    }

    {
        std::string arg = argv[1];
        #ifdef _WIN32
        int wn = 0;
        LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wn);
        if (wargv && wn >= 2)
            arg = toUtf8(wargv[1]);
        if (wargv)
            LocalFree(wargv);
        #endif
        if (vst3host::isVst3Path(arg))
            return checkVst3(arg);
    }

    BASS_Init(-1, 0, 0, NULL, NULL);

    HSTREAM stream = BASS_StreamCreate(44100, 2, 0, NULL, NULL);

    #ifdef _WIN32
    DWORD vst = BASS_VST_ChannelSetDSP(stream, argv[1], BASS_VST_KEEP_CHANS, 0);
    #else
    DWORD vst = BASS_VST_ChannelSetDSP(stream, argv[1], BASS_VST_KEEP_CHANS, 0);
    #endif


    if (vst == 0)
    {
        std::cout << argv[1] << " is not VST file" << std::endl;
        BASS_StreamFree(stream);
        BASS_Free();
        return 2;
    }


    int returnCode;
    BASS_VST_INFO info;

    if (BASS_VST_GetInfo(vst, &info))
    {
        returnCode = 0;

        std::cout << info.uniqueID << std::endl;
        std::cout << info.effectName << std::endl;
        std::cout << info.vendorName << std::endl;
        std::cout << argv[1] << std::endl;
    }
    else
    {
        std::cout << "Can't get info from " << argv[1] << std::endl;
        returnCode = 1;
    }

    BASS_VST_ChannelRemoveDSP(stream, vst);
    BASS_StreamFree(stream);
    BASS_Free();

    return returnCode;
}
