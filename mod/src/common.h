#pragma once
#include <string>

#include "main.h"      // ScriptHookV SDK: scriptRegister, scriptWait, ...
#include "natives.h"

namespace hikaye {

// SDK sürümüne göre native adları değişir (bkz. CLAUDE.md §3). Varsayılan: SDK 1.0.617.1a
// (GAMEPLAY/UI, char* parametreli). Yeni natives.h (MISC/HUD) için NEW_NATIVES tanımla.
// Değişen çağrılar sadece burada toplanır; diğer dosyalar bu sarmalayıcıları kullanır.
#ifdef NEW_NATIVES
inline Hash HashKey(const char* s) { return MISC::GET_HASH_KEY(s); }
inline void ShowHelp(const char* text) {
    HUD::BEGIN_TEXT_COMMAND_DISPLAY_HELP("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(text);
    HUD::END_TEXT_COMMAND_DISPLAY_HELP(0, false, true, -1);
}
#else
inline Hash HashKey(const char* s) { return GAMEPLAY::GET_HASH_KEY(const_cast<char*>(s)); }
inline void ShowHelp(const char* text) {
    UI::_SET_TEXT_COMPONENT_FORMAT(const_cast<char*>("STRING"));
    UI::_ADD_TEXT_COMPONENT_STRING(const_cast<char*>(text));
    UI::_DISPLAY_HELP_TEXT_FROM_STRING_LABEL(0, false, true, -1);
}
#endif

// Ekrana ortalanmış tek satır metin çizer (y: 0..1). Türkçe harfler ASCII'ye çevrilir. Her frame çağrılır.
void DrawLine(const std::string& text, float y, int r, int g, int b);

// Oyun klasöründeki HikayeTurkRP/ altına log yazar. Script thread'inden çağrılır.
void Log(const std::string& msg);

constexpr const char* kBaseDir = "HikayeTurkRP";

}  // namespace hikaye
