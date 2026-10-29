#include "controls.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>

#include "common.h"
#include "json.hpp"

namespace hikaye::controls {

namespace {
int g_phone = 'O';  // P, oyunun Pause tuşuyla çakışır
int g_talk = 'B';
int g_mode = VK_F9;
int g_switch = VK_F10;
int g_inventory = 'I';
int g_hitch = 'H', g_qr = 'J';
int g_lock = 'K', g_sig_l = VK_NUMPAD4, g_sig_r = VK_NUMPAD6, g_hazard = VK_NUMPAD5, g_engine = VK_NUMPAD8;
bool g_native_contact = true;
int g_native_index = 43;  // iFruitAddon2 varsayılanı 40'tan başlar; çakışmasın

struct Named {
    const char* name;
    int vk;
};
const Named kNamed[] = {{"UP", VK_UP},         {"DOWN", VK_DOWN},   {"LEFT", VK_LEFT},     {"RIGHT", VK_RIGHT},
                        {"TAB", VK_TAB},       {"BACKSPACE", VK_BACK}, {"ENTER", VK_RETURN}, {"SPACE", VK_SPACE},
                        {"HOME", VK_HOME},     {"END", VK_END},     {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE},
                        {"PAGEUP", VK_PRIOR},  {"PAGEDOWN", VK_NEXT}};

// "o" -> 'O', "F5" -> VK_F5, "UP" -> VK_UP. Tanınmazsa 0.
int Parse(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (s.size() == 1 && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= '0' && s[0] <= '9'))) return s[0];
    if (s.size() == 4 && s.rfind("NUM", 0) == 0 && s[3] >= '0' && s[3] <= '9') return VK_NUMPAD0 + (s[3] - '0');  // NUM0-NUM9
    if (s.size() >= 2 && s[0] == 'F') {
        const int n = std::atoi(s.c_str() + 1);
        if (n >= 1 && n <= 12) return VK_F1 + n - 1;
    }
    for (const auto& n : kNamed) {
        if (s == n.name) return n.vk;
    }
    return 0;
}
}  // namespace

void Load() {
    try {
        std::ifstream in(std::string(kBaseDir) + "/data/controls.json");
        if (!in) return;
        const nlohmann::json j = nlohmann::json::parse(in);
        if (const int k = Parse(j.value("phone", "O"))) g_phone = k;
        else Log("controls.json: gecersiz 'phone' tusu, varsayilan kullaniliyor");
        if (const int k = Parse(j.value("talk", "B"))) g_talk = k;
        else Log("controls.json: gecersiz 'talk' tusu, varsayilan kullaniliyor");
        if (const int k = Parse(j.value("switch", "F10"))) g_switch = k;
        if (const int k = Parse(j.value("inventory", "I"))) g_inventory = k;
        if (const int k = Parse(j.value("hitchhike", "H"))) g_hitch = k;
        if (const int k = Parse(j.value("qr", "J"))) g_qr = k;
        if (const int k = Parse(j.value("lock", "K"))) g_lock = k;
        if (const int k = Parse(j.value("signal_left", "NUM4"))) g_sig_l = k;
        if (const int k = Parse(j.value("signal_right", "NUM6"))) g_sig_r = k;
        if (const int k = Parse(j.value("hazard", "NUM5"))) g_hazard = k;
        if (const int k = Parse(j.value("engine", "NUM8"))) g_engine = k;
        if (const int k = Parse(j.value("mode", "F9"))) g_mode = k;
        else Log("controls.json: gecersiz 'mode' tusu, varsayilan kullaniliyor");
        g_native_contact = j.value("native_contact", true);
        g_native_index = j.value("native_contact_index", 43);
    } catch (const std::exception& e) {
        Log(std::string("controls.json okunamadi: ") + e.what());
    }
}

int ModeKey() { return g_mode; }
int SwitchKey() { return g_switch; }
int LockKey() { return g_lock; }
int InventoryKey() { return g_inventory; }
int HitchhikeKey() { return g_hitch; }
int QrKey() { return g_qr; }
int SignalLeftKey() { return g_sig_l; }
int SignalRightKey() { return g_sig_r; }
int HazardKey() { return g_hazard; }
int EngineKey() { return g_engine; }
bool NativeContactEnabled() { return g_native_contact; }
int NativeContactIndex() { return g_native_index; }
int PhoneKey() { return g_phone; }
int TalkKey() { return g_talk; }

std::string KeyName(int vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, static_cast<char>(vk));
    if (vk >= VK_F1 && vk <= VK_F12) return "F" + std::to_string(vk - VK_F1 + 1);
    for (const auto& n : kNamed) {
        if (n.vk == vk) return n.name;
    }
    return "?";
}

}  // namespace hikaye::controls
