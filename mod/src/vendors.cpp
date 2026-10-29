#include "vendors.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>

#include "karma.h"

namespace hikaye::vendors {

using nlohmann::json;

namespace {
int g_slot = -1;
std::set<std::string> g_found;
unsigned long long g_ask_cooldown = 0;

std::string Path(int slot) { return std::string(kBaseDir) + "/saves/vendors_" + std::to_string(slot) + ".json"; }
float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
float Rand01() {
    static std::mt19937 r(static_cast<unsigned>(GetTickCount64() * 3u));
    return std::uniform_real_distribution<float>(0.f, 1.f)(r);
}

void Save() {
    if (g_slot < 0) return;
    try {
        std::filesystem::create_directories(std::string(kBaseDir) + "/saves");
        json a = json::array();
        for (const auto& s : g_found) a.push_back(s);
        std::ofstream out(Path(g_slot));
        out << json{{"found", a}}.dump(1);
    } catch (...) {
    }
}

std::string StreetAt(const Vector3& p) {
    Hash s1 = 0, s2 = 0;
    PATHFIND::GET_STREET_NAME_AT_COORD(p.x, p.y, p.z, &s1, &s2);
    return s1 != 0 ? std::string(UI::GET_STREET_NAME_FROM_HASH_KEY(s1)) : std::string("arka sokak");
}

const char* Compass(const Vector3& from, const Vector3& to) {
    const float dx = to.x - from.x, dy = to.y - from.y;
    if (std::fabs(dx) > std::fabs(dy)) return dx > 0 ? "doğu" : "batı";
    return dy > 0 ? "kuzey" : "güney";
}
}  // namespace

bool Discovered(const std::string& id) { return g_found.count(id) > 0; }

void LoadSave(int slot) {
    if (slot == g_slot) return;
    if (g_slot >= 0) Save();
    g_slot = slot;
    g_found.clear();
    try {
        std::ifstream in(Path(slot));
        if (!in) return;
        for (const auto& s : json::parse(in).value("found", json::array())) g_found.insert(s.get<std::string>());
    } catch (...) {
    }
}

void Shutdown() {
    Save();
    g_slot = -1;
}

void Update(const std::vector<Shop>& shops, const Vector3& player_pos) {
    if (g_slot < 0) return;
    for (const auto& s : shops) {
        if (s.type != "street_vendor" || g_found.count(s.id)) continue;
        if (D2(s.pos, player_pos) < 18.f && std::fabs(s.pos.z - player_pos.z) < 12.f) {
            g_found.insert(s.id);
            Save();
            karma::Toast("Seyyar satıcıyı buldun: haritaya işlendi (keşfedildi)", 6);
            Log("seyyar satici kesfedildi: " + s.id);
        }
    }
}

std::vector<hml::Item> HintItems(const std::vector<Shop>& shops) {
    std::vector<hml::Item> out;
    static const char* const kUsers[] = {"kuzenim_55", "gece_kusu", "eski_dost", "mahalleli_cemo", "koseci_veli"};
    static const char* const kTemplates[] = {
        "Akşam çayına gelen olursa {s} köşesinde kapı hep aralık. 'Şişe' diyene fiyat kulağa söylenir. Sorma, görürsen anlarsın.",
        "{s} tarafında bir abi var, sepetinde herkesin bilmediği şeyler. Çok konuşan çıkmaz, dikkat çeken de. Selamsız gitme.",
        "Dedikodu değil ama {s} civarında 'özel mallar' el altından dönüyor diyorlar. Polis kokusu alırsa ortadan kaybolurmuş.",
    };
    int i = 0;
    for (const auto& s : shops) {
        if (s.type != "street_vendor") continue;
        const int k = i++;
        if (g_found.count(s.id)) continue;  // bulunan satıcı için ilan çıkmaz
        std::string text = kTemplates[k % 3];
        const size_t p = text.find("{s}");
        text.replace(p, 3, StreetAt(s.pos));
        hml::Item it;
        it.kind = hml::Kind::Post;
        it.text = text;
        it.meta = kUsers[k % 5];
        it.sub = std::string("@") + kUsers[k % 5] + "  ·  kodlu ilan";
        out.push_back(std::move(it));
    }
    return out;
}

json AskAround(const std::vector<Shop>& shops, const Vector3& player_pos) {
    json facts = {{"kind", "vendor_tip"}, {"known", false}};
    const unsigned long long now = GetTickCount64();
    if (now < g_ask_cooldown) {
        facts["reason"] = "asked_recently";
        return facts;
    }
    g_ask_cooldown = now + 20000ULL;
    const Shop* best = nullptr;
    float best_d = 1e9f;
    for (const auto& s : shops) {
        if (s.type != "street_vendor" || g_found.count(s.id)) continue;
        const float d = D2(s.pos, player_pos);
        if (d < best_d) {
            best_d = d;
            best = &s;
        }
    }
    if (!best) {
        facts["reason"] = "nothing_new";  // hepsini zaten biliyor
        return facts;
    }
    // Rastgele bir yayanın bilme şansı ününe göre: haydutlar/kötü şöhretlilere "bilen" daha çok çıkar, kahramana güvenmezler
    const std::string lvl = karma::Level();
    const float chance = lvl == "outlaw" ? 0.55f : lvl == "bad" ? 0.42f : lvl == "neutral" ? 0.28f : lvl == "good" ? 0.15f : 0.08f;
    if (Rand01() > chance) {
        facts["reason"] = "does_not_know";
        return facts;
    }
    facts["known"] = true;
    facts["street"] = StreetAt(best->pos);
    facts["direction"] = Compass(player_pos, best->pos);
    facts["distance_band"] = best_d < 400.f ? "yakın" : best_d < 1500.f ? "orta" : "uzak";
    // Konum öğrenildi: satıcı haritada "(keşfedildi)" ikonuyla görünür (blip ShopManager::EnsureBlips)
    g_found.insert(best->id);
    Save();
    facts["marked_on_map"] = true;
    karma::Toast("Seyyar satıcının konumu haritaya işlendi (keşfedildi)", 6);
    Log("seyyar satici ipucu verildi: " + best->id);
    return facts;
}

}  // namespace hikaye::vendors
