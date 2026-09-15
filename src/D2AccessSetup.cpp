// Accessible helper for Blizzard's classic Diablo II installer.
//
// The installer's main menu is drawn as graphics and its licence agreement
// only enables "Agree" after the text was scrolled with the mouse, so screen
// reader users cannot get through it on their own. This helper drives those
// two steps, reads every standard dialog aloud (CD-key, folder, DirectX,
// errors) and reports progress. The player still types their own CD-key.

#include "Logging.hpp"
#include "ScreenReader.hpp"

#include <Windows.h>
#include <Shlwapi.h>
#include <commdlg.h>
#include <conio.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Comdlg32.lib")

namespace fs = std::filesystem;
using namespace d2access;

namespace {

constexpr DWORD LoopDelayMs = 250;
constexpr ULONGLONG MainMenuSettleMs = 5000;
constexpr ULONGLONG NoWindowExitMs = 20000;

bool g_polish = true;
bool g_autoYes = false;
bool g_stopAtKey = false;

const wchar_t *Tr(const wchar_t *polish, const wchar_t *english)
{
    return g_polish ? polish : english;
}

std::wstring TrS(const wchar_t *polish, const wchar_t *english)
{
    return std::wstring(Tr(polish, english));
}

void Print(const std::wstring &text)
{
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const std::wstring line = text + L"\r\n";
    DWORD written = 0;
    if (!WriteConsoleW(out, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr))
    {
        const int size = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0,
                                             nullptr, nullptr);
        std::string utf8(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), utf8.data(), size, nullptr,
                            nullptr);
        WriteFile(out, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
}

// The console is read by the screen reader when it has focus; speak directly
// only while an installer window is in front.
void Say(const std::wstring &text)
{
    Print(text);
    LogLine(text);
    if (GetForegroundWindow() != GetConsoleWindow())
        Speak(text, true);
}

bool Ask(const std::wstring &question)
{
    if (g_autoYes)
    {
        Print(question + L" [auto: Enter]");
        LogLine(question + L" [auto: Enter]");
        return true;
    }
    SetForegroundWindow(GetConsoleWindow());
    Sleep(200);
    Say(question);
    while (_kbhit())
        _getwch();
    for (;;)
    {
        const wint_t ch = _getwch();
        if (ch == L'\r')
            return true;
        if (ch == 27)
            return false;
    }
}

std::wstring StripMnemonic(std::wstring text)
{
    text.erase(std::remove(text.begin(), text.end(), L'&'), text.end());
    return text;
}

// Joins spoken parts with ". " unless the previous part already ends a sentence.
void AppendPart(std::wstring &text, const std::wstring &part)
{
    if (!text.empty())
    {
        const wchar_t last = text.back();
        text += (last == L'.' || last == L':' || last == L'?' || last == L'!') ? L" " : L". ";
    }
    text += part;
}

std::wstring WindowText(HWND hwnd)
{
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

std::wstring ClassName(HWND hwnd)
{
    wchar_t name[256] = {};
    GetClassNameW(hwnd, name, 256);
    return name;
}

std::wstring ProcessName(DWORD pid)
{
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr)
        return {};
    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    std::wstring name;
    if (QueryFullProcessImageNameW(process, 0, path, &size))
        name = PathFindFileNameW(path);
    CloseHandle(process);
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return name;
}

std::vector<HWND> InstallerWindows()
{
    std::vector<HWND> windows;
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            if (!IsWindowVisible(hwnd))
                return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (ProcessName(pid) == L"installer.exe")
                reinterpret_cast<std::vector<HWND> *>(param)->push_back(hwnd);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));
    return windows;
}

std::vector<HWND> Children(HWND parent)
{
    std::vector<HWND> children;
    EnumChildWindows(
        parent,
        [](HWND hwnd, LPARAM param) -> BOOL {
            reinterpret_cast<std::vector<HWND> *>(param)->push_back(hwnd);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&children));
    return children;
}

HWND FindChildByClass(HWND parent, const wchar_t *className)
{
    for (HWND child : Children(parent))
    {
        if (ClassName(child) == className)
            return child;
    }
    return nullptr;
}

void PressKey(WORD vk, bool ctrl = false)
{
    if (ctrl)
        keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event(static_cast<BYTE>(vk), 0, 0, 0);
    Sleep(40);
    keybd_event(static_cast<BYTE>(vk), 0, KEYEVENTF_KEYUP, 0);
    if (ctrl)
        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    Sleep(150);
}

void BringToFront(HWND hwnd)
{
    keybd_event(VK_SHIFT, 0, 0, 0);
    keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    Sleep(300);
}

// ---------------------------------------------------------------------------
// Finding the installer
// ---------------------------------------------------------------------------

bool IsInstallerFolder(const fs::path &folder)
{
    std::error_code error;
    return fs::exists(folder / L"Installer.exe", error) && fs::exists(folder / L"Installer Tome.mpq", error);
}

std::wstring KnownFolder(const wchar_t *variable, const wchar_t *suffix)
{
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(variable, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return {};
    return std::wstring(buffer) + suffix;
}

std::vector<fs::path> FindInstallers()
{
    std::vector<fs::path> found;
    const auto add = [&found](const fs::path &folder) {
        if (IsInstallerFolder(folder) &&
            std::find(found.begin(), found.end(), fs::weakly_canonical(folder)) == found.end())
            found.push_back(fs::weakly_canonical(folder));
    };

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    add(fs::path(exePath).parent_path());
    add(fs::current_path());

    for (const std::wstring &root : {KnownFolder(L"USERPROFILE", L"\\Downloads"), KnownFolder(L"USERPROFILE", L"\\Desktop")})
    {
        std::error_code error;
        if (root.empty() || !fs::exists(root, error))
            continue;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error);
        for (; !error && it != fs::recursive_directory_iterator(); it.increment(error))
        {
            if (it.depth() > 3)
            {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_directory(error) && IsInstallerFolder(it->path()))
                add(it->path());
        }
    }
    return found;
}

fs::path ChooseInstallerWithDialog()
{
    wchar_t file[MAX_PATH] = L"Installer.exe";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetConsoleWindow();
    dialog.lpstrFilter = L"Installer.exe\0Installer.exe\0\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = Tr(L"Wskaż plik Installer.exe z instalatora Diablo II", L"Select Installer.exe of the Diablo II installer");
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog))
        return {};
    return fs::path(file);
}

fs::path ChooseInstaller(int argc, wchar_t **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring arg = argv[i];
        if (arg.rfind(L"--", 0) == 0)
            continue;
        fs::path path(arg);
        if (fs::is_directory(path))
            path /= L"Installer.exe";
        return path;
    }

    const std::vector<fs::path> installers = FindInstallers();
    if (installers.size() == 1)
    {
        Say(TrS(L"Znaleziono instalator: ", L"Found the installer: ") + installers.front().wstring());
        return installers.front() / L"Installer.exe";
    }
    if (installers.size() > 1 && installers.size() <= 9)
    {
        std::wstring list = Tr(L"Znaleziono kilka instalatorów. Naciśnij cyfrę: ", L"Several installers were found. Press a number: ");
        for (size_t i = 0; i < installers.size(); ++i)
            list += std::to_wstring(i + 1) + L": " + installers[i].filename().wstring() + L". ";
        SetForegroundWindow(GetConsoleWindow());
        Say(list);
        for (;;)
        {
            const wint_t ch = _getwch();
            if (ch >= L'1' && ch < static_cast<wint_t>(L'1' + installers.size()))
                return installers[static_cast<size_t>(ch - L'1')] / L"Installer.exe";
            if (ch == 27)
                return {};
        }
    }

    Say(Tr(L"Nie znaleziono instalatora. Otworzy się okno wyboru pliku, wskaż Installer.exe.",
           L"No installer was found. A file dialog opens, select Installer.exe."));
    return ChooseInstallerWithDialog();
}

// ---------------------------------------------------------------------------
// Installer screens
// ---------------------------------------------------------------------------

std::wstring ExistingInstallPath()
{
    wchar_t buffer[MAX_PATH] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Blizzard Entertainment\\Diablo II", L"InstallPath",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, buffer, &size) != ERROR_SUCCESS)
        return {};
    return buffer;
}

struct DialogInfo {
    std::wstring text;
    bool hasEdit = false;
    std::vector<std::wstring> paths;
};

DialogInfo ReadDialog(HWND dialog)
{
    DialogInfo info;
    std::wstring text = StripMnemonic(WindowText(dialog));
    // Read in visual order: top to bottom, then left to right.
    std::vector<HWND> children = Children(dialog);
    std::stable_sort(children.begin(), children.end(), [](HWND a, HWND b) {
        RECT ra{};
        RECT rb{};
        GetWindowRect(a, &ra);
        GetWindowRect(b, &rb);
        if (std::abs(ra.top - rb.top) > 6)
            return ra.top < rb.top;
        return ra.left < rb.left;
    });
    for (HWND child : children)
    {
        if (!IsWindowVisible(child))
            continue;
        const std::wstring cls = ClassName(child);
        const std::wstring value = StripMnemonic(WindowText(child));
        if (cls == L"Edit")
        {
            info.hasEdit = true;
            if (value.find(L":\\") != std::wstring::npos)
                info.paths.push_back(value);
            continue;
        }
        if (value.empty())
            continue;
        if (cls == L"Button")
        {
            const LONG style = GetWindowLongW(child, GWL_STYLE) & BS_TYPEMASK;
            const bool checkbox = style == BS_CHECKBOX || style == BS_AUTOCHECKBOX;
            AppendPart(text, value);
            if (checkbox)
                text += SendMessageW(child, BM_GETCHECK, 0, 0) == BST_CHECKED ? Tr(L", zaznaczone", L", checked")
                                                                              : Tr(L", niezaznaczone", L", not checked");
            else
                text += Tr(L", przycisk", L", button");
            if (!IsWindowEnabled(child))
                text += Tr(L", niedostępny", L", unavailable");
            continue;
        }
        AppendPart(text, value);
    }
    info.text = text;
    return info;
}

bool HandleLicense(HWND dialog)
{
    HWND agree = nullptr;
    HWND disagree = nullptr;
    for (HWND child : Children(dialog))
    {
        if (ClassName(child) != L"Button" || WindowText(child).empty())
            continue;
        // "Agree" stays disabled until the text was scrolled to the end.
        if (!IsWindowEnabled(child))
            agree = child;
        else if (disagree == nullptr)
            disagree = child;
    }
    const HWND browser = FindChildByClass(dialog, L"Internet Explorer_Server");

    const bool accepted = Ask(Tr(L"Okno umowy licencyjnej Blizzarda. Naciśnij Enter, aby zaakceptować umowę i kontynuować "
                                 L"instalację, albo Escape, aby ją odrzucić.",
                                 L"Blizzard licence agreement. Press Enter to accept the agreement and continue the "
                                 L"installation, or Escape to decline it."));
    if (!accepted)
    {
        if (disagree != nullptr)
            PostMessageW(disagree, BM_CLICK, 0, 0);
        Say(Tr(L"Umowa odrzucona.", L"Agreement declined."));
        return false;
    }
    if (agree == nullptr || browser == nullptr)
    {
        Say(Tr(L"Nie rozpoznaję okna umowy.", L"The agreement window was not recognised."));
        return false;
    }

    POINT saved{};
    GetCursorPos(&saved);
    BringToFront(dialog);
    RECT rect{};
    GetWindowRect(browser, &rect);
    SetCursorPos((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    Sleep(300);
    for (int attempt = 0; attempt < 8 && !IsWindowEnabled(agree); ++attempt)
    {
        PressKey(VK_END, true);
        Sleep(500);
    }
    SetCursorPos(saved.x, saved.y);

    if (!IsWindowEnabled(agree))
    {
        Say(Tr(L"Nie udało się odblokować przycisku akceptacji.", L"Could not enable the accept button."));
        return false;
    }
    BringToFront(dialog);
    PostMessageW(agree, BM_CLICK, 0, 0);
    Say(Tr(L"Umowa zaakceptowana.", L"Agreement accepted."));
    return true;
}

ULONGLONG FolderSize(const fs::path &folder)
{
    ULONGLONG total = 0;
    std::error_code error;
    for (fs::directory_iterator it(folder, error); !error && it != fs::directory_iterator(); it.increment(error))
    {
        if (it->is_regular_file(error))
            total += it->file_size(error);
    }
    return total;
}

bool NewInstallLog(const fs::path &folder, const fs::file_time_type &started)
{
    std::error_code error;
    for (fs::directory_iterator it(folder, error); !error && it != fs::directory_iterator(); it.increment(error))
    {
        const std::wstring name = it->path().filename().wstring();
        if (name.find(L"Install Log") != std::wstring::npos && it->last_write_time(error) > started)
            return true;
    }
    return false;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    g_polish = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_POLISH;
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring arg = argv[i];
        if (arg == L"--yes")
            g_autoYes = true;
        else if (arg == L"--stop-at-key")
            g_stopAtKey = true;
        else if (arg == L"--english")
            g_polish = false;
        else if (arg == L"--polish")
            g_polish = true;
    }

    InitializeLogging(L"D2AccessSetup.log");
    InitializeScreenReader();
    Sleep(300);
    Say(Tr(L"Asystent instalacji Diablo II. Obsłuży graficzne menu instalatora i umowę licencyjną, a pozostałe okna "
           L"przeczyta na głos. Klucz CD wpisujesz sam.",
           L"Diablo II installation assistant. It handles the graphical installer menu and the licence agreement and "
           L"reads the other windows aloud. You type the CD-key yourself."));

    const fs::path installer = ChooseInstaller(argc, argv);
    std::error_code error;
    if (installer.empty() || !fs::exists(installer, error))
    {
        Say(Tr(L"Nie wybrano instalatora. Koniec.", L"No installer selected. Exiting."));
        ShutdownScreenReader();
        return 1;
    }

    const std::wstring existing = ExistingInstallPath();
    if (!existing.empty() && fs::exists(fs::path(existing) / L"Game.exe", error))
    {
        Say(TrS(L"Uwaga: Diablo II jest już zainstalowane w folderze ", L"Note: Diablo II is already installed in ") +
            existing + Tr(L". Instalator może wtedy pokazać menu gry zamiast instalacji.",
                          L". The installer may then show the game menu instead of installing."));
    }

    const fs::path tome = installer.parent_path() / L"Installer Tome.mpq";
    const ULONGLONG expectedBytes = fs::exists(tome, error) ? fs::file_size(tome, error) : 1545313752ULL;
    const fs::file_time_type started = fs::file_time_type::clock::now();

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + installer.wstring() + L"\"";
    const std::wstring workDir = installer.parent_path().wstring();
    if (!CreateProcessW(installer.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, workDir.c_str(),
                        &startup, &process))
    {
        Say(Tr(L"Nie udało się uruchomić instalatora.", L"Could not start the installer."));
        ShutdownScreenReader();
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Say(Tr(L"Uruchamiam instalator, poczekaj chwilę.", L"Starting the installer, please wait."));

    std::map<HWND, std::wstring> dialogTexts;
    std::set<HWND> licenseDialogs;
    ULONGLONG mainSeenAt = 0;
    ULONGLONG lastWindowAt = GetTickCount64();
    bool pressedInstall = false;
    bool licenseAccepted = false;
    bool keyStepSeen = false;
    fs::path installDir = existing.empty() ? fs::path(L"C:\\Program Files (x86)\\Diablo II") : fs::path(existing);
    int lastPercent = -1;
    bool finished = false;

    while (!finished)
    {
        Sleep(LoopDelayMs);
        const ULONGLONG now = GetTickCount64();
        const std::vector<HWND> windows = InstallerWindows();
        if (windows.empty())
        {
            if (now - lastWindowAt > NoWindowExitMs)
            {
                Say(Tr(L"Instalator został zamknięty.", L"The installer was closed."));
                break;
            }
            continue;
        }
        lastWindowAt = now;

        HWND mainWindow = nullptr;
        std::vector<HWND> dialogs;
        for (HWND hwnd : windows)
        {
            const std::wstring cls = ClassName(hwnd);
            if (cls == L"#32770")
                dialogs.push_back(hwnd);
            else if (cls.rfind(L"(Blizzard Entertainment)", 0) == 0)
                mainWindow = hwnd;
        }

        for (HWND dialog : dialogs)
        {
            if (FindChildByClass(dialog, L"Internet Explorer_Server") != nullptr &&
                ClassName(dialog) == L"#32770" && FindChildByClass(dialog, L"Edit") == nullptr)
            {
                if (licenseDialogs.insert(dialog).second)
                {
                    licenseAccepted = HandleLicense(dialog) || licenseAccepted;
                    if (!licenseAccepted)
                        finished = true;
                }
                continue;
            }

            const DialogInfo info = ReadDialog(dialog);
            for (const std::wstring &path : info.paths)
                installDir = path;
            const auto known = dialogTexts.find(dialog);
            if (known != dialogTexts.end() && known->second == info.text)
                continue;
            dialogTexts[dialog] = info.text;
            BringToFront(dialog);
            std::wstring message = info.text;
            if (info.hasEdit)
                AppendPart(message, Tr(L"Wypełnij pola, przechodząc Tabem, i naciśnij Enter.",
                                       L"Fill in the fields, moving with Tab, and press Enter."));
            else
                AppendPart(message, Tr(L"Wybierz przycisk Tabem albo skrótem z Alt i zatwierdź.",
                                       L"Choose a button with Tab or its Alt shortcut and confirm."));
            Say(message);
            if (info.hasEdit)
            {
                keyStepSeen = true;
                if (g_stopAtKey)
                {
                    Say(L"--stop-at-key: closing the installer.");
                    for (HWND hwnd : InstallerWindows())
                        PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    finished = true;
                }
            }
        }
        if (finished || mainWindow == nullptr || !dialogs.empty() || !IsWindowEnabled(mainWindow))
            continue;

        if (mainSeenAt == 0)
            mainSeenAt = now;
        if (!pressedInstall)
        {
            if (now - mainSeenAt < MainMenuSettleMs)
                continue;
            Say(Tr(L"Menu instalatora. Wybieram instalację gry.", L"Installer menu. Choosing to install the game."));
            BringToFront(mainWindow);
            PressKey('D');
            pressedInstall = true;
            continue;
        }

        // Graphic progress page: report progress from the files written so far.
        if (licenseAccepted && keyStepSeen)
        {
            if (NewInstallLog(installDir, started))
            {
                Say(Tr(L"Instalacja zakończona. Zamykam instalator. Teraz możesz zainstalować dodatek Lord of Destruction "
                       L"albo uruchomić grę przez D2AccessLauncher.",
                       L"Installation complete. Closing the installer. You can now install Lord of Destruction or "
                       L"start the game with D2AccessLauncher."));
                PostMessageW(mainWindow, WM_CLOSE, 0, 0);
                finished = true;
                continue;
            }
            const ULONGLONG size = FolderSize(installDir);
            const int percent = static_cast<int>(std::min<ULONGLONG>(99, size * 100 / std::max<ULONGLONG>(expectedBytes, 1)));
            if (size > 0 && percent / 10 > lastPercent / 10)
            {
                lastPercent = percent;
                Say(Tr(L"Instalacja: ", L"Installing: ") + std::to_wstring(percent) + L"%");
            }
        }
    }

    Sleep(1500);
    Print(Tr(L"Naciśnij dowolny klawisz, aby zamknąć asystenta.", L"Press any key to close the assistant."));
    if (!g_autoYes)
        _getwch();
    ShutdownScreenReader();
    ShutdownLogging();
    return 0;
}
