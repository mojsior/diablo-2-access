#include "SkillTree.hpp"

#include "AudioCue.hpp"
#include "Localization.hpp"
#include "Logging.hpp"
#include "Panels.hpp"
#include "ScreenReader.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace d2access::skilltree {

namespace {

constexpr int UiSkillTree = 4;
constexpr int MaxHardPoints = 20;
constexpr int TabCount = 3;
constexpr DWORD64 SendIntervalMs = 260;
constexpr DWORD64 ResultDelayMs = 450;

struct Tab {
    int page = 0;
    bool expanded = false;
    std::vector<game::SkillDefinition> skills;
};

struct State {
    bool wasOpen = false;
    std::uint32_t classId = 0xFFFFFFFF;
    std::array<Tab, TabCount> tabs;
    int tab = 0;
    int skill = -1; // -1: the tab node itself

    int pendingSkill = -1;
    int pendingCount = 0;
    int pendingBase = 0;
    DWORD64 lastSend = 0;
};

struct NodeRef {
    int tab = 0;
    int skill = -1;
};

State g_state;
std::atomic<bool> g_openForKeys = false;

void Say(const std::wstring &text)
{
    LogLine(L"SkillTree: " + text);
    Speak(text, true);
}

struct TabName {
    const wchar_t *polish;
    const wchar_t *english;
};

// skilldesc.txt SkillPage 1..3 per class, checked against the skills listed on
// each page in game (e.g. Barbarian page 1 holds Bash and Leap).
constexpr std::array<std::array<TabName, TabCount>, 7> TabNames = {{
    {{{L"Łuki i kusze", L"Bow and Crossbow Skills"},
      {L"Umiejętności pasywne i magiczne", L"Passive and Magic Skills"},
      {L"Oszczepy i włócznie", L"Javelin and Spear Skills"}}},
    {{{L"Czary ognia", L"Fire Spells"}, {L"Czary błyskawic", L"Lightning Spells"}, {L"Czary zimna", L"Cold Spells"}}},
    {{{L"Klątwy", L"Curses"}, {L"Trucizna i kości", L"Poison and Bone Spells"}, {L"Przywoływanie", L"Summoning Spells"}}},
    {{{L"Umiejętności bojowe", L"Combat Skills"}, {L"Aury ofensywne", L"Offensive Auras"},
      {L"Aury defensywne", L"Defensive Auras"}}},
    {{{L"Umiejętności bojowe", L"Combat Skills"}, {L"Mistrzostwa bojowe", L"Combat Masteries"},
      {L"Okrzyki bojowe", L"Warcries"}}},
    {{{L"Przywoływanie", L"Summoning"}, {L"Zmiennokształtność", L"Shape Shifting"}, {L"Żywioły", L"Elemental"}}},
    {{{L"Pułapki", L"Traps"}, {L"Dyscypliny cienia", L"Shadow Disciplines"}, {L"Sztuki walki", L"Martial Arts"}}},
}};

std::wstring TabLabel(std::uint32_t classId, int page)
{
    if (classId < TabNames.size() && page >= 1 && page <= TabCount)
    {
        const TabName &name = TabNames[classId][static_cast<size_t>(page - 1)];
        return Tr(name.polish, name.english);
    }
    return TrS(L"Zakładka ", L"Tab ") + std::to_wstring(page);
}

std::wstring SkillName(const game::SkillDefinition &skill)
{
    std::wstring name = panels::CleanText(game::StringById(skill.nameStringId));
    if (name.empty())
        name = TrS(L"Umiejętność ", L"Skill ") + std::to_wstring(skill.id);
    return name;
}

std::wstring SkillNameById(int skillId)
{
    game::SkillDefinition skill;
    return game::ReadSkillDefinition(skillId, skill) ? SkillName(skill)
                                                      : TrS(L"Umiejętność ", L"Skill ") + std::to_wstring(skillId);
}

bool BuildTree(uintptr_t player)
{
    std::uint32_t classId = 0;
    if (!game::Read(player + game::off::UnitClassId, classId))
        return false;
    const bool hasSkills = std::any_of(g_state.tabs.begin(), g_state.tabs.end(),
                                       [](const Tab &tab) { return !tab.skills.empty(); });
    if (classId == g_state.classId && hasSkills)
        return true;

    std::array<Tab, TabCount> tabs;
    for (int i = 0; i < TabCount; ++i)
        tabs[static_cast<size_t>(i)].page = i + 1;
    for (int id : game::ClassSkillIds(classId))
    {
        game::SkillDefinition skill;
        if (game::ReadSkillDefinition(id, skill) && skill.page >= 1 && skill.page <= TabCount)
            tabs[static_cast<size_t>(skill.page - 1)].skills.push_back(skill);
    }
    for (Tab &tab : tabs)
    {
        std::sort(tab.skills.begin(), tab.skills.end(), [](const auto &a, const auto &b) {
            return a.row != b.row ? a.row < b.row : a.column < b.column;
        });
    }

    g_state.classId = classId;
    g_state.tabs = std::move(tabs);
    g_state.tab = 0;
    g_state.skill = -1;
    return std::any_of(g_state.tabs.begin(), g_state.tabs.end(), [](const Tab &tab) { return !tab.skills.empty(); });
}

std::wstring MissingRequirements(uintptr_t player, const game::SkillDefinition &skill)
{
    std::vector<std::wstring> parts;
    const int level = game::UnitStat(player, game::stat::Level);
    if (level < skill.requiredLevel)
        parts.push_back(TrS(L"poziom postaci ", L"character level ") + std::to_wstring(skill.requiredLevel));
    for (int required : skill.requiredSkills)
    {
        if (required >= 0 && game::PlayerSkillPoints(player, required) <= 0)
            parts.push_back(SkillNameById(required));
    }

    std::wstring text;
    for (const std::wstring &part : parts)
    {
        if (!text.empty())
            text += L", ";
        text += part;
    }
    return text;
}

std::wstring OrdinalText(int index, int count)
{
    return std::to_wstring(index + 1) + Tr(L" z ", L" of ") + std::to_wstring(count);
}

std::wstring SkillText(uintptr_t player, const game::SkillDefinition &skill, int index, int count)
{
    const int points = game::PlayerSkillPoints(player, skill.id);
    std::wstring text = SkillName(skill) + L", ";
    if (points > 0)
    {
        text += TrS(L"poziom ", L"level ") + std::to_wstring(points);
        const int bonus = game::SkillBonusLevels(player, skill);
        if (bonus > 0)
            text += TrS(L" plus ", L" plus ") + std::to_wstring(bonus) + Tr(L" z przedmiotów", L" from items");
    }
    else
    {
        text += Tr(L"nie nauczona", L"not learned");
    }

    const std::wstring missing = MissingRequirements(player, skill);
    if (points >= MaxHardPoints)
        text += Tr(L", poziom maksymalny", L", maximum level");
    else if (!missing.empty())
        text += TrS(L", zablokowana, wymaga: ", L", locked, requires: ") + missing;
    else if (game::UnitStat(player, game::stat::SkillPoints) > 0)
        text += Tr(L", można dodać punkt", L", a point can be added");
    else
        text += Tr(L", dostępna", L", available");

    return text + L", " + OrdinalText(index, count);
}

int SpentPoints(uintptr_t player, const Tab &tab)
{
    int total = 0;
    for (const game::SkillDefinition &skill : tab.skills)
        total += game::PlayerSkillPoints(player, skill.id);
    return total;
}

std::wstring TabText(uintptr_t player, int tabIndex)
{
    const Tab &tab = g_state.tabs[static_cast<size_t>(tabIndex)];
    return TabLabel(g_state.classId, tab.page) +
           (tab.expanded ? Tr(L", rozwinięta", L", expanded") : Tr(L", zwinięta", L", collapsed")) +
           TrS(L", umiejętności: ", L", skills: ") + std::to_wstring(tab.skills.size()) +
           TrS(L", wydane punkty: ", L", points spent: ") + std::to_wstring(SpentPoints(player, tab)) + L", " +
           OrdinalText(tabIndex, TabCount);
}

std::wstring CurrentText(uintptr_t player)
{
    const Tab &tab = g_state.tabs[static_cast<size_t>(g_state.tab)];
    if (g_state.skill < 0 || g_state.skill >= static_cast<int>(tab.skills.size()))
        return TabText(player, g_state.tab);
    return SkillText(player, tab.skills[static_cast<size_t>(g_state.skill)], g_state.skill,
                     static_cast<int>(tab.skills.size()));
}

const game::SkillDefinition *CurrentSkill()
{
    const Tab &tab = g_state.tabs[static_cast<size_t>(g_state.tab)];
    if (g_state.skill < 0 || g_state.skill >= static_cast<int>(tab.skills.size()))
        return nullptr;
    return &tab.skills[static_cast<size_t>(g_state.skill)];
}

std::vector<NodeRef> VisibleNodes()
{
    std::vector<NodeRef> nodes;
    for (int t = 0; t < TabCount; ++t)
    {
        nodes.push_back(NodeRef{t, -1});
        const Tab &tab = g_state.tabs[static_cast<size_t>(t)];
        if (tab.expanded)
        {
            for (int s = 0; s < static_cast<int>(tab.skills.size()); ++s)
                nodes.push_back(NodeRef{t, s});
        }
    }
    return nodes;
}

void SelectNode(uintptr_t player, int index)
{
    const std::vector<NodeRef> nodes = VisibleNodes();
    index = std::clamp(index, 0, static_cast<int>(nodes.size()) - 1);
    g_state.tab = nodes[static_cast<size_t>(index)].tab;
    g_state.skill = nodes[static_cast<size_t>(index)].skill;
    Say(CurrentText(player));
}

int CurrentNodeIndex()
{
    const std::vector<NodeRef> nodes = VisibleNodes();
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (nodes[i].tab == g_state.tab && nodes[i].skill == g_state.skill)
            return static_cast<int>(i);
    }
    return 0;
}

void MoveVertical(uintptr_t player, int delta)
{
    SelectNode(player, CurrentNodeIndex() + delta);
}

void MoveRight(uintptr_t player)
{
    Tab &tab = g_state.tabs[static_cast<size_t>(g_state.tab)];
    if (g_state.skill >= 0)
    {
        Say(CurrentText(player));
        return;
    }
    if (!tab.expanded)
    {
        tab.expanded = true;
        Say(TabText(player, g_state.tab));
        return;
    }
    if (!tab.skills.empty())
        g_state.skill = 0;
    Say(CurrentText(player));
}

void MoveLeft(uintptr_t player)
{
    Tab &tab = g_state.tabs[static_cast<size_t>(g_state.tab)];
    if (g_state.skill >= 0)
        g_state.skill = -1;
    else
        tab.expanded = false;
    Say(TabText(player, g_state.tab));
}

void SpeakDescription(uintptr_t player)
{
    const game::SkillDefinition *skill = CurrentSkill();
    if (skill == nullptr)
    {
        Say(TabText(player, g_state.tab));
        return;
    }

    std::wstring text = SkillName(*skill) + L". ";
    const std::wstring shortText = panels::CleanText(game::StringById(skill->shortStringId));
    if (!shortText.empty())
        text += shortText + L". ";
    text += TrS(L"Wymagany poziom postaci: ", L"Required character level: ") + std::to_wstring(skill->requiredLevel) + L".";

    std::wstring required;
    for (int id : skill->requiredSkills)
    {
        if (id < 0)
            continue;
        if (!required.empty())
            required += L", ";
        required += SkillNameById(id);
    }
    if (!required.empty())
        text += TrS(L" Wymagane umiejętności: ", L" Required skills: ") + required + L".";

    text += TrS(L" Wiersz ", L" Row ") + std::to_wstring(skill->row) + TrS(L", kolumna ", L", column ") +
            std::to_wstring(skill->column) + L".";
    Say(text);
}

void SpendPoints(uintptr_t player, bool all)
{
    const game::SkillDefinition *skill = CurrentSkill();
    if (skill == nullptr)
        return;

    const int available = game::UnitStat(player, game::stat::SkillPoints);
    const int points = game::PlayerSkillPoints(player, skill->id);
    if (available <= 0)
    {
        Say(Tr(L"Brak punktów umiejętności do wydania.", L"No skill points to spend."));
        return;
    }
    if (points >= MaxHardPoints)
    {
        Say(SkillName(*skill) + Tr(L": poziom maksymalny.", L": maximum level."));
        return;
    }
    const std::wstring missing = MissingRequirements(player, *skill);
    if (!missing.empty())
    {
        Say(TrS(L"Nie można dodać punktu. Wymaga: ", L"Cannot add a point. Requires: ") + missing + L".");
        return;
    }

    // Identical packets within 200 ms are dropped by the client, so several
    // points are sent one by one.
    g_state.pendingSkill = skill->id;
    g_state.pendingBase = points;
    g_state.pendingCount = all ? std::min(available, MaxHardPoints - points) : 1;
    g_state.lastSend = 0;
}

void ProcessQueue(uintptr_t player)
{
    if (g_state.pendingSkill < 0)
        return;

    const DWORD64 now = GetTickCount64();
    const int points = game::PlayerSkillPoints(player, g_state.pendingSkill);
    if (g_state.pendingCount > 0 && now - g_state.lastSend >= SendIntervalMs)
    {
        if (game::UnitStat(player, game::stat::SkillPoints) <= 0 || points >= MaxHardPoints ||
            !game::SendAddSkillPoint(g_state.pendingSkill))
        {
            g_state.pendingCount = 0;
        }
        else
        {
            --g_state.pendingCount;
            g_state.lastSend = now;
        }
    }

    if (g_state.pendingCount == 0 && now - g_state.lastSend >= ResultDelayMs)
    {
        const std::wstring name = SkillNameById(g_state.pendingSkill);
        const int left = game::UnitStat(player, game::stat::SkillPoints);
        if (points > g_state.pendingBase)
        {
            PlayCue(CueId::InteractionPossible);
            Say(name + TrS(L": poziom ", L": level ") + std::to_wstring(points) +
                TrS(L". Pozostałe punkty umiejętności: ", L". Skill points left: ") + std::to_wstring(left) + L".");
        }
        else
        {
            Say(TrS(L"Nie udało się dodać punktu do: ", L"Could not add a point to: ") + name + L".");
        }
        g_state.pendingSkill = -1;
    }
}

bool IsKey(DWORD virtualKey, DWORD a, DWORD b)
{
    return virtualKey == a || virtualKey == b;
}

} // namespace

void ResetSkillTree()
{
    g_state = State{};
    g_openForKeys = false;
}

void UpdateSkillTree(uintptr_t playerUnit)
{
    const bool open = game::IsUiPanelOpen(UiSkillTree);
    g_openForKeys = open;
    if (open && !g_state.wasOpen)
    {
        if (BuildTree(playerUnit))
        {
            Say(TrS(L"Drzewko umiejętności. Punkty do wydania: ", L"Skill tree. Unspent skill points: ") +
                std::to_wstring(game::UnitStat(playerUnit, game::stat::SkillPoints)) + L". " + CurrentText(playerUnit));
        }
        else
        {
            Say(Tr(L"Drzewko umiejętności. Nie mogę odczytać umiejętności klasy.",
                   L"Skill tree. Cannot read the class skills."));
        }
    }
    g_state.wasOpen = open;
    ProcessQueue(playerUnit);
}

bool IsSkillTreeFocused()
{
    return g_state.wasOpen;
}

bool IsSkillTreeOpenForKeys()
{
    return g_openForKeys.load();
}

bool HandleSkillTreeKey(uintptr_t playerUnit, DWORD virtualKey, bool ctrl, bool shift)
{
    (void)ctrl;
    if (!g_state.wasOpen || !BuildTree(playerUnit))
        return false;

    if (IsKey(virtualKey, VK_UP, VK_NUMPAD8))
        MoveVertical(playerUnit, -1);
    else if (IsKey(virtualKey, VK_DOWN, VK_NUMPAD2))
        MoveVertical(playerUnit, 1);
    else if (IsKey(virtualKey, VK_RIGHT, VK_NUMPAD6))
        MoveRight(playerUnit);
    else if (IsKey(virtualKey, VK_LEFT, VK_NUMPAD4))
        MoveLeft(playerUnit);
    else if (IsKey(virtualKey, VK_HOME, VK_NUMPAD7))
        SelectNode(playerUnit, 0);
    else if (IsKey(virtualKey, VK_END, VK_NUMPAD1))
        SelectNode(playerUnit, static_cast<int>(VisibleNodes().size()) - 1);
    else if (virtualKey == VK_SPACE)
        SpeakDescription(playerUnit);
    else if (virtualKey == VK_RETURN)
    {
        if (CurrentSkill() == nullptr)
        {
            Tab &tab = g_state.tabs[static_cast<size_t>(g_state.tab)];
            tab.expanded = !tab.expanded;
            Say(TabText(playerUnit, g_state.tab));
        }
        else
        {
            SpendPoints(playerUnit, shift);
        }
    }
    else
        return false;
    return true;
}

} // namespace d2access::skilltree
