#include "estate.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "binco.h"
#include "blips.h"
#include "economy.h"
#include "json.hpp"
#include "karma.h"
#include "overlay.h"

namespace hikaye {

using nlohmann::json;

namespace {
constexpr float kDoorRadius = 3.2f;
constexpr int kRentDays = 30;
std::string SavePath(int slot) { return std::string(kBaseDir) + "/saves/estate_" + std::to_string(slot) + ".json"; }
float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
std::string Money(int v) {
    std::string d = std::to_string(v), g;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) g += '.';
        g += d[i];
    }
    return "$" + g;
}
}  // namespace

void Estate::Load() {
    houses_.clear();
    try {
        std::ifstream in(std::string(kBaseDir) + "/data/places/houses.json");
        if (!in) {
            Log("houses.json yok (tools/fetch_houses.py ile uretilir)");
            return;
        }
        const json j = json::parse(in);
        for (const auto& it : j.value("items", json::array())) {
            House h;
            h.id = it.value("id", "");
            h.label = it.value("label", h.id);
            const auto& p = it["pos"];
            h.pos = {p[0].get<float>(), 0, p[1].get<float>(), 0, p[2].get<float>(), 0};
            if (it.contains("garage") && it["garage"].is_array()) {
                const auto& g = it["garage"];
                h.garage = {g[0].get<float>(), 0, g[1].get<float>(), 0, g[2].get<float>(), 0};
                h.has_garage = true;
            }
            h.heading = it.value("heading", 0.0f);
            h.tier = it.value("tier", 3);
            h.price = it.value("price", 50000);
            h.rent = it.value("rent", 450);
            if (!h.id.empty()) houses_.push_back(std::move(h));
        }
    } catch (const std::exception& e) {
        Log(std::string("estate.Load HATA: ") + e.what());
    }
    Log("emlak: " + std::to_string(houses_.size()) + " ev");
    LoadInteriors();
}

void Estate::LoadInteriors() {
    try {
        std::ifstream in(std::string(kBaseDir) + "/data/home_interiors.json");
        if (in) home_cfg_ = json::parse(in);
    } catch (const std::exception& e) {
        Log(std::string("home_interiors.json okunamadi: ") + e.what());
    }
}

// Ev puanı: fiyat/price_div + yükseklik (tepe/üst kat) bonusu + bölge bonusu -> eşiklerle iç mekan sırası (0 düşük ... 3 lüks)
int Estate::InteriorIndexFor(const House& h) const {
    const json& sc = home_cfg_.value("score", json::object());
    float score = static_cast<float>(h.price) / std::max(1.f, sc.value("price_div", 1000.f));
    const float z = h.pos.z;
    if (z >= sc.value("z_high", 120.f)) score += sc.value("z_high_bonus", 22.f);
    else if (z >= sc.value("z_mid", 70.f)) score += sc.value("z_mid_bonus", 8.f);
    for (const auto& zn : home_cfg_.value("zones", json::array())) {
        if (!zn.contains("bounds") || !zn["bounds"].is_array() || zn["bounds"].size() < 4) continue;
        const auto& b = zn["bounds"];
        if (h.pos.x >= b[0].get<float>() && h.pos.y >= b[1].get<float>() && h.pos.x <= b[2].get<float>() && h.pos.y <= b[3].get<float>()) score += zn.value("bonus", 0.f);
    }
    int idx = 0;
    const json th = sc.value("thresholds", json::array({60, 80, 100}));
    for (const auto& t : th) {
        if (score >= t.get<float>()) ++idx;
    }
    const int n = static_cast<int>(home_cfg_.value("interiors", json::array()).size());
    return n == 0 ? -1 : std::min(idx, n - 1);
}

bool Estate::EnterHouse(const House& h) {
    const int idx = InteriorIndexFor(h);
    if (idx < 0) {
        karma::Toast("Ev içi tanımlı değil (data/home_interiors.json)", 5);
        return false;
    }
    const json& in = home_cfg_["interiors"][static_cast<size_t>(idx)];
    for (const auto& ip : in.value("ipl", json::array())) {
        const std::string name = ip.get<std::string>();
        STREAMING::REQUEST_IPL(const_cast<char*>(name.c_str()));
        if (std::find(ipls_requested_.begin(), ipls_requested_.end(), name) == ipls_requested_.end()) ipls_requested_.push_back(name);
    }
    const auto& pt = in["inside"];
    const float x = pt[0].get<float>(), y = pt[1].get<float>(), z = pt[2].get<float>(), hd = pt.size() > 3 ? pt[3].get<float>() : 0.f;
    const Ped ped = PLAYER::PLAYER_PED_ID();
    CAM::DO_SCREEN_FADE_OUT(400);
    for (int i = 0; i < 30 && !CAM::IS_SCREEN_FADED_OUT(); ++i) WAIT(0);
    // IPL yüklenmesi için kısa bekleme; sonra iç mekanı sabitle
    for (int i = 0; i < 90; ++i) {
        STREAMING::REQUEST_COLLISION_AT_COORD(x, y, z);
        WAIT(0);
    }
    const int interior = INTERIOR::GET_INTERIOR_AT_COORDS(x, y, z);
    if (interior == 0 || !INTERIOR::IS_VALID_INTERIOR(interior)) {
        CAM::DO_SCREEN_FADE_IN(400);
        Log("ev ici: ic mekan bulunamadi (" + in.value("id", "?") + "), isinlanmadi");
        karma::Toast("Ev içi yüklenemedi (IPL/koordinat): data/home_interiors.json", 6);
        return false;
    }
    INTERIOR::_0x2CA429C029CCF247(interior);  // PIN_INTERIOR_IN_MEMORY
    STREAMING::SET_INTERIOR_ACTIVE(interior, true);
    INTERIOR::REFRESH_INTERIOR(interior);
    for (int i = 0; i < 180 && !INTERIOR::IS_INTERIOR_READY(interior); ++i) {
        STREAMING::REQUEST_COLLISION_AT_COORD(x, y, z);
        WAIT(0);
    }
    ENTITY::FREEZE_ENTITY_POSITION(ped, true);
    ENTITY::SET_ENTITY_COORDS(ped, x, y, z, false, false, false, true);
    ENTITY::SET_ENTITY_HEADING(ped, hd);
    for (int i = 0; i < 120 && !ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(ped); ++i) {
        STREAMING::REQUEST_COLLISION_AT_COORD(x, y, z);
        WAIT(0);
    }
    ENTITY::FREEZE_ENTITY_POSITION(ped, false);
    CAM::DO_SCREEN_FADE_IN(500);
    inside_ = true;
    inside_menu_ = false;
    inside_house_ = h.id;
    inside_interior_ = idx;
    inside_spawn_ = {x, 0, y, 0, z, 0};
    Log("ev ici: " + h.id + " -> " + in.value("id", "?") + " (kademe " + std::to_string(idx) + ")");
    karma::Toast(in.value("name", std::string("Evin içi")) + " (" + h.label + ")", 5);
    return true;
}

void Estate::ExitHouse() {
    const House* h = Find(inside_house_);
    const Ped ped = PLAYER::PLAYER_PED_ID();
    CAM::DO_SCREEN_FADE_OUT(400);
    for (int i = 0; i < 30 && !CAM::IS_SCREEN_FADED_OUT(); ++i) WAIT(0);
    if (h) {
        ENTITY::FREEZE_ENTITY_POSITION(ped, true);
        ENTITY::SET_ENTITY_COORDS(ped, h->pos.x, h->pos.y, h->pos.z, false, false, false, true);
        ENTITY::SET_ENTITY_HEADING(ped, h->heading);
        for (int i = 0; i < 120 && !ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(ped); ++i) {
            STREAMING::REQUEST_COLLISION_AT_COORD(h->pos.x, h->pos.y, h->pos.z);
            WAIT(0);
        }
        ENTITY::FREEZE_ENTITY_POSITION(ped, false);
    }
    CAM::DO_SCREEN_FADE_IN(500);
    inside_ = false;
    inside_menu_ = false;
    inside_house_.clear();
    inside_interior_ = -1;
    overlay::SetMenu({});
}

void Estate::UpdateInside(const Vector3& pos, bool e_pressed) {
    const Ped player = PLAYER::PLAYER_PED_ID();
    // güvenlik: ölürsen ya da iç mekandan çok uzaklaşırsan (ışınlanma, yanlış koordinat) ev içi durumu kapanır
    if (ENTITY::IS_ENTITY_DEAD(player) || D2(pos, inside_spawn_) > 150.f) {
        inside_ = false;
        inside_menu_ = false;
        overlay::SetMenu({});
        return;
    }
    const json& in = home_cfg_["interiors"][static_cast<size_t>(std::max(0, inside_interior_))];
    if (binco::MenuOpen()) return;  // gardırop menüsü açıkken ev menüsü kapalı kalsın
    if (!inside_menu_) {
        ShowHelp("~INPUT_PICKUP~ Ev: uyu / gardirop / disari cik");
        if (e_pressed) {
            inside_menu_ = true;
            menu_sel_ = 0;
        }
        return;
    }
    CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
    const int rest = in.value("rest_pct", 100), armor = in.value("armor", 0);
    std::vector<std::pair<std::string, std::string>> rows = {{"Uyu (sabah 08:00)", std::to_string(rest) + "% can" + (armor > 0 ? " + zırh" : "")}, {"Gardirop", "sahip olduğun kıyafetler"}, {"Disari cik", ""}, {"Vazgec", ""}};
    const int n = static_cast<int>(rows.size());
    if (keys_.Repeat(VK_DOWN)) menu_sel_ = (menu_sel_ + 1) % n;
    if (keys_.Repeat(VK_UP)) menu_sel_ = (menu_sel_ + n - 1) % n;
    menu_sel_ = std::clamp(menu_sel_, 0, n - 1);
    overlay::MenuView mv;
    mv.open = true;
    const House* h = Find(inside_house_);
    mv.title = h ? h->label : "Ev";
    mv.subtitle = in.value("name", std::string("Evin içi"));
    mv.hint = "↑↓ sec   Enter onay   Backspace vazgec";
    for (const auto& r : rows) mv.rows.push_back({r.first, r.second, false});
    mv.selected = menu_sel_;
    overlay::SetMenu(mv);
    if (keys_.Pressed(VK_BACK)) {
        inside_menu_ = false;
        overlay::SetMenu({});
        return;
    }
    if (!keys_.Pressed(VK_RETURN)) return;
    inside_menu_ = false;
    overlay::SetMenu({});
    if (menu_sel_ == 0) Sleep(rest, armor);
    else if (menu_sel_ == 1) binco::OpenWardrobe("Ev Gardirobu");
    else if (menu_sel_ == 2) ExitHouse();
}

void Estate::LoadSave(int slot) {
    if (slot == slot_) return;
    if (slot_ >= 0) Save();
    ClearBlips();
    slot_ = slot;
    owned_.clear();
    last_day_ = -1;
    try {
        std::ifstream in(SavePath(slot));
        if (!in) return;
        const json j = json::parse(in);
        for (const auto& o : j.value("owned", json::array())) {
            Owned w;
            w.id = o.value("id", "");
            w.rented = o.value("rented", false);
            w.days_left = o.value("days_left", 0);
            if (Find(w.id)) owned_.push_back(std::move(w));
        }
    } catch (...) {
    }
    Log("emlak: " + std::to_string(owned_.size()) + " mulk (slot " + std::to_string(slot) + ")");
}

void Estate::Save() const {
    if (slot_ < 0) return;
    try {
        std::filesystem::create_directories(std::string(kBaseDir) + "/saves");
        json arr = json::array();
        for (const auto& o : owned_) arr.push_back({{"id", o.id}, {"rented", o.rented}, {"days_left", o.days_left}});
        std::ofstream out(SavePath(slot_));
        out << json{{"owned", arr}}.dump(2);
    } catch (...) {
    }
}

void Estate::ClearBlips() {
    for (auto& o : owned_) blips::Remove(o.blip);
}

void Estate::Shutdown() {
    inside_ = false;
    inside_menu_ = false;
    for (const auto& n : ipls_requested_) STREAMING::REMOVE_IPL(const_cast<char*>(n.c_str()));
    ipls_requested_.clear();
    Save();
    ClearBlips();
    if (menu_open_) overlay::SetMenu({});
    menu_open_ = false;
    slot_ = -1;
}

const Estate::House* Estate::Find(const std::string& id) const {
    for (const auto& h : houses_) {
        if (h.id == id) return &h;
    }
    return nullptr;
}

Estate::Owned* Estate::FindOwned(const std::string& id) {
    for (auto& o : owned_) {
        if (o.id == id) return &o;
    }
    return nullptr;
}

bool Estate::OwnsBlip(Blip b) const {
    if (b == 0) return false;
    for (const auto& o : owned_) {
        if (o.blip == b) return true;
    }
    return false;
}

std::vector<Vector3> Estate::GarageSpots() const {
    std::vector<Vector3> out;
    for (const auto& o : owned_) {
        if (const House* h = Find(o.id); h && h->has_garage) out.push_back(h->garage);
    }
    return out;
}

std::vector<std::pair<std::string, Vector3>> Estate::AllHouses() const {
    std::vector<std::pair<std::string, Vector3>> out;
    for (const auto& h : houses_) out.push_back({h.label.empty() ? h.id : h.label, h.pos});
    return out;
}

std::vector<Vector3> Estate::OwnedPositions() const {
    std::vector<Vector3> out;
    for (const auto& o : owned_) {
        if (const House* h = Find(o.id)) out.push_back(h->pos);
    }
    return out;
}

std::vector<std::pair<std::string, Vector3>> Estate::OwnedHouses() const {
    std::vector<std::pair<std::string, Vector3>> out;
    for (const auto& o : owned_) {
        if (const House* h = Find(o.id)) out.push_back({h->label.empty() ? h->id : h->label, h->pos});
    }
    return out;
}

void Estate::Sleep(int heal_pct, int armor) {
    CAM::DO_SCREEN_FADE_OUT(800);
    for (int i = 0; i < 60 && !CAM::IS_SCREEN_FADED_OUT(); ++i) WAIT(0);
    const int h = TIME::GET_CLOCK_HOURS();
    TIME::ADD_TO_CLOCK_TIME((8 - h + 24) % 24, 0, 0);
    const Ped p = PLAYER::PLAYER_PED_ID();
    {   // dinlenme kalitesi evin kademesine göre: iyileşme yüzdesi, lüks evde zırh
        const int maxh = ENTITY::GET_ENTITY_MAX_HEALTH(p), cur = ENTITY::GET_ENTITY_HEALTH(p);
        ENTITY::SET_ENTITY_HEALTH(p, std::min(maxh, cur + static_cast<int>((maxh - cur) * heal_pct / 100.f + 0.5f)));
        if (armor > 0) PED::SET_PED_ARMOUR(p, std::max(PED::GET_PED_ARMOUR(p), armor));
    }
    for (int i = 0; i < 30; ++i) WAIT(0);
    CAM::DO_SCREEN_FADE_IN(900);
    karma::Toast("Güne dinlenmiş başladın (saat 08:00)", 5);
}

void Estate::Update(const Vector3& pos, bool e_pressed) {
    if (slot_ < 0 || houses_.empty()) return;
    // Sahip olunan evlerin ikonları
    for (auto& o : owned_) {
        const House* h = Find(o.id);
        if (!h) continue;
        if (o.blip == 0 || !UI::DOES_BLIP_EXIST(o.blip)) o.blip = blips::Add(h->pos, 40, 2, o.rented ? "Kiralik Evim" : "Evim", 0.9f, false);
    }
    // Kira: her oyun günü bir gün düşer; süre bitince otomatik yenilenir (nakit yoksa mülk elden gider)
    const int day = TIME::GET_CLOCK_DAY_OF_MONTH();
    if (last_day_ >= 0 && day != last_day_) {
        for (size_t i = 0; i < owned_.size();) {
            Owned& o = owned_[i];
            if (o.rented && --o.days_left <= 0) {
                const House* h = Find(o.id);
                if (h && economy::Spend(h->rent)) {
                    o.days_left = kRentDays;
                    karma::Toast("Kira ödendi: " + h->label + " (" + Money(h->rent) + ")", 5);
                } else {
                    karma::Toast("Kirayı ödeyemedin, ev elden gitti", 6);
                    blips::Remove(o.blip);
                    owned_.erase(owned_.begin() + static_cast<long>(i));
                    Save();
                    continue;
                }
                Save();
            }
            ++i;
        }
    }
    last_day_ = day;

    if (inside_) {  // ev içi: uyu / çık
        UpdateInside(pos, e_pressed);
        return;
    }

    // Kapı etkileşimi
    const House* door = nullptr;
    for (const auto& h : houses_) {
        if (D2(pos, h.pos) < kDoorRadius && std::fabs(pos.z - h.pos.z) < 4.f) {
            door = &h;
            break;
        }
    }
    if (menu_open_ && (!door || door->id != menu_house_)) {
        menu_open_ = false;
        overlay::SetMenu({});
    }
    if (!door || PED::IS_PED_IN_ANY_VEHICLE(PLAYER::PLAYER_PED_ID(), false)) return;
    Owned* mine = FindOwned(door->id);
    if (!menu_open_) {
        ShowHelp(mine ? "~INPUT_PICKUP~ Evim" : "~INPUT_PICKUP~ Ev: ayrintilar");
        if (e_pressed) {
            menu_open_ = true;
            menu_house_ = door->id;
            menu_sel_ = 0;
        }
        return;
    }
    std::vector<std::pair<std::string, std::string>> rows;  // etiket, değer
    std::vector<std::string> acts;
    if (mine) {
        rows.push_back({"Iceri gir", std::string("kademe ") + std::to_string(InteriorIndexFor(*door) + 1)});
        acts.push_back("enter");
        rows.push_back({"Uyu (sabah 08:00)", "kapida: %60 can"});
        acts.push_back("sleep");
        if (mine->rented) {
            rows.push_back({"Kirayi birak", std::to_string(mine->days_left) + " gun kaldi"});
            acts.push_back("leave");
        } else {
            rows.push_back({"Evi sat", Money(door->price * 8 / 10)});
            acts.push_back("sell");
        }
    } else {
        rows.push_back({"Satin al", Money(door->price)});
        acts.push_back("buy");
        rows.push_back({"Kirala (30 gun)", Money(door->rent)});
        acts.push_back("rent");
    }
    rows.push_back({"Vazgec", ""});
    acts.push_back("close");
    CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
    const int n = static_cast<int>(rows.size());
    if (keys_.Repeat(VK_DOWN)) menu_sel_ = (menu_sel_ + 1) % n;
    if (keys_.Repeat(VK_UP)) menu_sel_ = (menu_sel_ + n - 1) % n;
    menu_sel_ = std::clamp(menu_sel_, 0, n - 1);
    overlay::MenuView mv;
    mv.open = true;
    mv.title = door->label;
    mv.subtitle = std::string(mine ? (mine->rented ? "Kiralik evin" : "Senin evin") : "Satilik / kiralik") + "  ·  kademe " + std::to_string(door->tier);
    mv.hint = "↑↓ sec   Enter onay   Backspace vazgec";
    for (const auto& r : rows) mv.rows.push_back({r.first, r.second, false});
    mv.selected = menu_sel_;
    overlay::SetMenu(mv);
    if (keys_.Pressed(VK_BACK)) {
        menu_open_ = false;
        overlay::SetMenu({});
        return;
    }
    if (!keys_.Pressed(VK_RETURN)) return;
    const std::string act = acts[static_cast<size_t>(menu_sel_)];
    menu_open_ = false;
    overlay::SetMenu({});
    if (act == "buy" || act == "rent") {
        const int cost = act == "buy" ? door->price : door->rent;
        if (!economy::Spend(cost)) {
            karma::Toast("Yetersiz bakiye: " + Money(cost) + " gerekiyor", 5);
            return;
        }
        Owned o;
        o.id = door->id;
        o.rented = act == "rent";
        o.days_left = o.rented ? kRentDays : 0;
        owned_.push_back(std::move(o));
        Save();
        karma::Toast(std::string(act == "buy" ? "Ev satin alindi: " : "Ev kiralandi: ") + door->label, 6);
    } else if (act == "enter") {
        EnterHouse(*door);
    } else if (act == "sleep") {
        Sleep(60, 0);
    } else if (act == "sell" || act == "leave") {
        if (act == "sell") economy::Earn(door->price * 8 / 10);
        for (size_t i = 0; i < owned_.size(); ++i) {
            if (owned_[i].id != door->id) continue;
            blips::Remove(owned_[i].blip);
            owned_.erase(owned_.begin() + static_cast<long>(i));
            break;
        }
        Save();
        karma::Toast(act == "sell" ? "Ev satildi: +" + Money(door->price * 8 / 10) : std::string("Kira birakildi"), 6);
    }
}

std::vector<hml::Item> Estate::Source(const std::string& name) {
    std::vector<hml::Item> out;
    auto card = [&](std::string title, std::string sub, std::string price, std::string href) {
        hml::Item it;
        it.kind = hml::Kind::Card;
        it.text = std::move(title);
        it.sub = std::move(sub);
        it.meta = std::move(price);
        it.href = std::move(href);
        it.selectable = true;
        out.push_back(std::move(it));
    };
    const Vector3 pp = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true);
    if (name == "estate_mine") {
        if (owned_.empty()) {
            hml::Item it;
            it.text = "Henüz evin yok. Satılık ve kiralık evler sayfasından bir ev seç, kapısına git ve E'ye bas.";
            out.push_back(it);
        }
        for (const auto& o : owned_) {
            const House* h = Find(o.id);
            if (!h) continue;
            card(h->label, std::string(o.rented ? "Kiracısın · " + std::to_string(o.days_left) + " gün kaldı" : "Mülk sahibisin") + " · seç: haritada işaretle",
                 o.rented ? Money(h->rent) + "/ay" : Money(h->price), "act:look:" + h->id);
        }
        return out;
    }
    std::vector<const House*> list;
    for (const auto& h : houses_) {
        if (!FindOwned(h.id)) list.push_back(&h);
    }
    std::sort(list.begin(), list.end(), [&](const House* a, const House* b) { return D2(pp, a->pos) < D2(pp, b->pos); });
    for (size_t i = 0; i < list.size() && i < 12; ++i) {
        const House* h = list[i];
        const int meters = static_cast<int>(D2(pp, h->pos));
        const std::string dist = meters >= 1000 ? std::to_string(meters / 1000) + "," + std::to_string(meters % 1000 / 100) + " km" : std::to_string(meters) + " m";
        card(h->label, "Kiralık " + Money(h->rent) + "/ay · " + dist + " uzakta · seç: haritada işaretle, kapıda E ile satın al/kirala",
             Money(h->price), "act:look:" + h->id);
    }
    return out;
}

std::string Estate::Action(const std::string& cmd) {
    const size_t c = cmd.find(':');
    const std::string verb = c == std::string::npos ? cmd : cmd.substr(0, c);
    const std::string arg = c == std::string::npos ? "" : cmd.substr(c + 1);
    if (verb == "look") {
        const House* h = Find(arg);
        if (!h) return "Ev bulunamadı";
        UI::SET_NEW_WAYPOINT(h->pos.x, h->pos.y);
        return h->label + " haritada işaretlendi";
    }
    return "Bilinmeyen işlem";
}

}  // namespace hikaye
