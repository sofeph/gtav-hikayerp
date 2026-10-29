#include "karma.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "crimes.h"
#include "json.hpp"

namespace hikaye::karma {

using nlohmann::json;

namespace {
int g_slot = -1;
int g_value = 0;
std::map<std::string, int> g_counters;
std::string g_toast;
unsigned long long g_toast_until = 0, g_scan_at = 0;
std::set<Ped> g_struck;  // bu oturumda saldırıya uğramış yayalar (aynı yayaya tekrar tekrar puan düşme)

float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
std::string Path(int slot) { return std::string(kBaseDir) + "/saves/karma_" + std::to_string(slot) + ".json"; }
}  // namespace

void Load(int slot) {
    if (slot == g_slot) return;
    if (g_slot >= 0) Save();
    g_slot = slot;
    g_value = 0;
    g_counters.clear();
    g_struck.clear();
    try {
        std::ifstream in(Path(slot));
        if (!in) return;
        const json j = json::parse(in);
        g_value = std::clamp(j.value("value", 0), -100, 100);
        const json counters = j.value("counters", json::object());
        for (const auto& [k, v] : counters.items()) g_counters[k] = v.get<int>();
    } catch (...) {
    }
    Log("karma: " + std::to_string(g_value) + " (slot " + std::to_string(slot) + ")");
}

void Save() {
    if (g_slot < 0) return;
    try {
        std::filesystem::create_directories(std::string(kBaseDir) + "/saves");
        json c = json::object();
        for (const auto& [k, v] : g_counters) c[k] = v;
        std::ofstream out(Path(g_slot));
        out << json{{"value", g_value}, {"counters", c}}.dump(2);
    } catch (...) {
    }
}

void Shutdown() {
    Save();
    g_slot = -1;
    g_toast_until = 0;
}

int Value() { return g_value; }

std::string Level() {
    if (g_value <= -60) return "outlaw";
    if (g_value <= -20) return "bad";
    if (g_value >= 60) return "hero";
    if (g_value >= 20) return "good";
    return "neutral";
}

std::string LevelLabel() {
    const std::string l = Level();
    if (l == "outlaw") return "Haydut";
    if (l == "bad") return "Kötü şöhretli";
    if (l == "hero") return "Halk kahramanı";
    if (l == "good") return "İyi biri";
    return "Sıradan";
}

int Counter(const std::string& key) {
    const auto it = g_counters.find(key);
    return it == g_counters.end() ? 0 : it->second;
}

void Toast(const std::string& text, int seconds) {
    g_toast = text;
    g_toast_until = GetTickCount64() + static_cast<unsigned long long>(seconds) * 1000ULL;
}

void Add(int delta, const std::string& reason, const std::string& counter) {
    if (g_slot < 0 || delta == 0) return;
    const int before = g_value;
    g_value = std::clamp(g_value + delta, -100, 100);
    if (!counter.empty()) ++g_counters[counter];
    g_toast = std::string("Onur ") + (delta > 0 ? "+" : "") + std::to_string(g_value - before) + ": " + reason;
    g_toast_until = GetTickCount64() + 4500;
    Log("karma " + std::to_string(before) + " -> " + std::to_string(g_value) + " (" + reason + ")");
    Save();
}

std::string Lookup(const std::string& key) {
    if (key == "karma") return std::to_string(g_value);
    if (key == "karma_level") return Level();
    if (key == "karma_label") return LevelLabel();
    if (key.rfind("count_", 0) == 0) return std::to_string(Counter(key.substr(6)));
    return "";
}

void Update() {
    const unsigned long long now = GetTickCount64();
    if (now < g_toast_until) DrawLine(g_toast, 0.70f, g_toast.find("+") != std::string::npos ? 140 : 255, g_toast.find("+") != std::string::npos ? 230 : 130, 130);
    if (g_slot < 0 || now - g_scan_at < 400) return;
    g_scan_at = now;
    // Fiziksel saldırı: yakındaki bir yaya oyuncudan hasar aldı (yumruk, silah). Aynı yaya için bir kez.
    const Ped player = PLAYER::PLAYER_PED_ID();
    // Yolcu (taksi, otostop, otobüs, arkadaş arabası) sürüş hatalarından muaftır: sadece sürücü koltuğundaki oyuncu araç suçu işler
    Vehicle pv = PED::GET_VEHICLE_PED_IS_IN(player, false);
    if (pv != 0 && VEHICLE::GET_PED_IN_VEHICLE_SEAT(pv, -1) != player) pv = 0;
    int arr[2 + 2 * 16];
    arr[0] = 16;
    const int n = PED::GET_PED_NEARBY_PEDS(player, arr, -1);
    for (int i = 0; i < n && i < 16; ++i) {
        const Ped p = arr[2 + i * 2];
        if (p == 0 || p == player || !ENTITY::DOES_ENTITY_EXIST(p) || !PED::IS_PED_HUMAN(p)) continue;
        // Hasar veren: oyuncunun kendisi (yumruk/silah) ya da sürdüğü araç (çarpma). Atıf başarısızsa: araç hızlıyken yanında yere
        // yığılan/ölen yaya da araç çarpması sayılır (heuristik).
        const bool by_player = ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(p, player, true);
        bool by_vehicle = pv != 0 && ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(p, pv, true);
        if (!by_player && !by_vehicle && pv != 0 && ENTITY::GET_ENTITY_SPEED(pv) > 4.f && !g_struck.count(p) &&
            (PED::IS_PED_RAGDOLL(p) || ENTITY::IS_ENTITY_DEAD(p)) &&
            D2(ENTITY::GET_ENTITY_COORDS(p, true), ENTITY::GET_ENTITY_COORDS(pv, true)) < 4.8f) {
            by_vehicle = true;
        }
        if (!by_player && !by_vehicle) continue;
        const bool via_car = by_vehicle && !by_player;
        ENTITY::CLEAR_ENTITY_LAST_DAMAGE_ENTITY(p);
        if (PED::IS_PED_IN_COMBAT(p, player) && !ENTITY::IS_ENTITY_DEAD(p) && g_struck.count(p)) continue;
        const bool dead = ENTITY::IS_ENTITY_DEAD(p);
        if (g_struck.count(p) && !dead) continue;
        g_struck.insert(p);
        const int ptype = PED::GET_PED_TYPE(p);
        const bool cop = ptype == 6 || ptype == 27 || ptype == 29;
        if (via_car) {
            Add(dead ? -10 : -4, dead ? "Araçla birini öldürdün" : "Araçla bir yayaya çarptın", dead ? "cinayet" : "carpma");
            if (!crimes::IsExempt(p, 0)) crimes::Record(cop && dead ? "cop_killed" : (dead ? "vehicular_kill" : "hit_and_run"), ENTITY::GET_ENTITY_COORDS(p, true));
        } else {
            Add(dead ? -12 : -5, dead ? "Masum birini öldürdün" : "Bir yayaya saldırdın", dead ? "cinayet" : "siddet");
            if (!crimes::IsExempt(p, 0)) crimes::Record(dead ? (cop ? "cop_killed" : "murder") : "assault", ENTITY::GET_ENTITY_COORDS(p, true));  // çağırdığın tamirci/taksi şoförü suç sayılmaz
        }
    }
    if (g_struck.size() > 200) g_struck.clear();
}

}  // namespace hikaye::karma
