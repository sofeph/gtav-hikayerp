#include "barber.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "blips.h"
#include "businesses.h"
#include "economy.h"
#include "input.h"
#include "json.hpp"
#include "karma.h"
#include "overlay.h"

namespace hikaye::barber {

using nlohmann::json;

namespace {
// ---- Sakal: 10 dakikada bir kademe uzar ----
constexpr int kMaxBeard = 7;
constexpr float kBeardGrowSeconds = 600.f;
struct Look {
    int index;
    float opacity;
};
const Look kBeard[kMaxBeard + 1] = {{255, 0.f}, {12, 0.35f}, {12, 0.7f}, {12, 1.f}, {11, 0.8f}, {11, 1.f}, {8, 0.8f}, {8, 1.f}};
const char* const kBeardNames[kMaxBeard + 1] = {"temiz", "hafif kirli sakal", "kirli sakal", "gür kirli sakal", "kısa sakal", "bakımlı sakal", "uzamış sakal", "bakımsız sakal"};

// ---- Saç: normal hızda uzar (20 dakikada bir kademe); uzunken (kademe 3+) şekil verilebilir ----
constexpr int kMaxHair = 4, kStyleFromStage = 3;
constexpr float kHairGrowSeconds = 1200.f;
const char* const kHairNames[kMaxHair + 1] = {"kazıtılmış", "kısa", "orta kısa", "orta uzun", "uzun"};
// Kademe -> freemode saç modelleri (component 2). İlk eleman o kademenin varsayılanıdır.
const std::vector<int> kMale[kMaxHair + 1] = {{0}, {1, 5}, {4, 10, 18, 3}, {9, 12, 19, 21, 22}, {7, 15, 16, 17, 20, 14}};
const std::vector<int> kFemale[kMaxHair + 1] = {{0}, {1, 12}, {2, 7, 15, 20}, {10, 19, 31, 11}, {4, 6, 16, 14, 18, 22, 29}};

constexpr int kBeardTrim = 15, kShave = 25, kHairTrim = 20, kStyle = 30;
constexpr float kEnterRadius = 3.5f;

struct Spot {
    const char* name;
    float x, y, z;
};
const Spot kSpots[] = {{"Hair on Hawick", -814.3f, -183.8f, 37.6f},   {"O'Sheas Barbers", -1282.6f, -1117.0f, 7.0f},
                       {"Herr Kutz (Davis)", 136.8f, -1708.4f, 29.3f}, {"Herr Kutz (Paleto)", -278.1f, 6228.5f, 31.7f},
                       {"Herr Kutz (Sandy Shores)", 1931.5f, 3729.7f, 32.8f}, {"Bob Mulet (Mirror Park)", 1212.8f, -472.9f, 66.2f}};

std::function<void(int&, int&)> g_hair_color;
int g_slot = -1;
int g_beard = -1;  // -1: henüz belirlenmedi (ped'in mevcut sakalından)
int g_hair = -1, g_hair_draw = -1;  // saç kademesi ve gerçek model numarası
float g_beard_grow = 0.f, g_hair_grow = 0.f;
unsigned long long g_tick = 0, g_save_at = 0, g_enforce_at = 0;
std::vector<Blip> g_blips;
struct DynSpot {
    std::string name;
    Vector3 pos{};
};
std::vector<DynSpot> g_spots;
bool g_blips_made = false;
bool g_open = false;
int g_sel = 0;
Keys g_keys;
std::string g_notice;
unsigned long long g_notice_until = 0;

std::string Path(int slot) { return std::string(kBaseDir) + "/saves/beard_" + std::to_string(slot) + ".json"; }
float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

const std::vector<int>& Styles(Ped ped, int stage) { return PED::IS_PED_MALE(ped) ? kMale[stage] : kFemale[stage]; }

// Sığmayan model numarası (ped'de yoksa) en yakın geçerli olana indirilir
int Valid(Ped ped, int draw) {
    const int n = PED::GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS(ped, 2);
    return n <= 0 ? draw : std::max(0, std::min(n - 1, draw));
}

void Save() {
    if (g_slot < 0 || g_beard < 0) return;
    try {
        std::filesystem::create_directories(std::string(kBaseDir) + "/saves");
        std::ofstream out(Path(g_slot));
        out << json{{"stage", g_beard}, {"grow", g_beard_grow}, {"hair", g_hair}, {"hair_draw", g_hair_draw}, {"hair_grow", g_hair_grow}}.dump();
    } catch (const std::exception& e) {
        Log(std::string("berber kaydi yazilamadi: ") + e.what());
    }
}

// Sakal rengi = saç rengi (stil değişince oyun katman rengini varsayılana döndürebilir: yeşil çıkıyordu)
void ApplyBeardColor(Ped ped) {
    int hc = 0, hh = 0;
    if (g_hair_color) g_hair_color(hc, hh);
    PED::_SET_PED_HEAD_OVERLAY_COLOR(ped, 1, 1, hc, hh);  // 1: saç renk paleti
}

void ApplyBeard(Ped ped) {
    if (g_beard < 0) return;
    const Look& l = kBeard[std::max(0, std::min(kMaxBeard, g_beard))];
    PED::SET_PED_HEAD_OVERLAY(ped, 1, l.index, l.opacity);
    ApplyBeardColor(ped);
}

void ApplyHair(Ped ped) {
    if (g_hair < 0 || g_hair_draw < 0) return;
    PED::SET_PED_COMPONENT_VARIATION(ped, 2, Valid(ped, g_hair_draw), 0, 0);
}

// Kademe değişince (uzama/kısalma) o kademenin modelleri arasından, eski modelin sırasına yakın olanı seç
int PickStyle(Ped ped, int stage, int old_pos) {
    const std::vector<int>& list = Styles(ped, stage);
    return list[static_cast<size_t>(std::max(0, old_pos)) % list.size()];
}
int PosInStage(Ped ped, int stage, int draw) {
    const std::vector<int>& list = Styles(ped, stage);
    const auto it = std::find(list.begin(), list.end(), draw);
    return it == list.end() ? 0 : static_cast<int>(it - list.begin());
}

void Say(const std::string& m) {
    g_notice = m;
    g_notice_until = GetTickCount64() + 4000ULL;
}

void Close() {
    g_open = false;
    overlay::SetMenu({});
}
}  // namespace

void SetHairColorProvider(std::function<void(int&, int&)> provider) { g_hair_color = std::move(provider); }

void LoadSave(int slot) {
    if (g_slot >= 0) Save();
    g_slot = slot;
    g_beard = g_hair = g_hair_draw = -1;
    g_beard_grow = g_hair_grow = 0.f;
    g_tick = 0;
    try {
        std::ifstream in(Path(slot));
        if (in) {
            const json j = json::parse(in);
            g_beard = std::max(0, std::min(kMaxBeard, j.value("stage", 0)));
            g_beard_grow = std::max(0.f, std::min(kBeardGrowSeconds, j.value("grow", 0.0f)));
            g_hair = std::max(-1, std::min(kMaxHair, j.value("hair", -1)));
            g_hair_draw = j.value("hair_draw", -1);
            g_hair_grow = std::max(0.f, std::min(kHairGrowSeconds, j.value("hair_grow", 0.0f)));
        }
    } catch (const std::exception& e) {
        Log(std::string("berber kaydi okunamadi: ") + e.what());
    }
}

void Shutdown() {
    Save();
    Close();
    g_slot = -1;
    g_beard = g_hair = g_hair_draw = -1;
}

int Stage() { return g_beard; }
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

void Update(const Vector3& pos, bool e_pressed, bool blocked) {
    if (g_slot < 0) return;
    const Ped player = PLAYER::PLAYER_PED_ID();
    const unsigned long long now = GetTickCount64();
    if (!g_blips_made) {  // konumlar GitHub verisinden (businesses.json); ikonları businesses modülü çizer
        g_spots.clear();
        for (const auto& s : businesses::Spots("barber")) g_spots.push_back({s.name, s.pos});
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
    if (g_beard < 0) {  // ilk kez: karakterin yarattığı sakaldan başla
        const int cur = PED::_GET_PED_HEAD_OVERLAY_VALUE(player, 1);
        g_beard = (cur == 255 || cur <= 0) ? 0 : 3;
    }
    if (g_hair < 0 || g_hair_draw < 0) {  // ilk kez: karakterin seçtiği saçtan başla (hangi kademede olduğu listelerden bulunur)
        const int cur = PED::GET_PED_DRAWABLE_VARIATION(player, 2);
        g_hair_draw = cur;
        g_hair = 2;
        for (int st = 0; st <= kMaxHair; ++st) {
            const std::vector<int>& l = Styles(player, st);
            if (std::find(l.begin(), l.end(), cur) != l.end()) g_hair = st;
        }
    }
    // büyüme: oynanan gerçek süre (çok uzun karelerde sınırlı)
    if (g_tick == 0) g_tick = now;
    const float dt = std::min(1.0f, static_cast<float>(now - g_tick) / 1000.f);
    g_tick = now;
    g_beard_grow += dt;
    g_hair_grow += dt;
    if (g_beard_grow >= kBeardGrowSeconds) {
        g_beard_grow -= kBeardGrowSeconds;
        if (g_beard < kMaxBeard) {
            ++g_beard;
            karma::Toast(std::string("Sakalın uzadı: ") + kBeardNames[g_beard], 4);
            ApplyBeard(player);
            Save();
        }
    }
    if (g_hair_grow >= kHairGrowSeconds) {
        g_hair_grow -= kHairGrowSeconds;
        if (g_hair < kMaxHair) {
            const int pos_in = PosInStage(player, g_hair, g_hair_draw);
            ++g_hair;
            g_hair_draw = PickStyle(player, g_hair, pos_in);
            karma::Toast(std::string("Saçın uzadı: ") + kHairNames[g_hair], 4);
            ApplyHair(player);
            Save();
        }
    }
    if (now - g_enforce_at > 2000ULL) {  // profil uygulanınca/ped değişince saç ve sakal geri gelsin
        g_enforce_at = now;
        if (PED::_GET_PED_HEAD_OVERLAY_VALUE(player, 1) != kBeard[g_beard].index) ApplyBeard(player);
        else if (g_beard > 0) ApplyBeardColor(player);  // renk her 2 sn saça eşitlenir
        if (PED::GET_PED_DRAWABLE_VARIATION(player, 2) != Valid(player, g_hair_draw)) ApplyHair(player);
    }
    if (now - g_save_at > 30000ULL) {
        g_save_at = now;
        Save();
    }

    constexpr int kRows = 6;
    if (g_open) {
        CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
        if (g_keys.Repeat(VK_DOWN)) g_sel = (g_sel + 1) % kRows;
        if (g_keys.Repeat(VK_UP)) g_sel = (g_sel + kRows - 1) % kRows;
        if (g_keys.Pressed(VK_BACK)) {
            Close();
            return;
        }
        if (g_keys.Pressed(VK_RETURN)) {
            if (g_sel == 0 || g_sel == 1) {  // sakal: kısalt / tıraş (sadece kısaltma)
                const int price = g_sel == 0 ? kBeardTrim : kShave;
                if (g_beard <= 0) Say("Sakalın zaten temiz");
                else if (!economy::Spend(price)) Say("Yetersiz bakiye");
                else {
                    g_beard = g_sel == 0 ? g_beard - 1 : 0;
                    ApplyBeard(player);
                    Save();
                    Say(std::string("Sakal: ") + kBeardNames[g_beard]);
                }
            } else if (g_sel == 2) {  // saç kısalt (bir kademe)
                if (g_hair <= 0) Say("Saçın zaten kazıtılmış");
                else if (!economy::Spend(kHairTrim)) Say("Yetersiz bakiye");
                else {
                    const int pos_in = PosInStage(player, g_hair, g_hair_draw);
                    --g_hair;
                    g_hair_draw = PickStyle(player, g_hair, pos_in);
                    ApplyHair(player);
                    Save();
                    Say(std::string("Saç: ") + kHairNames[g_hair]);
                }
            } else if (g_sel == 3) {  // şekil ver: sadece uzun saçta
                const std::vector<int>& list = Styles(player, g_hair);
                if (g_hair < kStyleFromStage) Say("Saçın şekil verilecek kadar uzun değil");
                else if (list.size() < 2) Say("Bu uzunlukta başka şekil yok");
                else if (!economy::Spend(kStyle)) Say("Yetersiz bakiye");
                else {
                    const int p = PosInStage(player, g_hair, g_hair_draw);
                    g_hair_draw = list[static_cast<size_t>(p + 1) % list.size()];
                    ApplyHair(player);
                    Save();
                    Say("Yeni saç şekli " + std::to_string((p + 1) % static_cast<int>(list.size()) + 1) + "/" + std::to_string(list.size()));
                }
            } else if (g_sel == 4) {
                Close();
                return;
            } else {
                Close();
                return;
            }
        }
        overlay::MenuView mv;
        mv.open = true;
        mv.title = "Berber";
        mv.subtitle = std::string("Sakal: ") + kBeardNames[g_beard] + "   Saç: " + kHairNames[g_hair] + "   Nakit: $" + std::to_string(std::max(0, economy::GetCash())) +
                      (now < g_notice_until ? "   " + g_notice : "");
        mv.rows.push_back({"Sakalı bir kademe kısalt", "$" + std::to_string(kBeardTrim), false});
        mv.rows.push_back({"Tamamen tıraş et", "$" + std::to_string(kShave), false});
        mv.rows.push_back({"Saçı bir kademe kısalt", "$" + std::to_string(kHairTrim), false});
        mv.rows.push_back({"Saça şekil ver (uzun saç)", g_hair >= kStyleFromStage ? "$" + std::to_string(kStyle) : "saç yeterince uzun değil", false});
        mv.rows.push_back({"Çık", "", false});
        mv.rows.push_back({"", "", true});
        mv.selected = g_sel;
        mv.hint = "↑↓ seç   Enter uygula   Backspace çık   (sakal 10 dk, saç 20 dk'da bir uzar; berber sadece kısaltır)";
        overlay::SetMenu(mv);
        return;
    }
    if (blocked || PED::IS_PED_IN_ANY_VEHICLE(player, false)) return;
    for (const auto& s : g_spots) {
        if (D2(pos, s.pos) > kEnterRadius + 1.5f) continue;
        ShowHelp("~INPUT_PICKUP~ Berber");
        if (e_pressed) {
            g_open = true;
            g_sel = 0;
        }
        return;
    }
}

}  // namespace hikaye::barber
