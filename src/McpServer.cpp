#include "McpServer.hpp"

#include "D2Game.hpp"
#include "EventLog.hpp"
#include "GameplayQueries.hpp"
#include "Logging.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

namespace d2access {

namespace {

using json = nlohmann::json;

constexpr int BasePort = 13450;
constexpr int PortAttempts = 10;
constexpr DWORD GameQueryTimeoutMs = 4000;
constexpr size_t MaxHeaderBytes = 64 * 1024;
constexpr size_t MaxBodyBytes = 1024 * 1024;

std::atomic<bool> g_running = false;
std::atomic<SOCKET> g_listenSocket = INVALID_SOCKET;

struct ToolResult {
    json value;
    bool isError = false;
};

struct ToolDef {
    std::string name;
    std::string description;
    json schema;
    std::function<ToolResult(const json &)> handler;
};

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
};

std::string ToLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

std::string Trim(const std::string &text)
{
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    const size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

std::string Dump(const json &value)
{
    return value.dump(2, ' ', false, json::error_handler_t::replace);
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

bool ReceiveRequest(SOCKET socket, HttpRequest &request)
{
    std::string data;
    char buffer[4096];
    size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos)
    {
        const int received = recv(socket, buffer, sizeof(buffer), 0);
        if (received <= 0)
            return false;
        data.append(buffer, static_cast<size_t>(received));
        headerEnd = data.find("\r\n\r\n");
        if (headerEnd == std::string::npos && data.size() > MaxHeaderBytes)
            return false;
    }

    std::istringstream headerStream(data.substr(0, headerEnd));
    std::string line;
    if (!std::getline(headerStream, line))
        return false;
    std::istringstream requestLine(Trim(line));
    requestLine >> request.method >> request.path;
    const size_t query = request.path.find('?');
    if (query != std::string::npos)
        request.path.resize(query);

    while (std::getline(headerStream, line))
    {
        const size_t colon = line.find(':');
        if (colon != std::string::npos)
            request.headers[ToLower(Trim(line.substr(0, colon)))] = Trim(line.substr(colon + 1));
    }

    size_t contentLength = 0;
    const auto length = request.headers.find("content-length");
    if (length != request.headers.end())
    {
        try
        {
            contentLength = static_cast<size_t>(std::stoul(length->second));
        }
        catch (...)
        {
            return false;
        }
    }
    if (contentLength > MaxBodyBytes)
        return false;

    request.body = data.substr(headerEnd + 4);
    while (request.body.size() < contentLength)
    {
        const int received = recv(socket, buffer, sizeof(buffer), 0);
        if (received <= 0)
            return false;
        request.body.append(buffer, static_cast<size_t>(received));
    }
    request.body.resize(contentLength);
    return true;
}

void SendAll(SOCKET socket, const std::string &data)
{
    size_t sent = 0;
    while (sent < data.size())
    {
        const int chunk = static_cast<int>(std::min<size_t>(data.size() - sent, 65536));
        const int result = send(socket, data.data() + sent, chunk, 0);
        if (result <= 0)
            return;
        sent += static_cast<size_t>(result);
    }
}

void SendResponse(SOCKET socket, int status, const char *reason, const std::string &body,
                  const char *extraHeaders = "")
{
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << extraHeaders << "Connection: close\r\n\r\n"
             << body;
    SendAll(socket, response.str());
}

std::string HostWithoutPort(const std::string &value)
{
    if (!value.empty() && value.front() == '[')
    {
        const size_t end = value.find(']');
        return end == std::string::npos ? value : value.substr(0, end + 1);
    }
    return value.substr(0, value.find(':'));
}

bool IsLoopbackName(const std::string &host)
{
    const std::string lower = ToLower(host);
    return lower == "127.0.0.1" || lower == "localhost" || lower == "[::1]";
}

// Blocks DNS rebinding and requests from web pages: only loopback Host headers,
// and an Origin header (sent by browsers) must be loopback too.
bool IsAllowedRequest(const HttpRequest &request)
{
    const auto host = request.headers.find("host");
    if (host == request.headers.end() || !IsLoopbackName(HostWithoutPort(host->second)))
        return false;

    const auto origin = request.headers.find("origin");
    if (origin == request.headers.end())
        return true;

    std::string value = ToLower(origin->second);
    const size_t scheme = value.find("://");
    if (scheme == std::string::npos)
        return false;
    value = value.substr(scheme + 3);
    value = value.substr(0, value.find('/'));
    return IsLoopbackName(HostWithoutPort(value));
}

// ---------------------------------------------------------------------------
// Input helpers
// ---------------------------------------------------------------------------

bool ParseKey(const std::string &name, DWORD &virtualKey)
{
    const std::string key = ToLower(Trim(name));
    static const std::map<std::string, DWORD> names = {
        {"enter", VK_RETURN},  {"return", VK_RETURN}, {"escape", VK_ESCAPE},  {"esc", VK_ESCAPE},
        {"space", VK_SPACE},   {"tab", VK_TAB},       {"backspace", VK_BACK}, {"up", VK_UP},
        {"down", VK_DOWN},     {"left", VK_LEFT},     {"right", VK_RIGHT},    {"pageup", VK_PRIOR},
        {"pagedown", VK_NEXT}, {"home", VK_HOME},     {"end", VK_END},        {"insert", VK_INSERT},
        {"delete", VK_DELETE}, {"clear", VK_CLEAR},   {"alt", VK_MENU},       {"shift", VK_SHIFT},
        {"ctrl", VK_CONTROL},
    };

    if (const auto it = names.find(key); it != names.end())
    {
        virtualKey = it->second;
        return true;
    }
    if (key.size() == 1 && std::isalnum(static_cast<unsigned char>(key[0])))
    {
        virtualKey = static_cast<DWORD>(std::toupper(static_cast<unsigned char>(key[0])));
        return true;
    }
    try
    {
        if (key.size() >= 2 && key[0] == 'f' && std::isdigit(static_cast<unsigned char>(key[1])))
        {
            const int number = std::stoi(key.substr(1));
            if (number >= 1 && number <= 24)
            {
                virtualKey = static_cast<DWORD>(VK_F1 + number - 1);
                return true;
            }
        }
        if (key.rfind("numpad", 0) == 0 && key.size() == 7 && std::isdigit(static_cast<unsigned char>(key[6])))
        {
            virtualKey = static_cast<DWORD>(VK_NUMPAD0 + (key[6] - '0'));
            return true;
        }
        if (key.rfind("0x", 0) == 0)
        {
            virtualKey = static_cast<DWORD>(std::stoul(key, nullptr, 16));
            return virtualKey > 0 && virtualKey < 256;
        }
        if (!key.empty() && std::all_of(key.begin(), key.end(), [](unsigned char ch) { return std::isdigit(ch); }))
        {
            virtualKey = static_cast<DWORD>(std::stoul(key));
            return virtualKey > 0 && virtualKey < 256;
        }
    }
    catch (...)
    {
    }
    return false;
}

HWND FindGameWindow()
{
    struct Search {
        DWORD pid;
        HWND window;
    } search{GetCurrentProcessId(), nullptr};

    EnumWindows(
        [](HWND window, LPARAM param) -> BOOL {
            auto *state = reinterpret_cast<Search *>(param);
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (pid == state->pid && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr)
            {
                state->window = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.window;
}

bool PostKeyToWindow(HWND window, DWORD virtualKey)
{
    const UINT scan = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);
    const bool extended = virtualKey == VK_UP || virtualKey == VK_DOWN || virtualKey == VK_LEFT ||
                          virtualKey == VK_RIGHT || virtualKey == VK_HOME || virtualKey == VK_END ||
                          virtualKey == VK_PRIOR || virtualKey == VK_NEXT || virtualKey == VK_INSERT ||
                          virtualKey == VK_DELETE;
    const LPARAM down = static_cast<LPARAM>(1 | (scan << 16) | (extended ? (1u << 24) : 0u));
    const LPARAM up = static_cast<LPARAM>(static_cast<std::uint32_t>(down) | (1u << 30) | (1u << 31));
    return PostMessageW(window, WM_KEYDOWN, virtualKey, down) != FALSE &&
           PostMessageW(window, WM_KEYUP, virtualKey, up) != FALSE;
}

// ---------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------

ToolResult Ok(json value)
{
    return ToolResult{std::move(value), false};
}

ToolResult Fail(const std::string &message)
{
    return ToolResult{json{{"error", message}}, true};
}

ToolResult RunGameQuery(std::function<json()> query)
{
    if (!IsInGame())
        return Fail("Postać nie jest w grze. Wejdź do gry (np. press_key w menu), potem spróbuj ponownie.");
    std::optional<json> result = QueryGameThread(std::move(query), GameQueryTimeoutMs);
    if (!result)
        return Fail("Wątek gry nie odpowiedział w 4 sekundy (gra wstrzymana albo w trakcie ładowania).");
    const bool isError = result->is_object() && result->contains("error");
    return ToolResult{std::move(*result), isError};
}

std::optional<int> OptionalInt(const json &args, const char *name)
{
    if (!args.contains(name) || args[name].is_null())
        return std::nullopt;
    if (args[name].is_number_integer())
        return args[name].get<int>();
    return std::nullopt;
}

json EventToJson(const GameEvent &event)
{
    return json{{"id", event.id}, {"tick_ms", event.tickMs}, {"type", event.type}, {"text", event.text}};
}

json ObjectSchema(json properties, json required = json::array())
{
    return json{{"type", "object"}, {"properties", std::move(properties)}, {"required", std::move(required)}};
}

const std::vector<ToolDef> &Tools()
{
    static const std::vector<ToolDef> tools = [] {
        std::vector<ToolDef> list;

        list.push_back(ToolDef{
            "game_status",
            "Stan gry i moda: czy postać jest w grze, pozycja (podpola), tryb animacji, poziom i jego nazwa, "
            "czy mapa poziomu jest wczytana, wybrana kategoria trackera, stan autowalku, id ostatniego zdarzenia.",
            ObjectSchema(json::object()),
            [](const json &) {
                if (!IsInGame())
                    return Ok(json{{"in_game", false}, {"last_event_id", LastEventId()}});
                return RunGameQuery([] { return QueryStatus(); });
            }});

        list.push_back(ToolDef{
            "get_events",
            "Zdarzenia moda od podanego id: speech (to, co powiedział czytnik), log (log moda), level, game, "
            "autowalk, mcp. Zwraca last_event_id do kolejnego wywołania.",
            ObjectSchema(json{{"after_id", {{"type", "integer"}, {"description", "Zwróć zdarzenia o id większym niż to (0 = od początku)."}}},
                              {"limit", {{"type", "integer"}, {"description", "Maksymalnie zdarzeń (domyślnie 100, max 500)."}}},
                              {"type", {{"type", "string"}, {"description", "Filtr typu, np. speech."}}}}),
            [](const json &args) {
                const std::uint64_t afterId = args.value("after_id", std::uint64_t{0});
                const size_t limit = std::clamp<size_t>(args.value("limit", size_t{100}), 1, 500);
                const std::string type = args.value("type", std::string());
                json events = json::array();
                for (const GameEvent &event : GetEvents(afterId, limit, type))
                    events.push_back(EventToJson(event));
                return Ok(json{{"events", std::move(events)}, {"last_event_id", LastEventId()}});
            }});

        list.push_back(ToolDef{
            "wait_for_event",
            "Czeka na pierwsze zdarzenie nowsze niż after_id, pasujące do typu i fragmentu tekstu "
            "(bez rozróżniania wielkości liter). Np. po Shift+Home czekaj na speech zawierające 'zasięgu'.",
            ObjectSchema(json{{"after_id", {{"type", "integer"}}},
                              {"type", {{"type", "string"}}},
                              {"contains", {{"type", "string"}}},
                              {"timeout_ms", {{"type", "integer"}, {"description", "Domyślnie 10000, max 60000."}}}}),
            [](const json &args) {
                const std::uint64_t afterId = args.value("after_id", LastEventId());
                const DWORD timeout = static_cast<DWORD>(std::clamp(args.value("timeout_ms", 10000), 0, 60000));
                GameEvent event;
                if (!WaitForEvent(afterId, args.value("type", std::string()), args.value("contains", std::string()),
                                  timeout, event))
                    return Ok(json{{"found", false}, {"last_event_id", LastEventId()}});
                return Ok(json{{"found", true}, {"event", EventToJson(event)}, {"last_event_id", LastEventId()}});
            }});

        json categories = json::array();
        for (const std::string &name : TargetCategoryNames())
            categories.push_back(name);

        list.push_back(ToolDef{
            "list_targets",
            "Lista celów trackera w kategorii, posortowana wg odległości, z kluczem (key) do find_path. "
            "Źródło: unit (załadowana jednostka), preset (obiekt z mapy poziomu), exit (przejście).",
            ObjectSchema(json{{"category", {{"type", "string"}, {"enum", categories}}}}, json::array({"category"})),
            [](const json &args) {
                const std::string category = args.value("category", std::string());
                return RunGameQuery([category] { return QueryTargets(category); });
            }});

        list.push_back(ToolDef{
            "level_info",
            "Mapa bieżącego poziomu: granice w podpolach, liczba pokoi i pokoi z kolizją, presety wg typu, "
            "lista wyjść z pozycjami.",
            ObjectSchema(json::object()),
            [](const json &) { return RunGameQuery([] { return QueryLevel(); }); }});

        list.push_back(ToolDef{
            "find_path",
            "Liczy ścieżkę od postaci do celu (target_key z list_targets) albo do punktu x,y tym samym "
            "pathfinderem co Home. Zwraca opis słowny, liczbę kroków, drzwi na drodze i komórki ścieżki.",
            ObjectSchema(json{{"target_key", {{"type", "string"}}}, {"x", {{"type", "integer"}}}, {"y", {{"type", "integer"}}}}),
            [](const json &args) {
                const std::optional<int> x = OptionalInt(args, "x");
                const std::optional<int> y = OptionalInt(args, "y");
                const std::string key = args.value("target_key", std::string());
                return RunGameQuery([x, y, key] { return QueryPath(x, y, key); });
            }});

        list.push_back(ToolDef{
            "collision_map",
            "Mapa ASCII kolizji wokół punktu (domyślnie postaci). @ postać, . można stanąć, , przejście tylko "
            "wąskie, # ściana/obiekt, D drzwi, ? nieznane. Wiersze od północy (y rośnie w dół), x rośnie w prawo.",
            ObjectSchema(json{{"x", {{"type", "integer"}}}, {"y", {{"type", "integer"}}},
                              {"radius", {{"type", "integer"}, {"description", "Domyślnie 20, max 40."}}}}),
            [](const json &args) {
                const std::optional<int> x = OptionalInt(args, "x");
                const std::optional<int> y = OptionalInt(args, "y");
                const int radius = std::clamp(args.value("radius", 20), 1, 40);
                return RunGameQuery([x, y, radius] { return QueryCollision(x, y, radius); });
            }});

        list.push_back(ToolDef{
            "nearby_units",
            "Wszystkie załadowane jednostki w promieniu (także te, które tracker pomija): typ, classId, id, "
            "tryb, pozycja, nazwa i kategoria trackera. Do diagnozy klasyfikacji.",
            ObjectSchema(json{{"radius", {{"type", "integer"}, {"description", "Domyślnie 40, max 200."}}}}),
            [](const json &args) {
                const int radius = std::clamp(args.value("radius", 40), 1, 200);
                return RunGameQuery([radius] { return QueryUnits(radius); });
            }});

        list.push_back(ToolDef{
            "press_key",
            "Naciska klawisz. W grze klawisze moda (PageUp, PageDown, Home, E, F, G, K, L, F1, strzałki, "
            "numpad) idą do moda z podanymi modyfikatorami, np. Shift+Home = autowalk. Pozostałe klawisze i "
            "menu gry dostają komunikat klawiatury w oknie gry (np. Enter, Escape, I).",
            ObjectSchema(json{{"key", {{"type", "string"}, {"description", "Np. PageDown, Home, E, Enter, F1, Numpad8, 0x41."}}},
                              {"ctrl", {{"type", "boolean"}}},
                              {"shift", {{"type", "boolean"}}}},
                         json::array({"key"})),
            [](const json &args) {
                DWORD virtualKey = 0;
                const std::string name = args.value("key", std::string());
                if (!ParseKey(name, virtualKey))
                    return Fail("Nieznany klawisz: " + name);
                const bool ctrl = args.value("ctrl", false);
                const bool shift = args.value("shift", false);
                const std::uint64_t before = LastEventId();
                if (IsInGame() && IsGameplayKey(virtualKey))
                {
                    InjectGameplayKey(virtualKey, ctrl, shift);
                    return Ok(json{{"sent", name}, {"via", "mod"}, {"events_after_id", before}});
                }
                HWND window = FindGameWindow();
                if (window == nullptr || !PostKeyToWindow(window, virtualKey))
                    return Fail("Nie znaleziono okna gry.");
                return Ok(json{{"sent", name}, {"via", "window"}, {"events_after_id", before}});
            }});

        list.push_back(ToolDef{
            "hold_direction",
            "Trzyma strzałki kierunku przez podany czas (ruch ciągły moda), potem zwraca pozycję. Kierunki: "
            "n, s, e, w, ne, nw, se, sw (północ = w górę mapy, y maleje).",
            ObjectSchema(json{{"direction", {{"type", "string"}, {"enum", {"n", "s", "e", "w", "ne", "nw", "se", "sw"}}}},
                              {"duration_ms", {{"type", "integer"}, {"description", "Domyślnie 1000, max 5000."}}}},
                         json::array({"direction"})),
            [](const json &args) {
                if (!IsInGame())
                    return Fail("Postać nie jest w grze.");
                const std::string direction = ToLower(args.value("direction", std::string()));
                std::vector<DWORD> keys;
                if (direction.find('n') != std::string::npos)
                    keys.push_back(VK_UP);
                if (direction.find('s') != std::string::npos)
                    keys.push_back(VK_DOWN);
                if (direction.find('e') != std::string::npos)
                    keys.push_back(VK_RIGHT);
                if (direction.find('w') != std::string::npos)
                    keys.push_back(VK_LEFT);
                if (keys.empty())
                    return Fail("Nieznany kierunek: " + direction);
                const int duration = std::clamp(args.value("duration_ms", 1000), 50, 5000);
                for (DWORD key : keys)
                    SetInjectedKeyHeld(key, true);
                Sleep(static_cast<DWORD>(duration));
                for (DWORD key : keys)
                    SetInjectedKeyHeld(key, false);
                return RunGameQuery([] { return QueryStatus(); });
            }});

        list.push_back(ToolDef{
            "type_text",
            "Wpisuje tekst do okna gry (np. imię postaci w menu tworzenia).",
            ObjectSchema(json{{"text", {{"type", "string"}}}}, json::array({"text"})),
            [](const json &args) {
                HWND window = FindGameWindow();
                if (window == nullptr)
                    return Fail("Nie znaleziono okna gry.");
                const std::wstring text = WideFromUtf8(args.value("text", std::string()));
                for (wchar_t ch : text)
                    PostMessageW(window, WM_CHAR, ch, 1);
                return Ok(json{{"typed_chars", text.size()}});
            }});

        list.push_back(ToolDef{
            "read_memory",
            "Czyta pamięć procesu gry (do dalszej inżynierii wstecznej). Adres jako liczba albo tekst hex, "
            "np. 0x79D0B0. Zwraca bajty hex i wartości DWORD.",
            ObjectSchema(json{{"address", {{"type", "string"}}},
                              {"size", {{"type", "integer"}, {"description", "Domyślnie 64, max 1024."}}}},
                         json::array({"address"})),
            [](const json &args) {
                uintptr_t address = 0;
                try
                {
                    if (args.contains("address") && args["address"].is_number_unsigned())
                        address = args["address"].get<uintptr_t>();
                    else
                        address = static_cast<uintptr_t>(std::stoull(args.value("address", std::string()), nullptr, 0));
                }
                catch (...)
                {
                    return Fail("Nieprawidłowy adres.");
                }
                const size_t size = std::clamp<size_t>(args.value("size", size_t{64}), 1, 1024);
                std::vector<std::uint8_t> bytes(size);
                if (!game::ReadBytes(address, bytes.data(), bytes.size()))
                    return Fail("Nie można odczytać pamięci pod tym adresem.");

                std::string hex;
                char buffer[16];
                for (size_t i = 0; i < bytes.size(); ++i)
                {
                    std::snprintf(buffer, sizeof(buffer), i == 0 ? "%02x" : " %02x", bytes[i]);
                    hex += buffer;
                }
                json dwords = json::array();
                for (size_t i = 0; i + 4 <= bytes.size() && dwords.size() < 64; i += 4)
                {
                    std::uint32_t value = 0;
                    std::memcpy(&value, bytes.data() + i, 4);
                    std::snprintf(buffer, sizeof(buffer), "0x%08x", value);
                    dwords.push_back(buffer);
                }
                std::snprintf(buffer, sizeof(buffer), "0x%08zx", static_cast<size_t>(address));
                return Ok(json{{"address", buffer}, {"size", size}, {"hex", hex}, {"dwords", dwords}});
            }});

        return list;
    }();
    return tools;
}

// ---------------------------------------------------------------------------
// JSON-RPC / MCP
// ---------------------------------------------------------------------------

constexpr const char *ServerInstructions =
    "Serwer moda D2 Access działający wewnątrz Diablo II (Game.exe 1.14b). Współrzędne są w podpolach gry "
    "(1 kafel = 5 podpól), północ to malejące y. Typowy test: game_status, list_targets, press_key PageDown, "
    "press_key Home z shift=true (autowalk), wait_for_event z type=speech, get_events. Narzędzia gry działają "
    "tylko, gdy postać jest w grze; w menu użyj press_key i type_text.";

json HandleMessage(const json &message)
{
    const bool hasId = message.is_object() && message.contains("id");
    const json id = hasId ? message["id"] : json(nullptr);
    const auto error = [&](int code, const std::string &text) {
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", text}}}};
    };
    const auto result = [&](json value) {
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(value)}};
    };

    if (!message.is_object() || !message.contains("method") || !message["method"].is_string())
        return hasId ? error(-32600, "Invalid request") : json();

    const std::string method = message["method"].get<std::string>();
    const json params = message.contains("params") && message["params"].is_object() ? message["params"] : json::object();

    if (!hasId)
        return json(); // notifications (e.g. notifications/initialized) need no reply

    if (method == "initialize")
    {
        std::string version = params.value("protocolVersion", std::string("2025-03-26"));
        if (version != "2024-11-05" && version != "2025-03-26" && version != "2025-06-18")
            version = "2025-03-26";
        return result(json{{"protocolVersion", version},
                           {"capabilities", {{"tools", {{"listChanged", false}}}}},
                           {"serverInfo", {{"name", "d2-access-game"}, {"version", "1.0.0"}}},
                           {"instructions", ServerInstructions}});
    }
    if (method == "ping")
        return result(json::object());
    if (method == "tools/list")
    {
        json list = json::array();
        for (const ToolDef &tool : Tools())
            list.push_back(json{{"name", tool.name}, {"description", tool.description}, {"inputSchema", tool.schema}});
        return result(json{{"tools", std::move(list)}});
    }
    if (method == "tools/call")
    {
        const std::string name = params.value("name", std::string());
        const json args = params.contains("arguments") && params["arguments"].is_object() ? params["arguments"]
                                                                                            : json::object();
        const auto tool = std::find_if(Tools().begin(), Tools().end(), [&name](const ToolDef &t) { return t.name == name; });
        if (tool == Tools().end())
            return error(-32602, "Unknown tool: " + name);

        ToolResult toolResult;
        try
        {
            toolResult = tool->handler(args);
        }
        catch (const std::exception &e)
        {
            toolResult = Fail(std::string("Wyjątek: ") + e.what());
        }
        return result(json{{"content", json::array({json{{"type", "text"}, {"text", Dump(toolResult.value)}}})},
                           {"isError", toolResult.isError}});
    }
    return error(-32601, "Method not found: " + method);
}

void HandleClient(SOCKET client)
{
    DWORD timeout = 15000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));

    HttpRequest request;
    if (!ReceiveRequest(client, request))
    {
        closesocket(client);
        return;
    }

    if (!IsAllowedRequest(request))
    {
        SendResponse(client, 403, "Forbidden", R"({"error":"Only local MCP clients are allowed"})");
    }
    else if (request.path != "/mcp" && request.path != "/")
    {
        SendResponse(client, 404, "Not Found", R"({"error":"Use /mcp"})");
    }
    else if (request.method != "POST")
    {
        SendResponse(client, 405, "Method Not Allowed", R"({"error":"POST only"})", "Allow: POST\r\n");
    }
    else
    {
        json message;
        try
        {
            message = json::parse(request.body);
        }
        catch (const std::exception &)
        {
            SendResponse(client, 400, "Bad Request",
                         R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error"}})");
            closesocket(client);
            return;
        }

        json response;
        if (message.is_array())
        {
            response = json::array();
            for (const json &item : message)
            {
                json reply = HandleMessage(item);
                if (!reply.is_null())
                    response.push_back(std::move(reply));
            }
            if (response.empty())
                response = json();
        }
        else
        {
            response = HandleMessage(message);
        }

        if (response.is_null())
            SendResponse(client, 202, "Accepted", "");
        else
            SendResponse(client, 200, "OK", response.dump(-1, ' ', false, json::error_handler_t::replace));
    }

    shutdown(client, SD_SEND);
    closesocket(client);
}

SOCKET OpenListenSocket(int &port)
{
    SOCKET listenSocket = INVALID_SOCKET;
    for (int attempt = 0; attempt < PortAttempts && listenSocket == INVALID_SOCKET; ++attempt)
    {
        SOCKET candidate = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (candidate == INVALID_SOCKET)
            break;
        int exclusive = 1;
        setsockopt(candidate, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive),
                   sizeof(exclusive));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<u_short>(BasePort + attempt));
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        if (bind(candidate, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0 &&
            listen(candidate, 16) == 0)
        {
            listenSocket = candidate;
            port = BasePort + attempt;
        }
        else
        {
            closesocket(candidate);
        }
    }
    return listenSocket;
}

// Leaving a game makes Game.exe call WSACleanup until Winsock is fully shut
// down, which closes our listening socket too. The server then starts Winsock
// again and reopens the port.
void ServerMain()
{
    while (g_running.load())
    {
        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            LogLine(L"MCP server: WSAStartup failed.");
            Sleep(1000);
            continue;
        }

        int port = 0;
        SOCKET listenSocket = OpenListenSocket(port);
        if (listenSocket == INVALID_SOCKET)
        {
            LogLine(L"MCP server: no free port.");
            WSACleanup();
            Sleep(2000);
            continue;
        }

        g_listenSocket = listenSocket;
        const std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port) + L"/mcp";
        LogLine(L"MCP server listening on " + url);
        PushEvent("mcp", L"listening on " + url);

        int failures = 0;
        while (g_running.load())
        {
            SOCKET client = accept(listenSocket, nullptr, nullptr);
            if (client == INVALID_SOCKET)
            {
                if (!g_running.load())
                    break;
                const int error = WSAGetLastError();
                if (error == WSAENOTSOCK || error == WSANOTINITIALISED || error == WSAEINVAL || ++failures >= 50)
                {
                    LogLine(L"MCP server: listening socket lost (error " + std::to_wstring(error) + L"), restarting.");
                    break;
                }
                Sleep(10);
                continue;
            }
            failures = 0;
            std::thread(HandleClient, client).detach();
        }

        SOCKET owned = listenSocket;
        if (g_listenSocket.compare_exchange_strong(owned, INVALID_SOCKET))
            closesocket(listenSocket);
        WSACleanup();
        if (g_running.load())
            Sleep(500);
    }
}

} // namespace

void StartMcpServer()
{
    if (g_running.exchange(true))
        return;
    std::thread(ServerMain).detach();
}

void StopMcpServer()
{
    if (!g_running.exchange(false))
        return;
    const SOCKET listenSocket = g_listenSocket.exchange(INVALID_SOCKET);
    if (listenSocket != INVALID_SOCKET)
        closesocket(listenSocket);
}

} // namespace d2access
