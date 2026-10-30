#include "garage.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "blips.h"
#include "crimes.h"
#include "businesses.h"
#include "economy.h"
#include "fuel.h"
#include "overlay.h"
#include "street_events.h"

namespace hikaye {

using nlohmann::json;

namespace {
constexpr int kValetFee = 150;
constexpr int kParkFee = 50;
constexpr int kBrakeAndStop = 27;
constexpr int kDrivingModeNormal = 786603;

std::string SavePath(int slot) { return std::string(kBaseDir) + "/saves/garage_" + std::to_string(slot) + ".json"; }
float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

bool LoadModel(Hash m) {
    if (!STREAMING::IS_MODEL_IN_CDIMAGE(m) || !STREAMING::IS_MODEL_VALID(m)) return false;
    STREAMING::REQUEST_MODEL(m);
    for (int i = 0; i < 150 && !STREAMING::HAS_MODEL_LOADED(m); ++i) WAIT(0);
    return STREAMING::HAS_MODEL_LOADED(m) != 0;
}

// Aracın rengi ve modifiyesi (garaj kaydıyla birlikte saklanır, araç tekrar çıkınca aynen uygulanır).
json CaptureLook(Vehicle v) {
    json j = json::object();
    int p = 0, s = 0, pearl = 0, wheel = 0;
    VEHICLE::GET_VEHICLE_COLOURS(v, &p, &s);
    VEHICLE::GET_VEHICLE_EXTRA_COLOURS(v, &pearl, &wheel);
    j["colours"] = {p, s, pearl, wheel};
    if (VEHICLE::GET_IS_VEHICLE_PRIMARY_COLOUR_CUSTOM(v)) {
        int r = 0, g = 0, b = 0;
        VEHICLE::GET_VEHICLE_CUSTOM_PRIMARY_COLOUR(v, &r, &g, &b);
        j["custom_primary"] = {r, g, b};
    }
    if (VEHICLE::GET_IS_VEHICLE_SECONDARY_COLOUR_CUSTOM(v)) {
        int r = 0, g = 0, b = 0;
        VEHICLE::GET_VEHICLE_CUSTOM_SECONDARY_COLOUR(v, &r, &g, &b);
        j["custom_secondary"] = {r, g, b};
    }
    json mods = json::object();
    for (int t = 0; t <= 48; ++t) {
        if (t >= 17 && t <= 22) {
            if (VEHICLE::IS_TOGGLE_MOD_ON(v, t)) mods[std::to_string(t)] = -2;  // -2: açık anahtar (turbo/duman/xenon)
        } else {
            const int m = VEHICLE::GET_VEHICLE_MOD(v, t);
            if (m >= 0) mods[std::to_string(t)] = m;
        }
    }
    j["mods"] = mods;
    j["custom_tyres"] = VEHICLE::GET_VEHICLE_MOD_VARIATION(v, 23) != 0;
    j["wheel_type"] = VEHICLE::GET_VEHICLE_WHEEL_TYPE(v);
    j["tint"] = VEHICLE::GET_VEHICLE_WINDOW_TINT(v);
    j["plate_style"] = VEHICLE::GET_VEHICLE_NUMBER_PLATE_TEXT_INDEX(v);
    j["livery"] = VEHICLE::GET_VEHICLE_LIVERY(v);
    return j;
}

void ApplyLook(Vehicle v, const json& j) {
    if (!j.is_object() || j.empty()) return;
    VEHICLE::SET_VEHICLE_MOD_KIT(v, 0);
    if (j.contains("wheel_type")) VEHICLE::SET_VEHICLE_WHEEL_TYPE(v, j.value("wheel_type", 0));
    const auto col = j.value("colours", std::vector<int>{});
    if (col.size() >= 4) {
        VEHICLE::SET_VEHICLE_COLOURS(v, col[0], col[1]);
        VEHICLE::SET_VEHICLE_EXTRA_COLOURS(v, col[2], col[3]);
    }
    const auto cp = j.value("custom_primary", std::vector<int>{});
    if (cp.size() >= 3) VEHICLE::SET_VEHICLE_CUSTOM_PRIMARY_COLOUR(v, cp[0], cp[1], cp[2]);
    const auto cs = j.value("custom_secondary", std::vector<int>{});
    if (cs.size() >= 3) VEHICLE::SET_VEHICLE_CUSTOM_SECONDARY_COLOUR(v, cs[0], cs[1], cs[2]);
    const bool tyres = j.value("custom_tyres", false);
    const json mods_json = j.value("mods", json::object());  // KOPYA: .items() geçiciye bakıp döngüden önce silinirdi
    for (const auto& [k, val] : mods_json.items()) {
        const int t = std::atoi(k.c_str()), m = val.get<int>();
        if (t >= 17 && t <= 22) VEHICLE::TOGGLE_VEHICLE_MOD(v, t, true);
        else VEHICLE::SET_VEHICLE_MOD(v, t, m, tyres);
    }
    VEHICLE::SET_VEHICLE_WINDOW_TINT(v, j.value("tint", 0));
    VEHICLE::SET_VEHICLE_NUMBER_PLATE_TEXT_INDEX(v, j.value("plate_style", 0));
    if (j.value("livery", -1) >= 0) VEHICLE::SET_VEHICLE_LIVERY(v, j.value("livery", 0));
}
}  // namespace

void Garage::Load(int slot) {
    CleanupParkRide(true);
    slot_ = slot;
    cars_.clear();
    valet_ = Valet{};
    last_used_ = 0;
    try {
        std::ifstream in(SavePath(slot));
        if (!in) return;
        const json j = json::parse(in);
        for (const auto& c : j.value("cars", json::array())) {
            Owned o;
            o.model = c.value("model", "");
            o.plate = c.value("plate", "");
            o.fuel = c.value("fuel", 100.f);
            o.km = c.value("km", 0.f);
            o.look = c.value("look", json::object());
            if (!o.model.empty()) cars_.push_back(std::move(o));
        }
    } catch (const std::exception& e) {
        Log(std::string("garaj okunamadi: ") + e.what());
    }
    try {  // data/vehicle.json: hırsızlık ayarları
        std::ifstream cin2(std::string(kBaseDir) + "/data/vehicle.json");
        if (cin2) {
            const json c = json::parse(cin2);
            const json t = c.value("theft", json::object());
            cfg_.theft = t.value("enabled", true);
            cfg_.period_s = std::max(60, t.value("period_s", 3600));
            cfg_.chance = t.value("chance", 0.20f);
            cfg_.far_m = t.value("far_m", 120.f);
            cfg_.garage_radius = c.value("garage_radius_m", 18.f);
        }
    } catch (...) {
    }
    Log("garaj: " + std::to_string(cars_.size()) + " arac (slot " + std::to_string(slot) + ")");
}

void Garage::Save() const {
    try {
        std::filesystem::create_directories(std::string(kBaseDir) + "/saves");
        json cars = json::array();
        for (const auto& c : cars_) cars.push_back({{"model", c.model}, {"plate", c.plate}, {"fuel", c.fuel}, {"km", c.km}, {"look", c.look}});
        std::ofstream out(SavePath(slot_));
        out << json{{"cars", cars}}.dump(2);
    } catch (...) {
    }
}

void Garage::Register(Vehicle veh, const std::string& model) {
    Owned o;
    o.model = model;
    char plate[16];
    std::snprintf(plate, sizeof(plate), "HK%05d", static_cast<int>(GetTickCount64() % 100000ULL));
    o.plate = plate;
    o.handle = veh;
    if (ENTITY::DOES_ENTITY_EXIST(veh)) {
        VEHICLE::SET_VEHICLE_NUMBER_PLATE_TEXT(veh, const_cast<char*>(o.plate.c_str()));
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(veh, true, true);  // oyun silmesin: bu senin araban
        VEHICLE::SET_VEHICLE_HAS_BEEN_OWNED_BY_PLAYER(veh, true);
        VEHICLE::SET_VEHICLE_IS_STOLEN(veh, false);
        VEHICLE::SET_VEHICLE_NEEDS_TO_BE_HOTWIRED(veh, false);
        crimes::MarkKnown(veh);
        fuel::Set(veh, 100.f);
        fuel::SetKm(veh, 0.f);  // galeriden sıfır araç: kilometre 0
        o.km = 0.f;
        o.look = CaptureLook(veh);
    }
    cars_.push_back(std::move(o));
    last_used_ = cars_.size() - 1;
    Save();
    Log("garaj: arac eklendi " + model);
}

int Garage::Pick(const std::string& which, bool in_world) const {
    auto lower = [](std::string s) {
        for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    };
    const std::string q = lower(which);
    if (!q.empty()) {
        for (size_t i = 0; i < cars_.size(); ++i) {
            if ((cars_[i].handle != 0) == in_world || !in_world) {
                if (lower(cars_[i].model).find(q) != std::string::npos) return static_cast<int>(i);
            }
        }
    }
    if (last_used_ < cars_.size() && (cars_[last_used_].handle != 0) == in_world) return static_cast<int>(last_used_);
    for (size_t i = 0; i < cars_.size(); ++i) {
        if ((cars_[i].handle != 0) == in_world) return static_cast<int>(i);
    }
    return -1;
}

json Garage::Locate(const std::string& which) {
    auto refuse = [](const char* reason) { return json{{"service", "locate_vehicle"}, {"found", false}, {"reason", reason}}; };
    if (cars_.empty()) return refuse("no_vehicle");
    int pick = Pick(which, true);
    if (pick < 0) pick = Pick(which, false);
    if (pick < 0) return refuse("no_vehicle");
    Owned& car = cars_[static_cast<size_t>(pick)];
    if (car.handle == 0 || !ENTITY::DOES_ENTITY_EXIST(car.handle)) {
        return {{"service", "locate_vehicle"}, {"found", true}, {"in_garage", true}, {"vehicle", car.model}};
    }
    const Vector3 vp = ENTITY::GET_ENTITY_COORDS(car.handle, true);
    const Vector3 pp = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true);
    Hash s1 = 0, s2 = 0;
    PATHFIND::GET_STREET_NAME_AT_COORD(vp.x, vp.y, vp.z, &s1, &s2);
    std::string street = s1 != 0 ? UI::GET_STREET_NAME_FROM_HASH_KEY(s1) : "";
    UI::SET_NEW_WAYPOINT(vp.x, vp.y);
    const float dx = vp.x - pp.x, dy = vp.y - pp.y;
    const char* dir = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? "dogu" : "bati") : (dy > 0 ? "kuzey" : "guney");
    last_used_ = static_cast<size_t>(pick);
    Log("arac nerede: " + car.model + " " + street);
    return {{"service", "locate_vehicle"}, {"found", true}, {"in_garage", false}, {"vehicle", car.model},
            {"distance_m", static_cast<int>(D2(vp, pp))}, {"direction", dir}, {"street", street}, {"waypoint_set", true}};
}

json Garage::ParkVehicle(const std::string& which) {
    auto refuse = [](const char* reason) { return json{{"service", "parking"}, {"accepted", false}, {"reason", reason}}; };
    if (cars_.empty()) return refuse("no_vehicle");
    if (PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) > 0) return refuse("wanted_level");
    if (park_ride_.stage != ParkRide::Stage::None) return refuse("already_on_the_way");
    const int pick = Pick(which, true);
    if (pick < 0) return refuse("already_in_garage");
    Owned& car = cars_[static_cast<size_t>(pick)];
    if (!ENTITY::DOES_ENTITY_EXIST(car.handle)) {
        car.handle = 0;
        return refuse("already_in_garage");
    }
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (valet_.active && valet_.veh == car.handle) return refuse("already_on_the_way");
    if (ENTITY::GET_ENTITY_SPEED(car.handle) > 3.f) return refuse("vehicle_moving");
    if (economy::GetCash() < kParkFee) return refuse("insufficient_funds");

    const Vector3 pos = ENTITY::GET_ENTITY_COORDS(player, true);
    Vector3 pickup{};
    const Vector3 car_pos = ENTITY::GET_ENTITY_COORDS(car.handle, true);
    if (D2(pos, car_pos) > 50.f) return refuse("vehicle_too_far");
    if (!PATHFIND::GET_CLOSEST_VEHICLE_NODE(pos.x, pos.y, pos.z, &pickup, 1, 3.0f, 0) ||
        D2(pos, pickup) > 80.f) {
        return refuse("no_road_nearby");
    }

    Vector3 garage{};
    float best_distance = 1e9f;
    for (const auto& spot : businesses::Spots("garage")) {
        const float distance = D2(car_pos, spot.pos);
        if (distance < best_distance) {
            garage = spot.pos;
            best_distance = distance;
        }
    }
    for (const auto& spot : extra_garages_) {
        const float distance = D2(car_pos, spot);
        if (distance < best_distance) {
            garage = spot;
            best_distance = distance;
        }
    }
    if (best_distance == 1e9f) return refuse("no_garage");
    Vector3 destination{};
    if (!PATHFIND::GET_CLOSEST_VEHICLE_NODE(garage.x, garage.y, garage.z, &destination, 1, 3.0f, 0) ||
        D2(garage, destination) > 100.f) {
        return refuse("no_garage_road");
    }

    Vector3 spawn{};
    bool found_spawn = false;
    const unsigned long long now = GetTickCount64();
    for (int k = 0; k < 8 && !found_spawn; ++k) {
        const float angle = static_cast<float>((now / 97 + k * 83) % 628) / 100.f;
        Vector3 node{};
        if (PATHFIND::GET_CLOSEST_VEHICLE_NODE(pos.x + 150.f * std::cos(angle), pos.y + 150.f * std::sin(angle),
                                                pos.z, &node, 1, 3.0f, 0) &&
            D2(node, pos) > 90.f) {
            spawn = node;
            found_spawn = true;
        }
    }
    if (!found_spawn) return refuse("no_road_nearby");

    const Hash taxi_model = HashKey("taxi");
    const Hash driver_model = HashKey("a_m_m_skater_01");
    const Hash valet_model = HashKey("s_m_y_valet_01");
    if (!LoadModel(taxi_model)) return refuse("vehicle_unavailable");
    if (!LoadModel(driver_model)) {
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(taxi_model);
        return refuse("crew_unavailable");
    }
    if (!LoadModel(valet_model)) {
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(taxi_model);
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(driver_model);
        return refuse("crew_unavailable");
    }

    const Vehicle taxi = VEHICLE::CREATE_VEHICLE(taxi_model, spawn.x, spawn.y, spawn.z, 0.f, false, false);
    if (!ENTITY::DOES_ENTITY_EXIST(taxi)) {
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(taxi_model);
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(driver_model);
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(valet_model);
        return refuse("vehicle_unavailable");
    }
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(taxi, true, true);
    VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(taxi);
    VEHICLE::SET_VEHICLE_DOORS_LOCKED(taxi, 1);
    VEHICLE::SET_VEHICLE_ENGINE_ON(taxi, true, true, false);
    const Ped taxi_driver = PED::CREATE_PED_INSIDE_VEHICLE(taxi, 4, driver_model, -1, false, false);
    const Ped valet = PED::CREATE_PED_INSIDE_VEHICLE(taxi, 4, valet_model, 0, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(taxi_model);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(driver_model);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(valet_model);
    if (!ENTITY::DOES_ENTITY_EXIST(taxi_driver) || !ENTITY::DOES_ENTITY_EXIST(valet)) {
        if (ENTITY::DOES_ENTITY_EXIST(taxi_driver)) {
            Ped cleanup_ped = taxi_driver;
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(cleanup_ped, true, true);
            PED::DELETE_PED(&cleanup_ped);
        }
        if (ENTITY::DOES_ENTITY_EXIST(valet)) {
            Ped cleanup_ped = valet;
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(cleanup_ped, true, true);
            PED::DELETE_PED(&cleanup_ped);
        }
        Vehicle cleanup_taxi = taxi;
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(cleanup_taxi, true, true);
        VEHICLE::DELETE_VEHICLE(&cleanup_taxi);
        return refuse("crew_unavailable");
    }
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(taxi_driver, true, true);
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(valet, true, true);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(taxi_driver, true);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(valet, true);
    VEHICLE::SET_VEHICLE_IS_STOLEN(taxi, false);
    AI::TASK_VEHICLE_DRIVE_TO_COORD(taxi_driver, taxi, pickup.x, pickup.y, pickup.z, 20.f, 0, taxi_model,
                                    kDrivingModeNormal, 6.f, 1.f);
    if (!economy::Spend(kParkFee)) {
        Ped cleanup_valet = valet;
        PED::DELETE_PED(&cleanup_valet);
        Ped cleanup_driver = taxi_driver;
        PED::DELETE_PED(&cleanup_driver);
        Vehicle cleanup_taxi = taxi;
        VEHICLE::DELETE_VEHICLE(&cleanup_taxi);
        return refuse("insufficient_funds");
    }

    park_ride_ = ParkRide{};
    park_ride_.stage = ParkRide::Stage::TaxiEnRoute;
    park_ride_.owned_vehicle = car.handle;
    park_ride_.taxi = taxi;
    park_ride_.taxi_driver = taxi_driver;
    park_ride_.valet = valet;
    park_ride_.plate = car.plate;
    park_ride_.model = car.model;
    park_ride_.pickup = pickup;
    park_ride_.destination = destination;
    park_ride_.original_lock = VEHICLE::GET_VEHICLE_DOOR_LOCK_STATUS(car.handle);
    park_ride_.started_at = park_ride_.stage_at = park_ride_.last_task_at = now;
    park_ride_.fee_paid = true;
    last_used_ = static_cast<size_t>(pick);
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(car.handle, true, true);
    VEHICLE::SET_VEHICLE_DOORS_LOCKED(car.handle, 2);
    toast_ = "Vale taksisi yola çıktı; aracınızı garaja götürecek";
    toast_until_ = now + 8000;
    const int eta = static_cast<int>(D2(spawn, pos) * 1.25f / 14.f);
    Log("otopark: vale taksisi yola cikti " + car.model);
    return {{"service", "parking"}, {"accepted", true}, {"action", "valet_transport"},
            {"eta_seconds", eta}, {"destination", "en yakın garaj"}, {"vehicle", car.model}, {"fee", kParkFee}};
}

json Garage::Summon(const std::string& which) {
    auto refuse = [](const char* reason) { return json{{"service", "my_vehicle"}, {"accepted", false}, {"reason", reason}}; };
    if (cars_.empty()) return refuse("no_vehicle");
    if (valet_.active) return refuse("already_on_the_way");
    const unsigned long long now = GetTickCount64();
    if (last_summon_ != 0 && now - last_summon_ < 60000ULL) return refuse("cooldown");
    if (PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) > 0) return refuse("wanted_level");
    if (economy::GetCash() < kValetFee) return refuse("insufficient_funds");

    if (last_used_ >= cars_.size()) last_used_ = 0;
    if (!which.empty()) {  // adı söylenen araç (garajdaki ya da dışarıdaki); bulunamazsa son kullanılan
        const int named = Pick(which, false);
        if (named >= 0) last_used_ = static_cast<size_t>(named);
    }
    Owned& car = cars_[last_used_];
    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vector3 pos = ENTITY::GET_ENTITY_COORDS(player, true);
    const Vehicle in_veh = PED::GET_VEHICLE_PED_IS_IN(player, false);
    if (in_veh != 0 && in_veh == car.handle) return refuse("already_in_it");

    // Doğma/ışınlanma noktası: oyuncudan ~140 m uzakta bir yol
    Vector3 spawn{};
    bool found = false;
    for (int k = 0; k < 8 && !found; ++k) {
        const float a = static_cast<float>((now / 97 + k * 83) % 628) / 100.f;
        Vector3 n{};
        if (PATHFIND::GET_CLOSEST_VEHICLE_NODE(pos.x + 140.f * std::cos(a), pos.y + 140.f * std::sin(a), pos.z, &n, 1, 3.0f, 0) &&
            D2(n, pos) > 90.f) {
            spawn = n;
            found = true;
        }
    }
    if (!found) return refuse("no_road_nearby");

    const Hash model = HashKey(car.model.c_str());
    Vehicle veh = 0;
    if (car.handle != 0 && ENTITY::DOES_ENTITY_EXIST(car.handle)) {
        veh = car.handle;  // araç zaten dünyada: yakına ışınla
        ENTITY::SET_ENTITY_COORDS(veh, spawn.x, spawn.y, spawn.z, false, false, false, true);
    } else {
        if (!LoadModel(model)) return refuse("vehicle_unavailable");
        veh = VEHICLE::CREATE_VEHICLE(model, spawn.x, spawn.y, spawn.z, 0.f, false, false);
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
        if (!ENTITY::DOES_ENTITY_EXIST(veh)) return refuse("vehicle_unavailable");
        if (!car.plate.empty()) VEHICLE::SET_VEHICLE_NUMBER_PLATE_TEXT(veh, const_cast<char*>(car.plate.c_str()));
        fuel::Set(veh, car.fuel);
        fuel::SetKm(veh, car.km);  // kayıtlı kilometre geri yüklenir
        ApplyLook(veh, car.look);
        car.handle = veh;
    }
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(veh, true, true);
    VEHICLE::SET_VEHICLE_HAS_BEEN_OWNED_BY_PLAYER(veh, true);
    VEHICLE::SET_VEHICLE_IS_STOLEN(veh, false);
    VEHICLE::SET_VEHICLE_NEEDS_TO_BE_HOTWIRED(veh, false);
    VEHICLE::SET_VEHICLE_DOORS_LOCKED(veh, 1);
    VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(veh);
    crimes::MarkKnown(veh);

    Hash dm = HashKey("s_m_y_valet_01");  // otopark görevlisi (vale)
    if (!LoadModel(dm)) return refuse("driver_unavailable");
    const Ped driver = PED::CREATE_PED_INSIDE_VEHICLE(veh, 4, dm, -1, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(dm);
    if (!ENTITY::DOES_ENTITY_EXIST(driver)) return refuse("driver_unavailable");
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(driver, true);
    VEHICLE::SET_VEHICLE_ENGINE_ON(veh, fuel::Get(veh) > 0.f, true, false);

    Vector3 target = pos;
    Vector3 road{};
    if (PATHFIND::GET_CLOSEST_VEHICLE_NODE(pos.x, pos.y, pos.z, &road, 1, 3.0f, 0)) target = road;
    AI::TASK_VEHICLE_DRIVE_TO_COORD(driver, veh, target.x, target.y, target.z, 22.0f, 0, model, kDrivingModeNormal, 5.0f, 1.0f);

    economy::Spend(kValetFee);
    last_summon_ = now;
    valet_ = Valet{true, driver, veh, target, now, 0, false};
    const int eta = static_cast<int>(D2(spawn, pos) * 1.25f / 14.f);
    Log("vale: " + car.model + " yolda, eta " + std::to_string(eta) + " sn");
    return {{"service", "my_vehicle"}, {"accepted", true}, {"eta_seconds", eta}, {"fee", kValetFee}, {"vehicle", car.model}};
}

void Garage::Update() {
    const unsigned long long now = GetTickCount64();
    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vehicle pv = PED::GET_VEHICLE_PED_IS_IN(player, false);
    for (size_t i = 0; i < cars_.size(); ++i) {
        Owned& c = cars_[i];
        if (c.handle != 0 && !ENTITY::DOES_ENTITY_EXIST(c.handle)) c.handle = 0;
        if (c.handle != 0) {
            const float f = fuel::Get(c.handle);
            if (f >= 0.f) c.fuel = f;
            const float km = fuel::GetKm(c.handle);
            if (km >= 0.f) c.km = km;  // sürüş kilometresi kayda yansısın
            if (now - c.look_at > 3000ULL) {  // modifiye/boya değişimleri kaydedilsin
                c.look_at = now;
                c.look = CaptureLook(c.handle);
            }
            if (pv == c.handle) last_used_ = i;  // son kullanılan araç çağrılır
        }
    }
    if (now - last_save_ > 30000ULL) {
        last_save_ = now;
        Save();
    }
    if (pv != 0 && IsOwned(pv)) {
        crimes::MarkKnown(pv);
        VEHICLE::SET_VEHICLE_IS_STOLEN(pv, false);
        VEHICLE::SET_VEHICLE_NEEDS_TO_BE_HOTWIRED(pv, false);
        VEHICLE::SET_VEHICLE_HAS_BEEN_OWNED_BY_PLAYER(pv, true);
    }
    ManageOwned(now, ENTITY::GET_ENTITY_COORDS(player, true), pv);
    UpdateTheft(now, ENTITY::GET_ENTITY_COORDS(player, true));
    UpdateLeave(now, ENTITY::GET_ENTITY_COORDS(player, true));
    UpdateParkRide(now, ENTITY::GET_ENTITY_COORDS(player, true));
    if (now < toast_until_) DrawLine(toast_, 0.74f, 255, 120, 90);

    if (!valet_.active) return;
    if (!ENTITY::DOES_ENTITY_EXIST(valet_.driver) || !ENTITY::DOES_ENTITY_EXIST(valet_.veh) || ENTITY::IS_ENTITY_DEAD(valet_.driver)) {
        valet_.active = false;
        return;
    }
    const Vector3 vp = ENTITY::GET_ENTITY_COORDS(valet_.veh, true);
    const Vector3 pp = ENTITY::GET_ENTITY_COORDS(player, true);
    if (!valet_.arrived) {
        if (D2(vp, valet_.target) < 20.f || D2(vp, pp) < 25.f || now - valet_.since > 240000ULL) {
            valet_.arrived = true;
            valet_.arrived_at = now;
            AI::TASK_VEHICLE_TEMP_ACTION(valet_.driver, valet_.veh, kBrakeAndStop, 8000);
            VEHICLE::SET_VEHICLE_DOORS_LOCKED(valet_.veh, 1);
            VEHICLE::SET_VEHICLE_IS_STOLEN(valet_.veh, false);
            VEHICLE::SET_VEHICLE_HAS_BEEN_OWNED_BY_PLAYER(valet_.veh, true);
            VEHICLE::SET_VEHICLE_NEEDS_TO_BE_HOTWIRED(valet_.veh, false);
            crimes::MarkKnown(valet_.veh);
        }
        return;
    }
    if (now - valet_.arrived_at > 2000ULL && PED::IS_PED_IN_VEHICLE(valet_.driver, valet_.veh, false)) {
        AI::TASK_LEAVE_VEHICLE(valet_.driver, valet_.veh, 0);  // sürücü iner, araç sende
        VEHICLE::SET_VEHICLE_DOORS_LOCKED(valet_.veh, 1);
        VEHICLE::SET_VEHICLE_IS_STOLEN(valet_.veh, false);
        VEHICLE::SET_VEHICLE_HAS_BEEN_OWNED_BY_PLAYER(valet_.veh, true);
        VEHICLE::SET_VEHICLE_NEEDS_TO_BE_HOTWIRED(valet_.veh, false);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(valet_.driver, true);
        crimes::MarkKnown(valet_.veh);
    }
    if (now - valet_.arrived_at > 8000ULL || (pv != 0 && pv == valet_.veh) || PED::IS_PED_IN_VEHICLE(player, valet_.veh, false)) {
        crimes::MarkKnown(valet_.veh);  // teslim edilen araca binmek car_theft sayılmaz
        VEHICLE::SET_VEHICLE_IS_STOLEN(valet_.veh, false);
        VEHICLE::SET_VEHICLE_HAS_BEEN_OWNED_BY_PLAYER(valet_.veh, true);
        VEHICLE::SET_VEHICLE_NEEDS_TO_BE_HOTWIRED(valet_.veh, false);
        if (pv != 0 && pv == valet_.veh) {
            PLAYER::CLEAR_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID());
        }
        if (ENTITY::DOES_ENTITY_EXIST(valet_.driver)) StartLeave(valet_.driver, pp);
        valet_.active = false;
    }
}

void Garage::AbortParkRide(const std::string& reason, unsigned long long now) {
    if (park_ride_.stage == ParkRide::Stage::None || park_ride_.stage == ParkRide::Stage::Aborting) return;
    park_ride_.stage = ParkRide::Stage::Aborting;
    park_ride_.stage_at = now;
    park_ride_.abort_reason = reason;
    if (park_ride_.taxi_driver && ENTITY::DOES_ENTITY_EXIST(park_ride_.taxi_driver)) {
        AI::CLEAR_PED_TASKS(park_ride_.taxi_driver);
        if (ENTITY::DOES_ENTITY_EXIST(park_ride_.taxi)) {
            AI::TASK_VEHICLE_TEMP_ACTION(park_ride_.taxi_driver, park_ride_.taxi, kBrakeAndStop, 5000);
        }
    }
    if (park_ride_.valet && ENTITY::DOES_ENTITY_EXIST(park_ride_.valet)) {
        AI::CLEAR_PED_TASKS(park_ride_.valet);
        if (ENTITY::DOES_ENTITY_EXIST(park_ride_.owned_vehicle)) {
            VEHICLE::SET_VEHICLE_HANDBRAKE(park_ride_.owned_vehicle, true);
            VEHICLE::SET_VEHICLE_ENGINE_ON(park_ride_.owned_vehicle, false, true, false);
            if (PED::IS_PED_IN_VEHICLE(park_ride_.valet, park_ride_.owned_vehicle, false)) {
                AI::TASK_LEAVE_VEHICLE(park_ride_.valet, park_ride_.owned_vehicle, 0);
            }
        }
    }
    if (park_ride_.vehicle_stored) {
        toast_ = "Aracınız garaja teslim edildi; taksi hizmeti tamamlanamadı";
    } else if (reason == "service_timeout") {
        toast_ = "Vale hizmeti zaman aşımına uğradı; ücret iade edildi";
    } else if (reason == "store_failed") {
        toast_ = "Araç garaja alınamadı; ücret iade edildi";
    } else {
        toast_ = "Vale hizmeti iptal edildi; aracınız dünyada bırakıldı";
    }
    toast_until_ = now + 8000;
    Log("otopark: vale gorevi iptal " + reason);
}

void Garage::CleanupParkRide(bool force) {
    if (park_ride_.stage == ParkRide::Stage::None) return;
    (void)force;
    if (park_ride_.owned_vehicle && ENTITY::DOES_ENTITY_EXIST(park_ride_.owned_vehicle) && !park_ride_.vehicle_stored) {
        VEHICLE::SET_VEHICLE_HANDBRAKE(park_ride_.owned_vehicle, false);
        VEHICLE::SET_VEHICLE_ENGINE_ON(park_ride_.owned_vehicle, true, true, false);
        VEHICLE::SET_VEHICLE_DOORS_LOCKED(park_ride_.owned_vehicle, park_ride_.original_lock);
    }
    if (park_ride_.fee_paid && !park_ride_.vehicle_stored) {
        if (!economy::Earn(kParkFee)) Log("otopark: ucret iadesi yapilamadi");
        park_ride_.fee_paid = false;
    }

    auto delete_ped = [](Ped ped) {
        if (ped && ENTITY::DOES_ENTITY_EXIST(ped)) {
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped, true, true);
            PED::DELETE_PED(&ped);
        }
    };
    if (park_ride_.taxi && ENTITY::DOES_ENTITY_EXIST(park_ride_.taxi)) {
        Vehicle taxi = park_ride_.taxi;
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(taxi, true, true);
        VEHICLE::DELETE_VEHICLE(&taxi);
    }
    delete_ped(park_ride_.taxi_driver);
    delete_ped(park_ride_.valet);
    park_ride_ = ParkRide{};
}

void Garage::StoreParkRideVehicle() {
    const Vehicle target = park_ride_.owned_vehicle;
    if (!target || !ENTITY::DOES_ENTITY_EXIST(target)) {
        AbortParkRide("vehicle_missing", GetTickCount64());
        return;
    }
    auto found = std::find_if(cars_.begin(), cars_.end(), [target](const Owned& car) { return car.handle == target; });
    if (found == cars_.end()) {
        AbortParkRide("vehicle_not_registered", GetTickCount64());
        return;
    }

    const float f = fuel::Get(target);
    const float km = fuel::GetKm(target);
    const json look = CaptureLook(target);
    found->fuel = f >= 0.f ? f : found->fuel;
    found->km = km >= 0.f ? km : found->km;
    found->look = look;
    DropBlip(*found);

    if (park_ride_.valet && ENTITY::DOES_ENTITY_EXIST(park_ride_.valet)) {
        Ped valet = park_ride_.valet;
        PED::DELETE_PED(&valet);
        park_ride_.valet = 0;
    }
    Vehicle vehicle_to_delete = target;
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(vehicle_to_delete, true, true);
    VEHICLE::DELETE_VEHICLE(&vehicle_to_delete);
    for (int i = 0; i < 5 && ENTITY::DOES_ENTITY_EXIST(target); ++i) WAIT(0);
    if (ENTITY::DOES_ENTITY_EXIST(target)) {
        Entity entity_to_delete = target;
        ENTITY::DELETE_ENTITY(&entity_to_delete);
        for (int i = 0; i < 5 && ENTITY::DOES_ENTITY_EXIST(target); ++i) WAIT(0);
    }
    if (ENTITY::DOES_ENTITY_EXIST(target)) {
        AbortParkRide("store_failed", GetTickCount64());
        return;
    }

    found->handle = 0;
    found->unlocked_since = 0;
    park_ride_.vehicle_stored = true;
    park_ride_.fee_paid = false;
    park_ride_.stage = ParkRide::Stage::Finishing;
    park_ride_.stage_at = GetTickCount64();
    park_ride_.last_task_at = park_ride_.stage_at;
    Save();
    Log("otopark: vale araci garaja teslim etti " + park_ride_.model);
}

void Garage::UpdateParkRide(unsigned long long now, const Vector3& player_pos) {
    if (park_ride_.stage == ParkRide::Stage::None) return;
    const Ped player = PLAYER::PLAYER_PED_ID();
    const auto exists = [](Entity entity) { return entity != 0 && ENTITY::DOES_ENTITY_EXIST(entity); };

    if (park_ride_.stage == ParkRide::Stage::Aborting) {
        CleanupParkRide(false);
        return;
    }

    if (park_ride_.stage == ParkRide::Stage::Finishing) {
        toast_ = "Vale aracınızı garaja teslim etti; siz olduğunuz yerde kaldınız";
        toast_until_ = now + 8000;
        CleanupParkRide(false);
        return;
    }

    const bool taxi_required = park_ride_.stage == ParkRide::Stage::TaxiEnRoute ||
                               park_ride_.stage == ParkRide::Stage::Pickup;
    if ((taxi_required && (!exists(park_ride_.taxi) || !exists(park_ride_.taxi_driver) ||
                           ENTITY::IS_ENTITY_DEAD(park_ride_.taxi_driver))) ||
        (!park_ride_.vehicle_stored &&
         (!exists(park_ride_.owned_vehicle) || !exists(park_ride_.valet) || ENTITY::IS_ENTITY_DEAD(park_ride_.valet)))) {
        AbortParkRide("service_vehicle_lost", now);
        return;
    }

    const unsigned long long elapsed = now - park_ride_.stage_at;
    if ((park_ride_.stage == ParkRide::Stage::TaxiEnRoute && elapsed > 240000ULL) ||
        (park_ride_.stage == ParkRide::Stage::Pickup && elapsed > 90000ULL) ||
        (park_ride_.stage == ParkRide::Stage::Delivering && elapsed > 600000ULL)) {
        AbortParkRide("service_timeout", now);
        return;
    }

    if (park_ride_.stage == ParkRide::Stage::TaxiEnRoute) {
        const Vector3 taxi_pos = ENTITY::GET_ENTITY_COORDS(park_ride_.taxi, true);
        if (D2(taxi_pos, park_ride_.pickup) < 12.f) {
            park_ride_.stage = ParkRide::Stage::Pickup;
            park_ride_.stage_at = now;
            park_ride_.last_task_at = now;
            AI::TASK_VEHICLE_TEMP_ACTION(park_ride_.taxi_driver, park_ride_.taxi, kBrakeAndStop, 12000);
            VEHICLE::SET_VEHICLE_HANDBRAKE(park_ride_.taxi, true);
            AI::TASK_LEAVE_VEHICLE(park_ride_.valet, park_ride_.taxi, 0);
            park_ride_.valet_exit_requested = true;
            toast_ = "Vale geldi; aracınızı alacak, siz olduğunuz yerde kalacaksınız";
            toast_until_ = now + 8000;
        } else if (now - park_ride_.last_task_at > 15000ULL) {
            AI::TASK_VEHICLE_DRIVE_TO_COORD(park_ride_.taxi_driver, park_ride_.taxi, park_ride_.pickup.x,
                                            park_ride_.pickup.y, park_ride_.pickup.z, 20.f, 0,
                                            HashKey("taxi"), kDrivingModeNormal, 6.f, 1.f);
            park_ride_.last_task_at = now;
        }
        return;
    }

    if (park_ride_.stage == ParkRide::Stage::Pickup) {
        const Vehicle target = park_ride_.owned_vehicle;
        const bool player_in_owned = PED::IS_PED_IN_VEHICLE(player, target, false);
        if (player_in_owned && (!park_ride_.player_exit_requested || now - park_ride_.last_task_at > 5000ULL)) {
            AI::TASK_LEAVE_VEHICLE(player, target, 0);
            park_ride_.player_exit_requested = true;
            park_ride_.last_task_at = now;
        }
        if (park_ride_.last_occupant_exit_at == 0 || now - park_ride_.last_occupant_exit_at > 5000ULL) {
            for (int seat = -1; seat <= 6; ++seat) {
                const Ped occupant = VEHICLE::GET_PED_IN_VEHICLE_SEAT(target, seat);
                if (occupant && occupant != player && occupant != park_ride_.valet && ENTITY::DOES_ENTITY_EXIST(occupant)) {
                    AI::TASK_LEAVE_VEHICLE(occupant, target, 0);
                }
            }
            park_ride_.last_occupant_exit_at = now;
        }

        if (exists(park_ride_.valet) && !PED::IS_PED_IN_VEHICLE(park_ride_.valet, park_ride_.taxi, false)) {
            const Vector3 valet_pos = ENTITY::GET_ENTITY_COORDS(park_ride_.valet, true);
            const Vector3 car_pos = ENTITY::GET_ENTITY_COORDS(target, true);
            if (D2(valet_pos, car_pos) < 7.f && ENTITY::GET_ENTITY_SPEED(target) < 3.f) {
                VEHICLE::SET_VEHICLE_DOORS_LOCKED(target, 1);
                if (VEHICLE::IS_VEHICLE_SEAT_FREE(target, -1)) {
                    if (!park_ride_.valet_enter_requested || now - park_ride_.last_task_at > 5000ULL) {
                        AI::TASK_ENTER_VEHICLE(park_ride_.valet, target, 18000, -1, 1.f, 1, 0);
                        park_ride_.valet_enter_requested = true;
                        park_ride_.last_task_at = now;
                    }
                    if (park_ride_.valet_enter_requested && now - park_ride_.stage_at > 16000ULL &&
                        VEHICLE::IS_VEHICLE_SEAT_FREE(target, -1)) {
                        PED::SET_PED_INTO_VEHICLE(park_ride_.valet, target, -1);
                    }
                }
            } else if (D2(valet_pos, car_pos) > 4.f && now - park_ride_.last_task_at > 7000ULL) {
                AI::TASK_GO_STRAIGHT_TO_COORD(park_ride_.valet, car_pos.x, car_pos.y, car_pos.z, 1.2f, 12000,
                                              0.f, 0.f);
                park_ride_.last_task_at = now;
            }
        }

        if (PED::IS_PED_IN_VEHICLE(park_ride_.valet, target, false) &&
            VEHICLE::GET_PED_IN_VEHICLE_SEAT(target, -1) == park_ride_.valet) {
            park_ride_.stage = ParkRide::Stage::Delivering;
            park_ride_.stage_at = park_ride_.last_task_at = now;
            VEHICLE::SET_VEHICLE_HANDBRAKE(target, false);
            VEHICLE::SET_VEHICLE_ENGINE_ON(target, true, true, false);
            AI::TASK_VEHICLE_DRIVE_TO_COORD(park_ride_.valet, target, park_ride_.destination.x,
                                            park_ride_.destination.y, park_ride_.destination.z, 18.f, 0,
                                            ENTITY::GET_ENTITY_MODEL(target), kDrivingModeNormal, 5.f, 1.f);
            AI::TASK_VEHICLE_DRIVE_WANDER(park_ride_.taxi_driver, park_ride_.taxi, 20.f, kDrivingModeNormal);
            Vehicle taxi = park_ride_.taxi;
            Ped taxi_driver = park_ride_.taxi_driver;
            ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&taxi);
            ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&taxi_driver);
            park_ride_.taxi = 0;
            park_ride_.taxi_driver = 0;
            toast_ = "Vale aracınızı aldı; siz olduğunuz yerde kalabilirsiniz";
            toast_until_ = now + 8000;
        }
        return;
    }

    if (park_ride_.stage == ParkRide::Stage::Delivering) {
        if (PED::IS_PED_IN_VEHICLE(player, park_ride_.owned_vehicle, false)) {
            AbortParkRide("player_reentered_vehicle", now);
            return;
        }
        const Vector3 car_pos = ENTITY::GET_ENTITY_COORDS(park_ride_.owned_vehicle, true);
        if (D2(car_pos, park_ride_.destination) < 22.f && ENTITY::GET_ENTITY_SPEED(park_ride_.owned_vehicle) < 4.f) {
            StoreParkRideVehicle();
            return;
        }
        if (now - park_ride_.last_task_at > 20000ULL) {
            AI::TASK_VEHICLE_DRIVE_TO_COORD(park_ride_.valet, park_ride_.owned_vehicle,
                                            park_ride_.destination.x, park_ride_.destination.y,
                                            park_ride_.destination.z, 18.f, 0,
                                            ENTITY::GET_ENTITY_MODEL(park_ride_.owned_vehicle),
                                            kDrivingModeNormal, 5.f, 1.f);
            park_ride_.last_task_at = now;
        }
        return;
    }
}

// Görevli işini bitirince kendine taksi çağırır: taksi gelir, tamirci biner, ikisi de uzaklaşıp kaybolur.
void Garage::StartLeave(Ped mech, const Vector3& player_pos) {
    CleanupLeave(true);
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(mech, true, true);
    const Vector3 mp = ENTITY::GET_ENTITY_COORDS(mech, true);
    // ~100m uzakta kaldırım düğümü bul; oyuncudan uzak taraf tercih edilir
    Vector3 dest = mp;
    bool found = false;
    for (int k = 0; k < 8 && !found; ++k) {
        const float a = static_cast<float>((GetTickCount64() / 103 + k * 73) % 628) / 100.f;
        Vector3 r{};
        if (PATHFIND::GET_CLOSEST_VEHICLE_NODE(mp.x + 105.f * std::cos(a), mp.y + 105.f * std::sin(a), mp.z, &r, 1, 3.0f, 0) &&
            D2(r, player_pos) > 50.f) {
            dest = r;
            found = true;
        }
    }
    if (!found) dest = {mp.x + 105.f, 0, mp.y, 0, mp.z, 0};
    AI::TASK_GO_STRAIGHT_TO_COORD(mech, dest.x, dest.y, dest.z, 1.0f, 60000, dest.x - mp.x, dest.z - mp.z);
    Leave lv{};
    lv.active = true;
    lv.mech = mech;
    lv.since = GetTickCount64();
    lv.walk_dest = dest;
    leave_ = lv;
    Log("vale: uzaklasma basliyor");
}

void Garage::CleanupLeave(bool force) {
    if (!leave_.active) return;
    auto del_ped = [](Ped p) {
        if (p != 0 && ENTITY::DOES_ENTITY_EXIST(p)) {
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, true, true);
            PED::DELETE_PED(&p);
        }
    };
    (void)force;
    del_ped(leave_.mech);
    del_ped(leave_.taxi_driver);
    if (leave_.taxi != 0 && ENTITY::DOES_ENTITY_EXIST(leave_.taxi)) {
        Vehicle t = leave_.taxi;
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(t, true, true);
        VEHICLE::DELETE_VEHICLE(&t);
    }
    leave_ = Leave{};
}

void Garage::UpdateLeave(unsigned long long now, const Vector3& player_pos) {
    if (!leave_.active) return;
    if (!ENTITY::DOES_ENTITY_EXIST(leave_.mech) || ENTITY::IS_ENTITY_DEAD(leave_.mech)) {
        CleanupLeave(true);
        return;
    }
    const Vector3 mp = ENTITY::GET_ENTITY_COORDS(leave_.mech, true);
    const unsigned long long el = now - leave_.since;

    // -- Aşama 1: Yürüme (100m uzaklaş) --
    if (!leave_.walked) {
        const bool far_enough = D2(mp, leave_.walk_dest) < 18.f || D2(mp, player_pos) > 90.f;
        const bool timeout = el > 30000ULL;
        if (far_enough || timeout) {
            AI::CLEAR_PED_TASKS(leave_.mech);
            leave_.walked = true;
            leave_.phone_at = now;
            AI::TASK_USE_MOBILE_PHONE_TIMED(leave_.mech, 4000);  // telefon animasyonu ~4 sn
            Log("vale: 100m uzaklasti, taksi cagiriyor");
        }
        if (el > 120000ULL) { CleanupLeave(true); return; }
        return;
    }

    // -- Aşama 2: Telefon animasyonu bitti -> taksi spawn --
    if (!leave_.phoned) {
        if (now - leave_.phone_at < 4500ULL) return;  // animasyon süresi dolsun
        leave_.phoned = true;
        // Taksiyi vale'nin yanında bir yol düğümüne doğurt
        const Hash tm = HashKey("taxi"), dm = HashKey("a_m_m_skater_01");
        Vector3 node{};
        bool ok = false;
        for (int k = 0; k < 6 && !ok; ++k) {
            const float a = static_cast<float>((now / 101 + k * 97) % 628) / 100.f;
            Vector3 r{};
            ok = PATHFIND::GET_CLOSEST_VEHICLE_NODE(mp.x + 80.f * std::cos(a), mp.y + 80.f * std::sin(a), mp.z, &r, 1, 3.0f, 0) &&
                 D2(r, player_pos) > 30.f;
            if (ok) node = r;
        }
        if (!ok) { CleanupLeave(true); return; }
        if (!LoadModel(tm) || !LoadModel(dm)) { CleanupLeave(true); return; }
        const Vehicle taxi = VEHICLE::CREATE_VEHICLE(tm, node.x, node.y, node.z, 0.f, false, false);
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(tm);
        if (!ENTITY::DOES_ENTITY_EXIST(taxi)) { CleanupLeave(true); return; }
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(taxi, true, true);
        VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(taxi);
        const Ped drv = PED::CREATE_PED_INSIDE_VEHICLE(taxi, 4, dm, -1, false, false);
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(dm);
        if (!ENTITY::DOES_ENTITY_EXIST(drv)) {
            Vehicle t = taxi;
            ENTITY::SET_VEHICLE_AS_NO_LONGER_NEEDED(&t);
            CleanupLeave(true);
            return;
        }
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(drv, true, true);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(drv, true);
        Vector3 pick = mp, r{};
        if (PATHFIND::GET_CLOSEST_VEHICLE_NODE(mp.x, mp.y, mp.z, &r, 1, 3.0f, 0)) pick = r;
        AI::TASK_VEHICLE_DRIVE_TO_COORD(drv, taxi, pick.x, pick.y, pick.z, 18.0f, 0, tm, kDrivingModeNormal, 5.0f, 1.0f);
        leave_.taxi = taxi;
        leave_.taxi_driver = drv;
        leave_.last_push = now;
        Log("vale: taksi yola cikti");
        return;
    }

    // -- Aşama 3: Taksi biniş ve gidiş --
    if (!ENTITY::DOES_ENTITY_EXIST(leave_.taxi) || !ENTITY::DOES_ENTITY_EXIST(leave_.taxi_driver) ||
        ENTITY::IS_ENTITY_DEAD(leave_.taxi_driver)) {
        Ped m = leave_.mech;
        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&m);
        leave_ = Leave{};
        return;
    }
    const Vector3 tp = ENTITY::GET_ENTITY_COORDS(leave_.taxi, true);
    if (!leave_.boarding && D2(tp, mp) > 14.f) {
        if (now - leave_.last_push > 6000ULL) {
            leave_.last_push = now;
            Vector3 pick = mp, r{};
            if (PATHFIND::GET_CLOSEST_VEHICLE_NODE(mp.x, mp.y, mp.z, &r, 1, 3.0f, 0)) pick = r;
            AI::TASK_VEHICLE_DRIVE_TO_COORD(leave_.taxi_driver, leave_.taxi, pick.x, pick.y, pick.z, 20.0f, 0, HashKey("taxi"),
                                            kDrivingModeNormal, 5.0f, 1.0f);
        }
        if (el > 60000ULL && !leave_.teleported) {
            leave_.teleported = true;
            Vector3 n{mp.x + 8.f, 0, mp.y, 0, mp.z, 0};
            Vector3 r{};
            if (PATHFIND::GET_CLOSEST_VEHICLE_NODE(mp.x + 8.f, mp.y, mp.z, &r, 1, 3.0f, 0) && D2(r, mp) < 30.f) n = r;
            ENTITY::SET_ENTITY_COORDS(leave_.taxi, n.x, n.y, n.z, false, false, false, true);
            VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(leave_.taxi);
            Log("vale: taksi yakina isinlandi");
        }
    }
    if (!leave_.boarding && D2(tp, mp) < 14.f) {
        AI::TASK_ENTER_VEHICLE(leave_.mech, leave_.taxi, 12000, 2, 1.0f, 1, 0);
        AI::TASK_VEHICLE_TEMP_ACTION(leave_.taxi_driver, leave_.taxi, kBrakeAndStop, 12000);
        leave_.boarding = true;
        leave_.boarded_at = now;
        Log("vale: taksiye biniyor");
    }
    if (leave_.boarding && !leave_.driving && !PED::IS_PED_IN_VEHICLE(leave_.mech, leave_.taxi, false) &&
        now - leave_.boarded_at > 5000ULL) {
        PED::SET_PED_INTO_VEHICLE(leave_.mech, leave_.taxi, 2);
    }
    if (leave_.boarding && !leave_.driving && PED::IS_PED_IN_VEHICLE(leave_.mech, leave_.taxi, false)) {
        AI::TASK_VEHICLE_DRIVE_WANDER(leave_.taxi_driver, leave_.taxi, 22.0f, kDrivingModeNormal);
        leave_.driving = true;
        Log("vale: taksi yola cikti");
    }
    if (!leave_.boarding && el > 90000ULL) {
        PED::SET_PED_INTO_VEHICLE(leave_.mech, leave_.taxi, 2);
        leave_.boarding = true;
        leave_.boarded_at = now;
    }
    if ((leave_.driving && D2(tp, player_pos) > 160.f) || el > 180000ULL) CleanupLeave(true);
}

bool Garage::OwnsBlip(Blip b) const {
    if (b == 0) return false;
    for (const auto& c : cars_) {
        if (c.blip == b) return true;
    }
    return false;
}

void Garage::DropBlip(Owned& c) {
    if (c.blip != 0 && UI::DOES_BLIP_EXIST(c.blip)) UI::REMOVE_BLIP(&c.blip);
    c.blip = 0;
}

// GTA Online mantığı: sahip olunan araç haritada ikonla görünür (içindeyken görünmez). Kilitsiz ve boşta duran araç belli aralıkla çalınabilir.
void Garage::ManageOwned(unsigned long long now, const Vector3& pos, Vehicle pv) {
    for (size_t i = 0; i < cars_.size(); ++i) {
        Owned& c = cars_[i];
        if (c.handle == 0 || !ENTITY::DOES_ENTITY_EXIST(c.handle)) {
            DropBlip(c);
            continue;
        }
        const bool player_in = pv == c.handle;
        if (player_in) {
            if (c.blip != 0) Log("garaj: ikon kaldirildi (araca binildi) " + c.model);
            DropBlip(c);
        } else {
            // Koordinat ikonu araca her karede bağlanır (varlık ikonu bazı araçlarda görünmüyordu; koordinat ikonu garanti görünür).
            const Vector3 vp = ENTITY::GET_ENTITY_COORDS(c.handle, true);
            if (c.blip == 0 || !UI::DOES_BLIP_EXIST(c.blip)) {
                c.blip = blips::Add(vp, 225, 3, "Kendi Araciniz", 1.1f, false);
                UI::SET_BLIP_AS_SHORT_RANGE(c.blip, false);
                UI::SET_BLIP_PRIORITY(c.blip, 10);
                UI::SET_BLIP_FLASHES(c.blip, true);  // yeni ikon dikkat çeksin
                UI::SET_BLIP_FLASH_TIMER(c.blip, 6000);
                Log("garaj: ikon olusturuldu " + c.model + " blip=" + std::to_string(c.blip));
            } else {
                UI::SET_BLIP_COORDS(c.blip, vp.x, vp.y, vp.z);
            }
            UI::SET_BLIP_DISPLAY(c.blip, 2);  // harita + mini harita (başka bir script gizlemiş olabilir)
            UI::SET_BLIP_ALPHA(c.blip, 255);
            static unsigned long long last_dbg = 0;
            if (now - last_dbg > 15000ULL) {
                last_dbg = now;
                Log("garaj: ikon durumu " + c.model + " blip=" + std::to_string(c.blip) + " var=" +
                    std::to_string(UI::DOES_BLIP_EXIST(c.blip)) + " display=" + std::to_string(UI::GET_BLIP_INFO_ID_DISPLAY(c.blip)) +
                    " konum=" + std::to_string(static_cast<int>(vp.x)) + "," + std::to_string(static_cast<int>(vp.y)));
            }
        }
        if (!cfg_.theft || theft_.active) continue;
        const bool unlocked = VEHICLE::GET_VEHICLE_DOOR_LOCK_STATUS(c.handle) != 2;
        const bool in_valet = (valet_.active && valet_.veh == c.handle) ||
                              (park_ride_.stage != ParkRide::Stage::None && park_ride_.owned_vehicle == c.handle);
        const bool idle = unlocked && !player_in && !in_valet && VEHICLE::GET_PED_IN_VEHICLE_SEAT(c.handle, -1) == 0;
        if (!idle) {
            c.unlocked_since = 0;
            continue;
        }
        const unsigned long long period = static_cast<unsigned long long>(cfg_.period_s) * 1000ULL;
        if (c.unlocked_since == 0) {
            c.unlocked_since = now;
            c.next_roll = now + period;
        } else if (now >= c.next_roll) {
            c.next_roll += period;
            if (street_events::Chance(cfg_.chance)) StartTheft(i, pos);
        }
    }
}

void Garage::Remove(Vehicle veh) {
    for (size_t i = 0; i < cars_.size(); ++i) {
        if (cars_[i].handle != veh) continue;
        DropBlip(cars_[i]);
        cars_.erase(cars_.begin() + static_cast<long>(i));
        break;
    }
    if (ENTITY::DOES_ENTITY_EXIST(veh)) {
        Vehicle v = veh;
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(v, true, true);
        VEHICLE::DELETE_VEHICLE(&v);
    }
    last_used_ = 0;
    Save();
}

void Garage::StartTheft(size_t i, const Vector3& pos) {
    Owned& c = cars_[i];
    const Vector3 vp = ENTITY::GET_ENTITY_COORDS(c.handle, true);
    const float d = D2(pos, vp);
    if (d > cfg_.far_m) {  // uzaktayken çalındı: araç sessizce silinir, uyarı çıkar
        Log("hirsizlik: " + c.model + " uzakta calindi");
        toast_ = "Arabanız çalındı! (" + c.model + ")";
        toast_until_ = GetTickCount64() + 8000;
        Remove(c.handle);
        return;
    }
    // yakındaysa hırsız görünür: araca binip kaçar
    Hash tm = HashKey("g_m_y_mexgoon_01");
    if (!LoadModel(tm)) {
        Remove(c.handle);
        toast_ = "Arabanız çalındı!";
        toast_until_ = GetTickCount64() + 8000;
        return;
    }
    const Vector3 side = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(c.handle, -3.0f, 0.f, 0.f);
    const Ped th = PED::CREATE_PED(4, tm, side.x, side.y, side.z, 0.f, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(tm);
    if (!ENTITY::DOES_ENTITY_EXIST(th)) {
        Remove(c.handle);
        toast_ = "Arabanız çalındı!";
        toast_until_ = GetTickCount64() + 8000;
        return;
    }
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(th, true);
    VEHICLE::SET_VEHICLE_DOORS_LOCKED(c.handle, 1);
    AI::TASK_ENTER_VEHICLE(th, c.handle, 10000, -1, 2.0f, 1, 0);
    theft_ = Theft{true, c.handle, th, false, GetTickCount64()};
    toast_ = "Biri arabanıza giriyor!";
    toast_until_ = GetTickCount64() + 6000;
    Log("hirsizlik basladi: " + c.model);
}

void Garage::UpdateTheft(unsigned long long now, const Vector3& pos) {
    if (!theft_.active) return;
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (!ENTITY::DOES_ENTITY_EXIST(theft_.thief) || !ENTITY::DOES_ENTITY_EXIST(theft_.veh) || ENTITY::IS_ENTITY_DEAD(theft_.thief)) {
        theft_.active = false;
        return;
    }
    if (PED::IS_PED_IN_VEHICLE(player, theft_.veh, false)) {  // sen bindin: hırsız kaçar
        AI::TASK_SMART_FLEE_PED(theft_.thief, player, 120.f, -1, false, false);
        Ped t = theft_.thief;
        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&t);
        theft_.active = false;
        toast_ = "Hırsızı püskürttün!";
        toast_until_ = now + 4000;
        return;
    }
    const bool in = PED::IS_PED_IN_VEHICLE(theft_.thief, theft_.veh, false) != 0;
    if (!in && now - theft_.since > 15000ULL) {  // binemedi: vazgeçer
        Ped t = theft_.thief;
        AI::TASK_WANDER_STANDARD(t, 10.0f, 10);
        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&t);
        theft_.active = false;
        return;
    }
    if (in && !theft_.driving) {
        AI::TASK_VEHICLE_DRIVE_WANDER(theft_.thief, theft_.veh, 26.0f, 786603);
        theft_.driving = true;
        toast_ = "Arabanız çalınıyor!";
        toast_until_ = now + 6000;
    }
    if (in) {
        const Vector3 vp = ENTITY::GET_ENTITY_COORDS(theft_.veh, true);
        if (D2(pos, vp) > cfg_.far_m + 80.f || now - theft_.since > 60000ULL) {  // gözden kayboldu: araç silinir
            Ped t = theft_.thief;
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(t, true, true);
            PED::DELETE_PED(&t);
            const Vehicle v = theft_.veh;
            theft_.active = false;
            Remove(v);
            toast_ = "Arabanız çalındı!";
            toast_until_ = now + 8000;
            Log("hirsizlik: arac calindi");
            crimes::Record("car_theft", pos, 0, false);  // NPC senin aracını çaldı; suç oyuncuya yüklenmez
        }
    }
}

Vehicle Garage::SpawnOwned(Owned& car, const Vector3& at) {
    const Hash model = HashKey(car.model.c_str());
    if (!LoadModel(model)) return 0;
    const Vehicle veh = VEHICLE::CREATE_VEHICLE(model, at.x, at.y, at.z, 0.f, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
    if (!ENTITY::DOES_ENTITY_EXIST(veh)) return 0;
    if (!car.plate.empty()) VEHICLE::SET_VEHICLE_NUMBER_PLATE_TEXT(veh, const_cast<char*>(car.plate.c_str()));
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(veh, true, true);
    VEHICLE::SET_VEHICLE_HAS_BEEN_OWNED_BY_PLAYER(veh, true);
    VEHICLE::SET_VEHICLE_IS_STOLEN(veh, false);
    VEHICLE::SET_VEHICLE_NEEDS_TO_BE_HOTWIRED(veh, false);
    VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(veh);
    crimes::MarkKnown(veh);
    fuel::Set(veh, car.fuel);
    fuel::SetKm(veh, car.km);
    ApplyLook(veh, car.look);
    car.handle = veh;
    return veh;
}

bool Garage::UpdateGarageSpots(const Vector3& pos, bool e_pressed) {
    bool at_house_garage = false;
    for (const auto& g : extra_garages_) at_house_garage |= D2(pos, g) < 10.f;
    if (cars_.empty() || !(at_house_garage || businesses::NearType("garage", pos, cfg_.garage_radius))) {
        if (menu_open_) {
            menu_open_ = false;
            overlay::SetMenu({});
        }
        return false;
    }
    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vehicle pv = PED::GET_VEHICLE_PED_IS_IN(player, false);
    if (pv != 0 && IsOwned(pv) && VEHICLE::GET_PED_IN_VEHICLE_SEAT(pv, -1) == player) {
        ShowHelp("~INPUT_PICKUP~ Araci garaja koy");
        if (!e_pressed) return false;
        for (auto& c : cars_) {
            if (c.handle != pv) continue;
            const float f = fuel::Get(pv);
            if (f >= 0.f) c.fuel = f;
            const float km = fuel::GetKm(pv);
            if (km >= 0.f) c.km = km;
            c.look = CaptureLook(pv);
            DropBlip(c);
            Vehicle v = pv;
            AI::TASK_LEAVE_VEHICLE(player, v, 16);  // çıkar (hemen)
            for (int i = 0; i < 40 && PED::IS_PED_IN_VEHICLE(player, v, false); ++i) WAIT(0);
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(v, true, true);
            VEHICLE::DELETE_VEHICLE(&v);
            c.handle = 0;
            c.unlocked_since = 0;
            toast_ = "Araç garaja konuldu (güvende)";
            toast_until_ = GetTickCount64() + 4000;
            Save();
            break;
        }
        return true;
    }
    if (pv != 0) return false;
    // yaya: garajdaki (dünyada olmayan) araçlar menüde listelenir; E menüyü açar
    std::vector<size_t> stored;
    for (size_t i = 0; i < cars_.size(); ++i) {
        if (cars_[i].handle == 0) stored.push_back(i);
    }
    if (stored.empty()) {
        menu_open_ = false;
        return false;
    }
    if (!menu_open_) {
        ShowHelp("~INPUT_PICKUP~ Garaj: arac sec");
        if (!e_pressed) return false;
        menu_open_ = true;
        menu_sel_ = 0;
        return true;
    }
    CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
    const int n = static_cast<int>(stored.size());
    if (keys_.Repeat(VK_DOWN)) menu_sel_ = (menu_sel_ + 1) % n;
    if (keys_.Repeat(VK_UP)) menu_sel_ = (menu_sel_ + n - 1) % n;
    menu_sel_ = std::clamp(menu_sel_, 0, n - 1);
    overlay::MenuView mv;
    mv.open = true;
    mv.title = "Garaj";
    mv.subtitle = "Almak istedigin araci sec";
    mv.hint = "↑↓ sec   Enter cikar   Backspace vazgec";
    for (size_t idx : stored) {
        const Owned& c = cars_[idx];
        mv.rows.push_back({c.model, c.plate + "  yakit %" + std::to_string(static_cast<int>(c.fuel)), false});
    }
    mv.selected = menu_sel_;
    overlay::SetMenu(mv);
    if (keys_.Pressed(VK_BACK) || D2(pos, ENTITY::GET_ENTITY_COORDS(player, true)) > cfg_.garage_radius + 6.f) {
        menu_open_ = false;
        overlay::SetMenu({});
        return true;
    }
    if (!keys_.Pressed(VK_RETURN)) return true;
    menu_open_ = false;
    overlay::SetMenu({});
    const size_t pick = stored[static_cast<size_t>(menu_sel_)];
    Vector3 at = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(player, 0.f, 5.f, 0.f);
    Vector3 nn{};
    if (PATHFIND::GET_CLOSEST_VEHICLE_NODE(pos.x, pos.y, pos.z, &nn, 1, 3.0f, 0)) at = nn;
    const Vehicle v = SpawnOwned(cars_[pick], at);
    if (v == 0) {
        toast_ = "Araç garajdan çıkarılamadı";
    } else {
        VEHICLE::SET_VEHICLE_DOORS_LOCKED(v, 1);
        last_used_ = pick;
        toast_ = "Araç garajdan çıkarıldı: " + cars_[pick].model;
    }
    toast_until_ = GetTickCount64() + 4000;
    return true;
}

std::vector<Garage::CarInfo> Garage::List() const {
    std::vector<CarInfo> out;
    for (const auto& c : cars_) out.push_back({c.model, c.plate, static_cast<int>(c.fuel), c.km, c.handle != 0 && ENTITY::DOES_ENTITY_EXIST(c.handle)});
    return out;
}

std::string Garage::AddStored(const std::string& model, float km) {
    Owned o;
    o.model = model;
    char plate[16];
    std::snprintf(plate, sizeof(plate), "HK%05d", static_cast<int>(GetTickCount64() % 100000ULL));
    o.plate = plate;
    o.fuel = 60.f;  // ikinci el araç yarım depo gelir
    o.km = km;      // ikinci el: ilan kilometresi; galeri (Register) sıfır km ile başlar
    cars_.push_back(std::move(o));
    Save();
    Log("garaj: ikinci el arac eklendi " + model + " (" + std::to_string(static_cast<int>(km)) + " km)");
    return plate;
}

bool Garage::SellByPlate(const std::string& plate) {
    for (size_t i = 0; i < cars_.size(); ++i) {
        if (cars_[i].plate != plate) continue;
        DropBlip(cars_[i]);
        if (cars_[i].handle != 0 && ENTITY::DOES_ENTITY_EXIST(cars_[i].handle)) {
            Vehicle v = cars_[i].handle;
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(v, true, true);
            VEHICLE::DELETE_VEHICLE(&v);
        }
        cars_.erase(cars_.begin() + static_cast<long>(i));
        last_used_ = 0;
        Save();
        return true;
    }
    return false;
}

bool Garage::IsOwned(Vehicle v) const {
    if (v == 0 || !ENTITY::DOES_ENTITY_EXIST(v)) return false;
    const char* plate = VEHICLE::GET_VEHICLE_NUMBER_PLATE_TEXT(v);
    if (plate && plate[0] != '\0') {
        for (const auto& c : cars_) {
            if (!c.plate.empty() && c.plate == plate) return true;
        }
    }
    for (const auto& c : cars_) {
        if (c.handle != 0 && c.handle == v) return true;
    }
    return false;
}

std::vector<Vehicle> Garage::Handles() const {
    std::vector<Vehicle> out;
    for (const auto& c : cars_) {
        if (c.handle != 0 && ENTITY::DOES_ENTITY_EXIST(c.handle)) out.push_back(c.handle);
    }
    return out;
}

void Garage::Shutdown() {
    CleanupParkRide(true);
    Save();
    if (valet_.active && ENTITY::DOES_ENTITY_EXIST(valet_.driver)) {
        Ped d = valet_.driver;
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(d, true, true);
        PED::DELETE_PED(&d);
    }
    valet_ = Valet{};
    CleanupLeave(true);
    if (menu_open_) overlay::SetMenu({});
    menu_open_ = false;
    for (auto& c : cars_) {  // moddan çıkarken araçlar garaja döner (yakıt kaydedilir), dünyada sızıntı kalmaz
        DropBlip(c);
        if (c.handle != 0 && ENTITY::DOES_ENTITY_EXIST(c.handle)) {
            const float f = fuel::Get(c.handle);
            if (f >= 0.f) c.fuel = f;
            Vehicle v = c.handle;
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(v, true, true);
            VEHICLE::DELETE_VEHICLE(&v);
        }
        c.handle = 0;
    }
    theft_ = Theft{};
    Save();
}

}  // namespace hikaye
