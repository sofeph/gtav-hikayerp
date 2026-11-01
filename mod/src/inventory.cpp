#include "pcos.h"
#include "inventory.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

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
std::map<std::string, int> g_weapon_ammo;
std::set<Hash> g_seen_weapons;
Ped g_weapon_scan_ped = 0;
bool g_weapon_scan_initialized = false;
bool g_driveby_enabled = false;
unsigned long long g_weapon_save_at = 0;
float g_max_weight = 20.f;
int g_max_slots = 24;
int g_slot = -1;

// menü
bool g_open = false;
int g_sel = 0, g_act_sel = 0;
bool g_in_actions = false;
Keys g_keys;

struct WeaponInfo {
    const char* hash;
    const char* name;
};

constexpr WeaponInfo kPickupWeapons[] = {
    {"WEAPON_PISTOL", "9mm Tabanca"},
    {"WEAPON_COMBATPISTOL", "Combat Tabanca"},
    {"WEAPON_APPISTOL", "AP Tabanca"},
    {"WEAPON_PISTOL50", "Pistol .50"},
    {"WEAPON_SNSPISTOL", "SNS Tabanca"},
    {"WEAPON_HEAVYPISTOL", "Ağır Tabanca"},
    {"WEAPON_VINTAGEPISTOL", "Vintage Tabanca"},
    {"WEAPON_REVOLVER", "Revolver"},
    {"WEAPON_STUNGUN", "Şok Tabancası"},
    {"WEAPON_MICROSMG", "Micro SMG"},
    {"WEAPON_SMG", "SMG"},
    {"WEAPON_ASSAULTSMG", "Assault SMG"},
    {"WEAPON_COMBATPDW", "Combat PDW"},
    {"WEAPON_MACHINEPISTOL", "Makinalı Tabanca"},
    {"WEAPON_MG", "Makineli Tüfek"},
    {"WEAPON_COMBATMG", "Combat MG"},
    {"WEAPON_GUSENBERG", "Gusenberg"},
    {"WEAPON_ASSAULTRIFLE", "AK-47"},
    {"WEAPON_CARBINERIFLE", "Carbine Rifle"},
    {"WEAPON_ADVANCEDRIFLE", "Advanced Rifle"},
    {"WEAPON_SPECIALCARBINE", "Special Carbine"},
    {"WEAPON_BULLPUPRIFLE", "Bullpup Rifle"},
    {"WEAPON_COMPACTRIFLE", "Compact Rifle"},
    {"WEAPON_PUMPSHOTGUN", "Pompalı Tüfek"},
    {"WEAPON_SAWNOFFSHOTGUN", "Kesik Namlulu Tüfek"},
    {"WEAPON_ASSAULTSHOTGUN", "Assault Shotgun"},
    {"WEAPON_BULLPUPSHOTGUN", "Bullpup Shotgun"},
    {"WEAPON_MUSKET", "Misket Tüfeği"},
    {"WEAPON_HEAVYSHOTGUN", "Heavy Shotgun"},
    {"WEAPON_DBSHOTGUN", "Çifte"},
    {"WEAPON_SNIPERRIFLE", "Keskin Nişancı Tüfeği"},
    {"WEAPON_HEAVYSNIPER", "Ağır Keskin Nişancı Tüfeği"},
    {"WEAPON_MARKSMANRIFLE", "Marksman Rifle"},
    {"WEAPON_GRENADELAUNCHER", "Bombaatar"},
    {"WEAPON_RPG", "RPG"},
    {"WEAPON_MINIGUN", "Minigun"}
};

std::string WeaponItemId(Hash weapon) {
    for (const auto& info : kPickupWeapons) {
        if (HashKey(info.hash) == weapon) {
            std::string id = info.hash;
            std::transform(id.begin(), id.end(), id.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return id;
        }
    }
    return {};
}

void Save();

void SyncWeaponAmmo(bool persist) {
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (!player) return;
    bool changed = false;
    for (auto& [id, ammo] : g_weapon_ammo) {
        const auto def = g_defs.find(id);
        if (def == g_defs.end() || def->second.use.value("type", "") != "weapon") continue;
        const Hash weapon = HashKey(def->second.use.value("weapon", "").c_str());
        if (!WEAPON::HAS_PED_GOT_WEAPON(player, weapon, false)) continue;
        const int current_ammo = WEAPON::GET_AMMO_IN_PED_WEAPON(player, weapon);
        if (current_ammo != ammo) {
            ammo = current_ammo;
            changed = true;
        }
    }
    if (changed && persist) Save();
}

// zamanlı etkiler
unsigned long long g_drunk_until = 0, g_high_until = 0, g_smoke_until = 0;
bool g_drunk_on = false, g_high_on = false;

std::string Path(int slot) { return std::string(kBaseDir) + "/saves/inventory_" + std::to_string(slot) + ".json"; }

void Save() {
    if (g_slot < 0) return;
    try {
        SyncWeaponAmmo(false);
        std::filesystem::create_directories(std::string(kBaseDir) + "/saves");
        json arr = json::array();
        for (const auto& [id, n] : g_items) {
            json item{{"id", id}, {"n", n}};
            const auto ammo = g_weapon_ammo.find(id);
            if (ammo != g_weapon_ammo.end()) item["ammo"] = ammo->second;
            arr.push_back(std::move(item));
        }
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
        const auto saved_ammo = g_weapon_ammo.find(id);
        const int ammo = saved_ammo == g_weapon_ammo.end() ? d.use.value("ammo", 1) : saved_ammo->second;
        WEAPON::GIVE_WEAPON_TO_PED(p, w, ammo, false, true);
        WEAPON::SET_PED_AMMO(p, w, ammo);
        g_seen_weapons.insert(w);
        if (PED::IS_PED_IN_ANY_VEHICLE(p, false)) {
            PLAYER::SET_PLAYER_CAN_DO_DRIVE_BY(PLAYER::PLAYER_ID(), true);
            g_driveby_enabled = true;
        }
        karma::Toast(d.name + " elinde", 3);
        return d.use.value("consume", false);
    }
    if (type == "ammo_pack") {
        const Hash weapon = HashKey(d.use.value("weapon", "").c_str());
        const int ammo = d.use.value("ammo", 0);
        const std::string weapon_id = WeaponItemId(weapon);
        if (ammo <= 0 || !WEAPON::HAS_PED_GOT_WEAPON(p, weapon, false)) {
            const auto stored = g_weapon_ammo.find(weapon_id);
            if (ammo <= 0 || stored == g_weapon_ammo.end()) {
                karma::Toast("Bu mühimmat için envanterinde silah yok", 4);
                return false;
            }
            stored->second += ammo;
        } else {
            WEAPON::ADD_AMMO_TO_PED(p, weapon, ammo);
            const auto stored = g_weapon_ammo.find(weapon_id);
            if (stored != g_weapon_ammo.end()) {
                stored->second = WEAPON::GET_AMMO_IN_PED_WEAPON(p, weapon);
            }
        }
        karma::Toast(d.name + " kullandın", 3);
        return true;
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
        for (const auto& info : kPickupWeapons) {
            std::string id = info.hash;
            std::transform(id.begin(), id.end(), id.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (g_defs.count(id)) continue;
            Def d;
            d.name = info.name;
            d.desc = "Yerden alınan, ruhsatsız silah";
            d.weight = 1.5f;
            d.legal = false;
            d.use = {{"type", "weapon"}, {"weapon", info.hash}, {"ammo", 0}};
            g_defs[id] = std::move(d);
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
    g_weapon_ammo.clear();
    g_seen_weapons.clear();
    g_weapon_scan_initialized = false;
    try {
        std::ifstream in(Path(slot));
        if (!in) return;
        for (const auto& e : json::parse(in).value("items", json::array())) {
            const std::string id = e.value("id", "");
            if (g_defs.count(id) && e.value("n", 0) > 0) {
                g_items.push_back({id, e.value("n", 0)});
                if (g_defs[id].use.value("type", "") == "weapon") {
                    g_weapon_ammo[id] = std::max(0, e.value("ammo", g_defs[id].use.value("ammo", 0)));
                }
            }
        }
    } catch (...) {
    }
}

void Shutdown() {
    Save();
    if (g_driveby_enabled) {
        PLAYER::SET_PLAYER_CAN_DO_DRIVE_BY(PLAYER::PLAYER_ID(), false);
        g_driveby_enabled = false;
    }
    StopDrunk();
    StopHigh();
    if (g_open) overlay::SetMenu({});
    g_open = false;
    g_in_actions = false;
    g_slot = -1;
    g_weapon_ammo.clear();
    g_seen_weapons.clear();
    g_weapon_scan_initialized = false;
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
        const auto def = g_defs.find(id);
        if (def != g_defs.end() && def->second.use.value("type", "") == "weapon" &&
            g_items[k].second == qty) {
            const Hash weapon = HashKey(def->second.use.value("weapon", "").c_str());
            const Ped player = PLAYER::PLAYER_PED_ID();
            if (WEAPON::HAS_PED_GOT_WEAPON(player, weapon, false)) {
                g_weapon_ammo[id] = WEAPON::GET_AMMO_IN_PED_WEAPON(player, weapon);
                WEAPON::REMOVE_WEAPON_FROM_PED(player, weapon);
            }
            g_weapon_ammo.erase(id);
            g_seen_weapons.erase(weapon);
        }
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
            if (it->second.use.value("type", "") == "weapon") {
                const Hash weapon = HashKey(it->second.use.value("weapon", "").c_str());
                WEAPON::REMOVE_WEAPON_FROM_PED(PLAYER::PLAYER_PED_ID(), weapon);
                g_weapon_ammo.erase(g_items[k].first);
                g_seen_weapons.erase(weapon);
            }
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

    const Ped player = PLAYER::PLAYER_PED_ID();
    if (!player) return;
    if (g_driveby_enabled && !PED::IS_PED_IN_ANY_VEHICLE(player, false)) {
        PLAYER::SET_PLAYER_CAN_DO_DRIVE_BY(PLAYER::PLAYER_ID(), false);
        g_driveby_enabled = false;
    }
    if (!g_weapon_scan_initialized || g_weapon_scan_ped != player) {
        g_seen_weapons.clear();
        g_weapon_scan_ped = player;
        for (const auto& info : kPickupWeapons) {
            const Hash weapon = HashKey(info.hash);
            if (WEAPON::HAS_PED_GOT_WEAPON(player, weapon, false)) g_seen_weapons.insert(weapon);
        }
        g_weapon_scan_initialized = true;
    } else {
        for (const auto& info : kPickupWeapons) {
            const Hash weapon = HashKey(info.hash);
            const bool has_weapon = WEAPON::HAS_PED_GOT_WEAPON(player, weapon, false);
            const bool had_weapon = g_seen_weapons.count(weapon) != 0;
            if (has_weapon && !had_weapon) {
                const std::string id = WeaponItemId(weapon);
                if (!id.empty()) {
                    if (Count(id) == 0 && !Add(id, 1)) {
                        g_seen_weapons.insert(weapon);
                        continue;
                    }
                    const int ammo = WEAPON::GET_AMMO_IN_PED_WEAPON(player, weapon);
                    g_weapon_ammo[id] = std::max(g_weapon_ammo[id], ammo);
                    WEAPON::REMOVE_WEAPON_FROM_PED(player, weapon);
                    g_seen_weapons.erase(weapon);
                    Save();
                    karma::Toast(std::string(info.name) + " envantere eklendi (ruhsatsız)", 4);
                    continue;
                }
            }
            if (has_weapon) g_seen_weapons.insert(weapon);
            else g_seen_weapons.erase(weapon);
        }
        if (now >= g_weapon_save_at) {
            SyncWeaponAmmo(true);
            g_weapon_save_at = now + 5000ULL;
        }
    }

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
