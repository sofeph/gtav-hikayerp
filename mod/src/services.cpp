#include "services.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <fstream>

#include "crimes.h"
#include "blips.h"
#include "economy.h"
#include "fuel.h"
#include "street_events.h"

using nlohmann::json;

namespace hikaye {

namespace {
constexpr int kBrakeAndStop = 27;           // TASK_VEHICLE_TEMP_ACTION: dur
constexpr int kDrivingModeNormal = 786603;  // trafik kurallarına uyan sürüş
constexpr int kHeliLand = 20;               // TASK_HELI_MISSION: alçal ve in
constexpr int kHeliGoto = 4;                // TASK_HELI_MISSION: hedefe git
constexpr float kRoadFactor = 1.25f;        // kuş uçuşu mesafe -> yol mesafesi tahmini

float D2(const Vector3& a, const Vector3& b) { return Places::Dist2D(a, b); }

float HeadingTo(const Vector3& from, const Vector3& to) {
    return std::atan2(-(to.x - from.x), to.y - from.y) * 57.29578f;
}

unsigned long long Secs(const json& cfg, const char* key, int def) {
    return static_cast<unsigned long long>(cfg.value(key, def)) * 1000ULL;
}
}  // namespace

const char* Services::Name(ServiceKind k) {
    switch (k) {
        case ServiceKind::Taxi: return "taxi";
        case ServiceKind::HeliTaxi: return "heli_taxi";
        case ServiceKind::Police: return "police";
        case ServiceKind::Ambulance: return "ambulance";
        case ServiceKind::Fire: return "fire";
        case ServiceKind::Mechanic: return "mechanic";
    }
    return "?";
}

bool Services::IsHiredService(Ped p, Vehicle v) const {
    const auto hired = [](ServiceKind k) { return k == ServiceKind::Mechanic || k == ServiceKind::Taxi || k == ServiceKind::HeliTaxi; };
    for (const auto& d : disp_) {
        if (!hired(d.kind)) continue;
        if (v != 0 && d.veh == v) return true;
        for (int i = 0; i < d.crew_n; ++i) {
            if (p != 0 && d.crew[i] == p) return true;
        }
    }
    if (ride_.active) {
        if (v != 0 && ride_.veh == v) return true;
        if (p != 0 && ride_.driver == p) return true;
    }
    return false;
}

bool Services::HiredServiceNear(const Vector3& pos, float radius) const {
    const auto near_ = [&](Entity e) {
        if (e == 0 || !ENTITY::DOES_ENTITY_EXIST(e)) return false;
        const Vector3 q = ENTITY::GET_ENTITY_COORDS(e, true);
        return std::sqrt((q.x - pos.x) * (q.x - pos.x) + (q.y - pos.y) * (q.y - pos.y)) <= radius;
    };
    for (const auto& d : disp_) {
        if (d.kind != ServiceKind::Mechanic && d.kind != ServiceKind::Taxi && d.kind != ServiceKind::HeliTaxi) continue;
        if (near_(d.veh)) return true;
        for (int i = 0; i < d.crew_n; ++i) {
            if (near_(d.crew[i])) return true;
        }
    }
    return ride_.active && (near_(ride_.veh) || near_(ride_.driver));
}

const char* Services::Label(ServiceKind k) {
    switch (k) {
        case ServiceKind::Taxi: return "Taksi";
        case ServiceKind::HeliTaxi: return "Helikopter taksi";
        case ServiceKind::Police: return "Polis";
        case ServiceKind::Ambulance: return "Ambulans";
        case ServiceKind::Fire: return "İtfaiye";
        case ServiceKind::Mechanic: return "Tamirci";
    }
    return "?";
}

std::vector<Services::Status> Services::Statuses() const {
    std::vector<Status> out;
    for (ServiceKind k : {ServiceKind::Taxi, ServiceKind::HeliTaxi, ServiceKind::Police, ServiceKind::Ambulance, ServiceKind::Fire, ServiceKind::Mechanic}) {
        Status st{k, "", 0};
        for (const auto& d : disp_) {
            if (d.kind != k || d.dead || !ENTITY::DOES_ENTITY_EXIST(d.veh)) continue;
            if (d.s == Dispatched::S::Arriving) {
                const float avg = cfg_.value(Name(k), json::object()).value("avg_speed_mps", 14.0f);
                const float path = D2(ENTITY::GET_ENTITY_COORDS(d.veh, true), d.target) * (k == ServiceKind::HeliTaxi ? 1.f : kRoadFactor);
                st.state = "yolda";
                st.eta_s = static_cast<int>(path / std::max(avg, 1.f));
            } else if (d.s == Dispatched::S::Leaving) {
                st.state = "ayrılıyor";
            } else {
                st.state = "geldi";
            }
        }
        out.push_back(st);
    }
    return out;
}

void Services::Load() {
    try {
        std::ifstream in(std::string(kBaseDir) + "/data/services.json");
        if (in) cfg_ = json::parse(in);
        else Log("services.json yok: hizmetler calismaz");
    } catch (const std::exception& e) {
        Log(std::string("services.json okunamadi: ") + e.what());
    }
}

bool Services::RequestModel(Hash model) {
    if (!STREAMING::IS_MODEL_IN_CDIMAGE(model) || !STREAMING::IS_MODEL_VALID(model)) return false;
    STREAMING::REQUEST_MODEL(model);
    for (int i = 0; i < 200 && !STREAMING::HAS_MODEL_LOADED(model); ++i) WAIT(0);  // script thread'inde WAIT güvenli
    return STREAMING::HAS_MODEL_LOADED(model) != 0;
}

bool Services::FindRoadSpawn(const Vector3& around, float min_dist, Vector3& out) const {
    for (int nth : {25, 40, 60, 90, 130, 180}) {
        Vector3 v{};
        if (PATHFIND::GET_NTH_CLOSEST_VEHICLE_NODE(around.x, around.y, around.z, nth, &v, 1, 0, 0)) {
            const float d = D2(around, v);
            if (d >= min_dist && d <= 600.f) {
                out = v;
                return true;
            }
        }
    }
    Vector3 v{};
    float heading = 0.f;
    if (PATHFIND::GET_RANDOM_VEHICLE_NODE(around.x, around.y, around.z, 250.f, false, false, false, &v, &heading) &&
        D2(around, v) >= 60.f) {
        out = v;
        return true;
    }
    return false;
}

Ped Services::SpawnCrew(Vehicle veh, const std::string& model, int seat, int ped_type) {
    const Hash m = HashKey(model.c_str());
    if (!RequestModel(m)) return 0;
    Ped p = PED::CREATE_PED_INSIDE_VEHICLE(veh, ped_type, m, seat, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(m);
    if (!ENTITY::DOES_ENTITY_EXIST(p)) return 0;
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, true, true);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(p, true);
    PED::SET_DRIVER_ABILITY(p, 1.0f);
    return p;
}

// ---------------- Helikopter inişi ----------------

namespace {
// Aday sırası: önce hedefin kendisi, sonra 15/30/45 m yarıçaplı üç halkada 8'er yön.
Vector3 LandCandidate(const Vector3& center, int idx) {
    if (idx <= 0) return center;
    const int ring = (idx - 1) / 8;
    const float r = 15.f * static_cast<float>(ring + 1);
    const float a = static_cast<float>((idx - 1) % 8) * 0.785398f;  // 45°
    return {center.x + r * std::cos(a), 0, center.y + r * std::sin(a), 0, center.z, 0};
}
}  // namespace

void Services::LandBegin(HeliLanding& l, Ped pilot, Vehicle veh, const Vector3& target) {
    const json kcfg = cfg_.value("heli_taxi", json::object());
    l = HeliLanding{};
    l.target = target;
    l.p = HeliLanding::P::Cruise;
    l.t = GetTickCount64();
    // Hedefin üstüne YÜKSEKTEN yaklaş (binaların üstünden); inişe son anda karar verilir.
    AI::TASK_HELI_MISSION(pilot, veh, 0, 0, target.x, target.y, target.z + kcfg.value("cruise_height_m", 130.0f), kHeliGoto,
                          kcfg.value("cruise_speed_mps", 40.0f), 20.0f, -1.0f, 250,
                          static_cast<int>(kcfg.value("cruise_min_height_m", 130.0f)), 30.0f, 0);
}

int Services::CheckSpot(const Vector3& c, float ref_z, Vehicle veh, Vector3& spot) const {
    float gz = 0.f;
    if (!GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(c.x, c.y, ref_z + 120.f, &gz, false)) return 1;  // çarpışma verisi yüklenmedi
    if (std::fabs(gz - ref_z) > 4.5f) return 2;  // çatı, yamaç veya yüksek bir yapı

    // Düzlük: çevresindeki dört noktanın zemini yaklaşık aynı seviyede olmalı.
    static const float offs[4][2] = {{6.f, 0.f}, {-6.f, 0.f}, {0.f, 6.f}, {0.f, -6.f}};
    for (const auto& o : offs) {
        float z2 = 0.f;
        if (!GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(c.x + o[0], c.y + o[1], gz + 6.f, &z2, false)) return 2;
        if (std::fabs(z2 - gz) > 1.2f) return 2;
    }
    // Su: denize/havuza inme.
    float water = 0.f;
    if (WATER::GET_WATER_HEIGHT(c.x, c.y, gz + 1.f, &water) && water > gz - 0.5f) return 2;

    // Rotor payı: alanın üstünde 7 m yarıçaplı bir silindir boyunca (ağaç, direk, kablo, yapı) engel olmamalı.
    const int ray = WORLDPROBE::_CAST_3D_RAY_POINT_TO_POINT(c.x, c.y, gz + 3.f, c.x, c.y, gz + 40.f, 7.0f, 273, veh, 7);
    BOOL hit = FALSE;
    Vector3 end{}, normal{};
    Entity hit_entity = 0;
    int status = 1;
    for (int i = 0; i < 6 && status == 1; ++i) {
        status = WORLDPROBE::_GET_RAYCAST_RESULT(ray, &hit, &end, &normal, &hit_entity);
        if (status == 1) WAIT(0);
    }
    if (hit) return 2;

    spot = {c.x, 0, c.y, 0, gz, 0};
    return 0;
}

bool Services::LandUpdate(HeliLanding& l, Ped pilot, Vehicle veh) {
    using P = HeliLanding::P;
    const json kcfg = cfg_.value("heli_taxi", json::object());
    const unsigned long long now = GetTickCount64();
    const Vector3 vp = ENTITY::GET_ENTITY_COORDS(veh, true);
    const float agl = ENTITY::GET_ENTITY_HEIGHT_ABOVE_GROUND(veh);
    const float speed = ENTITY::GET_ENTITY_SPEED(veh);

    auto hover_over_spot = [&] {  // alanın 45 m üstünde asıl
        AI::TASK_HELI_MISSION(pilot, veh, 0, 0, l.spot.x, l.spot.y, l.spot.z + kcfg.value("hover_height_m", 45.0f), kHeliGoto,
                              12.0f, 3.0f, -1.0f, 250, 30, 10.0f, 0);
        l.p = P::Align;
        l.t = now;
    };
    auto start_descent = [&] {  // sakin, dikey iniş
        AI::TASK_HELI_MISSION(pilot, veh, 0, 0, l.spot.x, l.spot.y, l.spot.z, kHeliLand, kcfg.value("land_speed_mps", 4.0f),
                              2.0f, -1.0f, 100, 0, 5.0f, 32);
        l.p = P::Descend;
        l.t = now;
    };

    switch (l.p) {
        case P::Cruise:
            if (D2(vp, l.target) < 110.f) {  // yaklaştı: yavaşla ve hedefin üstünde asılı kal, alan aramaya başla
                AI::TASK_HELI_MISSION(pilot, veh, 0, 0, l.target.x, l.target.y, vp.z, kHeliGoto, 14.0f, 10.0f, -1.0f, 250, 40,
                                      20.0f, 0);
                l.p = P::Search;
                l.t = now;
                l.cand = 0;
            }
            break;
        case P::Search: {
            STREAMING::REQUEST_COLLISION_AT_COORD(l.target.x, l.target.y, l.target.z);
            if (D2(vp, l.target) > 70.f && now - l.t < 25000) break;  // zemin verisi için yeterince yaklaş
            if (l.cand >= kLandCandidates) {  // uygun alan bulunamadı: hedefin üstünden in
                l.spot = l.target;
                hover_over_spot();
                break;
            }
            Vector3 spot{};
            const int r = CheckSpot(LandCandidate(l.target, l.cand), l.target.z, veh, spot);  // karede bir aday
            if (r == 0) {
                l.spot = spot;
                hover_over_spot();
            } else if (r == 2 || ++l.no_ground > 120) {
                ++l.cand;
                l.no_ground = 0;
            }
            break;
        }
        case P::Align:
            if ((D2(vp, l.spot) < 7.f && speed < 4.f) || now - l.t > 25000) start_descent();
            break;
        case P::Descend:
            if (agl < 2.2f && speed < 2.0f) {
                l.p = P::Landed;
                return true;
            }
            if (D2(vp, l.spot) > 20.f && now - l.t > 4000) hover_over_spot();  // sürüklendi: tekrar hizala
            if (now - l.t > 45000) {  // takılırsa vazgeç
                l.p = P::Landed;
                return true;
            }
            break;
        case P::Landed:
            return true;
        case P::Idle:
            break;
    }
    return false;
}

json Services::Dispatch(ServiceKind kind) {
    const char* name = Name(kind);
    auto refuse = [&](const char* reason) { return json{{"service", name}, {"accepted", false}, {"reason", reason}}; };

    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vector3 pos = ENTITY::GET_ENTITY_COORDS(player, true);
    const json dcfg = cfg_.value("dispatch", json::object());
    const json kcfg = cfg_.value(name, json::object());
    const unsigned long long now = GetTickCount64();
    const int idx = static_cast<int>(kind);

    if (kcfg.empty()) return refuse("service_unavailable");
    for (const auto& d : disp_) {
        if (d.kind == kind && d.s != Dispatched::S::Leaving) return refuse("already_on_the_way");
    }
    if (last_dispatch_[idx] != 0 && now - last_dispatch_[idx] < Secs(dcfg, "cooldown_s", 45)) return refuse("cooldown");
    if (ride_.active) return refuse("busy_in_ride");
    const bool is_ride_service = kind == ServiceKind::Taxi || kind == ServiceKind::HeliTaxi;
    if (is_ride_service && PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) > 0) return refuse("wanted_level");

    // Tamirci: oyuncunun SON aracı yakında olmalı; ücret önceden tahmin edilir, para en az taban ücrete yetmeli.
    Vehicle fix_veh = 0;
    int fix_fee = 0;
    if (kind == ServiceKind::Mechanic) {
        fix_veh = PED::GET_VEHICLE_PED_IS_IN(player, true);
        if (fix_veh == 0 || !ENTITY::DOES_ENTITY_EXIST(fix_veh)) return refuse("no_vehicle");
        if (D2(ENTITY::GET_ENTITY_COORDS(fix_veh, true), pos) > 250.f) return refuse("vehicle_too_far");
        const int base = kcfg.value("base_fee", 120);
        const float hp = std::max(0.f, std::min(1000.f, VEHICLE::GET_VEHICLE_ENGINE_HEALTH(fix_veh)));
        const float f = fuel::Get(fix_veh);
        fix_fee = base + static_cast<int>((1000.f - hp) * kcfg.value("repair_per_hp", 0.15f)) +
                  static_cast<int>(std::ceil((100.f - (f < 0.f ? 50.f : f)) * kcfg.value("fuel_per_pct", 1.2f)));
        if (economy::GetCash() < base) return refuse("insufficient_funds");
    }

    // Doğma noktası: helikopter havada, diğerleri yolda.
    const bool heli = kind == ServiceKind::HeliTaxi;
    Vector3 spawn{};
    const float min_d = dcfg.value("spawn_distance_m", 140.0f);
    if (heli) {
        const float a = static_cast<float>(now % 628) / 100.f;  // rastgele yön
        spawn = {pos.x + 220.f * std::cos(a), 0, pos.y + 220.f * std::sin(a), 0, pos.z + 90.f, 0};
    } else if (!FindRoadSpawn(pos, min_d, spawn)) {
        return refuse("no_road_nearby");
    }

    const Hash vm = HashKey(kcfg.value("vehicle", "").c_str());
    if (!RequestModel(vm)) return refuse("vehicle_unavailable");
    Vehicle veh = VEHICLE::CREATE_VEHICLE(vm, spawn.x, spawn.y, spawn.z, HeadingTo(spawn, pos), false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(vm);
    if (!ENTITY::DOES_ENTITY_EXIST(veh)) return refuse("vehicle_unavailable");
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(veh, true, true);
    VEHICLE::SET_VEHICLE_DOORS_LOCKED(veh, 1);  // kapılar açık (yolcu binebilsin)
    VEHICLE::SET_VEHICLE_ENGINE_ON(veh, true, true, false);
    if (heli) VEHICLE::SET_HELI_BLADES_FULL_SPEED(veh);

    Dispatched d;
    d.id = next_id_++;
    d.kind = kind;
    d.veh = veh;
    d.origin = spawn;
    d.created = now;
    d.t = now;

    // Mürettebat
    std::string driver_model = kcfg.value("model", kcfg.value("pilot_model", ""));
    if (kind == ServiceKind::Taxi) {
        const json models = kcfg.value("driver_models", json::array());
        if (!models.empty()) driver_model = models[now % models.size()].get<std::string>();
    }
    const int ped_type = kind == ServiceKind::Police ? 6 : 4;
    d.crew[0] = SpawnCrew(veh, driver_model, -1, ped_type);
    d.crew_n = d.crew[0] ? 1 : 0;
    if (kind == ServiceKind::Police || kind == ServiceKind::Ambulance || kind == ServiceKind::Fire) {
        d.crew[1] = SpawnCrew(veh, driver_model, 0, ped_type);
        if (d.crew[1]) d.crew_n = 2;
    }
    if (!d.crew[0]) {  // sürücü doğmadıysa hizmet gelemez
        Vehicle v = veh;
        ENTITY::SET_VEHICLE_AS_NO_LONGER_NEEDED(&v);
        return refuse("crew_unavailable");
    }
    if (kind == ServiceKind::Taxi || kind == ServiceKind::HeliTaxi || kind == ServiceKind::Mechanic) {
        crimes::MarkKnown(veh);
        VEHICLE::SET_VEHICLE_HAS_BEEN_OWNED_BY_PLAYER(veh, true);
        VEHICLE::SET_VEHICLE_IS_STOLEN(veh, false);
        VEHICLE::SET_VEHICLE_NEEDS_TO_BE_HOTWIRED(veh, false);
    }

    // Hedef: oyuncunun yanındaki yol (helikopter doğrudan oyuncunun konumuna iner)
    d.target = kind == ServiceKind::Mechanic ? ENTITY::GET_ENTITY_COORDS(fix_veh, true) : pos;
    d.target_veh = fix_veh;
    d.fee = fix_fee;
    Vector3 road{};
    if (!heli && PATHFIND::GET_CLOSEST_VEHICLE_NODE(d.target.x, d.target.y, d.target.z, &road, 1, 3.0f, 0)) d.target = road;
    const float speed = kcfg.value("drive_speed_mps", 24.0f);
    if (heli) {
        LandBegin(d.land, d.crew[0], veh, d.target);  // yüksekten yaklaş, uygun alan bul, sakin dikey in
    } else {
        AI::TASK_VEHICLE_DRIVE_TO_COORD(d.crew[0], veh, d.target.x, d.target.y, d.target.z, speed, 0, vm,
                                        kDrivingModeNormal, 5.0f, 1.0f);
    }

    d.blip = blips::AddEntityBlip(veh, 1, kind == ServiceKind::Police ? 3 : (kind == ServiceKind::Ambulance ? 2 : 5), Label(kind), 1.0f);

    const float avg = kcfg.value("avg_speed_mps", 14.0f);
    const float path = D2(spawn, pos) * (heli ? 1.0f : kRoadFactor);
    json facts = {{"service", name}, {"accepted", true}, {"eta_seconds", static_cast<int>(path / std::max(avg, 1.f))}};
    if (kind == ServiceKind::Mechanic) facts["estimated_fee"] = fix_fee;
    if (is_ride_service) {
        facts["fare_info"] = {{"base_fare", kcfg.value("base_fare", 0)}, {"per_km", kcfg.value("per_km", 0)}};
    }
    last_dispatch_[idx] = now;
    disp_.push_back(d);
    Log(std::string("hizmet cagrildi: ") + name + " eta=" + std::to_string(facts["eta_seconds"].get<int>()) + "s");
    return facts;
}

void Services::SendAway(Dispatched& d) {
    d.s = Dispatched::S::Leaving;
    d.t = GetTickCount64();
    d.in_ride = false;
    const Ped drv = d.crew[0];
    if (!drv || !ENTITY::DOES_ENTITY_EXIST(drv) || !ENTITY::DOES_ENTITY_EXIST(d.veh)) return;
    if (d.kind == ServiceKind::HeliTaxi) {
        // Yerdeyse önce dikey yüksel (UpdateDispatch irtifa yetince uzaklaştırır); havadaysa doğrudan uzaklaş.
        const Vector3 vp = ENTITY::GET_ENTITY_COORDS(d.veh, true);
        AI::TASK_HELI_MISSION(drv, d.veh, 0, 0, vp.x, vp.y, vp.z + 80.f, kHeliGoto, 10.0f, 5.0f, -1.0f, 250, 45, 10.0f, 0);
        d.phase = 1;
    } else {
        AI::TASK_VEHICLE_DRIVE_TO_COORD(drv, d.veh, d.origin.x, d.origin.y, d.origin.z, 18.0f, 0,
                                        ENTITY::GET_ENTITY_MODEL(d.veh), kDrivingModeNormal, 10.0f, 1.0f);
    }
}

void Services::Cleanup(Dispatched& d) {
    if (d.blip != 0 && UI::DOES_BLIP_EXIST(d.blip)) UI::REMOVE_BLIP(&d.blip);
    for (int i = 0; i < 2; ++i) {
        if (d.crew[i] != 0 && ENTITY::DOES_ENTITY_EXIST(d.crew[i])) {
            PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(d.crew[i], false);
            ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&d.crew[i]);  // oyun yönetsin
        }
    }
    if (ENTITY::DOES_ENTITY_EXIST(d.veh)) ENTITY::SET_VEHICLE_AS_NO_LONGER_NEEDED(&d.veh);
    d.dead = true;
}

Services::Dispatched* Services::FindById(int id) {
    for (auto& d : disp_) {
        if (d.id == id) return &d;
    }
    return nullptr;
}

void Services::UpdateDispatch(Dispatched& d, const Vector3& player_pos) {
    const unsigned long long now = GetTickCount64();
    if (!ENTITY::DOES_ENTITY_EXIST(d.veh) || ENTITY::IS_ENTITY_DEAD(d.crew[0])) {
        if (d.in_ride) EndRide();
        return Cleanup(d);
    }
    const json dcfg = cfg_.value("dispatch", json::object());
    const json kcfg = cfg_.value(Name(d.kind), json::object());
    const Vector3 vp = ENTITY::GET_ENTITY_COORDS(d.veh, true);
    const bool heli = d.kind == ServiceKind::HeliTaxi;
    const Ped player = PLAYER::PLAYER_PED_ID();

    switch (d.s) {
        case Dispatched::S::Arriving: {
            const bool arrived = heli ? LandUpdate(d.land, d.crew[0], d.veh)
                                      : (D2(vp, player_pos) < 30.f || D2(vp, d.target) < 25.f);
            if (arrived) {
                d.s = Dispatched::S::Arrived;
                d.t = now;
                if (!heli) AI::TASK_VEHICLE_TEMP_ACTION(d.crew[0], d.veh, kBrakeAndStop, 60000);
                Log(std::string("hizmet vardi: ") + Name(d.kind));
            } else if (now - d.created > Secs(dcfg, "give_up_s", 300)) {
                SendAway(d);  // yolda takıldı
            }
            break;
        }
        case Dispatched::S::Arrived:
            if (d.kind == ServiceKind::Taxi || heli) {
                if (!d.in_ride && now - d.t > Secs(dcfg, "wait_timeout_s", 120)) SendAway(d);  // binmedin
            } else if (d.kind == ServiceKind::Police) {
                for (int i = 0; i < d.crew_n; ++i) AI::TASK_LEAVE_VEHICLE(d.crew[i], d.veh, 0);
                d.s = Dispatched::S::Working;
                d.t = now;
            } else {
                d.s = Dispatched::S::Working;
                d.t = now;
            }
            break;
        case Dispatched::S::Working:
            if (d.kind == ServiceKind::Police) {
                if (!d.crew_out && now - d.t > Secs(kcfg, "stay_s", 20)) {  // görev süresi doldu: araca bin
                    for (int i = 0; i < d.crew_n; ++i) AI::TASK_ENTER_VEHICLE(d.crew[i], d.veh, 8000, i == 0 ? -1 : 0, 2.0f, 1, 0);
                    d.crew_out = true;
                    d.t = now;
                } else if (d.crew_out && now - d.t > 9000) {
                    SendAway(d);
                }
            } else if (d.kind == ServiceKind::Ambulance) {
                const Ped medic = d.crew[1] ? d.crew[1] : d.crew[0];
                if (!d.crew_out) {  // sağlıkçı araçtan iner ve sana yürür
                    AI::TASK_LEAVE_VEHICLE(medic, d.veh, 0);
                    d.crew_out = true;
                    d.t = now;
                } else if (!d.done_effect) {
                    if (now - d.t > 1500 && now - d.t < 1600) AI::TASK_GO_TO_ENTITY(medic, player, -1, 2.0f, 2.0f, 0, 0);
                    if (now - d.t > 1500 && D2(ENTITY::GET_ENTITY_COORDS(medic, true), player_pos) < 4.f) {
                        ENTITY::SET_ENTITY_HEALTH(player, ENTITY::GET_ENTITY_MAX_HEALTH(player));
                        const int fee = kcfg.value("fee", 300);
                        economy::Spend(fee);  // para yetmiyorsa ücretsiz tedavi (acil durum)
                        AI::TASK_ENTER_VEHICLE(medic, d.veh, 8000, d.crew[1] ? 0 : -1, 2.0f, 1, 0);
                        d.done_effect = true;
                        d.t = now;
                        Log("ambulans: iyilestirildi");
                    } else if (now - d.t > 40000) {
                        SendAway(d);  // sağlıkçı sana ulaşamadı
                    }
                } else if (now - d.t > 9000) {
                    SendAway(d);
                }
            } else if (d.kind == ServiceKind::Mechanic) {
                const Ped mech = d.crew[0];
                if (!ENTITY::DOES_ENTITY_EXIST(d.target_veh)) {
                    SendAway(d);
                } else if (!d.crew_out) {  // tamirci araçtan iner, bozuk araca yürür
                    AI::TASK_LEAVE_VEHICLE(mech, d.veh, 0);
                    d.crew_out = true;
                    d.t = now;
                } else if (!d.done_effect) {
                    const Vector3 tv = ENTITY::GET_ENTITY_COORDS(d.target_veh, true);
                    if (d.work_phase == 0) {
                        if (now - d.t > 1500 && now - d.t < 1600) AI::TASK_GO_TO_ENTITY(mech, d.target_veh, -1, 2.5f, 2.0f, 0, 0);
                        if (now - d.t > 1500 && D2(ENTITY::GET_ENTITY_COORDS(mech, true), tv) < 5.f) {
                            AI::TASK_START_SCENARIO_IN_PLACE(mech, const_cast<char*>("WORLD_HUMAN_VEHICLE_MECHANIC"), 0, true);
                            d.work_phase = 1;
                            d.work_since = now;
                        } else if (now - d.t > 45000) {
                            SendAway(d);  // tamirci araca ulaşamadı
                        }
                    } else if (now - d.work_since > 7000) {  // onarım bitti
                        VEHICLE::SET_VEHICLE_FIXED(d.target_veh);
                        VEHICLE::SET_VEHICLE_DEFORMATION_FIXED(d.target_veh);
                        VEHICLE::SET_VEHICLE_ENGINE_HEALTH(d.target_veh, 1000.f);
                        VEHICLE::SET_VEHICLE_PETROL_TANK_HEALTH(d.target_veh, 1000.f);
                        VEHICLE::SET_VEHICLE_DIRT_LEVEL(d.target_veh, 0.f);
                        fuel::Set(d.target_veh, 100.f);  // depo de dolar
                        VEHICLE::SET_VEHICLE_ENGINE_ON(d.target_veh, true, true, false);
                        economy::Spend(d.fee);  // yetmiyorsa ücretsiz (acil yardım, ambulansla aynı mantık)
                        crimes::MarkKnown(d.target_veh);  // onarılan araca binmek car_theft sayılmaz
                        AI::TASK_ENTER_VEHICLE(mech, d.veh, 8000, -1, 2.0f, 1, 0);
                        d.done_effect = true;
                        d.t = now;
                        Log("tamirci: arac onarildi, ucret " + std::to_string(d.fee));
                    }
                } else if (now - d.t > 9000) {
                    SendAway(d);
                }
            } else if (d.kind == ServiceKind::Fire) {
                FIRE::STOP_FIRE_IN_RANGE(player_pos.x, player_pos.y, player_pos.z, kcfg.value("extinguish_radius_m", 40.0f));
                if (now - d.t > Secs(kcfg, "work_s", 10)) SendAway(d);
            }
            break;
        case Dispatched::S::Leaving:
            if (heli && d.phase == 1 && ENTITY::GET_ENTITY_HEIGHT_ABOVE_GROUND(d.veh) >= 40.f) {  // yükseldi: uzaklaş
                AI::TASK_HELI_MISSION(d.crew[0], d.veh, 0, 0, d.origin.x, d.origin.y, d.origin.z + 100.f, kHeliGoto, 40.0f,
                                      10.0f, -1.0f, 250, 130, -1.0f, 0);
                d.phase = 2;
            }
            if (now - d.t > 45000 || D2(vp, player_pos) > 250.f) Cleanup(d);
            break;
    }
}

void Services::Shutdown() {
    if (ride_.active) {
        if (ENTITY::DOES_ENTITY_EXIST(ride_.driver)) {
            Ped d = ride_.driver;
            PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(d, false);
            ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&d);
        }
        PLAYER::SET_PLAYER_CONTROL(PLAYER::PLAYER_ID(), true, 0);
    }
    for (auto& d : disp_) Cleanup(d);
    disp_.clear();
    ride_ = Ride{};
    ride_started_ = arrival_ready_ = false;
}

void Services::Update() {
    const Vector3 pos = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true);
    for (auto& d : disp_) {
        if (!d.dead) UpdateDispatch(d, pos);
    }
    disp_.erase(std::remove_if(disp_.begin(), disp_.end(), [](const Dispatched& d) { return d.dead; }), disp_.end());
    UpdateRide(pos);
}

// ---------------- Yolculuk ----------------

Vehicle Services::BoardableVehicle(const Vector3& player_pos) const {
    if (ride_.active) return 0;
    for (const auto& d : disp_) {
        const bool rideable = d.kind == ServiceKind::Taxi || d.kind == ServiceKind::HeliTaxi;
        if (!rideable || d.dead || d.s != Dispatched::S::Arrived || d.in_ride || !ENTITY::DOES_ENTITY_EXIST(d.veh)) continue;
        if (D2(ENTITY::GET_ENTITY_COORDS(d.veh, true), player_pos) < 9.f) return d.veh;
    }
    return 0;
}

void Services::BoardPlayer(Vehicle veh) {
    if (!ENTITY::DOES_ENTITY_EXIST(veh)) return;
    VEHICLE::SET_VEHICLE_DOORS_LOCKED(veh, 1);  // kapılar açık
    const int seat = VEHICLE::IS_VEHICLE_SEAT_FREE(veh, 1) ? 1 : 2;  // arka koltuk (sol, doluysa sağ)
    AI::TASK_ENTER_VEHICLE(PLAYER::PLAYER_PED_ID(), veh, 10000, seat, 2.0f, 1, 0);
    Log("oyuncu araca biniyor");
}

bool Services::ConsumeRideStart() {
    const bool r = ride_started_;
    ride_started_ = false;
    return r;
}

bool Services::ConsumeArrival(json& facts) {
    if (!arrival_ready_) return false;
    arrival_ready_ = false;
    facts = arrival_facts_;
    return true;
}

void Services::EndRide() {
    if (!ride_.active) return;
    Dispatched* d = ride_.dispatch_id ? FindById(ride_.dispatch_id) : nullptr;
    if (d) {
        SendAway(*d);  // çağırdığın araç gider
    } else if (ENTITY::DOES_ENTITY_EXIST(ride_.driver)) {  // sokaktan binilen taksi: şoförü oyuna geri ver
        Ped drv = ride_.driver;
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(drv, false);
        AI::CLEAR_PED_TASKS(drv);
        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&drv);
    }
    Log("yolculuk bitti");
    ride_ = Ride{};
}

void Services::UpdateRide(const Vector3& player_pos) {
    const Ped player = PLAYER::PLAYER_PED_ID();
    const unsigned long long now = GetTickCount64();

    if (!ride_.active) {
        if (!PED::IS_PED_IN_ANY_VEHICLE(player, false)) return;
        const Vehicle veh = PED::GET_VEHICLE_PED_IS_IN(player, false);
        const Ped drv = VEHICLE::GET_PED_IN_VEHICLE_SEAT(veh, -1);
        if (drv == 0 || drv == player || PED::IS_PED_A_PLAYER(drv)) return;  // şoför sensin veya şoför yok
        Dispatched* d = nullptr;
        for (auto& x : disp_) {
            if (x.veh == veh) d = &x;
        }
        // Sokaktan binilen taksi (oyunun taksi tanımı) veya çağırdığın taksi (model ne olursa olsun).
        const bool taxi = PED::IS_PED_IN_ANY_TAXI(player) || (d && d->kind == ServiceKind::Taxi);
        const bool our_heli = d && d->kind == ServiceKind::HeliTaxi && d->s == Dispatched::S::Arrived;
        if (!taxi && !our_heli) return;

        ride_ = Ride{};
        ride_.active = true;
        ride_.kind = our_heli ? RideKind::Heli : RideKind::Taxi;
        ride_.veh = veh;
        ride_.driver = drv;
        ride_.dispatch_id = d ? d->id : 0;
        ride_.t = now;
        if (d) d->in_ride = true;
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(drv, true, true);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(drv, true);
        if (!our_heli) AI::TASK_VEHICLE_TEMP_ACTION(drv, veh, kBrakeAndStop, 600000);  // hedef söylenene kadar bekle
        ride_started_ = true;
        Log(our_heli ? "yolculuk basladi: helikopter" : "yolculuk basladi: taksi");
        return;
    }

    // Aktif yolculuk: oyuncu indiyse, araç/şoför gittiyse bitir.
    if (!ENTITY::DOES_ENTITY_EXIST(ride_.veh) || !ENTITY::DOES_ENTITY_EXIST(ride_.driver) ||
        ENTITY::IS_ENTITY_DEAD(ride_.driver) || !PED::IS_PED_IN_VEHICLE(player, ride_.veh, false)) {
        return EndRide();
    }
    if (ride_.s == Ride::S::Takeoff) {
        const json kcfg = cfg_.value("heli_taxi", json::object());
        const bool high_enough = ENTITY::GET_ENTITY_HEIGHT_ABOVE_GROUND(ride_.veh) >= kcfg.value("takeoff_trigger_m", 30.0f);
        if (high_enough || now - ride_.t > 15000) {  // irtifa kazanıldı: hedefe uç (yüksekten, binaların üstünden)
            LandBegin(ride_.land, ride_.driver, ride_.veh, ride_.dest);  // yüksekten yaklaş, alan bul, sakin dikey in
            ride_.s = Ride::S::Driving;
            ride_.t = now;
        }
        return;
    }
    if (ride_.s != Ride::S::Driving) return;

    const Vector3 vp = ENTITY::GET_ENTITY_COORDS(ride_.veh, true);
    const bool heli = ride_.kind == RideKind::Heli;
    const bool arrived = heli ? LandUpdate(ride_.land, ride_.driver, ride_.veh) : (D2(vp, ride_.dest) < 18.f);
    if (!arrived) return;

    ride_.s = Ride::S::Arrived;
    const int charged = economy::Spend(ride_.fare) ? ride_.fare : 0;  // para yetmezse bedava (kaçak yolcu)
    if (!heli) AI::TASK_VEHICLE_TEMP_ACTION(ride_.driver, ride_.veh, kBrakeAndStop, 600000);
    arrival_facts_ = {{"arrived", true}, {"fare", ride_.fare}, {"charged", charged}};
    arrival_ready_ = true;
    Log("yolculuk: varildi, ucret " + std::to_string(charged));
}

json Services::RequestSpeed(int direction) {
    const char* dir = direction > 0 ? "faster" : "slower";
    auto out = [&](bool granted, const char* reason) {
        return json{{"kind", "speed_request"}, {"direction", dir}, {"granted", granted}, {"reason", reason},
                    {"speed_level", ride_.speed_level}};
    };
    if (!ride_.active) return out(false, "not_in_ride");
    if (ride_.kind == RideKind::Heli) return out(false, "helicopter_refuses");  // helikopterde hız ricası kabul edilmez
    if (ride_.s != Ride::S::Driving) return out(false, "no_destination");       // henüz yola çıkılmadı

    const json kcfg = cfg_.value("taxi", json::object());
    const unsigned long long now = GetTickCount64();
    const unsigned long long cooldown = static_cast<unsigned long long>(kcfg.value("speed_request_cooldown_s", 4)) * 1000ULL;
    if (ride_.last_speed_req != 0 && now - ride_.last_speed_req < cooldown) return out(false, "too_soon");
    ride_.last_speed_req = now;

    if (direction > 0 && ride_.speed_level >= 1) return out(false, "already_fastest");
    if (direction < 0 && ride_.speed_level <= -1) return out(false, "already_slowest");
    if (!street_events::Chance(kcfg.value("speed_request_accept_chance", 0.8))) return out(false, "driver_declines");

    ride_.speed_level += direction;
    const float speed = ride_.speed_level > 0   ? kcfg.value("faster_speed_mps", 34.0f)
                        : ride_.speed_level < 0 ? kcfg.value("slower_speed_mps", 11.0f)
                                                : kcfg.value("drive_speed_mps", 22.0f);
    AI::SET_DRIVE_TASK_CRUISE_SPEED(ride_.driver, speed);
    Log(std::string("taksi hiz ricasi kabul: ") + dir + " seviye=" + std::to_string(ride_.speed_level));
    return out(true, "ok");
}

json Services::SetDestination(const std::string& text, const Places& places) {
    auto refuse = [&](const char* reason) { return json{{"accepted", false}, {"reason", reason}}; };
    if (!ride_.active || ride_.s != Ride::S::WaitDest) return refuse("not_ready");

    const bool heli = ride_.kind == RideKind::Heli;
    const json kcfg = cfg_.value(heli ? "heli_taxi" : "taxi", json::object());
    const Vector3 from = ENTITY::GET_ENTITY_COORDS(ride_.veh, true);

    Place place;
    bool found = false;
    const std::string folded = Places::Fold(text);
    if (folded.find("isaret") != std::string::npos || folded.find("waypoint") != std::string::npos ||
        folded.find("harita") != std::string::npos) {
        if (UI::IS_WAYPOINT_ACTIVE()) {  // haritadaki işaret (sprite 8 = waypoint)
            const Blip wp = UI::GET_FIRST_BLIP_INFO_ID(8);
            if (wp != 0 && UI::DOES_BLIP_EXIST(wp)) {
                place.name = "haritadaki isaret";
                place.pos = UI::GET_BLIP_COORDS(wp);
                found = true;
            }
        }
        if (!found) return refuse("no_waypoint");
    } else {
        found = places.Resolve(text, from, place);
    }
    if (!found) return json{{"accepted", false}, {"reason", "unknown_destination"}, {"asked", text}};

    const float straight = D2(from, place.pos);
    if (heli && straight < kcfg.value("min_distance_m", 400.0f)) return refuse("too_close_for_heli");
    if (!heli && straight < 60.f) return refuse("already_there");

    // Hedefi yola oturt (bölge merkezlerinin z'si yok); helikopter de yol hizasına iner.
    Vector3 dest = place.pos;
    Vector3 node{};
    if (PATHFIND::GET_CLOSEST_VEHICLE_NODE(place.pos.x, place.pos.y, from.z, &node, 1, 3.0f, 0)) dest = node;
    else dest.z = from.z;

    const float km = straight / 1000.f * (heli ? 1.f : kRoadFactor);
    const int fare = static_cast<int>(std::lround(kcfg.value("base_fare", 10) + kcfg.value("per_km", 8) * km));
    const int cash = economy::GetCash();
    if (cash >= 0 && cash < fare) return json{{"accepted", false}, {"reason", "insufficient_funds"}, {"fare", fare}};

    ride_.dest = dest;
    ride_.fare = fare;
    ride_.t = GetTickCount64();
    if (heli) {
        // Önce olduğu yerde dikey yüksel (yerden doğrudan hedefe uçarsa çarpıyor); UpdateRide irtifa yetince hedefe yollar.
        ride_.s = Ride::S::Takeoff;
        VEHICLE::SET_HELI_BLADES_FULL_SPEED(ride_.veh);
        AI::TASK_HELI_MISSION(ride_.driver, ride_.veh, 0, 0, from.x, from.y, from.z + kcfg.value("takeoff_height_m", 55.0f),
                              kHeliGoto, 10.0f, 5.0f, -1.0f, 250, 45, 10.0f, 0);
    } else {
        ride_.s = Ride::S::Driving;
        AI::TASK_VEHICLE_DRIVE_TO_COORD(ride_.driver, ride_.veh, dest.x, dest.y, dest.z,
                                        kcfg.value("drive_speed_mps", 22.0f), 0, ENTITY::GET_ENTITY_MODEL(ride_.veh),
                                        kDrivingModeNormal, 6.0f, 1.0f);
    }
    const float avg = kcfg.value("avg_speed_mps", 14.0f);
    Log("yolculuk hedef: " + place.name + " ucret " + std::to_string(fare));
    return {{"accepted", true},
            {"destination", place.name},
            {"distance_m", static_cast<int>(straight)},
            {"fare", fare},
            {"eta_seconds", static_cast<int>(straight * (heli ? 1.f : kRoadFactor) / std::max(avg, 1.f))},
            {"zone", places.ZoneNameAt(dest)}};
}

}  // namespace hikaye
