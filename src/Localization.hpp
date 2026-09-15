#pragma once

#include <string>

// Speech language of the mod. Polish and English are supported; the language
// follows the game's string tables unless D2Access.ini next to the DLL says
// otherwise:
//
//   [General]
//   Language=auto   ; auto, pl or en
namespace d2access {

enum class Language {
    Polish,
    English,
};

// Reads D2Access.ini. Call once at start-up; the game language is detected
// lazily on the first query after the string tables are loaded.
void InitializeLocalization();

Language CurrentLanguage();
bool IsEnglish();

// Picks the text for the current language.
inline const wchar_t *Tr(const wchar_t *polish, const wchar_t *english)
{
    return IsEnglish() ? english : polish;
}

inline std::wstring TrS(const wchar_t *polish, const wchar_t *english)
{
    return std::wstring(Tr(polish, english));
}

} // namespace d2access
