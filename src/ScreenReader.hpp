#pragma once

#include <string_view>

namespace d2access {

void InitializeScreenReader();
void ShutdownScreenReader();
void Speak(std::wstring_view text, bool interrupt);

} // namespace d2access
