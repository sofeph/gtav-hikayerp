#include "customs.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "blips.h"
#include "businesses.h"
#include "economy.h"
#include "input.h"
#include "karma.h"
#include "overlay.h"

namespace hikaye::customs {

namespace {
struct Spot {
    const char* name;
    float x, y, z;
};
const Spot kSpots[] = {{"Los Santos Customs (Burton)", -337.4f, -136.9f, 38.6f},
                       {"Los Santos Customs (La Mesa)", 731.9f, -1088.8f, 21.7f},
                       {"Los Santos Customs (Havalimanı)", -1155.5f, -2007.2f, 13.2f},
                       {"Los Santos Customs (Harmony)", 1175.0f, 2640.2f, 37.8f},
                       {"Beeker's Garage (Paleto)", 110.8f, 6626.4f, 31.8f},
                       {"Los Santos Customs (Downtown)", -205.7f, -1310.7f, 31.3f}};
constexpr float kEnterRadius = 9.f;

std::vector<Blip> g_blips;
struct DynSpot {
    std::string name;
    Vector3 pos{};
};
std::vector<DynSpot> g_spots;
bool g_blips_made = false;
bool g_open = false;
Vehicle g_veh = 0;
int g_tab = 0, g_sel = 0;
Keys g_keys;
std::string g_notice;
unsigned long long g_notice_until = 0;

float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

const char* const kTabs[] = {"Renk", "Performans", "Görünüm", "Diğer"};

struct ColorOpt {
    const char* name;
    int id;
};
const ColorOpt kColors[] = {{"Siyah", 0},   {"Antrasit", 1}, {"Gümüş", 4},  {"Beyaz", 111},   {"Kırmızı", 27}, {"Turuncu", 38},
                            {"Sarı", 88},   {"Yeşil", 53},   {"Mavi", 64},  {"Lacivert", 62}, {"Mor", 145},    {"Pembe", 135}};
constexpr int kColorPrice = 400;

struct ModType {
    int type;
    const char* name;
    int level_price;  // performans: seviye başına
    bool perf;
};
const ModType kPerf[] = {{11, "Motor", 600, true}, {12, "Fren", 350, true}, {13, "Şanzıman", 450, true}, {15, "Süspansiyon", 400, true}, {16, "Zırh", 700, true}};
const ModType kLook[] = {{0, "Spoiler", 250, false}, {1, "Ön tampon", 250, false}, {2, "Arka tampon", 250, false}, {3, "Yan etek", 250, false},
                         {4, "Egzoz", 250, false},   {6, "Izgara", 250, false},    {7, "Kaput", 250, false},      {10, "Tavan", 250, false}};

struct Row {
    std::string label, value;
    int price = 0;
    std::function<void()> apply;  // para düşüldükten sonra uygulanır
};

void Say(const std::string& m) {
    g_notice = m;
    g_notice_until = GetTickCount64() + 4000ULL;
}

std::vector<Row> Rows() {
    std::vector<Row> rows;
    const Vehicle v = g_veh;
    VEHICLE::SET_VEHICLE_MOD_KIT(v, 0);
    if (g_tab == 0) {
        int c1 = 0, c2 = 0;
        VEHICLE::GET_VEHICLE_COLOURS(v, &c1, &c2);
        for (const auto& c : kColors) {
            Row r;
            r.label = c.name;
            r.value = (c1 == c.id ? "takılı  " : "") + std::string("$") + std::to_string(kColorPrice);
            r.price = c1 == c.id ? 0 : kColorPrice;
            const int id = c.id;
            r.apply = [v, id]() {
                VEHICLE::SET_VEHICLE_COLOURS(v, id, id);
                VEHICLE::SET_VEHICLE_EXTRA_COLOURS(v, 0, 0);
            };
            rows.push_back(std::move(r));
        }
    } else if (g_tab == 1) {
        for (const auto& m : kPerf) {
            const int n = VEHICLE::GET_NUM_VEHICLE_MODS(v, m.type);
            const int cur = VEHICLE::GET_VEHICLE_MOD(v, m.type);
            for (int lv = 0; lv < n; ++lv) {
                Row r;
                r.label = std::string(m.name) + ": seviye " + std::to_string(lv + 1);
                r.price = cur == lv ? 0 : m.level_price * (lv + 1);
                r.value = (cur == lv ? "takılı  " : "") + std::string("$") + std::to_string(m.level_price * (lv + 1));
                const int type = m.type;
                r.apply = [v, type, lv]() { VEHICLE::SET_VEHICLE_MOD(v, type, lv, false); };
                rows.push_back(std::move(r));
            }
            if (n > 0 && cur >= 0) {
                Row r;
                r.label = std::string(m.name) + ": stok";
                r.value = "ücretsiz";
                const int type = m.type;
                r.apply = [v, type]() { VEHICLE::SET_VEHICLE_MOD(v, type, -1, false); };
                rows.push_back(std::move(r));
            }
        }
        const bool turbo = VEHICLE::IS_TOGGLE_MOD_ON(v, 18) != 0;
        Row t;
        t.label = "Turbo";
        t.price = turbo ? 0 : 1500;
        t.value = turbo ? "takılı (kaldır: ücretsiz)" : "$1500";
        t.apply = [v, turbo]() { VEHICLE::TOGGLE_VEHICLE_MOD(v, 18, !turbo); };
        rows.push_back(std::move(t));
    } else if (g_tab == 2) {
        for (const auto& m : kLook) {
            const int n = VEHICLE::GET_NUM_VEHICLE_MODS(v, m.type);
            if (n <= 0) continue;
            const int cur = VEHICLE::GET_VEHICLE_MOD(v, m.type);
            const int next = cur + 1 >= n ? -1 : cur + 1;  // Enter: sıradaki model, sondan sonra stok
            Row r;
            r.label = std::string(m.name) + ": " + (cur < 0 ? "stok" : "model " + std::to_string(cur + 1));
            r.price = next < 0 ? 0 : m.level_price;
            r.value = next < 0 ? "stoka dön" : "sıradaki: $" + std::to_string(m.level_price);
            const int type = m.type;
            r.apply = [v, type, next]() { VEHICLE::SET_VEHICLE_MOD(v, type, next, false); };
            rows.push_back(std::move(r));
        }
        if (rows.empty()) {
            Row r;
            r.label = "Bu araçta görünüm parçası yok";
            rows.push_back(std::move(r));
        }
    } else {
        const int tint = VEHICLE::GET_VEHICLE_WINDOW_TINT(v);
        const int ntint = (tint + 1) % 6;
        Row a;
        a.label = "Cam filmi: seviye " + std::to_string(tint);
        a.price = ntint == 0 ? 0 : 150;
        a.value = ntint == 0 ? "kaldır" : "sıradaki: $150";
        a.apply = [v, ntint]() { VEHICLE::SET_VEHICLE_WINDOW_TINT(v, ntint); };
        rows.push_back(std::move(a));
        const int pl = VEHICLE::GET_VEHICLE_NUMBER_PLATE_TEXT_INDEX(v);
        Row b;
        b.label = "Plaka stili: " + std::to_string(pl + 1);
        b.price = 100;
        b.value = "sıradaki: $100";
        b.apply = [v, pl]() { VEHICLE::SET_VEHICLE_NUMBER_PLATE_TEXT_INDEX(v, (pl + 1) % 6); };
        rows.push_back(std::move(b));
        const bool xenon = VEHICLE::IS_TOGGLE_MOD_ON(v, 22) != 0;
        Row c;
        c.label = "Xenon farlar";
        c.price = xenon ? 0 : 500;
        c.value = xenon ? "takılı (kaldır: ücretsiz)" : "$500";
        c.apply = [v, xenon]() { VEHICLE::TOGGLE_VEHICLE_MOD(v, 22, !xenon); };
        rows.push_back(std::move(c));
        const bool smoke = VEHICLE::IS_TOGGLE_MOD_ON(v, 20) != 0;
        Row d;
        d.label = "Lastik dumanı";
        d.price = smoke ? 0 : 400;
        d.value = smoke ? "takılı (kaldır: ücretsiz)" : "$400";
        d.apply = [v, smoke]() { VEHICLE::TOGGLE_VEHICLE_MOD(v, 20, !smoke); };
        rows.push_back(std::move(d));
        Row rp;
        rp.label = "Onar ve yıka";
        rp.price = 250;
        rp.value = "$250";
        rp.apply = [v]() {
            VEHICLE::SET_VEHICLE_FIXED(v);
            VEHICLE::SET_VEHICLE_DEFORMATION_FIXED(v);
            VEHICLE::SET_VEHICLE_DIRT_LEVEL(v, 0.f);
            VEHICLE::SET_VEHICLE_ENGINE_HEALTH(v, 1000.f);
            VEHICLE::SET_VEHICLE_PETROL_TANK_HEALTH(v, 1000.f);
        };
        rows.push_back(std::move(rp));
        const bool neon = VEHICLE::_IS_VEHICLE_NEON_LIGHT_ENABLED(v, 0) != 0;
        Row e;
        e.label = "Neon altı aydınlatma";
        e.price = neon ? 0 : 800;
        e.value = neon ? "takılı (kaldır: ücretsiz)" : "$800";
        e.apply = [v, neon]() {
            for (int i = 0; i < 4; ++i) VEHICLE::_SET_VEHICLE_NEON_LIGHT_ENABLED(v, i, !neon);
            if (!neon) VEHICLE::_SET_VEHICLE_NEON_LIGHTS_COLOUR(v, 40, 120, 255);
        };
        rows.push_back(std::move(e));
    }
    return rows;
}

void Close() {
    if (g_open && g_veh != 0 && ENTITY::DOES_ENTITY_EXIST(g_veh)) ENTITY::FREEZE_ENTITY_POSITION(g_veh, false);
    g_open = false;
    g_veh = 0;
    overlay::SetMenu({});
}
}  // namespace

void Load() {}

bool MenuOpen() { return g_open; }

bool OwnsBlip(Blip b) { return b != 0 && std::find(g_blips.begin(), g_blips.end(), b) != g_blips.end(); }

void Reset() {
    Close();
    for (Blip b : g_blips) {
        Blip x = b;
        if (UI::DOES_BLIP_EXIST(x)) UI::REMOVE_BLIP(&x);
    }
    g_blips.clear();
    g_blips_made = false;
}

void Update(const Vector3& pos, bool e_pressed, bool blocked, const std::function<bool(Vehicle)>& is_owned) {
    if (!g_blips_made) {  // konumlar GitHub verisinden (businesses.json); ikonları businesses modülü çizer
        g_spots.clear();
        for (const auto& s : businesses::Spots("customs")) g_spots.push_back({s.name, s.pos});
        if (g_spots.empty()) {
            for (const auto& s : kSpots) {
                Vector3 p{};
                p.x = s.x;
                p.y = s.y;
                p.z = s.z;
                g_spots.push_back({s.name, p});
            }
        }
        g_blips_made = true;
    }
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (g_open) {
        CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
        if (g_veh == 0 || !ENTITY::DOES_ENTITY_EXIST(g_veh) || !PED::IS_PED_IN_VEHICLE(player, g_veh, false)) {
            Close();
            return;
        }
        const std::vector<Row> rows = Rows();
        const int n = static_cast<int>(rows.size());
        if (g_sel >= n) g_sel = std::max(0, n - 1);
        if (g_keys.Pressed(VK_LEFT)) { g_tab = (g_tab + 3) % 4; g_sel = 0; }
        if (g_keys.Pressed(VK_RIGHT)) { g_tab = (g_tab + 1) % 4; g_sel = 0; }
        if (g_keys.Repeat(VK_DOWN) && n > 0) g_sel = (g_sel + 1) % n;
        if (g_keys.Repeat(VK_UP) && n > 0) g_sel = (g_sel + n - 1) % n;
        if (g_keys.Pressed(VK_BACK)) {
            Close();
            return;
        }
        if (g_keys.Pressed(VK_RETURN) && n > 0 && rows[static_cast<size_t>(g_sel)].apply) {
            const Row& r = rows[static_cast<size_t>(g_sel)];
            if (r.price > 0 && !economy::Spend(r.price)) {
                Say("Yetersiz bakiye");
            } else {
                r.apply();
                Say(r.price > 0 ? "Uygulandı: -$" + std::to_string(r.price) : "Uygulandı");
            }
        }
        overlay::MenuView mv;
        mv.open = true;
        mv.title = "Los Santos Customs";
        mv.subtitle = "Nakit: $" + std::to_string(std::max(0, economy::GetCash())) + (GetTickCount64() < g_notice_until ? "   " + g_notice : "");
        for (const char* t : kTabs) mv.tabs.push_back(t);
        mv.tab = g_tab;
        const std::vector<Row> shown = Rows();
        for (const auto& r : shown) mv.rows.push_back({r.label, r.value, false});
        mv.selected = g_sel;
        mv.hint = "←→ kategori   ↑↓ seç   Enter uygula   Backspace çık";
        overlay::SetMenu(mv);
        return;
    }
    if (blocked || !PED::IS_PED_IN_ANY_VEHICLE(player, false)) return;
    const Vehicle veh = PED::GET_VEHICLE_PED_IS_IN(player, false);
    if (VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, -1) != player || !VEHICLE::IS_THIS_MODEL_A_CAR(ENTITY::GET_ENTITY_MODEL(veh))) return;
    for (const auto& s : g_spots) {
        if (D2(pos, s.pos) > kEnterRadius + 5.f) continue;
        ShowHelp("~INPUT_PICKUP~ Los Santos Customs");
        if (e_pressed) {
            if (!is_owned || !is_owned(veh)) {
                karma::Toast("Sadece kendi aracını modifiye edebilirsin.", 4);
                return;
            }
            g_open = true;
            g_veh = veh;
            g_tab = 0;
            g_sel = 0;
            VEHICLE::SET_VEHICLE_ENGINE_ON(veh, false, true, false);
            ENTITY::FREEZE_ENTITY_POSITION(veh, true);
        }
        return;
    }
}

}  // namespace hikaye::customs
