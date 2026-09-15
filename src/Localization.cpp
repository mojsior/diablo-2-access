#include "Localization.hpp"

#include "Addresses.hpp"
#include "Logging.hpp"

#include <Windows.h>
#include <Shlwapi.h>

#include <atomic>
#include <cwctype>

namespace d2access {
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

enum class Setting {
    Auto,
    Polish,
    English,
};

// sub_521090(char *out, 0) copies the three-letter code of the language the
// game loaded its string tables for ("ENG", "POL", ...).
constexpr uintptr_t VaGameLanguageCode = 0x521090;

std::atomic<Setting> g_setting = Setting::Auto;
std::atomic<int> g_detected = -1; // -1 unknown, 0 Polish, 1 English

using LanguageCode_t = char(__stdcall *)(char *, int);

bool SehLanguageCode(char *buffer)
{
    __try
    {
        reinterpret_cast<LanguageCode_t>(AbsoluteAddress(VaGameLanguageCode))(buffer, 0);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

int DetectGameLanguage()
{
    char code[64] = {};
    if (!SehLanguageCode(code) || code[0] == '\0')
        return -1;
    code[sizeof(code) - 1] = '\0';

    const bool polish = (code[0] == 'P' || code[0] == 'p') && (code[1] == 'O' || code[1] == 'o') &&
                        (code[2] == 'L' || code[2] == 'l');
    std::wstring wide;
    for (const char *p = code; *p != '\0'; ++p)
        wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
    LogLine(L"Game language code: " + wide + (polish ? L" (Polish speech)" : L" (English speech)"));
    return polish ? 0 : 1;
}

} // namespace

void InitializeLocalization()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
    PathRemoveFileSpecW(path);
    PathAppendW(path, L"D2Access.ini");

    wchar_t value[16] = {};
    GetPrivateProfileStringW(L"General", L"Language", L"auto", value, 16, path);
    for (wchar_t &ch : value)
        ch = static_cast<wchar_t>(std::towlower(ch));

    const std::wstring setting = value;
    if (setting == L"pl" || setting == L"pol" || setting == L"polish" || setting == L"polski")
        g_setting = Setting::Polish;
    else if (setting == L"en" || setting == L"eng" || setting == L"english" || setting == L"angielski")
        g_setting = Setting::English;
    else
        g_setting = Setting::Auto;
    LogLine(L"Speech language setting: " + setting);
}

Language CurrentLanguage()
{
    switch (g_setting.load())
    {
    case Setting::Polish:
        return Language::Polish;
    case Setting::English:
        return Language::English;
    case Setting::Auto:
        break;
    }

    int detected = g_detected.load();
    if (detected < 0)
    {
        detected = DetectGameLanguage();
        if (detected >= 0)
            g_detected = detected;
    }
    return detected == 0 ? Language::Polish : Language::English;
}

bool IsEnglish()
{
    return CurrentLanguage() == Language::English;
}

} // namespace d2access
