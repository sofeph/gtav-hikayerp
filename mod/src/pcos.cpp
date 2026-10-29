#include "pcos.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "karma.h"
#include "minigames.h"
#include "phoneos.h"
#include "transit.h"

namespace hikaye::pcos {

using nlohmann::json;

namespace {
Mode g_mode = Mode::None;
Missions::InstallSpec g_spec;
bool g_install_req = false, g_laptop_req = false, g_game = false;
bool g_sitting = false;  // evde oturma sahnesi başlatıldı
int g_slot = -1;

struct Mail {
    std::string from, subject, body;
    bool read = false;
};
std::vector<Mail> g_mail;
std::vector<std::string> g_notes;
bool g_welcomed = false;

std::string SavePath(int slot) { return std::string(kBaseDir) + "/saves/pc_" + std::to_string(slot) + ".json"; }

float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

std::string Esc(const std::string& s) {
    std::string o;
    for (char c : s) o += c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '&' ? "&amp;" : c == '"' ? "&quot;" : std::string(1, c);
    return o;
}

void Save() {
    if (g_slot < 0) return;
    try {
        std::filesystem::create_directories(std::string(kBaseDir) + "/saves");
        json mails = json::array();
        for (const auto& m : g_mail) mails.push_back({{"from", m.from}, {"subject", m.subject}, {"body", m.body}, {"read", m.read}});
        std::ofstream out(SavePath(g_slot));
        out << json{{"mail", mails}, {"notes", g_notes}, {"welcomed", g_welcomed},
                    {"best_snake", minigames::Best(minigames::Id::Snake)}, {"best_td", minigames::Best(minigames::Id::TopDown)}}.dump(1);
    } catch (const std::exception& e) {
        Log(std::string("bilgisayar kaydi yazilamadi: ") + e.what());
    }
}

// Dizüstü gücü: çakmaklık (otomobil/kamyon, kontak açık), otobüs/metro (USB), ev (priz). Market ve sokakta çalışmaz.
// Dönüş: kaynak adı ("" = güç yok). at_home: evde ve araçta değil (oturma sahnesi gerekir).
std::string PowerAt(const Vector3& pos, const std::vector<Vector3>& homes, bool& at_home, bool& moving_driver) {
    at_home = false;
    moving_driver = false;
    if (transit::OnTransit()) return "Otobüs/metro";
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (PED::IS_PED_IN_ANY_VEHICLE(player, false)) {
        const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(player, false);
        const Hash m = ENTITY::GET_ENTITY_MODEL(v);
        const bool lighter = VEHICLE::IS_THIS_MODEL_A_CAR(m) && !VEHICLE::IS_THIS_MODEL_A_BIKE(m) && !VEHICLE::IS_THIS_MODEL_A_BICYCLE(m) &&
                             !VEHICLE::IS_THIS_MODEL_A_QUADBIKE(m) && !VEHICLE::IS_THIS_MODEL_A_BOAT(m);
        if (lighter && VEHICLE::_IS_VEHICLE_ENGINE_ON(v)) {
            moving_driver = VEHICLE::GET_PED_IN_VEHICLE_SEAT(v, -1) == player && ENTITY::GET_ENTITY_SPEED(v) > 3.f;
            return "Araç çakmaklığı";
        }
        return "";
    }
    if (phoneos::AtHome(pos, homes)) {
        at_home = true;
        return "Ev (priz)";
    }
    return "";
}

void StandUp() {
    if (g_sitting) {
        AI::CLEAR_PED_TASKS(PLAYER::PLAYER_PED_ID());
        g_sitting = false;
    }
}
}  // namespace

void LoadSave(int slot) {
    if (g_slot >= 0) Save();
    g_slot = slot;
    g_mail.clear();
    g_notes.clear();
    g_welcomed = false;
    try {
        std::ifstream in(SavePath(slot));
        if (!in) return;
        const json j = json::parse(in);
        for (const auto& m : j.value("mail", json::array())) g_mail.push_back({m.value("from", ""), m.value("subject", ""), m.value("body", ""), m.value("read", false)});
        g_notes = j.value("notes", std::vector<std::string>());
        g_welcomed = j.value("welcomed", false);
        minigames::SetBest(minigames::Id::Snake, j.value("best_snake", 0));
        minigames::SetBest(minigames::Id::TopDown, j.value("best_td", 0));
    } catch (const std::exception& e) {
        Log(std::string("bilgisayar kaydi okunamadi: ") + e.what());
    }
}

void Shutdown() {
    Close();
    Save();
    g_slot = -1;
}

void PushMail(const std::string& from, const std::string& subject, const std::string& body) {
    g_mail.push_back({from, subject, body, false});
    if (g_mail.size() > 40) g_mail.erase(g_mail.begin());
    Save();
}

void OpenWork(const Missions::InstallSpec& spec) {
    g_spec = spec;
    g_mode = Mode::Work;
    g_game = false;
}

void RequestLaptop() { g_laptop_req = true; }
bool ConsumeLaptopRequest() {
    const bool r = g_laptop_req;
    g_laptop_req = false;
    return r;
}

bool TryOpenLaptop(const std::vector<Vector3>& homes, std::string& why) {
    const Vector3 pos = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true);
    bool at_home = false, moving = false;
    const std::string src = PowerAt(pos, homes, at_home, moving);
    if (src.empty()) {
        why = "Dizüstü sadece güç kaynağında çalışır ve ayakta kullanılmaz: evde otururken, araçta (kontak açık) ya da otobüste kullan.";
        return false;
    }
    if (moving) {
        why = "Araç hareket halindeyken dizüstü kullanılmaz: dur ya da yolcu ol.";
        return false;
    }
    if (at_home) {  // evde: oturma sahnesi
        AI::TASK_START_SCENARIO_IN_PLACE(PLAYER::PLAYER_PED_ID(), const_cast<char*>("PROP_HUMAN_SEAT_ARMCHAIR"), 0, true);
        g_sitting = true;
    }
    g_mode = Mode::Laptop;
    g_game = false;
    if (!g_welcomed) {
        g_welcomed = true;
        PushMail("Maze Bank", "Hesabınız hazır", "Merhaba, Maze Bank mobil uygulaması telefonunuzda etkinleştirildi. Hesap özetinize telefondan Maze Bank uygulamasıyla ulaşabilirsiniz.");
        PushMail("App Store", "Hoş geldiniz", "İnternet sitelerini telefonunuzun ana ekranına kısayol olarak ekleyebilirsiniz.");
    }
    return true;
}

bool Active() { return g_mode != Mode::None; }
Mode CurrentMode() { return g_mode; }

void Close() {
    if (g_game) {
        g_game = false;
    }
    StandUp();
    g_mode = Mode::None;
}

void Update(const Vector3& pos, const std::vector<Vector3>& homes) {
    if (g_mode != Mode::Laptop) return;
    bool at_home = false, moving = false;
    const std::string src = PowerAt(pos, homes, at_home, moving);
    if (src.empty()) {
        Close();
        karma::Toast("Dizüstünün gücü kesildi: güç kaynağı yok", 4);
        return;
    }
    if (moving) {
        Close();
        karma::Toast("Araç hareket ediyor: dizüstü kapandı", 4);
        return;
    }
    if (at_home && !g_sitting) {
        Close();
        karma::Toast("Ayağa kalktın: dizüstü kapandı", 4);
    }
}

bool ConsumeInstall(Missions::InstallSpec& out) {
    if (!g_install_req) return false;
    g_install_req = false;
    out = g_spec;
    return true;
}

bool GameActive() { return g_game && minigames::Active(); }

bool UpdateGame() {
    if (!GameActive()) return false;
    if (minigames::Update()) {
        g_game = false;
        Save();
        return true;
    }
    return false;
}

overlay::PhoneView GameView(const std::string& clock) {
    overlay::PhoneView v;
    v.open = true;
    v.web = true;
    v.pc = true;
    v.clock = clock;
    const minigames::Frame f = minigames::Draw();
    v.web_url = "pc://oyun/" + f.title;
    v.web_title = f.title;
    overlay::WebItem g;
    g.kind = 8;
    g.gw = static_cast<float>(f.w);
    g.gh = static_cast<float>(f.h);
    g.gbg = f.bg;
    g.shapes = f.shapes;
    v.web_items.push_back(std::move(g));
    return v;
}

// ================= sayfalar =================
std::string BuildPage(const std::string& url) {
    if (url.rfind("pc/", 0) != 0 || g_mode == Mode::None) return "";
    const std::string page = url.substr(3);
    const auto head = [](const std::string& t) { return "<page title=\"" + Esc(t) + "\" style=\"pc.hss\">\n"; };
    const auto foot = []() { return std::string("<hr/>\n<link href=\"pc/home\">Masaüstü</link>\n</page>\n"); };
    std::string h;
    if (page == "home") {
        if (g_mode == Mode::Work) {
            h = head("Bilgisayar") + "<h1>Bilgisayar</h1>\n<p>İş bilgisayarı</p>\n<tile icon=\"@\" color=\"#e8833a\" href=\"home\">İnternet</tile>\n<tile icon=\"K\" color=\"#2fbf71\" href=\"act:pc:install\">Kurulum</tile>\n</page>\n";
            return h;
        }
        int unread = 0;
        for (const auto& m : g_mail) unread += m.read ? 0 : 1;
        // İnternet: telefon tethering gerekli; sinyal / kota kontrol
        const std::string inet = (phoneos::SignalBars() == 0 || phoneos::PkgPagesLeft() == 0)
                                     ? "pc/internet_yok"
                                     : (phoneos::InternetSlow() ? "pc/internet_yavas" : "home");
        h = head("Dizüstü") + "<h1>Dizüstü</h1>\n<tile icon=\"O\" color=\"#9b6bff\" href=\"pc/games\">Oyun</tile>\n<tile icon=\"F\" color=\"#2aa7c9\" href=\"pc/office\">Facade Office</tile>\n<tile icon=\"@\" color=\"#e8833a\" href=\"" + inet + "\">İnternet</tile>\n";
        h += "<tile icon=\"M\" color=\"#4c8dff\" href=\"pc/mail\">Mail" + std::string(unread ? " (" + std::to_string(unread) + ")" : "") + "</tile>\n</page>\n";
        return h;
    }
    if (g_mode != Mode::Laptop) return "";
    if (page == "games") {
        h = head("Oyun") + "<h1>Oyun</h1>\n";
        h += "<link href=\"act:pc:play:snake\">Yılan  (en iyi " + std::to_string(minigames::Best(minigames::Id::Snake)) + ")</link>\n";
        h += "<link href=\"act:pc:play:topdown\">Tepeden Suç  (en iyi $" + std::to_string(minigames::Best(minigames::Id::TopDown)) + ")</link>\n";
        h += "<p>SNES emülatörü şimdilik yok.</p>\n";
        return h + foot();
    }
    if (page == "office") {
        h = head("Facade Office") + "<h1>Facade Office</h1>\n<link href=\"pc/notes\">Notlar</link>\n<link href=\"askact:pc:calc:{}\">Hesap makinesi (ör. 12*7+3)</link>\n";
        return h + foot();
    }
    if (page == "notes") {
        h = head("Notlar") + "<h1>Notlar</h1>\n<link href=\"askact:pc:addnote:{}\">+ Yeni not</link>\n";
        for (size_t i = 0; i < g_notes.size(); ++i) h += "<link href=\"act:pc:delnote:" + std::to_string(i) + "\">" + Esc(g_notes[i]) + "  (sil)</link>\n";
        if (g_notes.empty()) h += "<p>Not yok.</p>\n";
        return h + foot();
    }
    if (page == "mail") {
        h = head("Mail") + "<h1>Mail</h1>\n";
        for (size_t i = g_mail.size(); i-- > 0;) {
            h += "<link href=\"pc/mail/" + std::to_string(i) + "\">" + std::string(g_mail[i].read ? "" : "[Yeni] ") + Esc(g_mail[i].from) + ": " + Esc(g_mail[i].subject) + "</link>\n";
        }
        if (g_mail.empty()) h += "<p>Gelen kutusu boş.</p>\n";
        return h + foot();
    }
    if (page.rfind("mail/", 0) == 0) {
        const size_t i = static_cast<size_t>(std::atoi(page.c_str() + 5));
        if (i >= g_mail.size()) return "";
        g_mail[i].read = true;
        h = head(g_mail[i].subject) + "<h1>" + Esc(g_mail[i].subject) + "</h1>\n<p>Gönderen: " + Esc(g_mail[i].from) + "</p>\n<card title=\"Mesaj\">" + Esc(g_mail[i].body) + "</card>\n";
        h += "<link href=\"act:pc:delmail:" + std::to_string(i) + "\">Sil</link>\n<link href=\"pc/mail\">Gelen kutusu</link>\n";
        return h + foot();
    }
    if (page == "internet_yok") {
        h = head("İnternet") + "<h1>İnternet yok</h1>\n";
        if (phoneos::SignalBars() == 0)
            h += "<card title=\"Tethering yok\">Telefon şebekesi yok. Dizüstü internete telefonun mobil verisi üzerinden bağlanır.</card>\n";
        else
            h += "<card title=\"Kota bitti\">Bu dönemde internet sayfası kotanız doldu.</card>\n";
        // Operatör sitesi URL'si: OperatorName "Graham Telecom" -> "grahamtelecom.com" doğrudan gezinmek için
        const std::string op_url = phoneos::SignalBars() == 0 ? "pc/home" :
            (phoneos::OperatorName() == "Whiz" ? "whiz.com" :
             phoneos::OperatorName() == "Graham Telecom" ? "grahamtelecom.com" : "modatel.com");
        h += "<link href=\"" + op_url + "\">Tarifeyi yenile — " + Esc(phoneos::OperatorName()) + "</link>\n";
        return h + foot();
    }
    if (page == "internet_yavas") {
        h = head("İnternet") + "<h1>Zayıf bağlantı</h1>\n";
        const int bars = phoneos::SignalBars();
        h += "<card title=\"Tethering\">" + std::string(bars >= 3 ? "▄▃▂▁" : bars == 2 ? "░▃▂▁" : "░░▂▁") + " Telefon şebekesi zayıf; internet yavaş çalışabilir.</card>\n";
        h += "<link href=\"home\">Devam et (yavaş bağlantı)</link>\n";
        return h + foot();
    }
    return "";
}

// ================= eylemler =================
namespace {
// Basit hesap makinesi: + - * / ve ondalık, soldan sağa öncelikli (* / önce)
bool Eval(const std::string& s, double& out) {
    std::vector<double> nums;
    std::vector<char> ops;
    size_t i = 0;
    const auto num = [&](double& v) {
        while (i < s.size() && s[i] == ' ') ++i;
        const char* start = s.c_str() + i;
        char* end = nullptr;
        v = std::strtod(start, &end);
        if (end == start) return false;
        i += static_cast<size_t>(end - start);
        return true;
    };
    double v;
    if (!num(v)) return false;
    nums.push_back(v);
    while (true) {
        while (i < s.size() && s[i] == ' ') ++i;
        if (i >= s.size()) break;
        const char op = s[i++];
        if (op != '+' && op != '-' && op != '*' && op != '/' && op != 'x') return false;
        if (!num(v)) return false;
        if (op == '*' || op == '/' || op == 'x') {
            if ((op == '/') && v == 0) return false;
            nums.back() = op == '/' ? nums.back() / v : nums.back() * v;
        } else {
            ops.push_back(op);
            nums.push_back(v);
        }
    }
    double r = nums[0];
    for (size_t k = 0; k < ops.size(); ++k) r = ops[k] == '+' ? r + nums[k + 1] : r - nums[k + 1];
    out = r;
    return true;
}
}  // namespace

std::string Action(const std::string& cmd) {
    const size_t c = cmd.find(':');
    const std::string verb = cmd.substr(0, c);
    const std::string arg = c == std::string::npos ? "" : cmd.substr(c + 1);
    if (verb == "install" && g_mode == Mode::Work) {
        g_install_req = true;
        return "Kurulum başlıyor";
    }
    if (verb == "play") {
        g_game = true;
        minigames::Start(arg == "snake" ? minigames::Id::Snake : minigames::Id::TopDown);
        return "";
    }
    if (verb == "addnote") {
        if (arg.empty()) return "";
        g_notes.push_back(arg.substr(0, 120));
        if (g_notes.size() > 30) g_notes.erase(g_notes.begin());
        Save();
        return "@goto:pc/notes|Not kaydedildi";
    }
    if (verb == "delnote") {
        const size_t i = static_cast<size_t>(std::atoi(arg.c_str()));
        if (i < g_notes.size()) g_notes.erase(g_notes.begin() + static_cast<long>(i));
        Save();
        return "Not silindi";
    }
    if (verb == "delmail") {
        const size_t i = static_cast<size_t>(std::atoi(arg.c_str()));
        if (i < g_mail.size()) g_mail.erase(g_mail.begin() + static_cast<long>(i));
        Save();
        return "@goto:pc/mail|Mail silindi";
    }
    if (verb == "calc") {
        double r = 0;
        if (!Eval(arg, r)) return "Hesaplanamadı. Ör: 12*7+3";
        char b[64];
        std::snprintf(b, sizeof(b), "%s = %.10g", arg.c_str(), r);
        return b;
    }
    return "";
}

}  // namespace hikaye::pcos
