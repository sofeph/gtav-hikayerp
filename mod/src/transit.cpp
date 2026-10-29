#include "transit.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include "blips.h"
#include "controls.h"
#include "economy.h"
#include "json.hpp"

namespace hikaye::transit {

using nlohmann::json;

namespace {
constexpr int kDrivingModeNormal = 786603;
constexpr int kBrakeAndStop = 27;

struct Stop {
    std::string name;
    Vector3 pos{};
    Blip blip = 0;
};
struct Line {
    std::string id, name, type;  // type: "bus" | "tram"
    int fare = 3;
    int dwell_s = 12;        // durakta bekleme
    int headway_s = 180;     // metro: sefer aralığı
    int leg_s = 20;          // metro: iki durak arası (tarife)
    float fare_per_km = 0.f; // coach (bölgesel): kilometre başına ücret
    int fare_min = 10;       // coach: en düşük ücret
    int phase_s = 0;         // coach/metro: tarife kayması (ters yön hattı yarım sefer aralığı kaydırılır)
    std::vector<Stop> stops;
    // otobüs çalışma zamanı
    Vehicle bus = 0;
    Ped driver = 0;
    int target = 0;          // gidilen/bekleyilen durak
    bool dwelling = false;
    unsigned long long t = 0;
    bool rider = false;
};
std::vector<Line> g_lines;
bool g_blips_made = false;
unsigned long long g_toast_until = 0;
std::string g_toast;

// Metro yolculuğu (sembolik hızlı geçiş)
struct TramRide {
    bool active = false;
    int line = -1, stop = 0;
    enum class S { Black, AtStop } s = S::AtStop;
    unsigned long long t = 0;
} g_tram;

float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

void Toast(const std::string& t, unsigned ms = 3500) {
    g_toast = t;
    g_toast_until = GetTickCount64() + ms;
}

bool LoadModel(Hash m) {
    if (!STREAMING::IS_MODEL_IN_CDIMAGE(m) || !STREAMING::IS_MODEL_VALID(m)) return false;
    STREAMING::REQUEST_MODEL(m);
    for (int i = 0; i < 150 && !STREAMING::HAS_MODEL_LOADED(m); ++i) WAIT(0);
    return STREAMING::HAS_MODEL_LOADED(m) != 0;
}

// --- Otobüs ---
void DeleteBus(Line& l) {
    if (l.driver != 0 && ENTITY::DOES_ENTITY_EXIST(l.driver)) {
        Ped d = l.driver;
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(d, true, true);
        PED::DELETE_PED(&d);
    }
    if (l.bus != 0 && ENTITY::DOES_ENTITY_EXIST(l.bus)) {
        Vehicle b = l.bus;
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(b, true, true);
        VEHICLE::DELETE_VEHICLE(&b);
    }
    l.bus = 0;
    l.driver = 0;
    l.rider = false;
}

void DriveTo(Line& l, int idx) {
    l.target = idx;
    l.dwelling = false;
    l.t = GetTickCount64();
    Vector3 road = l.stops[idx].pos;
    Vector3 n{};
    if (PATHFIND::GET_CLOSEST_VEHICLE_NODE(road.x, road.y, road.z, &n, 1, 3.0f, 0)) road = n;
    AI::TASK_VEHICLE_DRIVE_TO_COORD(l.driver, l.bus, road.x, road.y, road.z, 13.0f, 0, ENTITY::GET_ENTITY_MODEL(l.bus), kDrivingModeNormal, 6.0f, 1.0f);
}

bool SpawnBus(Line& l) {
    Hash bm = HashKey("bus");
    if (!LoadModel(bm)) return false;
    Hash dm = HashKey("s_m_m_gentransport");
    if (!LoadModel(dm)) {
        dm = HashKey("a_m_m_business_01");
        if (!LoadModel(dm)) return false;
    }
    Vector3 p = l.stops[0].pos, n{};
    if (PATHFIND::GET_CLOSEST_VEHICLE_NODE(p.x, p.y, p.z, &n, 1, 3.0f, 0)) p = n;
    STREAMING::REQUEST_COLLISION_AT_COORD(p.x, p.y, p.z);
    l.bus = VEHICLE::CREATE_VEHICLE(bm, p.x, p.y, p.z, 0.f, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(bm);
    if (!ENTITY::DOES_ENTITY_EXIST(l.bus)) {
        l.bus = 0;
        return false;
    }
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(l.bus, true, true);
    VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(l.bus);
    VEHICLE::SET_VEHICLE_DOORS_LOCKED(l.bus, 1);
    l.driver = PED::CREATE_PED_INSIDE_VEHICLE(l.bus, 4, dm, -1, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(dm);
    if (!ENTITY::DOES_ENTITY_EXIST(l.driver)) {
        DeleteBus(l);
        return false;
    }
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(l.driver, true);
    PED::SET_PED_KEEP_TASK(l.driver, true);
    DriveTo(l, l.stops.size() > 1 ? 1 : 0);
    Log("otobus doğdu: hat " + l.id);
    return true;
}

void UpdateBus(Line& l, const Vector3& pos, bool e_pressed) {
    const unsigned long long now = GetTickCount64();
    float nearest = 1e9f;
    for (const auto& s : l.stops) nearest = std::min(nearest, D2(pos, s.pos));
    if (l.bus == 0) {
        if (nearest < 450.f && !l.stops.empty()) SpawnBus(l);
        return;
    }
    if (!ENTITY::DOES_ENTITY_EXIST(l.bus) || !ENTITY::DOES_ENTITY_EXIST(l.driver) || ENTITY::IS_ENTITY_DEAD(l.driver)) {
        DeleteBus(l);
        return;
    }
    if (nearest > 1300.f && !l.rider) {  // oyuncu çok uzak: otobüs kaldırılır (akış yükü)
        DeleteBus(l);
        return;
    }
    const Vector3 bp = ENTITY::GET_ENTITY_COORDS(l.bus, true);
    const Vector3 stop = l.stops[l.target].pos;
    if (!l.dwelling) {
        if (D2(bp, stop) < 20.f || now - l.t > 180000ULL) {  // durağa vardı (ya da çok uzun sürdü)
            l.dwelling = true;
            l.t = now;
            AI::TASK_VEHICLE_TEMP_ACTION(l.driver, l.bus, kBrakeAndStop, l.dwell_s * 1000 + 2000);
            if (l.rider) Toast("Durak: " + l.stops[l.target].name + " (E: in)", 6000);
        }
    } else if (now - l.t > static_cast<unsigned long long>(l.dwell_s) * 1000ULL) {
        DriveTo(l, (l.target + 1) % static_cast<int>(l.stops.size()));
    }

    const Ped player = PLAYER::PLAYER_PED_ID();
    const bool in_bus = PED::IS_PED_IN_VEHICLE(player, l.bus, false) != 0;
    if (l.rider && !in_bus) l.rider = false;  // indi ya da düştü
    if (in_bus) {
        l.rider = true;
        if (l.dwelling) {
            ShowHelp("~INPUT_PICKUP~ Otobusten in");
            if (e_pressed) AI::TASK_LEAVE_VEHICLE(player, l.bus, 0);
        } else {
            DrawLine("Hat " + l.id + ": sonraki durak " + l.stops[l.target].name, 0.90f, 230, 230, 230);
        }
        return;
    }
    // Durakta bekleyen oyuncu: biniş
    if (!PED::IS_PED_IN_ANY_VEHICLE(player, false) && l.dwelling && D2(pos, bp) < 14.f) {
        ShowHelp(("~INPUT_PICKUP~ Hat " + l.id + "'ye bin ($" + std::to_string(l.fare) + ")").c_str());
        if (e_pressed) {
            if (!economy::Spend(l.fare)) {
                Toast("Yetersiz bakiye");
                return;
            }
            for (int seat = 1; seat < 8; ++seat) {
                if (VEHICLE::IS_VEHICLE_SEAT_FREE(l.bus, seat)) {
                    PED::SET_PED_INTO_VEHICLE(player, l.bus, seat);
                    l.rider = true;
                    Toast("Hat " + l.id + ": sonraki durak " + l.stops[(l.target + 1) % l.stops.size()].name, 4000);
                    break;
                }
            }
        }
    }
}

// --- Metro / bölgesel otobüs ---
bool IsCoach(const Line& l) { return l.type == "coach"; }
// Bölgesel otobüs: i. duraktan bir sonrakine mesafeye göre ücret (metro ve şehir otobüsü: sabit ücret)
int LegFare(const Line& l, int i) {
    if (!IsCoach(l)) return l.fare;
    const int n = static_cast<int>(l.stops.size());
    const float d = D2(l.stops[static_cast<size_t>(i)].pos, l.stops[static_cast<size_t>((i + 1) % n)].pos);
    return std::max(l.fare_min, static_cast<int>(d / 1000.f * l.fare_per_km));
}
// Yolculuk süresi (saniye, ekran kararık kalma): bölgesel hat uzun, metro kısa
int LegBlackSeconds(const Line& l, int i) {
    if (!IsCoach(l)) return std::max(4, l.leg_s / 4);
    const int n = static_cast<int>(l.stops.size());
    const float d = D2(l.stops[static_cast<size_t>(i)].pos, l.stops[static_cast<size_t>((i + 1) % n)].pos);
    return std::max(6, std::min(16, static_cast<int>(d / 600.f)));
}

// Tarife: durak i, her headway_s saniyede bir, (i * leg_s) kaydırmayla gelir ve dwell_s boyunca durakta kalır.
bool TramAtStop(const Line& l, int i, int& wait_s) {
    const long long now_s = static_cast<long long>(GetTickCount64() / 1000ULL);
    const long long phase = (now_s - static_cast<long long>(i) * l.leg_s - l.phase_s) % l.headway_s;
    const long long p = phase < 0 ? phase + l.headway_s : phase;
    if (p < l.dwell_s) {
        wait_s = 0;
        return true;
    }
    wait_s = static_cast<int>(l.headway_s - p);
    return false;
}

// --- Durak bilgisi (geçen hatlar + QR) ve LST / Dashound web sayfaları ---
constexpr float kStopRadius = 18.f;
std::string g_near_url, g_qr_pending;
bool g_qr_prev = false;

std::string Slug(const std::string& s) {
    std::string o;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) o += static_cast<char>(std::tolower(u));
        else if (!o.empty() && o.back() != '-') o += '-';
    }
    while (!o.empty() && o.back() == '-') o.pop_back();
    return o;
}

std::string HmlEsc(const std::string& s) {
    std::string o;
    for (char c : s) o += c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '&' ? "&amp;" : c == '"' ? "&quot;" : std::string(1, c);
    return o;
}

struct Hit {
    const Line* line;
    int stop;
};
// Bu konumdaki (durak yarıçapı) durağı olan tüm hatlar
std::vector<Hit> LinesAt(const Vector3& pos) {
    std::vector<Hit> out;
    for (const auto& l : g_lines) {
        for (size_t i = 0; i < l.stops.size(); ++i) {
            if (D2(pos, l.stops[i].pos) <= kStopRadius) {
                out.push_back({&l, static_cast<int>(i)});
                break;
            }
        }
    }
    return out;
}

// Durağın site bölümü: şehir otobüsü / LST metro / Dashound bölgesel
std::string SectionOf(const std::vector<Hit>& hits) {
    bool metro = false;
    for (const auto& h : hits) {
        if (h.line->type == "bus") return "lst.com/citybus";
        if (!IsCoach(*h.line)) metro = true;
    }
    return metro ? "lst.com/metro" : "dashound.com/otogar";
}

bool LineInSection(const Line& l, const std::string& kind) {
    if (kind == "citybus") return l.type == "bus";
    if (kind == "metro") return l.type != "bus" && !IsCoach(l);
    return IsCoach(l);  // otogar
}

struct Spot {
    std::string name;
    Vector3 pos{};
};
// Benzersiz duraklar (6 m içindeki ortak duraklar tek sayılır); kind boşsa hepsi
std::vector<Spot> Spots(const std::string& kind) {
    std::vector<Spot> out;
    for (const auto& l : g_lines) {
        if (!kind.empty() && !LineInSection(l, kind)) continue;
        for (const auto& s : l.stops) {
            bool dup = false;
            for (const auto& q : out) dup = dup || D2(q.pos, s.pos) < 6.f;
            if (!dup) out.push_back({s.name, s.pos});
        }
    }
    return out;
}

bool NearSpot(const Vector3& pos, Spot& out) {
    float best = kStopRadius;
    bool found = false;
    for (const auto& sp : Spots("")) {
        const float d = D2(pos, sp.pos);
        if (d <= best) {
            best = d;
            out = sp;
            found = true;
        }
    }
    return found;
}

// Hattın i. durağına tahmini varış metni
std::string Eta(const Line& l, int i) {
    if (l.type == "bus") {
        if (l.bus == 0 || !ENTITY::DOES_ENTITY_EXIST(l.bus)) return "sefer yok";
        if (l.target == i) {
            if (l.dwelling) return "durakta";
            return "~" + std::to_string(static_cast<int>(D2(ENTITY::GET_ENTITY_COORDS(l.bus, true), l.stops[static_cast<size_t>(i)].pos) * 1.25f / 8.f)) + " sn";
        }
        return "yolda";
    }
    int wait = 0;
    if (TramAtStop(l, i, wait)) return "durakta";
    return "~" + std::to_string(wait) + " sn";
}

void ShowStopInfo(const Vector3& pos, bool on_foot) {
    g_near_url.clear();
    if (!on_foot) return;
    Spot sp;
    if (!NearSpot(pos, sp)) return;
    const auto hits = LinesAt(sp.pos);
    if (hits.empty()) return;
    g_near_url = SectionOf(hits) + "/" + Slug(sp.name);
    float y = 0.56f;
    DrawLine("Durak: " + sp.name, y, 150, 235, 170);
    y += 0.036f;
    DrawLine("Bu duraktan gecen hatlar:", y, 230, 230, 230);
    y += 0.034f;
    int shown = 0;
    for (const auto& h : hits) {
        if (shown >= 6) {
            DrawLine("+" + std::to_string(hits.size() - 6) + " hat daha", y, 200, 200, 200);
            y += 0.032f;
            break;
        }
        DrawLine("Hat " + h.line->id + "  " + h.line->name + "  -  " + Eta(*h.line, h.stop), y, 230, 230, 230);
        y += 0.032f;
        ++shown;
    }
    DrawLine("Telefonla QR okut [" + controls::KeyName(controls::QrKey()) + "]: " + g_near_url, y + 0.008f, 120, 200, 255);
}

std::string LineCard(const Line& l, int stop) {
    const std::string fare = "$" + std::to_string(stop >= 0 ? LegFare(l, stop) : l.fare);
    std::string s = "<card title=\"Hat " + HmlEsc(l.id) + "  " + HmlEsc(l.name) + "\">";
    if (stop >= 0) s += "Sonraki sefer: " + Eta(l, stop) + "  |  ";
    s += "Ücret: " + fare + "</card>\n";
    return s;
}

void TramLeg(const Vector3& pos) {
    (void)pos;
    const unsigned long long now = GetTickCount64();
    Line& l = g_lines[static_cast<size_t>(g_tram.line)];
    CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
    if (g_tram.s == TramRide::S::Black) {
        const int next = (g_tram.stop + 1) % static_cast<int>(l.stops.size());
        const float leg_d = D2(l.stops[static_cast<size_t>(g_tram.stop)].pos, l.stops[static_cast<size_t>(next)].pos);
        if (IsCoach(l)) {
            const int mins = std::max(1, static_cast<int>(leg_d / 22.f / 60.f));
            DrawLine("Bölgesel Otobüs " + l.id + "  -  " + l.stops[next].name + " otogarına gidiliyor (~" + std::to_string(mins) + " dk)", 0.46f, 230, 230, 230);
        } else {
            DrawLine("LST Metro  -  " + l.stops[next].name + " durağına gidiliyor...", 0.46f, 230, 230, 230);
        }
        if (now - g_tram.t > static_cast<unsigned long long>(LegBlackSeconds(l, g_tram.stop)) * 1000ULL) {
            if (IsCoach(l)) {  // yol süresi kadar oyun saati ilerler (ortalama 22 m/s)
                const int mins = std::max(1, static_cast<int>(leg_d / 22.f / 60.f));
                TIME::ADD_TO_CLOCK_TIME(mins / 60, mins % 60, 0);
            }
            const Ped player = PLAYER::PLAYER_PED_ID();
            const Vector3& p = l.stops[next].pos;
            STREAMING::REQUEST_COLLISION_AT_COORD(p.x, p.y, p.z);
            ENTITY::SET_ENTITY_COORDS(player, p.x, p.y, p.z, false, false, false, true);
            for (int i = 0; i < 90 && !ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(player); ++i) {
                STREAMING::REQUEST_COLLISION_AT_COORD(p.x, p.y, p.z);
                WAIT(0);
            }
            {   // verilen yükseklik tahmin olabilir (otogar): zemini bul, yerin altında kalma
                float gz = 0.f;
                if (GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(p.x, p.y, p.z + 150.f, &gz, false)) {
                    ENTITY::SET_ENTITY_COORDS(player, p.x, p.y, gz + 1.0f, false, false, false, true);
                }
            }
            ENTITY::FREEZE_ENTITY_POSITION(player, false);
            CAM::DO_SCREEN_FADE_IN(500);
            g_tram.stop = next;
            g_tram.s = TramRide::S::AtStop;
            g_tram.t = now;
            Toast((IsCoach(l) ? "Otogar: " : "Istasyon: ") + l.stops[next].name + " (E: devam et, uzaklaşırsan inersin)", 6000);
        }
    }
}
}  // namespace

bool ConsumeQr(std::string& url) {
    if (g_qr_pending.empty()) return false;
    url = g_qr_pending;
    g_qr_pending.clear();
    return true;
}

std::string BuildPage(const std::string& url) {
    if (g_lines.empty()) return "";
    std::vector<std::string> parts;
    size_t p = 0;
    while (p <= url.size()) {
        const size_t q = url.find('/', p);
        parts.push_back(url.substr(p, q == std::string::npos ? std::string::npos : q - p));
        if (q == std::string::npos) break;
        p = q + 1;
    }
    if (parts.size() < 2) return "";
    const std::string& site = parts[0];
    const std::string& kind = parts[1];
    const std::string slug = parts.size() > 2 && !parts[2].empty() ? parts[2] : "index";
    const bool lst = site == "lst.com" && (kind == "citybus" || kind == "metro");
    const bool dash = site == "dashound.com" && kind == "otogar";
    if (!lst && !dash) return "";
    const std::string brand = lst ? (kind == "citybus" ? "LST Şehir Otobüsü" : "LST Metro") : "Dashound Bölgesel Otobüs";
    const std::string home = site;
    std::string h = "<page title=\"" + brand + "\" style=\"site.hss\" keywords=\"lst dashound durak hat otobus metro otogar sefer\">\n";

    if (slug == "index") {
        h += "<h1>" + brand + "</h1>\n<h2>Hatlar</h2>\n";
        for (const auto& l : g_lines) {
            if (!LineInSection(l, kind)) continue;
            h += LineCard(l, -1);
            std::string names;
            for (size_t i = 0; i < l.stops.size(); ++i) names += (i ? ", " : "") + l.stops[i].name;
            h += "<p>Duraklar: " + HmlEsc(names) + "</p>\n";
        }
        h += "<h2>Duraklar</h2>\n";
        for (const auto& sp : Spots(kind)) h += "<link href=\"" + site + "/" + kind + "/" + Slug(sp.name) + "\">" + HmlEsc(sp.name) + "</link>\n";
    } else {
        Spot found;
        bool ok = false;
        for (const auto& sp : Spots(kind)) {
            if (Slug(sp.name) == slug) {
                found = sp;
                ok = true;
                break;
            }
        }
        if (!ok) return "";
        h += "<h1>Durak: " + HmlEsc(found.name) + "</h1>\n<p>Bu duraktan geçen hatlar ve tahmini sefer süreleri:</p>\n";
        for (const auto& hit : LinesAt(found.pos)) h += LineCard(*hit.line, hit.stop);
    }
    h += "<hr/>\n<link href=\"" + site + "/" + kind + "\">Tüm hatlar ve duraklar</link>\n<link href=\"" + home + "\">" + (lst ? "LST ana sayfa" : "Dashound ana sayfa") + "</link>\n<link href=\"home\">Ana sayfa</link>\n</page>\n";
    return h;
}


bool OnTransit() {
    if (g_tram.active) return true;
    for (const auto& l : g_lines) {
        if (l.rider) return true;
    }
    return false;
}

std::vector<MapLine> MapLines() {
    std::vector<MapLine> out;
    for (const auto& l : g_lines) {
        MapLine m;
        m.id = l.id;
        m.type = l.type;
        for (const auto& s : l.stops) m.pts.push_back(s.pos);
        out.push_back(std::move(m));
    }
    return out;
}

void Load() {
    g_lines.clear();
    try {
        std::ifstream in(std::string(kBaseDir) + "/data/transit.json");
        if (!in) {
            Log("transit.json yok (tools/gen_transit.py ile uretilir)");
            return;
        }
        const json j = json::parse(in);
        for (const auto& lj : j.value("lines", json::array())) {
            Line l;
            l.id = lj.value("id", "");
            l.name = lj.value("name", l.id);
            l.type = lj.value("type", "bus");
            l.fare = lj.value("fare", 3);
            l.dwell_s = lj.value("dwell_s", 12);
            l.headway_s = std::max(30, lj.value("headway_s", 180));
            l.leg_s = std::max(5, lj.value("leg_s", 20));
            l.fare_per_km = lj.value("fare_per_km", 0.f);
            l.fare_min = lj.value("fare_min", 10);
            l.phase_s = lj.value("phase_s", 0);
            for (const auto& sj : lj.value("stops", json::array())) {
                Stop s;
                s.name = sj.value("name", "");
                const auto& p = sj["pos"];
                s.pos = {p[0].get<float>(), 0, p[1].get<float>(), 0, p[2].get<float>(), 0};
                l.stops.push_back(std::move(s));
            }
            if (l.stops.size() >= 2) g_lines.push_back(std::move(l));
        }
    } catch (const std::exception& e) {
        Log(std::string("transit.Load HATA: ") + e.what());
    }
    Log("toplu tasima: " + std::to_string(g_lines.size()) + " hat");
}

json PlanRoute(const Vector3& from, const Vector3& dest, const std::string& dest_name) {
    json out = {{"kind", "transit_route"}, {"destination", dest_name}, {"found", false}};
    if (g_lines.empty()) {
        out["reason"] = "no_service";
        return out;
    }
    const float walk_direct_s = D2(from, dest) / 1.4f * 1.2f;
    double best = 1e18;
    const Line* bl = nullptr;
    int bi = -1, bj = -1, bhops = 0;
    for (const auto& l : g_lines) {
        const int n = static_cast<int>(l.stops.size());
        const bool bus = l.type == "bus";
        for (int i = 0; i < n; ++i) {
            const float w1 = D2(from, l.stops[i].pos);
            if (w1 > 1500.f) continue;
            float ride = 0.f;
            for (int hops = 1; hops < n; ++hops) {
                const int a = (i + hops - 1) % n, b = (i + hops) % n;
                ride += D2(l.stops[a].pos, l.stops[b].pos) * 1.25f;
                const float w2 = D2(l.stops[b].pos, dest);
                const bool coach = l.type == "coach";
                const double est = w1 / 1.4 + (bus ? 90.0 : (coach ? 150.0 : 60.0)) + ride / (bus ? 8.0 : (coach ? 22.0 : 12.0)) + hops * l.dwell_s + w2 / 1.4;
                if (est < best) {
                    best = est;
                    bl = &l;
                    bi = i;
                    bj = b;
                    bhops = hops;
                }
            }
        }
    }
    if (!bl) {
        out["reason"] = "no_stop_nearby";
        return out;
    }
    if (walk_direct_s <= best * 1.1) {  // yürümek daha kısa
        out["reason"] = "walk";
        out["walk_minutes"] = std::max(1, static_cast<int>(walk_direct_s / 60.f));
        return out;
    }
    const Stop& board = bl->stops[bi];
    const Stop& alight = bl->stops[bj];
    UI::SET_NEW_WAYPOINT(board.pos.x, board.pos.y);  // önce biniş durağı
    out["found"] = true;
    out["line"] = bl->id;
    out["line_type"] = bl->type == "bus" ? "otobus" : (bl->type == "coach" ? "bolgesel_otobus" : "metro");
    out["board_stop"] = board.name;
    out["board_walk_m"] = static_cast<int>(D2(from, board.pos));
    out["alight_stop"] = alight.name;
    out["stops_to_ride"] = bhops;
    out["final_walk_m"] = static_cast<int>(D2(alight.pos, dest));
    out["est_minutes"] = std::max(1, static_cast<int>(best / 60.0));
    out["fare"] = bl->type == "coach" ? std::max(bl->fare_min, static_cast<int>(D2(board.pos, alight.pos) / 1000.f * bl->fare_per_km)) : bl->fare;
    out["waypoint_set"] = true;
    Log("toplu tasima rotasi: hat " + bl->id + " " + board.name + " -> " + alight.name);
    return out;
}

bool OwnsBlip(Blip b) {
    if (b == 0) return false;
    for (const auto& l : g_lines) {
        for (const auto& s : l.stops) {
            if (s.blip == b) return true;
        }
    }
    return false;
}

void Reset() {
    for (auto& l : g_lines) {
        DeleteBus(l);
        for (auto& s : l.stops) blips::Remove(s.blip);
    }
    g_blips_made = false;
    if (g_tram.active) {
        PLAYER::SET_PLAYER_CONTROL(PLAYER::PLAYER_ID(), true, 0);
        ENTITY::FREEZE_ENTITY_POSITION(PLAYER::PLAYER_PED_ID(), false);
        CAM::DO_SCREEN_FADE_IN(300);
    }
    g_tram = TramRide{};
}

void Update(const Vector3& pos, bool e_pressed, bool input_blocked) {
    if (g_lines.empty()) return;
    const unsigned long long now = GetTickCount64();
    if (!g_blips_made) {  // durak ikonları (harita kapağında her zaman, mini haritada yakınken)
        std::vector<Vector3> placed;  // aynı noktadaki (hatlar arası ortak) duraklara tek ikon
        for (auto& l : g_lines) {
            const bool bus = l.type == "bus";
            for (auto& s : l.stops) {
                bool dup = false;
                for (const auto& q : placed) dup = dup || D2(q, s.pos) < 6.f;
                if (dup) continue;
                placed.push_back(s.pos);
                if (IsCoach(l)) s.blip = blips::Add(s.pos, 1, 47, "Otogar " + s.name, 0.8f, true);
                else s.blip = blips::Add(s.pos, 1, bus ? 3 : 29, std::string(bus ? "Otobus Duragi " : "Metro Istasyonu ") + s.name, 0.55f, true);
            }
        }
        g_blips_made = true;
    }
    if (now < g_toast_until) DrawLine(g_toast, 0.80f, 150, 235, 170);

    if (g_tram.active) {  // metro yolculuğu sürüyor
        Line& l = g_lines[static_cast<size_t>(g_tram.line)];
        if (g_tram.s == TramRide::S::Black) {
            TramLeg(pos);
            return;
        }
        // Durakta: E devam, uzaklaşırsan yolculuk biter
        if (D2(pos, l.stops[static_cast<size_t>(g_tram.stop)].pos) > 18.f) {
            g_tram = TramRide{};
            return;
        }
        const int cont_fare = IsCoach(l) ? LegFare(l, g_tram.stop) : 0;
        ShowHelp((std::string("~INPUT_PICKUP~ ") + (IsCoach(l) ? "Yolculuga devam et" : "Metro ile devam et") + " (Hat " + l.id + ")" +
                  (cont_fare > 0 ? " $" + std::to_string(cont_fare) : std::string())).c_str());
        if (e_pressed) {
            if (cont_fare > 0 && !economy::Spend(cont_fare)) {
                Toast("Yetersiz bakiye");
                return;
            }
            CAM::DO_SCREEN_FADE_OUT(500);
            ENTITY::FREEZE_ENTITY_POSITION(PLAYER::PLAYER_PED_ID(), true);
            g_tram.s = TramRide::S::Black;
            g_tram.t = now;
        }
        return;
    }

    const Ped player = PLAYER::PLAYER_PED_ID();
    const bool on_foot = !PED::IS_PED_IN_ANY_VEHICLE(player, false);
    ShowStopInfo(pos, on_foot);
    {
        const bool down = !input_blocked && (GetAsyncKeyState(controls::QrKey()) & 0x8000) != 0;
        if (down && !g_qr_prev && !g_near_url.empty()) g_qr_pending = g_near_url;
        g_qr_prev = down;
    }
    for (auto& l : g_lines) {
        if (l.type == "bus") {
            UpdateBus(l, pos, e_pressed);
            continue;
        }
        // Metro durağı: tarife bilgisi ve biniş
        if (!on_foot) continue;
        for (size_t i = 0; i < l.stops.size(); ++i) {
            if (D2(pos, l.stops[i].pos) > 10.f) continue;
            int wait = 0;
            if (TramAtStop(l, static_cast<int>(i), wait)) {
                const int board_fare = LegFare(l, static_cast<int>(i));
                ShowHelp((std::string("~INPUT_PICKUP~ ") + (IsCoach(l) ? "Bolgesel otobuse bin (" + l.stops[(i + 1) % l.stops.size()].name + ")" : "Metro " + l.id + "'e bin") +
                          " ($" + std::to_string(board_fare) + ")").c_str());
                if (e_pressed) {
                    if (!economy::Spend(board_fare)) {
                        Toast("Yetersiz bakiye");
                    } else {
                        g_tram = TramRide{true, static_cast<int>(&l - &g_lines[0]), static_cast<int>(i), TramRide::S::Black, now};
                        CAM::DO_SCREEN_FADE_OUT(500);
                        ENTITY::FREEZE_ENTITY_POSITION(player, true);
                        Toast((IsCoach(l) ? "Bölgesel otobüs " : "Metro ") + l.id + " kalkıyor");
                    }
                }
            } else {
                DrawLine((IsCoach(l) ? "Bölgesel Otobüs " : "LST Metro ") + l.id + " — " + l.stops[i].name + ": sonraki sefer " + std::to_string(wait) + " sn", 0.84f, 230, 230, 230);
            }
            break;
        }
    }
}

}  // namespace hikaye::transit
