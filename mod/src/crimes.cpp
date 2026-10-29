#include "crimes.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <map>
#include <set>

#include "json.hpp"

namespace hikaye::crimes {

using nlohmann::json;

namespace {
struct Event {
    std::string kind, street;
    int hour = 0, minute = 0, witnesses = 0;
};
std::vector<Event> g_events;  // eskiden yeniye
int g_slot = -1;
std::map<std::string, unsigned long long> g_last;  // tür başına son kayıt (aynı olay yağmuru olmasın)
unsigned long long g_scan_at = 0;
int g_prev_wanted = 0;
std::function<bool(Ped)> g_witness_excluded;
std::function<bool(Ped, Vehicle)> g_service_exempt;
std::function<bool(const Vector3&, float)> g_service_near;
Vehicle g_prev_veh = 0;
std::set<Vehicle> g_known_veh;  // oyuncunun daha önce bindiği/zaten kaydedilmiş araçlar: tekrar binmek yeni suç değil
std::function<bool(Vehicle)> g_is_owned;
std::function<bool(Ped, int)> g_police_caller;

std::string SavePath(int slot) { return std::string(kBaseDir) + "/saves/crimes_" + std::to_string(slot) + ".json"; }

std::string StreetAt(const Vector3& p) {
    Hash s1 = 0, s2 = 0;
    PATHFIND::GET_STREET_NAME_AT_COORD(p.x, p.y, p.z, &s1, &s2);
    return s1 != 0 ? std::string(UI::GET_STREET_NAME_FROM_HASH_KEY(s1)) : std::string("şehir merkezi");
}

int CountWitnesses(const Vector3& pos) {
    const Ped player = PLAYER::PLAYER_PED_ID();
    int arr[2 + 2 * 24];
    arr[0] = 24;
    const int n = PED::GET_PED_NEARBY_PEDS(player, arr, -1);
    int w = 0;
    for (int i = 0; i < n && i < 24; ++i) {
        const Ped p = arr[2 + i * 2];
        if (p == 0 || p == player || !ENTITY::DOES_ENTITY_EXIST(p) || !PED::IS_PED_HUMAN(p) || ENTITY::IS_ENTITY_DEAD(p)) continue;
        const Vector3 q = ENTITY::GET_ENTITY_COORDS(p, true);
        if (std::abs(q.x - pos.x) < 25.f && std::abs(q.y - pos.y) < 25.f) ++w;
    }
    return w;
}

// Suçu gören en yakın sivil (polis değil, canlı, görüş hattı olan)
Ped PickWitness(const Vector3& pos) {
    const Ped player = PLAYER::PLAYER_PED_ID();
    int arr[2 + 2 * 24];
    arr[0] = 24;
    const int n = PED::GET_PED_NEARBY_PEDS(player, arr, -1);
    Ped best = 0;
    float best_d = 1e9f;
    for (int i = 0; i < n && i < 24; ++i) {
        const Ped p = arr[2 + i * 2];
        if (p == 0 || p == player || !ENTITY::DOES_ENTITY_EXIST(p) || !PED::IS_PED_HUMAN(p) || ENTITY::IS_ENTITY_DEAD(p)) continue;
        const int t = PED::GET_PED_TYPE(p);
        if (t == 6 || t == 27 || t == 29 || PED::IS_PED_IN_ANY_VEHICLE(p, false)) continue;
        if (g_witness_excluded && g_witness_excluded(p)) continue;  // dükkân sahibi tanık sayılmaz
        if (g_service_exempt && g_service_exempt(p, 0)) continue;   // mekanik/vale ekibi ihbar etmez
        const Vector3 q = ENTITY::GET_ENTITY_COORDS(p, true);
        const float d = std::abs(q.x - pos.x) + std::abs(q.y - pos.y);
        if (d > 40.f || d >= best_d) continue;
        if (!ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY(p, player, 17)) continue;
        best = p;
        best_d = d;
    }
    return best;
}

// Suçun ağırlığı -> arama seviyesi (yıldız)
int StarsFor(const std::string& kind) {
    if (kind == "cop_killed") return 5;
    if (kind == "murder" || kind == "explosion") return 4;
    if (kind == "carjack" || kind == "shooting" || kind == "vehicular_kill") return 3;
    if (kind == "assault" || kind == "hit_and_run" || kind == "car_theft") return 2;
    return 0;  // weapon_*, warrant, wanted: kendi akışları var (weaponwatch / zaten aranıyor)
}

// Araç sahipliği: araç NPC'lerin sayılır (trafikteki/park halindeki sivil araçlar), şu durumlarda OYUNCUNUN sayılır:
//  - garaj kaydında (el, plaka "HK#####" ya da bayiden/ikinci elden alınmış),
//  - mission entity (vale, taksi, görev, bayi aracı: oyun kendi sahiplenmiştir),
//  - daha önce bu araca binilmiş ve suç zaten kaydedilmişse (aynı aracı tekrar kullanmak ikinci hırsızlık değildir).
bool PlayerOwns(Vehicle v) {
    if (g_is_owned && g_is_owned(v)) return true;
    if (ENTITY::IS_ENTITY_A_MISSION_ENTITY(v)) return true;
    if (g_known_veh.count(v)) return true;
    const char* plate = VEHICLE::GET_VEHICLE_NUMBER_PLATE_TEXT(v);
    if (plate && plate[0] == 'H' && plate[1] == 'K' && std::isdigit(static_cast<unsigned char>(plate[2]))) return true;  // modun verdiği plaka
    return false;
}

std::string Title(const std::string& kind) {
    if (kind == "murder") return "Cinayet: bir kişi hayatını kaybetti";
    if (kind == "assault") return "Yaralamalı saldırı";
    if (kind == "cop_killed") return "Polis memuruna saldırı: memur hayatını kaybetti";
    if (kind == "carjack") return "Araç gasbı";
    if (kind == "weapon_report") return "Silahlı şahıs ihbarı: vatandaş polisi aradı";
    if (kind == "weapon_seized") return "Yasadışı silah ele geçirildi";
    if (kind == "warrant") return "Yakalama kararı çıkarıldı: şüpheli aranıyor";
    if (kind == "hit_and_run") return "Çarpıp kaçma: yaya yaralandı";
    if (kind == "vehicular_kill") return "Ölümlü çarpma: sürücü olay yerinden ayrıldı";
    if (kind == "car_theft") return "Araç hırsızlığı: park halindeki araç çalındı";
    if (kind == "shooting") return "Silahlı çatışma, ateş sesleri";
    if (kind == "explosion") return "Patlama";
    if (kind == "wanted") return "Büyük çaplı arama: polis operasyonu";
    return "Asayiş olayı";
}

std::string Body(const Event& e) {
    char t[16];
    std::snprintf(t, sizeof(t), "%02d:%02d", e.hour, e.minute);
    std::string s = e.street + " civarında, saat " + t + ". ";
    if (e.kind == "murder" || e.kind == "cop_killed") s += e.witnesses > 0 ? "Birkaç görgü tanığı ifade verdi, faili arayan ekipler bölgede." : "Tanık bulunamadı, polis kamera kayıtlarını inceliyor.";
    else if (e.kind == "hit_and_run" || e.kind == "vehicular_kill") s += e.witnesses > 0 ? "Görgü tanıkları aracı tarif ediyor, polis plaka arıyor." : "Yaya yerde bulundu, sürücü kaçtı; kamera kayıtları inceleniyor.";
    else if (e.kind == "weapon_report") s += "Bir vatandaş, halka açık alanda silahla dolaşan birini bildirdi; devriye yönlendirildi.";
    else if (e.kind == "weapon_seized") s += "Arama sırasında yasadışı silah bulundu ve müsadere edildi.";
    else if (e.kind == "warrant") s += "Şüpheli aramadan kaçtı veya yasadışı silahla yakalandı; polis tutuklama için arıyor.";
    else if (e.kind == "car_theft") s += e.witnesses > 0 ? "Bir tanık şüpheliyi aracın başında gördü, plaka aranıyor." : "Araç sahibi aracını bulamadı, hırsızlık şikayeti yapıldı.";
    else if (e.kind == "carjack") s += e.witnesses > 0 ? "Sürücü araçtan atıldı, tanıklar şüpheliyi tarif ediyor." : "Araç sahibi şikayetçi oldu, şüpheli aranıyor.";
    else if (e.kind == "explosion") s += "Çevredeki vatandaşlar tahliye edildi, itfaiye ve polis olay yerinde.";
    else if (e.kind == "wanted") s += "Yoğun polis trafiği nedeniyle bölgedeki yollar kapatıldı.";
    else s += e.witnesses > 0 ? "Çevredekiler paniğe kapıldı, polis soruşturma başlattı." : "Olayı gören olmadı, soruşturma sürüyor.";
    return s;
}
}  // namespace

void SetServiceExemption(std::function<bool(Ped, Vehicle)> exempt, std::function<bool(const Vector3&, float)> near_service) {
    g_service_exempt = std::move(exempt);
    g_service_near = std::move(near_service);
}
bool IsExempt(Ped p, Vehicle v) { return g_service_exempt && g_service_exempt(p, v); }

void SetWitnessExclusion(std::function<bool(Ped)> excluded) { g_witness_excluded = std::move(excluded); }
void SetOwnedPredicate(std::function<bool(Vehicle)> is_owned) { g_is_owned = std::move(is_owned); }
void SetPoliceCaller(std::function<bool(Ped, int)> caller) { g_police_caller = std::move(caller); }

void Load(int slot) {
    if (slot == g_slot) return;
    if (g_slot >= 0) Save();
    g_slot = slot;
    g_events.clear();
    g_last.clear();
    try {
        std::ifstream in(SavePath(slot));
        if (!in) return;
        for (const auto& e : json::parse(in).value("events", json::array())) {
            Event ev;
            ev.kind = e.value("kind", "");
            ev.street = e.value("street", "");
            ev.hour = e.value("hour", 0);
            ev.minute = e.value("minute", 0);
            ev.witnesses = e.value("witnesses", 0);
            if (!ev.kind.empty()) g_events.push_back(std::move(ev));
        }
    } catch (...) {
    }
}

void Save() {
    if (g_slot < 0) return;
    try {
        std::filesystem::create_directories(std::string(kBaseDir) + "/saves");
        json arr = json::array();
        for (const auto& e : g_events) arr.push_back({{"kind", e.kind}, {"street", e.street}, {"hour", e.hour}, {"minute", e.minute}, {"witnesses", e.witnesses}});
        std::ofstream out(SavePath(g_slot));
        out << json{{"events", arr}}.dump(1);
    } catch (...) {
    }
}

void Shutdown() {
    Save();
    g_slot = -1;
}

void Record(const std::string& kind, const Vector3& pos, int witnesses) {
    if (g_slot < 0) return;
    const unsigned long long now = GetTickCount64();
    unsigned long long cooldown = 45000ULL;  // aynı türde tekrar haber olması için asgari süre
    if (kind == "murder" || kind == "cop_killed" || kind == "vehicular_kill" || kind == "hit_and_run") cooldown = 8000ULL;
    auto it = g_last.find(kind);
    if (it != g_last.end() && now - it->second < cooldown) return;
    g_last[kind] = now;
    Event e;
    e.kind = kind;
    e.street = StreetAt(pos);
    e.hour = TIME::GET_CLOCK_HOURS();
    e.minute = TIME::GET_CLOCK_MINUTES();
    e.witnesses = witnesses >= 0 ? witnesses : CountWitnesses(pos);
    g_events.push_back(std::move(e));
    if (g_events.size() > 30) g_events.erase(g_events.begin());
    Save();
    // Polis bildirimi: suçu gören biri varsa arar (tacizdeki gibi); tanık yoksa sadece haber olur.
    if (g_police_caller && PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) < 5) {
        const int stars = StarsFor(kind);
        if (stars > 0) {
            if (const Ped w = PickWitness(pos); w != 0) {
                if (g_police_caller(w, stars)) Log("suc: tanik polisi ariyor (" + kind + ", " + std::to_string(stars) + " yildiz)");
            }
        }
    }
    Log("suc haberi: " + kind + " @ " + g_events.back().street + " tanik=" + std::to_string(g_events.back().witnesses));
}

void Update() {
    if (g_slot < 0) return;
    const unsigned long long now = GetTickCount64();
    if (now - g_scan_at < 300) return;
    g_scan_at = now;
    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vector3 pos = ENTITY::GET_ENTITY_COORDS(player, true);
    if (PED::IS_PED_JACKING(player)) {
        const Vehicle target = PED::GET_VEHICLE_PED_IS_TRYING_TO_ENTER(player);
        const Ped victim = target != 0 ? VEHICLE::GET_PED_IN_VEHICLE_SEAT(target, -1) : 0;
        if (!IsExempt(victim, target)) Record("carjack", pos);  // çağırdığın tamirci/taksi aracı gasp sayılmaz
    }
    // Araç hırsızlığı: boş ve park halindeki bir sivil araca sürücü olarak binmek (kendi/vale/görev/taksi araçları mission entity -> suç değil)
    const Vehicle cur = PED::GET_VEHICLE_PED_IS_IN(player, false);
    if (cur != 0 && cur != g_prev_veh && VEHICLE::GET_PED_IN_VEHICLE_SEAT(cur, -1) == player && !PED::IS_PED_JACKING(player) &&
        VEHICLE::IS_THIS_MODEL_A_CAR(ENTITY::GET_ENTITY_MODEL(cur))) {
        if (!PlayerOwns(cur)) Record("car_theft", pos);  // NPC'nin aracı
        g_known_veh.insert(cur);                         // kendi aracın ya da zaten kaydedilen çalıntı: bir daha sayılmaz
        if (g_known_veh.size() > 80) g_known_veh.clear();
    }
    g_prev_veh = cur;
    if (PED::IS_PED_SHOOTING(player) && CountWitnesses(pos) >= 2 && !(g_service_near && g_service_near(pos, 40.f))) Record("shooting", pos);  // çağırdığın hizmetin yakınında ateş suç sayılmaz
    if (FIRE::IS_EXPLOSION_IN_SPHERE(-1, pos.x, pos.y, pos.z, 30.f)) Record("explosion", pos);
    // Oyunun kendi yıldız/suç sistemi dinlenmez: sadece bu dosyadaki kendi tespitlerimiz (cinayet, çarpma, silah, hırsızlık...) haber olur.
    g_prev_wanted = 0;
}

void MarkKnown(Vehicle v) {
    if (v != 0) g_known_veh.insert(v);
}

std::vector<hml::Item> NewsItems(int max_items) {
    std::vector<hml::Item> out;
    if (g_events.empty()) {
        hml::Item it;
        it.text = "Kayıtlı suç haberi yok. Şehir sakin.";
        out.push_back(it);
        return out;
    }
    for (auto e = g_events.rbegin(); e != g_events.rend() && static_cast<int>(out.size()) < max_items; ++e) {
        hml::Item it;
        it.kind = hml::Kind::Card;
        it.text = Title(e->kind);
        it.sub = Body(*e);
        it.meta = "Asayiş";
        out.push_back(std::move(it));
    }
    return out;
}

std::vector<std::string> Topics(int max_items) {
    std::vector<std::string> out;
    for (auto e = g_events.rbegin(); e != g_events.rend() && static_cast<int>(out.size()) < max_items; ++e) {
        out.push_back(Title(e->kind) + " (" + e->street + ")");
    }
    return out;
}

}  // namespace hikaye::crimes
