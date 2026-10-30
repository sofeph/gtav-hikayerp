#include "phoneos.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "economy.h"
#include "jail.h"
#include "karma.h"
#include "missions.h"
#include "places.h"
#include "services.h"
#include "shop_manager.h"
#include "transit.h"

namespace hikaye::phoneos {

using nlohmann::json;

namespace {
Deps g_deps;

struct Person {  // el ile ya da sokak NPC'sinden kaydedilen kişi
    std::string id, name, number;  // number: 10 hane
    json npc;                      // rastgele NPC ise kişilik verisi (aranınca onunla konuşulur), yoksa null
};
std::vector<Person> g_people;
std::map<std::string, std::string> g_names;  // rehberde kullanıcının verdiği özel adlar (kimlik -> ad); hikaye karakterleri ve kayıtlı kişiler
std::vector<std::string> g_shortcuts = {"bleeter.com", "weazelnews.com", "generalsale.com", "eyefind.com"};
std::vector<Vector3> g_default_homes;
int g_slot = -1;
int g_next_person = 1;

float g_battery = 100.f;
bool g_charging = false;
std::string g_source;
unsigned long long g_last_tick = 0, g_last_save = 0;
int g_last_warn = 101;

struct Nav {
    bool active = false;
    std::string name;
    Vector3 pos{};
} g_nav;
std::string g_pending_call;

// ---- Operatör & Şebeke ----
struct PkgInfo {
    const char* name;
    int price;    // $/2 oyun günü
    int pages;    // MB cinsinden veri kotası; -1 = sınırsız
    int minutes;  // arama dakikası; -1 = sınırsız
};
struct OpInfo {
    const char* id;    // URL kökü
    const char* name;
    float quality;     // şebeke güç çarpanı (1.0=en iyi)
    PkgInfo pkgs[4];
};
static const OpInfo kOpTable[] = {
    {"whiz.com", "Whiz", 1.00f, {
        {"Hızlı",     400,  5000,  50},
        {"Standart",  900, 15000, 150},
        {"Premium",  1800, 35000, 300},
        {"Kurumsal", 3000, 60000, 500},
    }},
    {"grahamtelecom.com", "Graham Telecom", 0.85f, {
        {"Başlangıç",  200,  3000,  30},
        {"Standart",   500, 10000, 100},
        {"Plus",      1000, 25000, 200},
        {"Sınırsız",  1500,    -1,  -1},
    }},
    {"modatel.com", "Modatel", 0.68f, {
        {"Cep",       100,  2000,  20},
        {"Temel",     280,  8000,  80},
        {"Artı",      600, 20000, 150},
        {"Sınırsız",  900,    -1,  -1},
    }},
};
static constexpr int kOpCount = 3;
int g_operator = 1;           // 0=Whiz 1=Graham(varsayılan) 2=Modatel
int g_pkg_tier = 1;           // aktif paketin operatördeki indeksi (0-3)
int g_signal_bars = 3;        // 0-4
unsigned long long g_signal_next = 0;
// ---- Paket ----
int g_pkg_pages_left = 10000; // MB; -1 = sınırsız
int g_pkg_minutes_left = 100; // -1 = sınırsız
int g_pkg_renew_day = -1;     // -1: henüz başlatılmadı; >=0: paket başladığı oyun günü (yıl*400+ay*32+gün)

// Operatör geçiş (taşıma) süreci
bool g_switch_pending = false;
int g_switch_op = -1;
int g_switch_tier = -1;
unsigned long long g_switch_call_time = 0ULL;
bool g_switch_ring_ready = false;

const PkgInfo& CurrentPkg() { return kOpTable[g_operator].pkgs[g_pkg_tier]; }

// Oyun günü sayısı (yenilenme takibi için takvim-bağımsız artan değer)
int GameDay() {
    return TIME::GET_CLOCK_YEAR() * 400 + TIME::GET_CLOCK_MONTH() * 32 + TIME::GET_CLOCK_DAY_OF_MONTH();
}

int CalcSignalBars(const Vector3& pos, int op) {
    const float dx = pos.x - 200.f, dy = pos.y + 700.f;
    const float dist = std::sqrt(dx * dx + dy * dy);
    float base;
    if      (dist < 2000.f)  base = 1.00f;
    else if (dist < 4000.f)  base = 0.85f;
    else if (dist < 6000.f)  base = 0.65f;
    else if (dist < 9000.f)  base = 0.40f;
    else if (dist < 12000.f) base = 0.22f;
    else                     base = 0.08f;
    // Dağ / yüksek irtifa cezası
    if (pos.z > 250.f) base *= std::max(0.05f, 1.f - (pos.z - 250.f) / 700.f * 0.85f);
    base *= kOpTable[op].quality;
    base = std::max(0.f, std::min(1.f, base));
    if      (base >= 0.80f) return 4;
    else if (base >= 0.55f) return 3;
    else if (base >= 0.30f) return 2;
    else if (base >= 0.12f) return 1;
    return 0;
}

// Veri miktarını MB → GB/MB/Sınırsız olarak göster (1000 MB = 1 GB)
std::string DataStr(int mb) {
    if (mb < 0) return "Sınırsız";
    if (mb >= 1000) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.1f GB", mb / 1000.0f);
        return buf;
    }
    return std::to_string(mb) + " MB";
}

// 4 çubuk Unicode gösterimi (▁▂▃▄ / ░)
std::string SignalStr(int bars) {
    static const char* kFull[] = {"▁", "▂", "▃", "▄"};
    std::string s;
    for (int i = 0; i < 4; ++i) s += (i < bars) ? kFull[i] : "░";
    return s;
}

std::string SignalSvg(int bars) {
    static const int heights[] = {8, 14, 20, 26};
    std::string svg = "<svg w=\"360\" h=\"36\">";
    for (int i = 0; i < 4; ++i) {
        const int x = 136 + i * 24;
        const int height = heights[i];
        const char* color = i < bars ? "#46d27b" : "#df5555";
        svg += "<rect x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(30 - height) +
               "\" w=\"16\" h=\"" + std::to_string(height) + "\" rx=\"3\" fill=\"" + color + "\"/>";
    }
    return svg + "</svg>\n";
}

// ---- Saat / Alarm / Takvim / Notlar (hepsi karaktere özel kayıtlı) ----
struct Alarm {
    int h = 7, m = 0;
    bool on = true;
    std::string label;
    long fired = -1;
};
struct Ev {
    int y = 2013, m = 1, d = 1;
    std::string text;
};
std::vector<Alarm> g_alarms;
std::vector<Ev> g_events;
std::vector<std::string> g_notes;

// Haviland sesi (XTTS-v2 konuşmacıları): Ayarlar > Haviland > Ses seç
struct Voice {
    const char* name;
    bool female;
};
const Voice kVoices[] = {{"Claribel Dervla", true}, {"Daisy Studious", true}, {"Gracie Wise", true},  {"Tammie Ema", true},
                         {"Alison Dietlinde", true}, {"Ana Florence", true},  {"Damien Black", false}, {"Baldur Sanjin", false},
                         {"Craig Gutsy", false},     {"Viktor Eka", false},   {"Ilkin Urbano", false}, {"Ludvig Milivoj", false}};
std::string g_hvoice = "Claribel Dervla";

// Dünya saati: katalog data/world_clock.json (202 yer: ülkeler + Liberty City/Vice City...). Los Santos = oyun saati (ls_offset dk, varsayılan UTC-8).
std::string Two2(int v) {
    char b[8];
    std::snprintf(b, sizeof(b), "%02d", v);
    return b;
}

struct WClock {
    std::string id, name;
    int off = 0;  // UTC'ye göre dakika
};
std::vector<WClock> g_wc_catalog;
std::vector<std::string> g_wc_default = {"turkiye_istanbul", "liberty_city"};
std::vector<std::string> g_wc_sel;  // kullanıcının eklediği yerler (kimlikler)
int g_ls_off = -480;

const WClock* FindWc(const std::string& id) {
    for (const auto& w : g_wc_catalog) {
        if (w.id == id) return &w;
    }
    return nullptr;
}
std::string UtcLabel(int off) {
    const int a = off < 0 ? -off : off;
    return std::string("UTC") + (off < 0 ? "-" : "+") + std::to_string(a / 60) + (a % 60 ? ":" + Two2(a % 60) : "");
}

const char* const kMonths[] = {"Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran", "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık"};
const char* const kDays[] = {"Pazar", "Pazartesi", "Salı", "Çarşamba", "Perşembe", "Cuma", "Cumartesi"};
int DaysIn(int y, int m) {  // m: 1..12
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : d[(m - 1) % 12];
}
std::string Two(int v) {
    char b[8];
    std::snprintf(b, sizeof(b), "%02d", v);
    return b;
}
bool ParseClock(const std::string& s, int& h, int& m, std::string& rest) {  // "07:30 Kalk", "7.30", "0730"
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    std::string tok;
    while (i < s.size() && s[i] != ' ') tok += s[i++];
    while (i < s.size() && s[i] == ' ') ++i;
    rest = s.substr(i);
    std::string a, b;
    const size_t sep = tok.find_first_of(":.");
    if (sep != std::string::npos) {
        a = tok.substr(0, sep);
        b = tok.substr(sep + 1);
    } else if (tok.size() == 4) {
        a = tok.substr(0, 2);
        b = tok.substr(2);
    } else if (!tok.empty() && tok.size() <= 2) {
        a = tok;
        b = "0";
    } else {
        return false;
    }
    for (char ch : a + b) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
    }
    if (a.empty() || b.empty()) return false;
    h = std::atoi(a.c_str());
    m = std::atoi(b.c_str());
    return h >= 0 && h < 24 && m >= 0 && m < 60;
}
bool g_admin_req = false;

constexpr char kAreaCode[] = "310";

// Sabit hizmet hatları (555-01xx: kurgu için ayrılmış aralık)
struct Svc {
    const char* id;
    const char* name;
    const char* number;  // 10 hane
};
// Haviland burada YOK: telefonun yapay zekâ asistanıdır (Haviland uygulaması), rehberde kişi ya da numara değildir.
const Svc kServices[] = {{"svc_taxi", "Taksi", "3105550101"},
                         {"svc_heli", "Helikopter Taksi", "3105550102"},      {"svc_police", "Polis", "3105550110"},
                         {"svc_ambulance", "Ambulans", "3105550112"},         {"svc_fire", "İtfaiye", "3105550114"},
                         {"svc_mechanic", "Mekanik / Tamirci", "3105550120"}, {"svc_parking", "Otopark Görevlisi", "3105550121"},
                         {"svc_musteri_whiz",   "Whiz Müşteri Hizmetleri",          "3105550130"},
                         {"svc_musteri_graham", "Graham Telecom Müşteri Hizmetleri","3105550131"},
                         {"svc_musteri_mod",    "Modatel Müşteri Hizmetleri",       "3105550132"}};
static constexpr int kSvcCount = 10;          // toplam hizmet hattı
static constexpr int kSvcSupportBase = 7;     // kServices[7+op] = operatör müşteri hizmetleri

float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

std::string Lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Esc(const std::string& s) {
    std::string o;
    for (char c : s) o += c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '&' ? "&amp;" : c == '"' ? "&quot;" : std::string(1, c);
    return o;
}

unsigned long long Fnv(const std::string& s) {
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string SavePath(int slot) { return std::string(kBaseDir) + "/saves/phone_" + std::to_string(slot) + ".json"; }

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string OwnNumber() { return NumberFor("player_" + std::to_string(g_slot)); }

std::string Money(int v) {
    const std::string d = std::to_string(v < 0 ? -v : v);
    std::string g;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) g += '.';
        g += d[i];
    }
    return (v < 0 ? "-$" : "$") + g;
}

std::string Num1(float v) {
    char b[24];
    std::snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

// ---- Kayıt ----
void Save() {
    if (g_slot < 0) return;
    try {
        std::filesystem::create_directories(std::string(kBaseDir) + "/saves");
        json people = json::array();
        for (const auto& p : g_people) people.push_back({{"id", p.id}, {"name", p.name}, {"number", p.number}, {"npc", p.npc}});
        json alarms = json::array(), events = json::array();
        for (const auto& a : g_alarms) alarms.push_back({{"h", a.h}, {"m", a.m}, {"on", a.on}, {"label", a.label}});
        for (const auto& e : g_events) events.push_back({{"y", e.y}, {"m", e.m}, {"d", e.d}, {"text", e.text}});
        std::ofstream out(SavePath(g_slot));
        out << json{{"battery", g_battery}, {"people", people}, {"names", g_names}, {"shortcuts", g_shortcuts}, {"next", g_next_person},
                    {"alarms", alarms}, {"events", events}, {"wclocks", g_wc_sel}, {"notes", g_notes}, {"hvoice", g_hvoice},
                    {"operator", g_operator}, {"pkg_tier", g_pkg_tier},
                    {"pkg_pages_left", g_pkg_pages_left},
                    {"pkg_minutes_left", g_pkg_minutes_left}, {"pkg_renew_day", g_pkg_renew_day},
                    {"switch_pending", g_switch_pending}, {"switch_op", g_switch_op}, {"switch_tier", g_switch_tier}}.dump(1);
    } catch (const std::exception& e) {
        Log(std::string("telefon kaydi yazilamadi: ") + e.what());
    }
}

// ---- Şarj ----
std::string SourceAt(const Vector3& pos, const std::vector<Vector3>& homes) {
    if (transit::OnTransit()) return "Otobüs/metro (USB)";
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (PED::IS_PED_IN_ANY_VEHICLE(player, false)) {
        const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(player, false);
        const Hash m = ENTITY::GET_ENTITY_MODEL(v);
        // Çakmaklık: otomobil ve kamyon sınıfı araçlarda var (bisiklet, motor, dörtteker, tekne, uçak yok); kontak açık olmalı
        const bool has_lighter = VEHICLE::IS_THIS_MODEL_A_CAR(m) && !VEHICLE::IS_THIS_MODEL_A_BIKE(m) && !VEHICLE::IS_THIS_MODEL_A_BICYCLE(m) &&
                                 !VEHICLE::IS_THIS_MODEL_A_QUADBIKE(m) && !VEHICLE::IS_THIS_MODEL_A_BOAT(m);
        if (has_lighter && VEHICLE::_IS_VEHICLE_ENGINE_ON(v)) return "Araç çakmaklığı";
    }
    if (g_deps.shops) {
        for (const auto& s : g_deps.shops->shops()) {
            if (s.type == "market" && D2(pos, s.pos) < 14.f) return "Market";
        }
    }
    for (const auto& h : homes) {
        if (D2(pos, h) < 35.f) return "Ev";
    }
    for (const auto& h : g_default_homes) {
        if (D2(pos, h) < 35.f) return "Ev";
    }
    return "";
}

// ---- Numara parçaları ----
struct Entry {  // arama hedefi
    std::string id, name, number;
    bool service = false, story = false, deletable = false;
    bool mission_ready = false;
};

std::vector<Entry> StoryEntries() {
    std::vector<Entry> out;
    if (!g_deps.missions) return out;
    for (const auto& c : g_deps.missions->Contacts()) {
        Entry e;
        e.id = c.id;
        e.name = g_names.count(Lower(c.id)) ? g_names[Lower(c.id)] : c.name;
        e.number = NumberFor("npc:" + c.id);
        e.story = true;
        e.deletable = true;
        e.mission_ready = c.mission_ready;
        out.push_back(std::move(e));
    }
    return out;
}

const Person* FindPerson(const std::string& id) {
    for (const auto& p : g_people) {
        if (Lower(p.id) == Lower(id)) return &p;
    }
    return nullptr;
}

bool FindEntry(const std::string& id, Entry& out) {
    const std::string lid = Lower(id);
    for (const auto& s : kServices) {
        if (lid == s.id) {
            out = Entry{s.id, s.name, s.number, true, false, false, false};
            return true;
        }
    }
    for (const auto& e : StoryEntries()) {
        if (Lower(e.id) == lid) {
            out = e;
            return true;
        }
    }
    if (const Person* p = FindPerson(id)) {
        out = Entry{p->id, g_names.count(Lower(p->id)) ? g_names[Lower(p->id)] : p->name, p->number, false, false, true, false};
        return true;
    }
    return false;
}

std::string Contact(const Entry& e) {
    return "<link href=\"phone/kisi/" + Lower(e.id) + "\">" + Esc(e.name) + "  " + Format(e.number) + (e.mission_ready ? "  [Yeni görev]" : "") + "</link>\n";
}

// ---- Harita ----
struct Pt {
    float x, y;
};
constexpr float kMapW = 400.f, kMapH = 574.f;
Pt Project(float wx, float wy) {
    const float s = kMapW / 8500.f;
    return {(wx + 4000.f) * s, (8200.f - wy) * s};
}
std::string Pts(const std::vector<Pt>& v) {
    std::string s;
    for (const auto& p : v) s += Num1(std::max(0.f, std::min(kMapW, p.x))) + "," + Num1(std::max(0.f, std::min(kMapH, p.y))) + " ";
    return s;
}

std::string MapSvg(const Vector3& me) {
    // Şematik harita: kıta gövdesi kabaca çizilmiştir (oyunun harita dokusu dağıtılmaz); hatlar ve konumlar gerçek koordinattır.
    static const float land[][2] = {{-3350, 300},  {-3250, 900},  {-2800, 1500}, {-2300, 2500}, {-2100, 3300}, {-1700, 4300}, {-1500, 5100},
                                    {-1000, 5600}, {-450, 6300},  {100, 7000},   {900, 7050},   {1600, 6200},  {2300, 5600},  {3300, 4700},
                                    {3600, 3800},  {3350, 2400},  {3100, 1600},  {3300, 500},   {3400, -700},  {2700, -1900}, {2900, -2600},
                                    {1500, -3200}, {400, -3500},  {-400, -3300}, {-1300, -3300}, {-1900, -2700}, {-1900, -1500}, {-2000, -800}, {-2900, -350}};
    std::vector<Pt> poly;
    for (const auto& p : land) poly.push_back(Project(p[0], p[1]));
    std::string s = "<svg w=\"400\" h=\"574\" bg=\"#0b2a3d\">";
    s += "<polygon points=\"" + Pts(poly) + "\" fill=\"#27402b\" stroke=\"#3f6a45\"/>";
    for (const auto& l : transit::MapLines()) {
        std::vector<Pt> v;
        for (const auto& p : l.pts) v.push_back(Project(p.x, p.y));
        const char* col = l.type == "bus" ? "#f0a82a" : l.type == "coach" ? "#c58cff" : "#2fbf71";
        s += "<polyline points=\"" + Pts(v) + "\" stroke=\"" + col + "\"/>";
    }
    s += "<text x=\"8\" y=\"18\" size=\"13\" fill=\"#9fb8c8\">Şematik harita (kuzey yukarı)</text>";
    s += "<text x=\"8\" y=\"34\" size=\"11\" fill=\"#f0a82a\">sarı: otobüs</text><text x=\"92\" y=\"34\" size=\"11\" fill=\"#2fbf71\">yeşil: metro</text><text x=\"170\" y=\"34\" size=\"11\" fill=\"#c58cff\">mor: bölgesel</text>";
    if (g_nav.active) {
        const Pt d = Project(g_nav.pos.x, g_nav.pos.y);
        s += "<line x1=\"" + Num1(d.x - 7) + "\" y1=\"" + Num1(d.y - 7) + "\" x2=\"" + Num1(d.x + 7) + "\" y2=\"" + Num1(d.y + 7) + "\" stroke=\"#ff5c5c\"/>";
        s += "<line x1=\"" + Num1(d.x - 7) + "\" y1=\"" + Num1(d.y + 7) + "\" x2=\"" + Num1(d.x + 7) + "\" y2=\"" + Num1(d.y - 7) + "\" stroke=\"#ff5c5c\"/>";
    }
    const Pt m = Project(me.x, me.y);
    s += "<circle cx=\"" + Num1(m.x) + "\" cy=\"" + Num1(m.y) + "\" r=\"6\" fill=\"#4c8dff\" stroke=\"#ffffff\"/>";
    s += "</svg>\n";
    return s;
}

std::string Head(const std::string& title) { return "<page title=\"" + Esc(title) + "\" style=\"phone.hss\">\n"; }
std::string Foot(bool home = true) { return std::string("<hr/>\n") + (home ? "<link href=\"phone/home\">Ana ekran</link>\n" : "") + "</page>\n"; }

std::string SiteTitle(const std::string& site) {
    const std::string src = ReadFile(std::string(kBaseDir) + "/data/web/" + site + "/index.hml");
    const size_t a = src.find("title=\"");
    if (a == std::string::npos) return site;
    const size_t b = src.find('"', a + 7);
    return b == std::string::npos ? site : src.substr(a + 7, b - a - 7);
}

bool HasShortcut(const std::string& site) { return std::find(g_shortcuts.begin(), g_shortcuts.end(), site) != g_shortcuts.end(); }

std::string ReasonText(const std::string& reason) {
    if (reason == "already_on_the_way") return "Zaten yolda";
    if (reason == "cooldown") return "Biraz bekle, yakın zamanda çağırdın";
    if (reason == "wanted_level") return "Arandığın için hizmet verilemiyor";
    if (reason == "no_road_nearby") return "Yakında uygun yol yok";
    if (reason == "busy_in_ride") return "Şu an yolculuktasın";
    return "Şu an hizmet verilemiyor";
}

std::string CallService(ServiceKind kind) {
    if (!g_deps.services) return "Hizmet şu an yok";
    const json facts = g_deps.services->Dispatch(kind);
    if (facts.value("accepted", false)) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s yola çıktı (~%d dk)", Services::Label(kind), (facts.value("eta_seconds", 0) + 59) / 60);
        return buf;
    }
    return std::string(Services::Label(kind)) + ": " + ReasonText(facts.value("reason", ""));
}

// Bir hedefi ara. Dönüş: bildirim metni
std::string CallEntry(const Entry& e) {
    if (g_signal_bars == 0) return "Şebeke yok – sinyal alınamıyor";
    if (g_pkg_minutes_left == 0) return "Arama dakikanız kalmadı – tarifeyi yenile";
    const std::string id = Lower(e.id);
    if (e.service) {
        if (id == "svc_taxi") return CallService(ServiceKind::Taxi);
        if (id == "svc_heli") return CallService(ServiceKind::HeliTaxi);
        if (id == "svc_police") return CallService(ServiceKind::Police);
        if (id == "svc_ambulance") return CallService(ServiceKind::Ambulance);
        if (id == "svc_fire") return CallService(ServiceKind::Fire);
        if (id == "svc_mechanic") g_pending_call = "mechanic_line";
        else if (id == "svc_parking") g_pending_call = "parking_line";
        else if (id == "svc_musteri_whiz" || id == "svc_musteri_graham" || id == "svc_musteri_mod") {
            // Müşteri hizmetleri: paketten dakika düşmez; kaliteye göre bağlanma şansı
            const int op_idx = id == "svc_musteri_whiz" ? 0 : id == "svc_musteri_graham" ? 1 : 2;
            const float q = kOpTable[op_idx].quality;  // 1.0 / 0.85 / 0.68
            // Bağlanma olasılığı: Whiz ~90%, Graham ~70%, Modatel ~50%
            const int chance = static_cast<int>(q * 90.f);
            const unsigned int roll = static_cast<unsigned int>(GetTickCount64()) % 100;
            if (static_cast<int>(roll) < chance) {
                g_pending_call = "musteri_hizmetleri:" + std::to_string(op_idx);
                return "Bekleme müziği... " + std::string(kOpTable[op_idx].name) + " müşteri hizmetlerine bağlanıyorsunuz";
            } else {
                return std::string(kOpTable[op_idx].name) + " hatları şu an meşgul. Lütfen daha sonra tekrar arayın.";
            }
        } else g_pending_call = "santral";
        return "Aranıyor: " + e.name;
    }
    if (e.story) {
        g_pending_call = e.id;
        if (g_pkg_minutes_left > 0) --g_pkg_minutes_left;  // -1 = sınırsız: >0 koşulu False tutar
        return "Aranıyor: " + e.name;
    }
    if (const Person* p = FindPerson(e.id)) {
        if (!p->npc.is_null() && !p->npc.empty()) {
            g_pending_call = "person:" + p->id;
            if (g_pkg_minutes_left > 0) --g_pkg_minutes_left;
            return "Aranıyor: " + p->name;
        }
        return "Aranıyor: " + p->name + "... açan olmadı";
    }
    return "Numara bulunamadı";
}

std::string Dial(const std::string& typed) {
    {   // hile kodu: numara değil, kelime
        std::string w;
        for (char c : typed) {
            if (c != ' ') w += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (w == "phpmyadmin") {
            g_admin_req = true;
            return "";
        }
    }
    const std::string digits_only = [&] {
        std::string d;
        for (char c : typed) {
            if (std::isdigit(static_cast<unsigned char>(c))) d += c;
        }
        return d;
    }();
    if (digits_only == "911") {
        g_pending_call = "santral";
        return "911 aranıyor";
    }
    const std::string n = NormalizeDialed(typed);
    if (n.empty()) return "Geçersiz numara. ABD biçimi: 310 555 0123";
    if (n == OwnNumber()) return "Kendi numaranı arıyorsun";
    for (const auto& s : kServices) {
        if (n == s.number) return CallEntry(Entry{s.id, s.name, s.number, true});
    }
    for (const auto& e : StoryEntries()) {
        if (e.number == n) return CallEntry(e);
    }
    if (g_deps.missions) {  // numarası bilinen (tanışılmış) ama rehberden silinmiş kişi
        for (const auto& k : g_deps.missions->KnownCharacters()) {
            if (NumberFor("npc:" + k.id) == n) {
                Entry e{k.id, k.name, n, false, true};
                return CallEntry(e);
            }
        }
    }
    for (const auto& p : g_people) {
        if (p.number == n) return CallEntry(Entry{p.id, p.name, p.number});
    }
    return "Aradığınız numara kullanılmamaktadır.";
}

std::string AddPerson(const std::string& name, const std::string& number, const json& npc) {
    Person p;
    p.id = "m" + std::to_string(g_next_person++);
    p.name = name;
    p.number = number;
    p.npc = npc;
    g_people.push_back(std::move(p));
    Save();
    return g_people.back().id;
}

std::string NavText(const Vector3& me) {
    const float d = D2(me, g_nav.pos);
    const int km_x10 = static_cast<int>(d / 100.f);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%.1f km, yön: %s", km_x10 / 10.f, Places::Compass(me, g_nav.pos).c_str());
    return buf;
}
}  // namespace

// ================= numara sistemi =================
bool IsValidNanp(const std::string& d) {
    if (d.size() != 10) return false;
    for (char c : d) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    // NPA (alan kodu): ilk hane 2-9, N11 (211, 311 ...) ve 37x/96x ayrılmıştır
    if (d[0] < '2' || (d[1] == '1' && d[2] == '1') || (d[0] == '3' && d[1] == '7') || (d[0] == '9' && d[1] == '6')) return false;
    // NXX (santral): ilk hane 2-9, N11 olamaz
    if (d[3] < '2' || (d[4] == '1' && d[5] == '1')) return false;
    return true;
}

std::string GenerateNumber(unsigned long long seed) {
    static const char* const kAreas[] = {"213", "310", "323", "424", "818", "626"};  // Los Angeles çevresi
    unsigned long long x = seed ? seed : 0x9E3779B97F4A7C15ULL;
    const auto next = [&]() {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        return x;
    };
    for (int tries = 0; tries < 50; ++tries) {
        std::string d = kAreas[next() % 6];
        d += static_cast<char>('2' + next() % 8);
        d += static_cast<char>('0' + next() % 10);
        d += static_cast<char>('0' + next() % 10);
        for (int i = 0; i < 4; ++i) d += static_cast<char>('0' + next() % 10);
        if (d.substr(3, 3) == "555") continue;  // 555: hizmet hatları ve kurgu aralığı
        if (IsValidNanp(d)) return d;
    }
    return "3105550199";
}

std::string NumberFor(const std::string& key) { return GenerateNumber(Fnv(key)); }

std::string Format(const std::string& d) {
    if (d.size() != 10) return d;
    return "(" + d.substr(0, 3) + ") " + d.substr(3, 3) + "-" + d.substr(6);
}

std::string NormalizeDialed(const std::string& typed) {
    std::string d;
    for (char c : typed) {
        if (std::isdigit(static_cast<unsigned char>(c))) d += c;
    }
    if (d.size() == 11 && d[0] == '1') d.erase(0, 1);
    if (d.size() == 7) d = std::string(kAreaCode) + d;  // yerel arama
    return IsValidNanp(d) ? d : "";
}

// ================= kayıt, pil =================
void Bind(const Deps& deps) { g_deps = deps; }

void LoadWorldClock();

void Load() {
    LoadWorldClock();
    try {
        const std::string src = ReadFile(std::string(kBaseDir) + "/data/phone.json");
        if (src.empty()) return;
        const json j = json::parse(src);
        for (const auto& h : j.value("home_spots", json::array())) {
            if (h.is_array() && h.size() >= 3) {
                Vector3 v{};
                v.x = h[0].get<float>();
                v.y = h[1].get<float>();
                v.z = h[2].get<float>();
                g_default_homes.push_back(v);
            }
        }
    } catch (const std::exception& e) {
        Log(std::string("phone.json okunamadi: ") + e.what());
    }
}

void LoadWorldClock() {
    try {
        const std::string src = ReadFile(std::string(kBaseDir) + "/data/world_clock.json");
        if (src.empty()) return;
        const json j = json::parse(src);
        g_ls_off = j.value("ls_offset", -480);
        g_wc_default = j.value("default", g_wc_default);
        g_wc_catalog.clear();
        for (const auto& it : j.value("items", json::array())) g_wc_catalog.push_back({it.value("id", ""), it.value("name", ""), it.value("off", 0)});
    } catch (const std::exception& e) {
        Log(std::string("world_clock.json okunamadi: ") + e.what());
    }
}

void LoadSave(int slot) {
    if (g_slot >= 0) Save();
    g_slot = slot;
    g_people.clear();
    g_next_person = 1;
    g_battery = 100.f;
    g_last_warn = 101;
    g_alarms.clear();
    g_events.clear();
    g_notes.clear();
    g_wc_sel = g_wc_default;
    g_hvoice = "Claribel Dervla";
    try {
        const std::string src = ReadFile(SavePath(slot));
        if (src.empty()) return;
        const json j = json::parse(src);
        g_battery = std::max(0.f, std::min(100.f, j.value("battery", 100.0f)));
        g_next_person = j.value("next", 1);
        for (const auto& p : j.value("people", json::array())) {
            g_people.push_back({p.value("id", ""), p.value("name", ""), p.value("number", ""), p.value("npc", json())});
        }
        g_names.clear();
        if (j.contains("names") && j["names"].is_object()) {
            for (const auto& [k, v] : json(j["names"]).items()) {
                if (v.is_string()) g_names[k] = v.get<std::string>();
            }
        }
        g_alarms.clear();
        g_events.clear();
        for (const auto& a : j.value("alarms", json::array())) g_alarms.push_back({a.value("h", 7), a.value("m", 0), a.value("on", true), a.value("label", ""), -1});
        for (const auto& e : j.value("events", json::array())) g_events.push_back({e.value("y", 2013), e.value("m", 1), e.value("d", 1), e.value("text", "")});
        g_notes = j.value("notes", std::vector<std::string>());
        g_wc_sel = j.contains("wclocks") ? j.value("wclocks", std::vector<std::string>()) : g_wc_default;
        g_hvoice = j.value("hvoice", std::string("Claribel Dervla"));
        if (j.contains("shortcuts") && j["shortcuts"].is_array()) g_shortcuts = j["shortcuts"].get<std::vector<std::string>>();
        g_operator  = std::max(0, std::min(kOpCount - 1, j.value("operator", 1)));
        g_pkg_tier  = std::max(0, std::min(3, j.value("pkg_tier", 1)));
        g_pkg_pages_left   = j.value("pkg_pages_left",   CurrentPkg().pages);
        g_pkg_minutes_left = j.value("pkg_minutes_left", CurrentPkg().minutes);
        g_pkg_renew_day    = j.value("pkg_renew_day",   -1);
        g_switch_pending   = j.value("switch_pending",   false);
        g_switch_op        = j.value("switch_op",        -1);
        g_switch_tier      = j.value("switch_tier",      -1);
        if (g_switch_pending && g_switch_op >= 0) {
            g_switch_call_time = GetTickCount64() + 8000ULL;
            g_switch_ring_ready = false;
        }
    } catch (const std::exception& e) {
        Log(std::string("telefon kaydi okunamadi: ") + e.what());
    }
    // Hiç kaydedilmemiş ya da paket henüz başlatılmamış: oyun açılışında ücretsiz başlat
    if (g_pkg_renew_day < 0) {
        g_pkg_pages_left   = CurrentPkg().pages;
        g_pkg_minutes_left = CurrentPkg().minutes;
        g_pkg_renew_day    = 0;  // GameDay() Update'te ilk tickta set edilir
    }
}

void Shutdown() {
    Save();
    g_slot = -1;
}

void Update(const Vector3& pos, bool screen_on, const std::vector<Vector3>& homes) {
    if (g_slot < 0) return;
    const unsigned long long now = GetTickCount64();
    if (g_last_tick == 0) g_last_tick = now;
    if (now - g_last_tick < 500ULL) return;
    const float dt = static_cast<float>(now - g_last_tick) / 1000.f;
    g_last_tick = now;
    {   // alarm: oyun saati eşleşince bir kez çalar (telefon kapalı/şarjı bitikse çalmaz)
        const int H = TIME::GET_CLOCK_HOURS(), M = TIME::GET_CLOCK_MINUTES();
        const long key = static_cast<long>(TIME::GET_CLOCK_DAY_OF_MONTH()) * 1440L + H * 60 + M;
        for (auto& a : g_alarms) {
            if (!a.on || a.h != H || a.m != M || a.fired == key || g_battery < 0.5f) continue;
            a.fired = key;
            karma::Toast("Alarm " + Two(a.h) + ":" + Two(a.m) + (a.label.empty() ? "" : " - " + a.label), 8);
            AUDIO::PLAY_SOUND_FRONTEND(-1, const_cast<char*>("Text_Arrive_Tone"), const_cast<char*>("Phone_SoundSet_Default"), true);
        }
    }
    g_source = SourceAt(pos, homes);
    g_charging = !g_source.empty();
    if (g_charging) {
        g_battery = std::min(100.f, g_battery + dt * 0.45f);
        g_last_warn = 101;
    } else {
        g_battery = std::max(0.f, g_battery - dt * (screen_on ? 0.12f : 0.035f));  // boşta ~48 dk, ekran açıkken ~14 dk
        const int pct = static_cast<int>(g_battery);
        for (int th : {20, 10, 5, 0}) {
            if (pct <= th && g_last_warn > th) {
                g_last_warn = th;
                karma::Toast(th == 0 ? "Telefonun şarjı bitti. Market, otobüs, araç ya da evde şarja tak." : "Telefon şarjı %" + std::to_string(th), 5);
                break;
            }
        }
    }
    // ---- Sinyal güncellemesi (her 3 sn) ----
    if (now >= g_signal_next) {
        g_signal_next = now + 3000ULL;
        g_signal_bars = CalcSignalBars(pos, g_operator);
    }
    // ---- Paket başlatma / yenileme (ilk tick veya 2 oyun günü geçince) ----
    {
        const int gday = GameDay();
        if (g_pkg_renew_day == 0) {
            g_pkg_renew_day = gday;
        } else if (gday >= g_pkg_renew_day + 2) {
            const PkgInfo& pkg = CurrentPkg();
            if (economy::GetCash() >= pkg.price) {
                economy::Spend(pkg.price);
                g_pkg_pages_left   = pkg.pages;
                g_pkg_minutes_left = pkg.minutes;
                g_pkg_renew_day    = gday;
                karma::Toast(std::string(kOpTable[g_operator].name) + " " + pkg.name + " tarifeniz yenilendi ($" + std::to_string(pkg.price) + ")", 5);
            } else {
                // Bakiye yetersiz: paket askıya alınır
                g_pkg_pages_left   = 0;
                g_pkg_minutes_left = 0;
                karma::Toast(std::string(kOpTable[g_operator].name) + ": Yetersiz bakiye nedeniyle paket yenilenemedi. Arama ve internet durduruldu.", 6);
            }
            Save();
        }
        // Operatör geçiş geri arama zamanlayıcısı
        if (g_switch_pending && !g_switch_ring_ready && g_switch_call_time > 0 && now >= g_switch_call_time) {
            g_switch_ring_ready = true;
        }
    }
    if (now - g_last_save > 30000ULL) {
        g_last_save = now;
        Save();
    }
}

bool AtHome(const Vector3& pos, const std::vector<Vector3>& homes) {
    for (const auto& h : homes) {
        if (D2(pos, h) < 35.f) return true;
    }
    for (const auto& h : g_default_homes) {
        if (D2(pos, h) < 35.f) return true;
    }
    return false;
}

std::string HavilandVoice() { return g_hvoice; }
bool HavilandVoiceFemale() {
    for (const auto& v : kVoices) {
        if (g_hvoice == v.name) return v.female;
    }
    return true;
}

bool Dead() { return g_slot >= 0 && g_battery < 0.5f && !g_charging; }
int Battery() { return static_cast<int>(g_battery + 0.5f); }
bool Charging() { return g_charging; }
std::string ChargeSource() { return g_source; }
std::string BatteryText() { return "%" + std::to_string(Battery()) + (g_charging ? " şarjda" : ""); }

bool ConsumeAdminRequest() {
    const bool r = g_admin_req;
    g_admin_req = false;
    return r;
}

bool ConsumeCall(std::string& id) {
    if (g_pending_call.empty()) return false;
    if (g_signal_bars == 0) { g_pending_call.clear(); return false; }
    // CallEntry checks and consumes minutes before queuing; checking again would reject the last-minute call.
    id = g_pending_call;
    g_pending_call.clear();
    return true;
}

bool PersonNpc(const std::string& id, json& npc_json) {
    std::string pid = id.rfind("person:", 0) == 0 ? id.substr(7) : id;
    if (const Person* p = FindPerson(pid)) {
        if (p->npc.is_null() || p->npc.empty()) return false;
        npc_json = p->npc;
        return true;
    }
    return false;
}

json CallContacts() {
    json out = json::array();
    for (const auto& e : StoryEntries()) {
        out.push_back({{"id", e.id}, {"name", e.name}});
    }
    for (const auto& p : g_people) {
        if (p.npc.is_null() || p.npc.empty()) continue;
        out.push_back({{"id", "person:" + p.id},
                       {"name", g_names.count(Lower(p.id)) ? g_names[Lower(p.id)] : p.name}});
    }
    return out;
}

bool IsCallableContact(const std::string& id) {
    if (id.rfind("person:", 0) == 0) {
        const Person* p = FindPerson(id.substr(7));
        return p && !p->npc.is_null() && !p->npc.empty();
    }
    return g_deps.missions && g_deps.missions->IsKnownCharacter(id);
}

std::string CallContact(const std::string& id) {
    if (!IsCallableContact(id)) return "Rehber kişisi bulunamadı";
    const bool person_contact = id.rfind("person:", 0) == 0;
    Entry entry;
    const std::string lookup_id = person_contact ? id.substr(7) : id;
    if (!FindEntry(lookup_id, entry) || entry.service || (person_contact ? entry.story : !entry.story)) {
        return "Rehber kişisi bulunamadı";
    }
    return CallEntry(entry);
}

bool AddNote(const std::string& text) {
    if (g_slot < 0 || text.empty()) return false;
    for (const auto& n : g_notes) {
        if (n == text) return false;
    }
    g_notes.push_back(text.size() > 200 ? text.substr(0, 200) : text);
    if (g_notes.size() > 40) g_notes.erase(g_notes.begin());
    Save();
    return true;
}

json OfferNumber(const json& npc_json, int karma_value) {
    const json prof = npc_json.value("profile", json::object());
    const std::string name = prof.value("name", "");
    const std::string key = "person:" + name + "|" + prof.value("hometown", "") + "|" + npc_json.value("gender", "");
    const std::string number = NumberFor(key);  // aynı NPC her zaman aynı numarayı verir
    for (const auto& p : g_people) {
        if (p.number == number) return json{{"kind", "number"}, {"accepted", true}, {"already_saved", true}, {"number", Format(number)}, {"name", name}};
    }
    if (karma_value < -15) return json{{"kind", "number"}, {"accepted", false}, {"reason", "doesnt_trust"}};
    if (Fnv(key) % 100 >= 65) return json{{"kind", "number"}, {"accepted", false}, {"reason", "not_comfortable"}};  // bazıları vermez: sabit
    AddPerson(name.empty() ? "Tanıdık" : name, number, npc_json);
    return json{{"kind", "number"}, {"accepted", true}, {"number", Format(number)}, {"name", name}};
}

// ================= sayfalar =================
// ---- Operatör web sayfası (dinamik içerik) ----
std::string BuildOpPage(const std::string& url) {
    int op = -1;
    for (int i = 0; i < kOpCount; ++i)
        if (url == kOpTable[i].id || url.rfind(std::string(kOpTable[i].id) + "/", 0) == 0) { op = i; break; }
    if (op < 0) return "";
    const bool is_mine = (g_operator == op);
    const OpInfo& o = kOpTable[op];
    const char* bg  = op == 0 ? "#0a1a3a" : op == 1 ? "#1a2e1a" : "#2a0a1a";
    const char* col = op == 0 ? "#4db3ff" : op == 1 ? "#66cc44" : "#ff4488";
    const char* cov = op == 0 ? "Tam kapsama (dağlık alanlar dahil)" : op == 1 ? "Şehir: mükemmel · Kırsal: iyi · Dağ: zayıf" : "Şehir: iyi · Kırsal: orta · Dağ: çok zayıf";
    std::string h = "<page title=\"" + Esc(o.name) + "\" style=\"phone.hss\">\n";
    h += "<svg w=\"400\" h=\"64\" bg=\"" + std::string(bg) + "\"><text x=\"24\" y=\"40\" size=\"30\" fill=\"" + std::string(col) + "\">" + Esc(o.name) + "</text>";
    h += "<text x=\"24\" y=\"56\" size=\"13\" fill=\"#aaaaaa\">Mobil iletişim · " + std::string(cov) + "</text></svg>\n";
    if (g_switch_pending && g_switch_op == op) {
        h += "<card title=\"Geçiş Talebiniz Alındı\">Bu operatöre numara taşıma/geçiş talebiniz işleniyor. Müşteri temsilcimiz onay için kısa süre içinde sizi arayacaktır.</card>\n";
    }
    // Kullanım durumu (mevcut operatörse)
    if (is_mine) {
        const PkgInfo& cp = CurrentPkg();
        const std::string dk  = cp.minutes < 0 ? "Sınırsız" : std::to_string(g_pkg_minutes_left) + " / " + std::to_string(cp.minutes) + " dk";
        const std::string veri = cp.pages   < 0 ? "Sınırsız" : DataStr(g_pkg_pages_left) + " / " + DataStr(cp.pages) + " kaldı";
        h += "<h2>Kullanımınız — " + Esc(cp.name) + "</h2>\n";
        h += "<card title=\"İnternet\">" + veri + "</card>\n";
        h += "<card title=\"Arama\">" + dk + " · dakika başı 20 kelime</card>\n";
        h += "<card title=\"Şebeke\">" + std::to_string(g_signal_bars) + "/4 çubuk</card>\n" + SignalSvg(g_signal_bars);
        h += "<link href=\"act:ph:op_renew\">Tarifeyi şimdi yenile ($" + std::to_string(cp.price) + ")</link>\n";
    }
    // Paket listesi
    h += "<h2>Paketler (Seçmek için tıkla)</h2>\n";
    for (int t = 0; t < 4; ++t) {
        const PkgInfo& pk = o.pkgs[t];
        const bool active = is_mine && g_pkg_tier == t;
        const std::string vd   = pk.pages   < 0 ? "Sınırsız veri"   : DataStr(pk.pages);
        const std::string mink = pk.minutes  < 0 ? "Sınırsız arama"  : std::to_string(pk.minutes) + " dk";
        const std::string pr   = "$" + std::to_string(pk.price);
        const std::string act_href = "act:ph:op_buy:" + std::to_string(op) + ":" + std::to_string(t);
        if (active) {
            h += "<card title=\"" + Esc(pk.name) + " ✓ Aktif\" price=\"" + pr + "\">" + vd + " · " + mink + " (Mevcut Tarifeniz)</card>\n";
        } else {
            h += "<card title=\"" + Esc(pk.name) + "\" price=\"" + pr + "\" href=\"" + act_href + "\">" + vd + " · " + mink + " (Seçmek için tıkla)</card>\n";
            h += "<link href=\"" + act_href + "\">↳ " + Esc(pk.name) + " Paketine Geç (" + pr + ")</link>\n";
        }
    }
    if (!is_mine) h += "<p>Geçerseniz mevcut paketin süresi sıfırlanır; yeni dönem başlar.</p>\n";
    // Müşteri hizmetleri (paketten dakika düşmez)
    {
        static const char* const kSupportId[] = {"svc_musteri_whiz", "svc_musteri_graham", "svc_musteri_mod"};
        const int chance = static_cast<int>(kOpTable[op].quality * 90.f);
        h += "<h2>Müşteri Hizmetleri</h2>\n";
        h += "<link href=\"act:ph:call:" + std::string(kSupportId[op]) + "\">Ara: " + Format(kServices[kSvcSupportBase + op].number) + "  (~%" + std::to_string(chance) + " bağlanma) · dakika harcanmaz</link>\n";
    }
    h += "<hr/>\n<link href=\"home\">Ana sayfa</link>\n</page>\n";
    return h;
}

std::string BuildPage(const std::string& url) {
    if (g_slot < 0) return "";
    // Operatör web siteleri dinamik
    for (int i = 0; i < kOpCount; ++i) {
        if (url == kOpTable[i].id || url.rfind(std::string(kOpTable[i].id) + "/", 0) == 0) return BuildOpPage(url);
    }
    if (url == "lslawyers.gov" || url == "lslawyers.gov/" || url == "lslawyers.gov/index" ||
        url == "lslawyers.gov/index.hml") {
        const bool wanted = g_deps.jail && g_deps.jail->HasWantedCase();
        const int chance = g_deps.jail ? g_deps.jail->BarAssociationChance() : 40;
        std::string page = "<page title=\"Los Santos Barosu\"><h1>Los Santos Barosu</h1>"
                           "<p>LS Lawyers Association · Ceza hukuku ve acil hukuki destek</p><hr/>";
        if (wanted) {
            page += "<card title=\"Aktif yakalama kaydı\">Bir avukatla görüşerek kaydın kaldırılması için başvur.</card>\n";
            page += "<card title=\"Sonuçlanma olasılığı\">Bu aramadaki olasılık: %" + std::to_string(chance) + "</card>\n";
            page += "<link href=\"act:ph:baro\">Baroyu ara</link>\n";
        } else {
            page += "<card title=\"Yakalama kaydı yok\">Baroya başvurabilmek için aktif bir aranma kaydın olmalı. "
                    "İlk başvuru olasılığı %40; başarısız her başvuru 10 puan düşürür. Aranma bitince olasılık sıfırlanır.</card>\n";
        }
        page += "<hr/><link href=\"home\">Ana sayfa</link></page>";
        return page;
    }
    if (url.rfind("phone/", 0) != 0) return "";
    const std::string page = url.substr(6);
    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vector3 me = ENTITY::GET_ENTITY_COORDS(player, true);
    std::string h;

    if (page == "home") {
        h = Head("Telefon") + "<h1>Telefon</h1>\n";
        // Durum satırı: şebeke + operatör + pil
        const std::string op_name = kOpTable[g_operator].name;
        const bool unlimited_data = (g_pkg_pages_left < 0);
        const std::string veri_str = unlimited_data ? "Sınırsız" : DataStr(g_pkg_pages_left);
        const std::string dk_str   = g_pkg_minutes_left < 0 ? "∞ dk" : std::to_string(g_pkg_minutes_left) + " dk";
        if (g_signal_bars == 0) {
            h += "<p><b>Şebeke yok</b>  " + Esc(op_name) + "  ·  Şarj: " + BatteryText() + (g_charging ? " (" + g_source + ")" : "") + "</p>\n";
        } else {
            h += "<p>" + Esc(op_name) + "  ·  " + veri_str + "  " + dk_str + "  ·  " + BatteryText() + (g_charging ? " (" + g_source + ")" : "") + "</p>\n";
        }
        h += SignalSvg(g_signal_bars);
        // İnternet tile: sinyal 0 veya sayfa bitti → internet_yok; sinyal 1-2 → internet_yavas
        const bool no_inet = g_signal_bars == 0 || g_pkg_pages_left == 0;
        const std::string inet_href = no_inet ? "phone/internet_yok" :
                                      (g_signal_bars <= 2) ? "phone/internet_yavas" : "home";
        h += "<tile icon=\"T\" color=\"#2fbf71\" href=\"phone/telefon\">Telefon</tile>\n<tile icon=\"R\" color=\"#4c8dff\" href=\"phone/rehber\">Rehber</tile>\n<tile icon=\"@\" color=\"#e8833a\" href=\"" + inet_href + "\">İnternet</tile>\n";
        h += "<tile icon=\"H\" color=\"#9b6bff\" href=\"phone/haviland\">Haviland</tile>\n<tile icon=\"M\" color=\"#2aa7c9\" href=\"phone/maps\">Maps</tile>\n";
        h += "<tile icon=\"$\" color=\"#d4a017\" href=\"phone/maze\">Maze Bank</tile>\n<tile icon=\"A\" color=\"#e0508a\" href=\"phone/store\">App Store</tile>\n";
        h += "<tile icon=\"S\" color=\"#2a8f8a\" href=\"phone/saat\">Saat</tile>\n<tile icon=\"!\" color=\"#d9534f\" href=\"phone/alarm\">Alarm</tile>\n<tile icon=\"K\" color=\"#3e9b4f\" href=\"phone/takvim\">Takvim</tile>\n<tile icon=\"N\" color=\"#c9a227\" href=\"phone/notlar\">Notlar</tile>\n";
        h += "<tile icon=\"✓\" color=\"#e8b23a\" href=\"phone/yapilacaklar\">Yapılacaklar</tile>\n";
        for (const auto& s : g_shortcuts) {
            const std::string title = SiteTitle(s);
            std::string ic(1, title.empty() ? '?' : static_cast<char>(std::toupper(static_cast<unsigned char>(title[0]))));
            h += "<tile icon=\"" + Esc(ic) + "\" color=\"#56607a\" href=\"" + s + "\">" + Esc(title) + "</tile>\n";
        }
        return h + Foot(false);
    }
    if (page == "telefon") {
        h = Head("Telefon") + "<h1>Telefon</h1>\n<p>Numaran: " + Format(OwnNumber()) + "</p>\n";
        if (g_signal_bars == 0) h += "<card title=\"Şebeke yok\">Sinyal alınamıyor; arama ve SMS çalışmaz.</card>\n" + SignalSvg(0);
        else if (g_pkg_minutes_left == 0) h += "<card title=\"Dakika bitti\">Tarifenizde arama dakikanız kalmadı. Operatör sitesinden yenileyin.</card>\n";
        else {
            const std::string dk = g_pkg_minutes_left < 0 ? "Sınırsız" : std::to_string(g_pkg_minutes_left) + " dk kaldı";
            h += "<card title=\"Şebeke\">" + Esc(kOpTable[g_operator].name) + "  ·  " + dk + "</card>\n" + SignalSvg(g_signal_bars);
        }
        h += "<link href=\"askact:ph:dial:{}\">Numara çevir (ör. 310 555 0100, acil: 911)</link>\n";
        // Mevcut operatörün müşteri hizmetleri en üste (paketten dakika düşmez)
        {
            static const char* const kSupportId[] = {"svc_musteri_whiz", "svc_musteri_graham", "svc_musteri_mod"};
            const auto& sv = kServices[kSvcSupportBase + g_operator];
            const float q = kOpTable[g_operator].quality;
            const int chance = static_cast<int>(q * 90.f);
            h += "<h2>Müşteri Hizmetleri</h2>\n";
            h += "<link href=\"act:ph:call:" + std::string(sv.id) + "\">" + sv.name + "  " + Format(sv.number) + "  (~%" + std::to_string(chance) + " bağlanma)</link>\n";
        }
        h += "<h2>Hizmet hatları</h2>\n";
        for (int i = 0; i < kSvcSupportBase; ++i) h += "<link href=\"act:ph:call:" + std::string(kServices[i].id) + "\">" + kServices[i].name + "  " + Format(kServices[i].number) + "</link>\n";
        h += "<link href=\"act:ph:call:santral\">Acil çağrı  911</link>\n";
        return h + Foot();
    }
    if (page == "rehber") {
        int missing = 0;
        if (g_deps.missions) {
            for (const auto& k : g_deps.missions->KnownCharacters()) {
                bool have = false;
                for (const auto& e : StoryEntries()) have = have || e.id == k.id;
                if (!have) ++missing;
            }
        }
        h = Head("Rehber") + "<h1>Rehber</h1>\n<p>Numaran: " + Format(OwnNumber()) + "</p>\n";
        h += "<link href=\"askact:ph:newcontact:{}\">+ Yeni kişi kaydet (Ad Soyad 3105550123)</link>\n";
        h += "<link href=\"act:ph:resave\">Görev karakterlerini yeniden kaydet (" + std::to_string(missing) + " eksik)</link>\n<h2>Kişiler</h2>\n";
        const auto story = StoryEntries();
        for (const auto& e : story) h += Contact(e);
        for (const auto& p : g_people) h += Contact(Entry{p.id, g_names.count(Lower(p.id)) ? g_names[Lower(p.id)] : p.name, p.number});
        if (story.empty() && g_people.empty()) h += "<p>Kayıtlı kişi yok.</p>\n";
        h += "<h2>Hizmet hatları</h2>\n";
        for (int i = 0; i < kSvcSupportBase; ++i) h += "<link href=\"phone/kisi/" + std::string(kServices[i].id) + "\">" + kServices[i].name + "  " + Format(kServices[i].number) + "</link>\n";
        h += "<h2>Operatör müşteri hizmetleri</h2>\n";
        for (int i = kSvcSupportBase; i < kSvcCount; ++i) h += "<link href=\"phone/kisi/" + std::string(kServices[i].id) + "\">" + kServices[i].name + "  " + Format(kServices[i].number) + "</link>\n";
        return h + Foot();
    }
    if (page.rfind("kisi/", 0) == 0) {
        Entry e;
        if (!FindEntry(page.substr(5), e)) return "";
        h = Head(e.name) + "<h1>" + Esc(e.name) + "</h1>\n<card title=\"Numara\">" + Format(e.number) + "</card>\n";
        h += "<link href=\"act:ph:call:" + Lower(e.id) + "\">Ara</link>\n";
        if (e.deletable) h += "<link href=\"askact:ph:rename:" + Lower(e.id) + ":{}\">Adı değiştir (boş bırakırsan özgün ad)</link>\n<link href=\"act:ph:del:" + Lower(e.id) + "\">Rehberden sil</link>\n";
        h += "<link href=\"phone/rehber\">Rehber</link>\n";
        return h + Foot();
    }
    if (page == "yapilacaklar") {  // görev listesi: aktif görev + tamamlananlar (HUD'da artık yok)
        h = Head("Yapılacaklar") + "<h1>Yapılacaklar</h1>\n";
        if (g_deps.missions) {
            const auto todos = g_deps.missions->Todos();
            if (todos.empty()) h += "<p>Şu an görev yok.</p>\n";
            for (const auto& t : todos) {
                if (t.done) h += "<card title=\"✓ " + Esc(t.title) + "\">" + Esc(t.objective) + "</card>\n";
                else h += "<card title=\"☐ " + Esc(t.title) + "\">" + Esc(t.objective) + "</card>\n";
            }
        } else {
            h += "<p>Görev sistemi yüklenemedi.</p>\n";
        }
        return h + Foot();
    }
    if (page == "haviland") {
        h = Head("Haviland") + "<h1>Haviland</h1>\n<p>Telefonun yapay zekâ asistanı (kişi değil). Taksi, helikopter, polis, ambulans, itfaiye çağırır; toplu taşıma rotası verir; araç işlerini yönlendirir.</p>\n";
        h += "<link href=\"act:ph:haviland\">Haviland'ı çağır (B: konuş, T: yaz)</link>\n<card title=\"Ses\">" + Esc(g_hvoice) + "</card>\n<link href=\"phone/haviland/ses\">Ses seç</link>\n";
        return h + Foot();
    }
    if (page == "haviland/ses") {
        h = Head("Haviland sesi") + "<h1>Haviland sesi</h1>\n<p>Seçtiğin ses bir sonraki konuşmada kullanılır.</p>\n<h2>Kadın sesleri</h2>\n";
        for (const auto& v : kVoices) {
            if (v.female) h += std::string("<link href=\"act:ph:hvoice:") + v.name + "\">" + v.name + (g_hvoice == v.name ? "  ✓ seçili" : "") + "</link>\n";
        }
        h += "<h2>Erkek sesleri</h2>\n";
        for (const auto& v : kVoices) {
            if (!v.female) h += std::string("<link href=\"act:ph:hvoice:") + v.name + "\">" + v.name + (g_hvoice == v.name ? "  ✓ seçili" : "") + "</link>\n";
        }
        return h + Foot();
    }
    if (page == "saat" || page == "saat/ayar") {
        const int H = TIME::GET_CLOCK_HOURS(), M = TIME::GET_CLOCK_MINUTES(), D = TIME::GET_CLOCK_DAY_OF_MONTH(), Mo = TIME::GET_CLOCK_MONTH(), Y = TIME::GET_CLOCK_YEAR();
        const int dow = TIME::GET_CLOCK_DAY_OF_WEEK() % 7;
        if (page == "saat/ayar") {  // ülke/şehir ekle-çıkar: tüm katalog
            h = Head("Saat ayarları") + "<h1>Dünya saati</h1>\n<p>Eklemek/çıkarmak için seç. Saatler standart saattir (yaz saati uygulanmaz).</p>\n<link href=\"askact:ph:wcadd:{}\">Ada göre ekle (ör. Japonya, Brezilya)</link>\n";
            for (const auto& w : g_wc_catalog) {
                const bool on = std::find(g_wc_sel.begin(), g_wc_sel.end(), w.id) != g_wc_sel.end();
                h += "<link href=\"act:ph:wctoggle:" + w.id + "\">" + Esc(w.name) + "  " + UtcLabel(w.off) + (on ? "  [ekli]" : "") + "</link>\n";
            }
            return h + Foot();
        }
        h = Head("Saat") + "<h1>Saat</h1>\n<svg w=\"400\" h=\"150\" bg=\"#0e1118\"><text x=\"200\" y=\"28\" size=\"72\" fill=\"#ffffff\" anchor=\"middle\">" + Two(H) + ":" + Two(M) +
            "</text><text x=\"200\" y=\"112\" size=\"22\" fill=\"#8aa4d6\" anchor=\"middle\">Los Santos · " + std::to_string(D) + " " + kMonths[Mo % 12] + " " + std::to_string(Y) + ", " + kDays[dow] + "</text></svg>\n";
        h += "<h2>Dünya saati</h2>\n";
        const int ls_min = H * 60 + M;
        for (const auto& id : g_wc_sel) {
            const WClock* w = FindWc(id);
            if (!w) continue;
            const int tm = ls_min + (w->off - g_ls_off);
            const int shift = tm < 0 ? -1 : tm >= 1440 ? 1 : 0;
            const int tt = ((tm % 1440) + 1440) % 1440;
            h += "<card title=\"" + Esc(w->name) + "\" href=\"act:ph:wcdel:" + w->id + "\">" + Two(tt / 60) + ":" + Two(tt % 60) + "   " + kDays[((dow + shift) % 7 + 7) % 7] +
                 (shift > 0 ? " (yarın)" : shift < 0 ? " (dün)" : "") + "   " + UtcLabel(w->off) + "  (Enter: kaldır)</card>\n";
        }
        if (g_wc_sel.empty()) h += "<p>Liste boş: Ayarlar'dan ülke/şehir ekle.</p>\n";
        h += "<link href=\"askact:ph:wcadd:{}\">+ Ülke/şehir ekle (ada göre)</link>\n<link href=\"phone/saat/ayar\">Ayarlar: tüm ülke ve şehirler</link>\n";
        int next = -1, best = 24 * 60 + 1;
        for (size_t i = 0; i < g_alarms.size(); ++i) {
            if (!g_alarms[i].on) continue;
            int diff = g_alarms[i].h * 60 + g_alarms[i].m - ls_min;
            if (diff <= 0) diff += 24 * 60;
            if (diff < best) {
                best = diff;
                next = static_cast<int>(i);
            }
        }
        h += next >= 0 ? "<p>Sonraki alarm: " + Two(g_alarms[static_cast<size_t>(next)].h) + ":" + Two(g_alarms[static_cast<size_t>(next)].m) + " (" + std::to_string(best / 60) + " sa " + std::to_string(best % 60) + " dk sonra)</p>\n" : "<p>Kurulu alarm yok.</p>\n";
        h += "<link href=\"phone/alarm\">Alarmlar</link>\n<link href=\"phone/takvim\">Takvim</link>\n";
        return h + Foot();
    }
    if (page == "alarm") {
        h = Head("Alarm") + "<h1>Alarm</h1>\n<link href=\"askact:ph:alarmadd:{}\">+ Yeni alarm (ör. 07:30 Kalk)</link>\n";
        if (g_alarms.empty()) h += "<p>Alarm yok.</p>\n";
        for (size_t i = 0; i < g_alarms.size(); ++i) {
            const Alarm& a = g_alarms[i];
            h += "<link href=\"act:ph:alarmtoggle:" + std::to_string(i) + "\">" + Two(a.h) + ":" + Two(a.m) + "  " + Esc(a.label) + "  [" + (a.on ? "AÇIK" : "kapalı") + "]</link>\n";
            h += "<link href=\"act:ph:alarmdel:" + std::to_string(i) + "\">  sil</link>\n";
        }
        return h + Foot();
    }
    if (page == "takvim") {
        const int D = TIME::GET_CLOCK_DAY_OF_MONTH(), Mo = TIME::GET_CLOCK_MONTH() % 12 + 1, Y = TIME::GET_CLOCK_YEAR(), dow = TIME::GET_CLOCK_DAY_OF_WEEK() % 7;
        const int first = ((dow - (D - 1)) % 7 + 7) % 7;  // ayın ilk günü (0 Pazar)
        const int col0 = (first + 6) % 7;                  // Pazartesi başlangıçlı sütun
        const int total = DaysIn(Y, Mo);
        std::string svg = "<svg w=\"400\" h=\"290\" bg=\"#0e1118\">";
        static const char* const hdr[] = {"Pzt", "Sal", "Çar", "Per", "Cum", "Cmt", "Paz"};
        for (int c2 = 0; c2 < 7; ++c2) svg += "<text x=\"" + std::to_string(30 + c2 * 56) + "\" y=\"6\" size=\"15\" fill=\"#8aa4d6\" anchor=\"middle\">" + hdr[c2] + "</text>";
        for (int day = 1; day <= total; ++day) {
            const int idx = col0 + day - 1, cc = idx % 7, rr = idx / 7;
            const int cx = 30 + cc * 56, cy = 40 + rr * 40;
            bool has_ev = false;
            for (const auto& e : g_events) has_ev = has_ev || (e.y == Y && e.m == Mo && e.d == day);
            if (day == D) svg += "<circle cx=\"" + std::to_string(cx) + "\" cy=\"" + std::to_string(cy + 8) + "\" r=\"17\" fill=\"#4c8dff\"/>";
            svg += "<text x=\"" + std::to_string(cx) + "\" y=\"" + std::to_string(cy) + "\" size=\"17\" fill=\"#ffffff\" anchor=\"middle\">" + std::to_string(day) + "</text>";
            if (has_ev) svg += "<circle cx=\"" + std::to_string(cx) + "\" cy=\"" + std::to_string(cy + 24) + "\" r=\"3\" fill=\"#f0a82a\"/>";
        }
        svg += "</svg>\n";
        h = Head("Takvim") + "<h1>" + std::string(kMonths[Mo - 1]) + " " + std::to_string(Y) + "</h1>\n" + svg;
        h += "<link href=\"askact:ph:evadd:{}\">+ Etkinlik ekle (ör. 15 Doktor ya da 15.11 Doktor)</link>\n";
        std::vector<size_t> order;
        for (size_t i = 0; i < g_events.size(); ++i) order.push_back(i);
        std::sort(order.begin(), order.end(), [](size_t a, size_t b) {
            const Ev& x = g_events[a];
            const Ev& y = g_events[b];
            return x.y != y.y ? x.y < y.y : x.m != y.m ? x.m < y.m : x.d < y.d;
        });
        if (order.empty()) h += "<p>Etkinlik yok.</p>\n";
        for (size_t i : order) h += "<link href=\"act:ph:evdel:" + std::to_string(i) + "\">" + Two(g_events[i].d) + "." + Two(g_events[i].m) + "  " + Esc(g_events[i].text) + "  (sil)</link>\n";
        return h + Foot();
    }
    if (page == "notlar") {
        h = Head("Notlar") + "<h1>Notlar</h1>\n<link href=\"askact:ph:noteadd:{}\">+ Yeni not</link>\n";
        for (size_t i = 0; i < g_notes.size(); ++i) h += "<card title=\"Not " + std::to_string(i + 1) + "\" href=\"act:ph:notedel:" + std::to_string(i) + "\">" + Esc(g_notes[i]) + "  (Enter: sil)</card>\n";
        if (g_notes.empty()) h += "<p>Not yok.</p>\n";
        return h + Foot();
    }
    if (page == "maps" || page == "maps/rota") {
        const std::string zone = g_deps.places ? g_deps.places->ZoneNameAt(me) : "";
        if (page == "maps") {
            h = Head("Eyefind Maps") + "<h1>Eyefind Maps</h1>\n" + MapSvg(me);
            h += "<p>Konum: " + Esc(zone.empty() ? "bilinmiyor" : zone) + "</p>\n";
            if (g_nav.active) h += "<p>Hedef: " + Esc(g_nav.name) + " - " + Esc(NavText(me)) + "</p>\n";
            h += "<link href=\"askact:ph:nav:{}\">Yer ara, yol tarifi al (ör. havaalanı, Davis, en yakın hastane)</link>\n";
            if (g_nav.active) h += "<link href=\"phone/maps/rota\">Rota ayrıntısı</link>\n<link href=\"act:ph:navclear\">İşareti kaldır</link>\n";
            return h + Foot();
        }
        h = Head("Rota") + "<h1>Rota</h1>\n";
        if (!g_nav.active) return h + "<p>Önce bir hedef ara.</p>\n<link href=\"phone/maps\">Harita</link>\n" + Foot();
        const float d = D2(me, g_nav.pos);
        h += "<card title=\"" + Esc(g_nav.name) + "\">" + Esc(NavText(me)) + "</card>\n";
        h += "<p>Araçla yaklaşık " + std::to_string(std::max(1, static_cast<int>(d * 1.35f / 14.f / 60.f))) + " dk. Hedef haritaya işaretlendi; araçtayken yol çizgisi mini haritada görünür.</p>\n";
        const json r = transit::PlanRoute(me, g_nav.pos, g_nav.name);
        if (r.value("found", false)) {
            h += "<h2>Toplu taşıma</h2>\n<p>" + std::to_string(r.value("board_walk_m", 0)) + " m yürü, " + Esc(r.value("board_stop", "")) + " durağından " +
                 Esc(r.value("line", "")) + " hattına bin, " + Esc(r.value("alight_stop", "")) + " durağında in, " + std::to_string(r.value("final_walk_m", 0)) +
                 " m yürü (~" + std::to_string(r.value("est_minutes", 0)) + " dk, $" + std::to_string(r.value("fare", 0)) + ").</p>\n";
        } else {
            h += "<p>Toplu taşıma gerekmez ya da uygun hat yok.</p>\n";
        }
        h += "<link href=\"phone/maps\">Harita</link>\n";
        return h + Foot();
    }
    if (page == "maze") {
        h = Head("Maze Bank") + "<h1>Maze Bank</h1>\n<card title=\"Bakiye\">" + Money(economy::GetCash()) + "</card>\n<h2>Son işlemler</h2>\n";
        const auto tx = economy::History();
        if (tx.empty()) h += "<p>Henüz işlem yok.</p>\n";
        for (auto it = tx.rbegin(); it != tx.rend(); ++it) {
            char t[16];
            std::snprintf(t, sizeof(t), "%02d:%02d", it->hour, it->minute);
            h += "<item>" + std::string(t) + "  " + (it->amount < 0 ? "Harcama " : "Gelir ") + Money(it->amount < 0 ? -it->amount : it->amount) + "</item>\n";
        }
        return h + Foot();
    }
    if (page == "store") {
        h = Head("App Store") + "<h1>App Store</h1>\n<p>İnternet sitelerini ana ekrana kısayol olarak ekle ya da kaldır.</p>\n";
        try {
            std::vector<std::string> sites;
            for (const auto& e : std::filesystem::directory_iterator(std::string(kBaseDir) + "/data/web")) {
                if (e.is_directory()) sites.push_back(e.path().filename().string());
            }
            std::sort(sites.begin(), sites.end());
            for (const auto& s : sites) {
                h += "<link href=\"act:ph:store:" + s + "\">" + Esc(SiteTitle(s)) + " (" + s + ") - " + (HasShortcut(s) ? "Kurulu: kaldır" : "Ekle") + "</link>\n";
            }
        } catch (...) {
            h += "<p>Mağaza şu an kapalı.</p>\n";
        }
        return h + Foot();
    }
    if (page == "internet_yok") {
        h = Head("İnternet") + "<h1>İnternet yok</h1>\n";
        if (g_signal_bars == 0)
            h += "<card title=\"Şebeke yok\">İnternet için şebeke bağlantısı gerekli. Daha iyi sinyal alınan bir yere git.</card>\n" + SignalSvg(0);
        else
            h += "<card title=\"Kota bitti\">Bu dönemde internet sayfası kotanız doldu.</card>\n";
        h += "<link href=\"" + std::string(kOpTable[g_operator].id) + "\">Tarifeyi yenile — " + Esc(kOpTable[g_operator].name) + "</link>\n";
        return h + Foot();
    }
    if (page == "operatör") {
        h = Head("Operatör") + "<h1>Operatör</h1>\n";
        {
            const PkgInfo& cp = CurrentPkg();
            const std::string veri = g_pkg_pages_left < 0 ? "Sınırsız" : DataStr(g_pkg_pages_left);
            const std::string dk   = g_pkg_minutes_left < 0 ? "Sınırsız" : std::to_string(g_pkg_minutes_left) + " dk";
            h += "<card title=\"Mevcut\">" + Esc(kOpTable[g_operator].name) + " — " + Esc(cp.name) + "  " + veri + "  " + dk + "</card>\n";
            h += SignalSvg(g_signal_bars);
        }
        h += "<h2>Operatörler</h2>\n";
        for (int i = 0; i < kOpCount; ++i) {
            const PkgInfo& fp = kOpTable[i].pkgs[0];  // en ucuz paket özeti
            h += "<link href=\"" + std::string(kOpTable[i].id) + "\">" + Esc(kOpTable[i].name) + (g_operator == i ? " ✓ mevcut" : "") + " — $" + std::to_string(fp.price) + " başlangıç  4 paket seçeneği</link>\n";
        }
        return h + Foot();
    }
    return "";
}

// ================= eylemler =================
std::string Action(const std::string& cmd) {
    if (g_slot < 0) return "";
    const size_t c = cmd.find(':');
    const std::string verb = cmd.substr(0, c);
    const std::string arg = c == std::string::npos ? "" : cmd.substr(c + 1);
    if (verb == "dial") return Dial(arg);
    if (verb == "call") {
        if (Lower(arg) == "santral") {
            g_pending_call = "santral";
            return "Santral aranıyor";
        }
        Entry e;
        if (!FindEntry(arg, e)) return "Kişi bulunamadı";
        return CallEntry(e);
    }
    if (verb == "baro") {
        if (!g_deps.jail || !g_deps.jail->HasWantedCase()) return "Aktif bir aranma kaydın yok";
        return CallEntry(Entry{"bar_lawyer", "Los Santos Barosu", "", false, true});
    }
    if (verb == "hvoice") {
        for (const auto& v : kVoices) {
            if (arg == v.name) {
                g_hvoice = arg;
                Save();
                return "@goto:phone/haviland/ses|Haviland sesi: " + arg;
            }
        }
        return "Ses bulunamadı";
    }
    if (verb == "wctoggle" || verb == "wcdel") {
        if (!FindWc(arg)) return "";
        const auto it = std::find(g_wc_sel.begin(), g_wc_sel.end(), arg);
        if (verb == "wcdel" || it != g_wc_sel.end()) {
            if (it != g_wc_sel.end()) g_wc_sel.erase(it);
        } else {
            if (g_wc_sel.size() >= 20) return "En fazla 20 yer";
            g_wc_sel.push_back(arg);
        }
        Save();
        return verb == "wcdel" ? "Kaldırıldı" : "";
    }
    if (verb == "wcadd") {  // ada göre ara (Türkçe harf/büyük-küçük harf duyarsız), ilk eşleşen eklenir
        const std::string q = Places::Fold(arg);
        if (q.size() < 2) return "En az 2 harf yaz";
        for (const auto& w : g_wc_catalog) {
            if (Places::Fold(w.name).find(q) == std::string::npos) continue;
            if (std::find(g_wc_sel.begin(), g_wc_sel.end(), w.id) != g_wc_sel.end()) return w.name + " zaten ekli";
            if (g_wc_sel.size() >= 20) return "En fazla 20 yer";
            g_wc_sel.push_back(w.id);
            Save();
            return "@goto:phone/saat|Eklendi: " + w.name;
        }
        return "Bulunamadı: " + arg;
    }
    if (verb == "alarmadd") {
        int hh = 0, mm = 0;
        std::string rest;
        if (!ParseClock(arg, hh, mm, rest)) return "Saat anlaşılamadı. Ör: 07:30 Kalk";
        if (g_alarms.size() >= 12) return "En fazla 12 alarm";
        if (rest.size() > 30) rest.resize(30);
        g_alarms.push_back({hh, mm, true, rest, -1});
        Save();
        return "@goto:phone/alarm|Alarm kuruldu: " + Two(hh) + ":" + Two(mm);
    }
    if (verb == "alarmtoggle" || verb == "alarmdel") {
        const size_t i = static_cast<size_t>(std::atoi(arg.c_str()));
        if (i >= g_alarms.size()) return "";
        if (verb == "alarmdel") g_alarms.erase(g_alarms.begin() + static_cast<long>(i));
        else g_alarms[i].on = !g_alarms[i].on;
        Save();
        return verb == "alarmdel" ? "Alarm silindi" : "";
    }
    if (verb == "evadd") {  // "15 Doktor" (bu ay) ya da "15.11 Doktor"
        std::string text = arg;
        while (!text.empty() && text.front() == ' ') text.erase(text.begin());
        size_t i = 0;
        std::string a, b;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) a += text[i++];
        if (i < text.size() && (text[i] == '.' || text[i] == '/')) {
            ++i;
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) b += text[i++];
        }
        while (i < text.size() && text[i] == ' ') ++i;
        const int Y = TIME::GET_CLOCK_YEAR();
        const int mo = b.empty() ? TIME::GET_CLOCK_MONTH() % 12 + 1 : std::atoi(b.c_str());
        const int d = std::atoi(a.c_str());
        if (a.empty() || mo < 1 || mo > 12 || d < 1 || d > DaysIn(Y, mo)) return "Tarih anlaşılamadı. Ör: 15 Doktor ya da 15.11 Doktor";
        std::string desc = text.substr(i);
        if (desc.empty()) desc = "Etkinlik";
        if (desc.size() > 40) desc.resize(40);
        if (g_events.size() >= 60) return "En fazla 60 etkinlik";
        g_events.push_back({Y, mo, d, desc});
        Save();
        return "@goto:phone/takvim|Etkinlik eklendi: " + Two(d) + "." + Two(mo);
    }
    if (verb == "evdel") {
        const size_t i = static_cast<size_t>(std::atoi(arg.c_str()));
        if (i < g_events.size()) g_events.erase(g_events.begin() + static_cast<long>(i));
        Save();
        return "Etkinlik silindi";
    }
    if (verb == "noteadd") {
        std::string n = arg;
        while (!n.empty() && n.front() == ' ') n.erase(n.begin());
        if (n.empty()) return "";
        if (n.size() > 200) n.resize(200);
        if (g_notes.size() >= 40) return "En fazla 40 not";
        g_notes.push_back(n);
        Save();
        return "@goto:phone/notlar|Not kaydedildi";
    }
    if (verb == "notedel") {
        const size_t i = static_cast<size_t>(std::atoi(arg.c_str()));
        if (i < g_notes.size()) g_notes.erase(g_notes.begin() + static_cast<long>(i));
        Save();
        return "Not silindi";
    }
    if (verb == "haviland") {
        g_pending_call = "haviland";
        return "Haviland dinliyor";
    }
    if (verb == "resave") {
        const int n = g_deps.missions ? g_deps.missions->RestoreContacts() : 0;
        return n ? std::to_string(n) + " karakter rehbere yeniden kaydedildi" : "Eksik karakter yok";
    }
    if (verb == "del") {
        Entry e;
        if (!FindEntry(arg, e) || !e.deletable) return "Silinemez";
        if (e.story) {
            if (g_deps.missions) g_deps.missions->RemoveContact(e.id);
        } else {
            g_people.erase(std::remove_if(g_people.begin(), g_people.end(), [&](const Person& p) { return Lower(p.id) == Lower(e.id); }), g_people.end());
            Save();
        }
        return "@goto:phone/rehber|" + e.name + " rehberden silindi";
    }
    if (verb == "rename") {  // "<kimlik>:<yeni ad>"; boş ad = özgün ada dön
        const size_t cc = arg.find(':');
        if (cc == std::string::npos) return "";
        const std::string id = Lower(arg.substr(0, cc));
        std::string nm;
        for (char ch : arg.substr(cc + 1)) {
            if (static_cast<unsigned char>(ch) >= 32 && ch != '<' && ch != '>' && ch != '"') nm += ch;
        }
        while (!nm.empty() && nm.front() == ' ') nm.erase(nm.begin());
        while (!nm.empty() && nm.back() == ' ') nm.pop_back();
        Entry e;
        if (!FindEntry(id, e) || !e.deletable) return "Bu kişinin adı değiştirilemez";
        if (nm.size() > 40) nm.resize(40);
        if (nm.empty()) g_names.erase(id);
        else g_names[id] = nm;
        Save();
        return "@goto:phone/kisi/" + id + "|" + (nm.empty() ? std::string("Özgün ada dönüldü") : "Ad değiştirildi: " + nm);
    }
    if (verb == "newcontact") {
        // "Ad Soyad 3105550123": son sözcük numara, kalanı ad
        std::string text = arg;
        while (!text.empty() && text.back() == ' ') text.pop_back();
        const size_t sp = text.find_last_of(' ');
        const std::string name = sp == std::string::npos ? "" : text.substr(0, sp);
        const std::string number = NormalizeDialed(sp == std::string::npos ? text : text.substr(sp + 1));
        if (number.empty()) return "Geçersiz numara. Ör: Ali Veli 310 555 0123 (ABD biçimi)";
        const std::string nm = name.empty() ? "Kayıtsız" : name;
        for (const auto& p : g_people) {
            if (p.number == number) return p.name + " zaten kayıtlı";
        }
        AddPerson(nm, number, json());
        return "@goto:phone/rehber|" + nm + " kaydedildi: " + Format(number);
    }
    if (verb == "nav") {
        if (!g_deps.places) return "Harita şu an yok";
        const Vector3 me = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true);
        Place pl;
        if (!g_deps.places->Resolve(arg, me, pl)) return "Yer bulunamadı: " + arg;
        g_nav.active = true;
        g_nav.name = pl.name;
        g_nav.pos = pl.pos;
        UI::SET_NEW_WAYPOINT(pl.pos.x, pl.pos.y);
        return "@goto:phone/maps/rota|Rota hazır: " + pl.name;
    }
    if (verb == "navclear") {
        g_nav.active = false;
        UI::SET_WAYPOINT_OFF();
        return "İşaret kaldırıldı";
    }
    if (verb == "store") {
        const auto it = std::find(g_shortcuts.begin(), g_shortcuts.end(), arg);
        const bool had = it != g_shortcuts.end();
        if (had) g_shortcuts.erase(it);
        else g_shortcuts.push_back(arg);
        Save();
        return had ? "Kısayol kaldırıldı" : "Ana ekrana eklendi";
    }
    if (verb == "op_buy") {  // act:ph:op_buy:<op>:<tier>
        const size_t sc = arg.find(':');
        const int op   = std::atoi(arg.c_str());
        const int tier = sc == std::string::npos ? 0 : std::atoi(arg.substr(sc + 1).c_str());
        if (op < 0 || op >= kOpCount || tier < 0 || tier > 3) return "Geçersiz paket";
        const PkgInfo& pk = kOpTable[op].pkgs[tier];
        if (economy::GetCash() < pk.price) return "Para yetersiz ($" + std::to_string(pk.price) + " gerekli)";
        
        // Zaten bu operatördeyse paketini doğrudan değiştir
        if (g_operator == op) {
            economy::Spend(pk.price);
            g_pkg_tier = tier;
            g_pkg_pages_left   = pk.pages;
            g_pkg_minutes_left = pk.minutes;
            g_pkg_renew_day    = GameDay();
            Save();
            return "@goto:" + std::string(kOpTable[op].id) + "|" + Esc(kOpTable[op].name) + " " + pk.name + " paketi aktif edildi";
        }

        // FARKLI OPERATÖRE GEÇİŞ: Web sayfasından talep oluşturulur ve operatör geri arar
        g_switch_pending = true;
        g_switch_op = op;
        g_switch_tier = tier;
        g_switch_call_time = GetTickCount64() + 8000ULL;  // 8 sn sonra müşteri hizmetleri arar
        g_switch_ring_ready = false;
        Save();
        return "@goto:" + std::string(kOpTable[op].id) + "|Geçiş talebiniz alındı! " + Esc(kOpTable[op].name) + " müşteri temsilcisi onay için birazdan sizi arayacak.";
    }
    if (verb == "op_renew") {  // act:ph:op_renew
        const PkgInfo& pk = CurrentPkg();
        if (economy::GetCash() < pk.price) return "Para yetersiz ($" + std::to_string(pk.price) + " gerekli)";
        economy::Spend(pk.price);
        g_pkg_pages_left   = pk.pages;
        g_pkg_minutes_left = pk.minutes;
        g_pkg_renew_day    = GameDay();
        Save();
        return "@goto:" + std::string(kOpTable[g_operator].id) + "|" + Esc(kOpTable[g_operator].name) + " " + pk.name + " yenilendi";
    }
    return "";
}

// ---- Şebeke public API ----
int SignalBars()          { return g_slot >= 0 ? g_signal_bars : 0; }
std::string SignalText()  { return SignalStr(SignalBars()); }
std::string OperatorName(){ return g_slot >= 0 ? std::string(kOpTable[g_operator].name) : ""; }
bool HasSignal()          { return SignalBars() > 0; }
bool InternetSlow()       { return SignalBars() > 0 && SignalBars() <= 2; }
int PkgPagesLeft()        { return g_slot >= 0 ? g_pkg_pages_left : 0; }
int PkgMinutesLeft()      { return g_slot >= 0 ? g_pkg_minutes_left : 0; }

// Bir internet sayfası yüklendi — kota düşür (operatör siteleri intranet: kota harcanmaz).
// Maliyet sayfa kaynağının bayt boyutuna göre: 2 KB kaynak ≈ 1 MB veri (yazı yoğunluğu), en az 1 MB.
void ConsumeInternetPage(const std::string& url, int src_bytes) {
    if (g_slot < 0) return;
    // Operatör sitesi intranet: kota harcanmaz
    for (int i = 0; i < kOpCount; ++i)
        if (url == kOpTable[i].id || url.rfind(std::string(kOpTable[i].id) + "/", 0) == 0) return;
    if (g_pkg_pages_left > 0) {
        const int mb = std::max(1, (src_bytes + 2047) / 2048);  // tavan: 2 KB kaynak = 1 MB
        g_pkg_pages_left = std::max(0, g_pkg_pages_left - mb);
        Save();
    }
    // g_pkg_pages_left == -1 ise sınırsız: düşürme
}

// İnternet erişimi var mı? Operatör siteleri intranet (her zaman erişilebilir).
bool InternetAllowed(const std::string& url) {
    if (g_slot < 0) return false;
    for (int i = 0; i < kOpCount; ++i)
        if (url == kOpTable[i].id || url.rfind(std::string(kOpTable[i].id) + "/", 0) == 0) return true;  // intranet
    return g_signal_bars > 0 && (g_pkg_pages_left != 0);  // -1=sınırsız, >0=kota var
}

// ---- Operatör Geçiş & Arama API ----
bool ConsumeIncomingCall(Missions::Talk& out) {
    if (!g_switch_pending || !g_switch_ring_ready || g_switch_op < 0 || g_switch_op >= kOpCount) return false;
    const int op = g_switch_op;
    const int tier = (g_switch_tier >= 0 && g_switch_tier <= 3) ? g_switch_tier : 0;
    const PkgInfo& pk = kOpTable[op].pkgs[tier];

    out.npc_id = "op_switch:" + std::to_string(op) + ":" + std::to_string(tier);
    out.name = std::string(kOpTable[op].name) + " Müşteri Hizmetleri";
    out.model = "s_m_m_autoshop_01";
    out.ped = 0;

    json npc_prof = {
        {"name", std::string(kOpTable[op].name) + " Müşteri Hizmetleri"},
        {"job", "Mobil Hat Temsilcisi"},
        {"city", "Los Santos"},
        {"country", "Amerika"},
        {"voice", json{{"speaker", op == 0 ? "Claribel Dervla" : (op == 1 ? "Damien Black" : "Daisy Studious")}}}
    };

    std::string intro = "Merhaba! " + std::string(kOpTable[op].name) + " Los Santos merkezinden arıyorum. "
        "Numara taşıma ve operatör geçiş talebinizi aldık. Amerika'nın ve Los Santos'un en kaliteli şebekesine hoş geldiniz! "
        "Seçtiğiniz " + std::string(pk.name) + " tarifesi onaylanmış ve hattınıza tanımlanmıştır.";

    out.npc_json = {
        {"id", out.npc_id},
        {"personality", std::string(kOpTable[op].name) + " müşteri temsilcisisin. Los Santos, Amerika'da kurumsal ve kibar bir müşteri temsilcisi gibi konuş. Karşındaki müşteriye geçişinin tamamlandığını ve Los Santos'ta iyi günler dilediğini söyle."},
        {"profile", npc_prof},
        {"intro", intro}
    };

    g_switch_ring_ready = false;
    g_switch_call_time = GetTickCount64() + 25000ULL;  // cevaplanmazsa 25 sn sonra tekrar
    return true;
}

void ConfirmSwitch() {
    if (!g_switch_pending || g_switch_op < 0 || g_switch_op >= kOpCount) return;
    const int op = g_switch_op;
    const int tier = (g_switch_tier >= 0 && g_switch_tier <= 3) ? g_switch_tier : 0;
    const PkgInfo& pk = kOpTable[op].pkgs[tier];

    if (economy::GetCash() >= pk.price) {
        economy::Spend(pk.price);
    }
    g_operator = op;
    g_pkg_tier = tier;
    g_pkg_pages_left   = pk.pages;
    g_pkg_minutes_left = pk.minutes;
    g_pkg_renew_day    = GameDay();
    g_switch_pending   = false;
    g_switch_op        = -1;
    g_switch_tier      = -1;
    g_switch_ring_ready = false;
    g_signal_bars = CalcSignalBars(ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true), g_operator);
    Save();
    karma::Toast(std::string(kOpTable[g_operator].name) + " şebekesine geçiş tamamlandı! Hoş geldiniz.", 6);
}

void RetrySwitchCall() {
    if (g_switch_pending) {
        g_switch_ring_ready = false;
        g_switch_call_time = GetTickCount64() + 15000ULL; // 15 sn sonra tekrar ara
    }
}

bool HasPendingSwitch() {
    return g_switch_pending;
}

}  // namespace hikaye::phoneos
