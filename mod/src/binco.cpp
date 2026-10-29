#include "binco.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "blips.h"
#include "businesses.h"
#include "economy.h"
#include "input.h"
#include "json.hpp"
#include "karma.h"
#include "overlay.h"

namespace hikaye::binco {

using nlohmann::json;

namespace {
struct Cat {
    const char* name;
    int comp;     // component numarası (props: 0 şapka, 1 gözlük)
    bool prop;
    int price;
};
const Cat kCats[] = {{"Üst", 11, false, 60}, {"Alt", 4, false, 50}, {"Ayakkabı", 6, false, 45}, {"Şapka", 0, true, 25}, {"Gözlük", 1, true, 20}};
constexpr int kNCats = 5;
constexpr float kEnterRadius = 3.5f;

struct Spot {
    const char* name;
    float x, y, z;
};
const Spot kSpots[] = {{"Binco (Strawberry)", 425.6f, -806.5f, 29.5f},   {"Binco (Del Perro)", -822.5f, -1073.5f, 11.3f},
                       {"Binco (Sandy Shores)", 1196.7f, 2710.2f, 38.2f}, {"Binco (Paleto Bay)", 4.9f, 6512.4f, 31.9f},
                       {"Binco (Harmony)", 1693.6f, 4822.8f, 42.1f}};

struct Item {
    int draw = 0, tex = 0;  // props: draw -1 = yok
};

int g_slot = -1;
Item g_worn[kNCats];        // kayıtlı/giyilen
bool g_has_worn[kNCats] = {};
std::set<std::string> g_owned;
Item g_prev[kNCats];        // menüde önizlenen
Item g_snap[kNCats];        // menü açılırken gerçekte giyilen (çıkışta geri)
bool g_open = false;
int g_sel = 0;
Keys g_keys;
std::string g_notice;
unsigned long long g_notice_until = 0, g_enforce_at = 0, g_save_at = 0;
std::vector<Blip> g_blips;
struct DynSpot {
    std::string name;
    Vector3 pos{};
};
std::vector<DynSpot> g_spots;
bool g_blips_made = false;

std::string Path(int slot) { return std::string(kBaseDir) + "/saves/wardrobe_" + std::to_string(slot) + ".json"; }
float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
std::string Key(int cat, const Item& it) { return std::to_string(cat) + ":" + std::to_string(it.draw) + ":" + std::to_string(it.tex); }

Item ReadWorn(Ped ped, int cat) {
    Item it;
    if (kCats[cat].prop) {
        it.draw = PED::GET_PED_PROP_INDEX(ped, kCats[cat].comp);
        it.tex = it.draw < 0 ? 0 : PED::GET_PED_PROP_TEXTURE_INDEX(ped, kCats[cat].comp);
    } else {
        it.draw = PED::GET_PED_DRAWABLE_VARIATION(ped, kCats[cat].comp);
        it.tex = PED::GET_PED_TEXTURE_VARIATION(ped, kCats[cat].comp);
    }
    return it;
}

void Apply(Ped ped, int cat, const Item& it) {
    const Cat& c = kCats[cat];
    if (c.prop) {
        if (it.draw < 0) PED::CLEAR_PED_PROP(ped, c.comp);
        else PED::SET_PED_PROP_INDEX(ped, c.comp, it.draw, it.tex, true);
    } else {
        PED::SET_PED_COMPONENT_VARIATION(ped, c.comp, it.draw, it.tex, 0);
        if (c.comp == 11) {  // üst değişince çıplak kol/iç gömlek (model tablosu olmadan en güvenli varsayılan)
            PED::SET_PED_COMPONENT_VARIATION(ped, 3, 15, 0, 0);
            PED::SET_PED_COMPONENT_VARIATION(ped, 8, 15, 0, 0);
        }
    }
}

int NumDraw(Ped ped, int cat) {
    const Cat& c = kCats[cat];
    return c.prop ? PED::GET_NUMBER_OF_PED_PROP_DRAWABLE_VARIATIONS(ped, c.comp) : PED::GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS(ped, c.comp);
}
int NumTex(Ped ped, int cat, int draw) {
    const Cat& c = kCats[cat];
    if (draw < 0) return 1;
    return std::max(1, c.prop ? PED::GET_NUMBER_OF_PED_PROP_TEXTURE_VARIATIONS(ped, c.comp, draw) : PED::GET_NUMBER_OF_PED_TEXTURE_VARIATIONS(ped, c.comp, draw));
}

void Save() {
    if (g_slot < 0) return;
    try {
        std::filesystem::create_directories(std::string(kBaseDir) + "/saves");
        json worn = json::object();
        for (int i = 0; i < kNCats; ++i) {
            if (g_has_worn[i]) worn[std::to_string(i)] = {g_worn[i].draw, g_worn[i].tex};
        }
        std::ofstream out(Path(g_slot));
        out << json{{"worn", worn}, {"owned", g_owned}}.dump(1);
    } catch (const std::exception& e) {
        Log(std::string("gardirop kaydi yazilamadi: ") + e.what());
    }
}

void Say(const std::string& m) {
    g_notice = m;
    g_notice_until = GetTickCount64() + 4000ULL;
}

void CloseMenu(bool revert) {
    if (g_open && revert) {
        const Ped ped = PLAYER::PLAYER_PED_ID();
        for (int i = 0; i < kNCats; ++i) Apply(ped, i, g_has_worn[i] ? g_worn[i] : g_snap[i]);
    }
    g_open = false;
    overlay::SetMenu({});
}
std::string g_title = "Binco";
bool g_wardrobe_only = false;  // ev gardırobu: sadece sahip olunanlar, fiyat yok
}  // namespace

void LoadSave(int slot) {
    if (g_slot >= 0) Save();
    g_slot = slot;
    for (int i = 0; i < kNCats; ++i) g_has_worn[i] = false;
    g_owned.clear();
    try {
        std::ifstream in(Path(slot));
        if (in) {
            const json j = json::parse(in);
            for (const auto& [k, v] : j.value("worn", json::object()).items()) {  // KOPYA üzerinde dolaş (geçici .items() asılı kalır)
                const int i = std::atoi(k.c_str());
                if (i >= 0 && i < kNCats && v.is_array() && v.size() >= 2) {
                    g_worn[i].draw = v[0].get<int>();
                    g_worn[i].tex = v[1].get<int>();
                    g_has_worn[i] = true;
                }
            }
            for (const auto& o : j.value("owned", json::array())) g_owned.insert(o.get<std::string>());
        }
    } catch (const std::exception& e) {
        Log(std::string("gardirop kaydi okunamadi: ") + e.what());
    }
}

void Shutdown() {
    Save();
    CloseMenu(true);
    g_slot = -1;
}

bool MenuOpen() { return g_open; }
bool OwnsBlip(Blip b) { return b != 0 && std::find(g_blips.begin(), g_blips.end(), b) != g_blips.end(); }

void Reset() {
    CloseMenu(true);
    for (Blip b : g_blips) {
        Blip x = b;
        if (UI::DOES_BLIP_EXIST(x)) UI::REMOVE_BLIP(&x);
    }
    g_blips.clear();
    g_blips_made = false;
}

void Update(const Vector3& pos, bool e_pressed, bool blocked) {
    if (g_slot < 0) return;
    const Ped player = PLAYER::PLAYER_PED_ID();
    const unsigned long long now = GetTickCount64();
    if (!g_blips_made) {  // konumlar GitHub verisinden (businesses.json); ikonları businesses modülü çizer
        g_spots.clear();
        for (const auto& s : businesses::Spots("clothing")) g_spots.push_back({s.name, s.pos});
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
    if (!g_open && now - g_enforce_at > 2000ULL) {  // kayıtlı kıyafet profil uygulanınca/ped değişince geri gelsin
        g_enforce_at = now;
        for (int i = 0; i < kNCats; ++i) {
            if (!g_has_worn[i]) continue;
            const Item cur = ReadWorn(player, i);
            if (cur.draw != g_worn[i].draw || cur.tex != g_worn[i].tex) Apply(player, i, g_worn[i]);
        }
    }
    if (now - g_save_at > 30000ULL) {
        g_save_at = now;
        Save();
    }

    constexpr int kRows = kNCats * 2 + 1;
    if (g_open) {
        CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
        if (g_keys.Repeat(VK_DOWN)) g_sel = (g_sel + 1) % kRows;
        if (g_keys.Repeat(VK_UP)) g_sel = (g_sel + kRows - 1) % kRows;
        if (g_keys.Pressed(VK_BACK)) {
            CloseMenu(true);
            return;
        }
        const int cat = g_sel / 2;
        if (g_sel < kNCats * 2) {
            const int dir = (g_keys.Repeat(VK_RIGHT) ? 1 : 0) - (g_keys.Repeat(VK_LEFT) ? 1 : 0);
            if (dir != 0) {
                Item& it = g_prev[cat];
                if (g_sel % 2 == 0) {  // model
                    const int n = NumDraw(player, cat);
                    const int lo = kCats[cat].prop ? -1 : 0;
                    int d = it.draw + dir;
                    if (d > n - 1) d = lo;
                    if (d < lo) d = n - 1;
                    it.draw = d;
                    it.tex = 0;
                } else {  // renk
                    const int m = NumTex(player, cat, it.draw);
                    it.tex = ((it.tex + dir) % m + m) % m;
                }
                Apply(player, cat, it);
            }
            if (g_keys.Pressed(VK_RETURN)) {
                const Item& it = g_prev[cat];
                const bool owned = g_owned.count(Key(cat, it)) > 0 || (kCats[cat].prop && it.draw < 0);
                if (g_wardrobe_only && !owned) {
                    Say("Buna sahip değilsin (Binco'dan al)");
                } else if (!owned && !economy::Spend(kCats[cat].price)) {
                    Say("Yetersiz bakiye");
                } else {
                    if (!owned) g_owned.insert(Key(cat, it));
                    g_worn[cat] = it;
                    g_has_worn[cat] = true;
                    g_snap[cat] = it;
                    Save();
                    Say(owned ? "Giydin" : "Satın alındı: -$" + std::to_string(kCats[cat].price));
                }
            }
        } else if (g_keys.Pressed(VK_RETURN)) {
            CloseMenu(true);
            return;
        }
        overlay::MenuView mv;
        mv.open = true;
        mv.title = g_title;
        mv.subtitle = g_wardrobe_only ? std::string("Gardirop") : "Nakit: $" + std::to_string(std::max(0, economy::GetCash()));
        if (now < g_notice_until) mv.subtitle += "   " + g_notice;
        for (int i = 0; i < kNCats; ++i) {
            const Item& it = g_prev[i];
            const int n = NumDraw(player, i);
            const bool owned = g_owned.count(Key(i, it)) > 0 || (kCats[i].prop && it.draw < 0);
            const std::string price = g_wardrobe_only ? (owned ? "sahipsin" : "sahip değil") : (owned ? "sahipsin" : "$" + std::to_string(kCats[i].price));
            const std::string model = kCats[i].prop && it.draw < 0 ? "yok" : std::to_string(it.draw + 1) + "/" + std::to_string(n);
            mv.rows.push_back({std::string(kCats[i].name) + ": model", "◄ " + model + " ►   " + price, false});
            mv.rows.push_back({std::string(kCats[i].name) + ": renk", "◄ " + std::to_string(it.tex + 1) + "/" + std::to_string(NumTex(player, i, it.draw)) + " ►", false});
        }
        mv.rows.push_back({"Çık", "", false});
        mv.selected = g_sel;
        mv.hint = g_wardrobe_only ? "↑↓ satır   ←→ model/renk (önizleme)   Enter giy   Backspace çık"
                                  : "↑↓ satır   ←→ model/renk (önizleme)   Enter satın al/giy   Backspace çık";
        overlay::SetMenu(mv);
        return;
    }
    if (blocked || PED::IS_PED_IN_ANY_VEHICLE(player, false)) return;
    for (const auto& s : g_spots) {
        if (D2(pos, s.pos) > kEnterRadius + 1.5f) continue;
        ShowHelp("~INPUT_PICKUP~ Binco");
        if (e_pressed) {
            g_title = "Binco";
            g_wardrobe_only = false;
            g_open = true;
            g_sel = 0;
            for (int i = 0; i < kNCats; ++i) {
                g_snap[i] = ReadWorn(player, i);
                g_prev[i] = g_snap[i];
            }
        }
        return;
    }
}

void OpenWardrobe(const char* title) {
    if (g_slot < 0 || g_open) return;
    const Ped player = PLAYER::PLAYER_PED_ID();
    g_title = title ? title : "Gardirop";
    g_wardrobe_only = true;
    g_open = true;
    g_sel = 0;
    for (int i = 0; i < kNCats; ++i) {
        g_snap[i] = ReadWorn(player, i);
        g_prev[i] = g_has_worn[i] ? g_worn[i] : g_snap[i];
    }
}

void CloseWardrobe(bool revert) { CloseMenu(revert); }

}  // namespace hikaye::binco
