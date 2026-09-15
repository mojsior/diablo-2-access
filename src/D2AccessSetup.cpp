// Accessible helper for installing classic Diablo II and the Diablo 2 Access mod.
//
// Blizzard's installer has a graphical main menu and a licence agreement whose
// "Agree" button only unlocks after the text was scrolled with the mouse, so
// screen reader users cannot get through it on their own. This helper drives
// those two steps, reads every standard dialog aloud (CD-key, folder, DirectX,
// errors) and reports progress. The player still types their own CD-key.
// Afterwards it downloads the newest Diablo 2 Access release from GitHub and
// installs it into the game folder.

#include "Logging.hpp"
#include "ScreenReader.hpp"

#include <nlohmann/json.hpp>

#include <Windows.h>
#include <Shlwapi.h>
#include <commdlg.h>
#include <conio.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Winhttp.lib")

namespace fs = std::filesystem;
using namespace d2access;

namespace {

constexpr DWORD LoopDelayMs = 250;
constexpr ULONGLONG MainMenuSettleMs = 5000;
constexpr ULONGLONG NoWindowExitMs = 20000;
constexpr int NvdaClientResourceId = 101;
constexpr const wchar_t *ReleasesUrl = L"https://api.github.com/repos/mojsior/diablo-2-access/releases?per_page=10";
constexpr const char *ReleaseAssetSuffix = "-windows.zip";

bool g_polish = true;
bool g_autoYes = false;
bool g_stopAtKey = false;
bool g_modOnly = false;
bool g_noMod = false;

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
// only while another window is in front.
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

std::wstring Widen(const std::string &text)
{
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
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
    std::transform(name.begin(), name.end(), name.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
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

// The single-file download carries the NVDA controller client as a resource.
void ExtractEmbeddedNvdaClient()
{
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(NvdaClientResourceId), RT_RCDATA);
    if (resource == nullptr)
        return;
    const HGLOBAL loaded = LoadResource(nullptr, resource);
    const DWORD size = SizeofResource(nullptr, resource);
    const void *data = loaded != nullptr ? LockResource(loaded) : nullptr;
    if (data == nullptr || size == 0)
        return;

    std::error_code error;
    const fs::path folder = fs::temp_directory_path(error) / L"D2AccessSetup";
    fs::create_directories(folder, error);
    const fs::path target = folder / L"nvdaControllerClient32.dll";
    if (fs::exists(target, error) && fs::file_size(target, error) == size)
        return;
    std::ofstream file(target, std::ios::binary | std::ios::trunc);
    file.write(static_cast<const char *>(data), size);
}

// ---------------------------------------------------------------------------
// Diablo 2 Access download
// ---------------------------------------------------------------------------

bool HttpGet(const std::wstring &url, std::string &body, const std::function<void(ULONGLONG, ULONGLONG)> &progress)
{
    body.clear();
    wchar_t host[256] = {};
    wchar_t path[4096] = {};
    wchar_t extra[2048] = {};
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts))
        return false;

    const std::wstring object = std::wstring(path, parts.dwUrlPathLength) + std::wstring(extra, parts.dwExtraInfoLength);
    const HINTERNET session = WinHttpOpen(L"D2AccessSetup/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
        return false;
    const HINTERNET connection = WinHttpConnect(session, std::wstring(host, parts.dwHostNameLength).c_str(), parts.nPort, 0);
    const HINTERNET request =
        connection != nullptr
            ? WinHttpOpenRequest(connection, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
            : nullptr;

    bool ok = false;
    if (request != nullptr &&
        WinHttpSendRequest(request, L"Accept: application/vnd.github+json, application/octet-stream\r\n",
                           static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr))
    {
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
        DWORD length = 0;
        DWORD lengthSize = sizeof(length);
        const ULONGLONG total =
            WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &length, &lengthSize, WINHTTP_NO_HEADER_INDEX)
                ? length
                : 0;

        ok = status == 200;
        std::vector<char> buffer(64 * 1024);
        for (;;)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available))
            {
                ok = false;
                break;
            }
            if (available == 0)
                break;
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer.data(), std::min<DWORD>(available, static_cast<DWORD>(buffer.size())), &read))
            {
                ok = false;
                break;
            }
            body.append(buffer.data(), read);
            if (progress)
                progress(body.size(), total);
        }
        LogLine(L"HTTP " + std::to_wstring(status) + L" " + url + L" (" + std::to_wstring(body.size()) + L" bytes)");
    }

    if (request != nullptr)
        WinHttpCloseHandle(request);
    if (connection != nullptr)
        WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

struct ReleaseAsset {
    std::wstring tag;
    std::wstring name;
    std::wstring url;
};

// The newest non-draft release, prereleases included (1.0 beta is one).
bool FindLatestRelease(ReleaseAsset &asset)
{
    std::string body;
    if (!HttpGet(ReleasesUrl, body, nullptr))
        return false;
    const nlohmann::json releases = nlohmann::json::parse(body, nullptr, false);
    if (!releases.is_array())
        return false;
    for (const nlohmann::json &release : releases)
    {
        if (release.value("draft", false) || !release.contains("assets"))
            continue;
        for (const nlohmann::json &item : release["assets"])
        {
            const std::string name = item.value("name", std::string());
            const std::string suffix = ReleaseAssetSuffix;
            if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            {
                asset.tag = Widen(release.value("tag_name", std::string()));
                asset.name = Widen(name);
                asset.url = Widen(item.value("browser_download_url", std::string()));
                return !asset.url.empty();
            }
        }
    }
    return false;
}

bool RunHidden(const std::wstring &commandLine)
{
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    std::wstring command = commandLine;
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &process))
        return false;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode == 0;
}

bool InstallMod(const fs::path &gameDir)
{
    Say(Tr(L"Sprawdzam najnowszą wersję moda Diablo 2 Access na GitHubie.",
           L"Checking GitHub for the newest Diablo 2 Access release."));
    ReleaseAsset asset;
    if (!FindLatestRelease(asset))
    {
        Say(Tr(L"Nie udało się pobrać informacji o wydaniu. Sprawdź połączenie z internetem.",
               L"Could not read the release information. Check your internet connection."));
        return false;
    }

    Say(TrS(L"Pobieram wersję ", L"Downloading version ") + asset.tag + L".");
    int lastQuarter = 0;
    std::string archive;
    const bool downloaded = HttpGet(asset.url, archive, [&lastQuarter](ULONGLONG done, ULONGLONG total) {
        if (total == 0)
            return;
        const int quarter = static_cast<int>(done * 4 / total);
        if (quarter > lastQuarter && quarter < 4)
        {
            lastQuarter = quarter;
            Say(TrS(L"Pobrano ", L"Downloaded ") + std::to_wstring(quarter * 25) + L"%");
        }
    });
    if (!downloaded || archive.empty())
    {
        Say(Tr(L"Pobieranie moda nie powiodło się.", L"Downloading the mod failed."));
        return false;
    }

    std::error_code error;
    const fs::path work = fs::temp_directory_path(error) / L"D2AccessSetup";
    const fs::path zip = work / asset.name;
    const fs::path extracted = work / L"release";
    fs::create_directories(work, error);
    fs::remove_all(extracted, error);
    fs::create_directories(extracted, error);
    {
        std::ofstream file(zip, std::ios::binary | std::ios::trunc);
        file.write(archive.data(), static_cast<std::streamsize>(archive.size()));
    }

    wchar_t system[MAX_PATH] = {};
    GetSystemDirectoryW(system, MAX_PATH);
    const std::wstring tar = std::wstring(system) + L"\\tar.exe";
    if (!RunHidden(L"\"" + tar + L"\" -xf \"" + zip.wstring() + L"\" -C \"" + extracted.wstring() + L"\""))
    {
        Say(Tr(L"Nie udało się rozpakować moda.", L"Could not extract the mod."));
        return false;
    }

    // Accept a zip with one top-level folder as well as files at the root.
    fs::path source = extracted;
    std::vector<fs::path> entries;
    for (const fs::directory_entry &entry : fs::directory_iterator(extracted, error))
        entries.push_back(entry.path());
    if (entries.size() == 1 && fs::is_directory(entries.front(), error))
        source = entries.front();

    fs::copy(source, gameDir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
    if (error || !fs::exists(gameDir / L"D2AccessLauncher.exe"))
    {
        LogLine(L"Copy error: " + Widen(error.message()));
        Say(Tr(L"Nie udało się skopiować moda do folderu gry. Zamknij grę, jeśli jest uruchomiona, i spróbuj ponownie.",
               L"Could not copy the mod into the game folder. Close the game if it is running and try again."));
        return false;
    }

    Say(TrS(L"Mod Diablo 2 Access w wersji ", L"Diablo 2 Access version ") + asset.tag +
        TrS(L" jest zainstalowany w folderze ", L" is installed in ") + gameDir.wstring() +
        Tr(L". Grę z modem uruchamiasz plikiem D2AccessLauncher.exe z tego folderu.",
           L". Start the game with the mod using D2AccessLauncher.exe from that folder."));
    return true;
}

// ---------------------------------------------------------------------------
// Finding the installer
// ---------------------------------------------------------------------------

bool IsInstallerFolder(const fs::path &folder)
{
    std::error_code error;
    return fs::exists(folder / L"Installer.exe", error) && fs::exists(folder / L"Installer Tome.mpq", error);
}

std::wstring EnvironmentFolder(const wchar_t *variable, const wchar_t *suffix)
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
        std::error_code error;
        if (!IsInstallerFolder(folder))
            return;
        const fs::path canonical = fs::weakly_canonical(folder, error);
        if (std::find(found.begin(), found.end(), canonical) == found.end())
            found.push_back(canonical);
    };

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    add(fs::path(exePath).parent_path());
    add(fs::current_path());

    for (const std::wstring &root :
         {EnvironmentFolder(L"USERPROFILE", L"\\Downloads"), EnvironmentFolder(L"USERPROFILE", L"\\Desktop")})
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

fs::path ChooseInstaller(const std::wstring &argument)
{
    if (!argument.empty())
    {
        fs::path path(argument);
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

    Say(Tr(L"Nie znaleziono instalatora gry. Otworzy się okno wyboru pliku, wskaż Installer.exe.",
           L"No game installer was found. A file dialog opens, select Installer.exe."));
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
    std::error_code error;
    return fs::exists(fs::path(buffer) / L"Game.exe", error) ? std::wstring(buffer) : std::wstring();
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

// Runs Blizzard's installer. Returns the folder the game was installed to, or
// an empty path when the installation did not finish.
fs::path RunGameInstaller(const fs::path &installer)
{
    std::error_code error;
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
        return {};
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Say(Tr(L"Uruchamiam instalator gry, poczekaj chwilę.", L"Starting the game installer, please wait."));

    std::map<HWND, std::wstring> dialogTexts;
    std::set<HWND> licenseDialogs;
    ULONGLONG mainSeenAt = 0;
    ULONGLONG lastWindowAt = GetTickCount64();
    bool pressedInstall = false;
    bool licenseAccepted = false;
    bool keyStepSeen = false;
    fs::path installDir = L"C:\\Program Files (x86)\\Diablo II";
    int lastPercent = -1;

    for (;;)
    {
        Sleep(LoopDelayMs);
        const ULONGLONG now = GetTickCount64();
        const std::vector<HWND> windows = InstallerWindows();
        if (windows.empty())
        {
            if (now - lastWindowAt > NoWindowExitMs)
            {
                Say(Tr(L"Instalator został zamknięty przed końcem instalacji.",
                       L"The installer was closed before the installation finished."));
                return {};
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
            if (FindChildByClass(dialog, L"Internet Explorer_Server") != nullptr && FindChildByClass(dialog, L"Edit") == nullptr)
            {
                if (licenseDialogs.insert(dialog).second)
                {
                    licenseAccepted = HandleLicense(dialog) || licenseAccepted;
                    if (!licenseAccepted)
                        return {};
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
                    return {};
                }
            }
        }
        if (mainWindow == nullptr || !dialogs.empty() || !IsWindowEnabled(mainWindow))
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
                Say(Tr(L"Instalacja gry zakończona. Zamykam instalator.", L"The game installation is complete. Closing the installer."));
                PostMessageW(mainWindow, WM_CLOSE, 0, 0);
                const std::wstring registered = ExistingInstallPath();
                return registered.empty() ? installDir : fs::path(registered);
            }
            const ULONGLONG size = FolderSize(installDir);
            const int percent = static_cast<int>(std::min<ULONGLONG>(99, size * 100 / std::max<ULONGLONG>(expectedBytes, 1)));
            if (size > 0 && percent / 10 > lastPercent / 10)
            {
                lastPercent = percent;
                Say(Tr(L"Instalacja gry: ", L"Installing the game: ") + std::to_wstring(percent) + L"%");
            }
        }
    }
}

int Finish(int code)
{
    Sleep(1500);
    Print(Tr(L"Naciśnij dowolny klawisz, aby zamknąć asystenta.", L"Press any key to close the assistant."));
    if (!g_autoYes)
        _getwch();
    ShutdownScreenReader();
    ShutdownLogging();
    return code;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    g_polish = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_POLISH;
    std::wstring pathArgument;
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring arg = argv[i];
        if (arg == L"--yes")
            g_autoYes = true;
        else if (arg == L"--stop-at-key")
            g_stopAtKey = true;
        else if (arg == L"--mod-only")
            g_modOnly = true;
        else if (arg == L"--no-mod")
            g_noMod = true;
        else if (arg == L"--english")
            g_polish = false;
        else if (arg == L"--polish")
            g_polish = true;
        else if (arg.rfind(L"--", 0) != 0)
            pathArgument = arg;
    }

    ExtractEmbeddedNvdaClient();
    InitializeLogging(L"D2AccessSetup.log");
    InitializeScreenReader();
    Sleep(300);
    Say(Tr(L"Asystent instalacji Diablo II i moda Diablo 2 Access. Przeprowadzi przez instalator gry, czytając jego okna, "
           L"a potem pobierze i zainstaluje najnowszą wersję moda w folderze gry. Klucz CD wpisujesz sam.",
           L"Installation assistant for Diablo II and the Diablo 2 Access mod. It guides you through the game installer, "
           L"reading its windows, then downloads the newest mod release and installs it into the game folder. You type "
           L"the CD-key yourself."));

    std::error_code error;
    if (g_modOnly)
    {
        const fs::path gameDir = pathArgument.empty() ? fs::path(ExistingInstallPath()) : fs::path(pathArgument);
        if (gameDir.empty() || !fs::exists(gameDir / L"Game.exe", error))
        {
            Say(Tr(L"Nie znaleziono zainstalowanego Diablo II.", L"No Diablo II installation was found."));
            return Finish(1);
        }
        return Finish(InstallMod(gameDir) ? 0 : 1);
    }

    const std::wstring existing = ExistingInstallPath();
    if (!existing.empty())
    {
        if (Ask(TrS(L"Diablo II jest już zainstalowane w folderze ", L"Diablo II is already installed in ") + existing +
                Tr(L". Naciśnij Enter, aby zainstalować lub zaktualizować tylko moda Diablo 2 Access, albo Escape, aby "
                   L"mimo to uruchomić instalator gry.",
                   L". Press Enter to install or update only the Diablo 2 Access mod, or Escape to run the game "
                   L"installer anyway.")))
            return Finish(InstallMod(existing) ? 0 : 1);
    }

    const fs::path installer = ChooseInstaller(pathArgument);
    if (installer.empty() || !fs::exists(installer, error))
    {
        Say(Tr(L"Nie wybrano instalatora. Koniec.", L"No installer selected. Exiting."));
        return Finish(1);
    }

    const fs::path gameDir = RunGameInstaller(installer);
    if (gameDir.empty())
        return Finish(1);

    if (g_noMod)
        return Finish(0);
    Sleep(2000);
    if (!Ask(Tr(L"Zainstalować teraz moda Diablo 2 Access w folderze gry? Enter instaluje, Escape pomija.",
                L"Install the Diablo 2 Access mod into the game folder now? Enter installs, Escape skips.")))
        return Finish(0);
    const bool installed = InstallMod(gameDir);
    if (installed)
        Say(Tr(L"Gotowe. Jeśli masz dodatek Lord of Destruction, zainstaluj go teraz jego instalatorem, uruchamiając "
               L"ponownie tego asystenta.",
               L"Done. If you have Lord of Destruction, install it now with its installer by running this assistant "
               L"again."));
    return Finish(installed ? 0 : 1);
}
