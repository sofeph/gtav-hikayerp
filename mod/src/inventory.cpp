#include "pcos.h"
#include "inventory.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

#include "controls.h"
#include "addons.h"
#include "input.h"
#include "json.hpp"
#include "karma.h"
#include "overlay.h"

namespace hikaye::inventory {

using nlohmann::json;

namespace {
struct Def {
    std::string name, desc;
    float weight = 0.5f;
    bool legal = true;
    json use = json::object();
};
std::map<std::string, Def> g_defs;
std::vector<std::pair<std::string, int>> g_items;  // sıralı: (id, adet)
float g_max_weight = 20.f;
int g_max_slots = 24;
int g_slot = -1;

// menü
bool g_open = false;
int g_sel = 0, g_act_sel = 0;
bool g_in_actions = false;
Keys g_keys;

// zamanlı etkiler
unsigned long long g_drunk_until = 0, g_high_until = 0, g_smoke_until = 0;
bool g_drunk_on = false, g_high_on = false;

std::string Path(int slot) { return std::string(kBaseDir) + "/saves/inventory_" + std::to_string(slot) + ".json"; }

void Save() {
    if (g_slot < 0) return;
    try {
        std::filesystem::create_directories(std::string(kBaseDir) + "/saves");
        json arr = json::array();
        for (const auto& [id, n] : g_items) arr.push_back({{"id", id}, {"n", n}});
        std::ofstream out(Path(g_slot));
        out << json{{"items", arr}}.dump(1);
    } catch (...) {
    }
}

std::string W(float kg) {
    char b[24];
    std::snprintf(b, sizeof(b), "%.1f", kg);
    return b;
}

void StopDrunk() {
    if (!g_drunk_on) return;
    const Ped p = PLAYER::PLAYER_PED_ID();
    g_drunk_on = false;
    AUDIO::SET_PED_IS_DRUNK(p, false);
    PED::RESET_PED_MOVEMENT_CLIPSET(p, 1.0f);
    CAM::STOP_GAMEPLAY_CAM_SHAKING(true);
    if (!g_high_on) GRAPHICS::CLEAR_TIMECYCLE_MODIFIER();
}

void StopHigh() {
    if (!g_high_on) return;
    g_high_on = false;
    CAM::STOP_GAMEPLAY_CAM_SHAKING(true);
    if (!g_drunk_on) GRAPHICS::CLEAR_TIMECYCLE_MODIFIER();
}

void StartDrunk(int seconds) {
    const Ped p = PLAYER::PLAYER_PED_ID();
    STREAMING::REQUEST_ANIM_SET(const_cast<char*>("move_m@drunk@verydrunk"));
    for (int i = 0; i < 60 && !STREAMING::HAS_ANIM_SET_LOADED(const_cast<char*>("move_m@drunk@verydrunk")); ++i) WAIT(0);
    PED::SET_PED_MOVEMENT_CLIPSET(p, const_cast<char*>("move_m@drunk@verydrunk"), 1.0f);
    AUDIO::SET_PED_IS_DRUNK(p, true);
    GRAPHICS::SET_TIMECYCLE_MODIFIER(const_cast<char*>("Drunk"));
    CAM::SHAKE_GAMEPLAY_CAM(const_cast<char*>("DRUNK_SHAKE"), 0.9f);
    g_drunk_on = true;
    g_drunk_until = std::max(g_drunk_until, GetTickCount64()) + static_cast<unsigned long long>(seconds) * 1000ULL;
}

void StartHigh(int seconds) {
    GRAPHICS::SET_TIMECYCLE_MODIFIER(const_cast<char*>("drug_flying_01"));
    GRAPHICS::SET_TIMECYCLE_MODIFIER_STRENGTH(0.8f);
    CAM::SHAKE_GAMEPLAY_CAM(const_cast<char*>("HAND_SHAKE"), 0.25f);
    g_high_on = true;
    g_high_until = std::max(g_high_until, GetTickCount64()) + static_cast<unsigned long long>(seconds) * 1000ULL;
}

// Kullan: etki uygulanırsa true (eşya harcanır)
bool UseItem(const std::string& id, const Def& d) {
    const Ped p = PLAYER::PLAYER_PED_ID();
    const std::string type = d.use.value("type", "");
    if (type == "heal") {
        const int maxh = ENTITY::GET_ENTITY_MAX_HEALTH(p), cur = ENTITY::GET_ENTITY_HEALTH(p);
        ENTITY::SET_ENTITY_HEALTH(p, std::min(maxh, cur + static_cast<int>((maxh - 100) * d.use.value("pct", 20.f) / 100.f)));
        karma::Toast(d.name + " kullandın", 3);
        return true;
    }
    if (type == "weapon") {
        const Hash w = HashKey(d.use.value("weapon", "WEAPON_KNIFE").c_str());
        WEAPON::GIVE_WEAPON_TO_PED(p, w, d.use.value("ammo", 1), false, true);
        karma::Toast(d.name + " elinde", 3);
        return d.use.value("consume", false);
    }
    if (type == "drunk") {
        StartDrunk(d.use.value("seconds", 60));
        karma::Toast(d.name + " içtin: kafan güzel", 4);
        return true;
    }
    if (type == "high") {
        StartHigh(d.use.value("seconds", 60));
        karma::Toast(d.name + " kullandın: dünya yumuşadı", 4);
        return true;
    }
    if (type == "laptop") {  // dizüstü: güç + oturma koşulunu main denetler; eşya harcanmaz
        pcos::RequestLaptop();
        return false;
    }
    if (type == "smoke") {
        AI::TASK_START_SCENARIO_IN_PLACE(p, const_cast<char*>("WORLD_HUMAN_SMOKING"), 0, true);
        g_smoke_until = GetTickCount64() + 9000ULL;
        karma::Toast("Sigara yaktın", 3);
        return true;
    }
    (void)id;
    karma::Toast("Bu eşya kullanılamaz", 3);
    return false;
}
}  // namespace

void Load() {
    g_defs.clear();
    try {
        std::ifstream in(std::string(kBaseDir) + "/data/items.json");
        if (!in) {
            Log("items.json yok");
            return;
        }
        const json j = json::parse(in);
        g_max_weight = j.value("max_weight", 20.f);
        g_max_slots = j.value("max_slots", 24);
        const json defs = j.value("items", json::object());  // KOPYA: .items() geçiciye bakarsa döngüden önce silinir (0 eşya yükleniyordu)
        for (const auto& [id, v] : defs.items()) {
            Def d;
            d.name = v.value("name", id);
            d.desc = v.value("desc", "");
            d.weight = v.value("weight", 0.3f);
            d.legal = v.value("legal", true);
            d.use = v.value("use", json::object());
            g_defs[id] = std::move(d);
        }
        for (const auto& dir : addons::Dirs()) {  // eklenti eşyaları (aynı kimlik varsa eklenti üzerine yazar)
            std::ifstream ai(dir + "/items.json");
            if (!ai) continue;
            const json aj = json::parse(ai);
            const json adefs = aj.value("items", json::object());
            for (const auto& [aid, av] : adefs.items()) {
                Def d;
                d.name = av.value("name", aid);
                d.desc = av.value("desc", "");
                d.weight = av.value("weight", 0.3f);
                d.legal = av.value("legal", true);
                d.use = av.value("use", json::object());
                g_defs[aid] = std::move(d);
            }
        }
    } catch (const std::exception& e) {
        Log(std::string("items.json HATA: ") + e.what());
    }
    Log("envanter: " + std::to_string(g_defs.size()) + " esya tanimi");
}

void LoadSave(int slot) {
    if (slot == g_slot) return;
    if (g_slot >= 0) Save();
    g_slot = slot;
    g_items.clear();
    try {
        std::ifstream in(Path(slot));
        if (!in) return;
        for (const auto& e : json::parse(in).value("items", json::array())) {
            const std::string id = e.value("id", "");
            if (g_defs.count(id) && e.value("n", 0) > 0) g_items.push_back({id, e.value("n", 0)});
        }
    } catch (...) {
    }
}

void Shutdown() {
    Save();
    StopDrunk();
    StopHigh();
    if (g_open) overlay::SetMenu({});
    g_open = false;
    g_in_actions = false;
    g_slot = -1;
}

float Weight() {
    float w = 0.f;
    for (const auto& [id, n] : g_items) {
        const auto it = g_defs.find(id);
        if (it != g_defs.end()) w += it->second.weight * static_cast<float>(n);
    }
    return w;
}

int Count(const std::string& id) {
    for (const auto& [i, n] : g_items) {
        if (i == id) return n;
    }
    return 0;
}

bool Add(const std::string& id, int qty) {
    const auto it = g_defs.find(id);
    if (it == g_defs.end() || qty <= 0 || g_slot < 0) return false;
    if (Weight() + it->second.weight * static_cast<float>(qty) > g_max_weight + 0.001f) {
        karma::Toast("Envanter dolu: taşıyamazsın (" + W(Weight()) + "/" + W(g_max_weight) + " kg)", 4);
        return false;
    }
    for (auto& [i, n] : g_items) {
        if (i == id) {
            n += qty;
            Save();
            return true;
        }
    }
    if (static_cast<int>(g_items.size()) >= g_max_slots) {
        karma::Toast("Envanter yuvaları dolu", 4);
        return false;
    }
    g_items.push_back({id, qty});
    Save();
    return true;
}

bool Remove(const std::string& id, int qty) {
    for (size_t k = 0; k < g_items.size(); ++k) {
        if (g_items[k].first != id) continue;
        if (g_items[k].second < qty) return false;
        g_items[k].second -= qty;
        if (g_items[k].second == 0) g_items.erase(g_items.begin() + static_cast<long>(k));
        Save();
        return true;
    }
    return false;
}

bool HasIllegal() {
    for (const auto& [id, n] : g_items) {
        const auto it = g_defs.find(id);
        if (it != g_defs.end() && !it->second.legal) return true;
    }
    return false;
}

std::vector<std::string> ConfiscateIllegal() {
    std::vector<std::string> out;
    for (size_t k = 0; k < g_items.size();) {
        const auto it = g_defs.find(g_items[k].first);
        if (it != g_defs.end() && !it->second.legal) {
            out.push_back(it->second.name);
            g_items.erase(g_items.begin() + static_cast<long>(k));
        } else {
            ++k;
        }
    }
    if (!out.empty()) Save();
    return out;
}

bool MenuOpen() { return g_open; }

void Update(bool blocked) {
    const unsigned long long now = GetTickCount64();
    // zamanlı etkilerin sonu
    if (g_drunk_on && now >= g_drunk_until) StopDrunk();
    if (g_high_on && now >= g_high_until) StopHigh();
    if (g_smoke_until != 0 && now >= g_smoke_until) {
        AI::CLEAR_PED_TASKS(PLAYER::PLAYER_PED_ID());
        g_smoke_until = 0;
    }
    if (g_slot < 0) return;

    const bool open_key = g_keys.Pressed(controls::InventoryKey());
    if (blocked || ENTITY::IS_ENTITY_DEAD(PLAYER::PLAYER_PED_ID())) {
        if (g_open) {
            g_open = false;
            g_in_actions = false;
            overlay::SetMenu({});
        }
        return;
    }
    if (!g_open) {
        if (open_key) {
            g_open = true;
            g_in_actions = false;
            g_sel = 0;
        }
        return;
    }

    CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
    const int n = static_cast<int>(g_items.size());
    overlay::MenuView mv;
    mv.open = true;
    mv.title = "Envanter";
    mv.subtitle = "Ağırlık " + W(Weight()) + " / " + W(g_max_weight) + " kg  ·  yuva " + std::to_string(n) + "/" + std::to_string(g_max_slots);

    if (!g_in_actions) {
        if (n > 0) {
            if (g_keys.Repeat(VK_DOWN)) g_sel = (g_sel + 1) % n;
            if (g_keys.Repeat(VK_UP)) g_sel = (g_sel + n - 1) % n;
            g_sel = std::clamp(g_sel, 0, n - 1);
        }
        for (const auto& [id, cnt] : g_items) {
            const Def& d = g_defs[id];
            mv.rows.push_back({d.name + "  x" + std::to_string(cnt), W(d.weight * static_cast<float>(cnt)) + " kg" + (d.legal ? "" : "  · yasadışı"), false});
        }
        if (n == 0) mv.rows.push_back({"Envanter boş", "", true});
        mv.selected = g_sel;
        mv.hint = "↑↓ seç   Enter: işlem   Backspace/" + controls::KeyName(controls::InventoryKey()) + ": kapat";
        overlay::SetMenu(mv);
        if (g_keys.Pressed(VK_BACK) || open_key) {
            g_open = false;
            overlay::SetMenu({});
            return;
        }
        if (n > 0 && g_keys.Pressed(VK_RETURN)) {
            g_in_actions = true;
            g_act_sel = 0;
        }
        return;
    }

    // işlem menüsü: Kullan / At (1) / Hepsini at / Geri
    if (g_sel >= n) {
        g_in_actions = false;
        return;
    }
    const std::string id = g_items[static_cast<size_t>(g_sel)].first;
    const Def& d = g_defs[id];
    const std::vector<std::string> acts = {"Kullan", "At (1 adet)", "Hepsini at", "Geri"};
    if (g_keys.Repeat(VK_DOWN)) g_act_sel = (g_act_sel + 1) % 4;
    if (g_keys.Repeat(VK_UP)) g_act_sel = (g_act_sel + 3) % 4;
    mv.subtitle = d.name + (d.desc.empty() ? "" : "  ·  " + d.desc);
    for (const auto& a : acts) mv.rows.push_back({a, "", false});
    mv.selected = g_act_sel;
    mv.hint = "↑↓ seç   Enter onay   Backspace geri";
    overlay::SetMenu(mv);
    if (g_keys.Pressed(VK_BACK)) {
        g_in_actions = false;
        return;
    }
    if (!g_keys.Pressed(VK_RETURN)) return;
    if (g_act_sel == 0) {
        if (UseItem(id, d)) Remove(id, 1);
        g_open = false;  // kullanınca menü kapanır (oyuna dönülür)
        overlay::SetMenu({});
    } else if (g_act_sel == 1) {
        Remove(id, 1);
        karma::Toast(d.name + " attın", 3);
    } else if (g_act_sel == 2) {
        Remove(id, g_items[static_cast<size_t>(g_sel)].second);
        karma::Toast(d.name + " tamamen attın", 3);
    }
    if (g_act_sel >= 1) g_in_actions = false;
    const int n2 = static_cast<int>(g_items.size());
    if (g_sel >= n2) g_sel = std::max(0, n2 - 1);
}

}  // namespace hikaye::inventory
