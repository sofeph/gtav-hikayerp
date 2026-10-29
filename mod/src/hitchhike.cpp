#include "hitchhike.h"

#include <windows.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>

#include "controls.h"
#include "json.hpp"
#include "karma.h"

namespace hikaye::hitchhike {

namespace {
constexpr int kDrivingModeNormal = 786603;
constexpr int kBrakeAndStop = 27;
constexpr int kFrontPassengerSeat = 0;
constexpr unsigned long long kBoardTimeoutMs = 15000ULL;

float g_chance = 0.40f;
float g_range_m = 45.f;
unsigned long long g_cooldown_ms = 10000ULL;

enum class S { Idle, Boarding, Riding } g_s = S::Idle;
Ped g_driver = 0;
Vehicle g_veh = 0;
unsigned long long g_t = 0, g_next_ok = 0, g_wp_check = 0;
Vector3 g_dest{};
bool g_has_dest = false, g_arrived = false, g_prev_key = false, g_boarded = false, g_chat_dest = false, g_arrival_ready = false;
float g_max_distance_m = 5000.f;
nlohmann::json g_arrival;
std::string g_dest_name;

float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

bool Waypoint(Vector3& out) {
    if (!UI::IS_WAYPOINT_ACTIVE()) return false;
    const Blip wp = UI::GET_FIRST_BLIP_INFO_ID(8);  // sprite 8 = waypoint
    if (wp == 0 || !UI::DOES_BLIP_EXIST(wp)) return false;
    out = UI::GET_BLIP_COORDS(wp);
    return true;
}

void Release() {
    if (g_driver != 0 && ENTITY::DOES_ENTITY_EXIST(g_driver)) {
        if (g_veh != 0 && ENTITY::DOES_ENTITY_EXIST(g_veh) && PED::IS_PED_IN_VEHICLE(g_driver, g_veh, false)) {
            AI::TASK_VEHICLE_DRIVE_WANDER(g_driver, g_veh, 18.0f, kDrivingModeNormal);
        }
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(g_driver, false);
        Ped d = g_driver;
        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&d);
    }
    if (g_veh != 0 && ENTITY::DOES_ENTITY_EXIST(g_veh)) {
        Vehicle v = g_veh;
        ENTITY::SET_VEHICLE_AS_NO_LONGER_NEEDED(&v);
    }
    g_driver = 0;
    g_veh = 0;
    g_s = S::Idle;
    g_has_dest = g_arrived = g_boarded = g_chat_dest = g_arrival_ready = false;
}

// Başparmak: yakındaki seyir halindeki sivil otomobiller arasından en yakını (yolcu koltuğu boş, polis/görev aracı değil)
bool PickCar(const Vector3& pos, Vehicle& veh, Ped& drv) {
    const Ped player = PLAYER::PLAYER_PED_ID();
    int arr[2 + 2 * 16];
    arr[0] = 16;
    const int n = PED::GET_PED_NEARBY_VEHICLES(player, arr);
    float best = 1e9f;
    for (int i = 0; i < n && i < 16; ++i) {
        const Vehicle v = arr[2 + i * 2];
        if (v == 0 || !ENTITY::DOES_ENTITY_EXIST(v)) continue;
        const Hash m = ENTITY::GET_ENTITY_MODEL(v);
        if (!VEHICLE::IS_THIS_MODEL_A_CAR(m) || ENTITY::IS_ENTITY_A_MISSION_ENTITY(v)) continue;
        if (!VEHICLE::IS_VEHICLE_SEAT_FREE(v, kFrontPassengerSeat)) continue;
        const Ped d = VEHICLE::GET_PED_IN_VEHICLE_SEAT(v, -1);
        if (d == 0 || d == player || !ENTITY::DOES_ENTITY_EXIST(d) || ENTITY::IS_ENTITY_DEAD(d) || !PED::IS_PED_HUMAN(d)) continue;
        const int t = PED::GET_PED_TYPE(d);
        if (t == 6 || t == 27 || t == 29) continue;  // polis/askeri
        if (ENTITY::GET_ENTITY_SPEED(v) < 2.f) continue;
        const float dist = D2(pos, ENTITY::GET_ENTITY_COORDS(v, true));
        if (dist < 6.f || dist > g_range_m || dist >= best) continue;
        best = dist;
        veh = v;
        drv = d;
    }
    return best < 1e8f;
}

void DriveTo(const Vector3& dest) {
    g_dest = dest;
    g_has_dest = true;
    g_arrived = false;
    AI::TASK_VEHICLE_DRIVE_TO_COORD(g_driver, g_veh, dest.x, dest.y, dest.z, 20.0f, 0, ENTITY::GET_ENTITY_MODEL(g_veh), kDrivingModeNormal, 8.0f, 1.0f);
}
}  // namespace

void Load() {
    try {
        std::ifstream in(std::string(kBaseDir) + "/data/hitchhike.json");
        if (!in) return;
        const nlohmann::json j = nlohmann::json::parse(in);
        g_chance = j.value("accept_chance", 0.40f);
        g_range_m = j.value("range_m", 45.0f);
        g_cooldown_ms = static_cast<unsigned long long>(j.value("cooldown_s", 10)) * 1000ULL;
        g_max_distance_m = j.value("max_distance_m", 5000.0f);
    } catch (const std::exception& e) {
        Log(std::string("hitchhike.json okunamadi: ") + e.what());
    }
}

void Reset() {
    if (g_s != S::Idle) Release();
    g_prev_key = false;
}

void Update(const Vector3& pos, bool e_pressed, bool input_blocked) {
    const unsigned long long now = GetTickCount64();
    const Ped player = PLAYER::PLAYER_PED_ID();
    const bool key = !input_blocked && (GetAsyncKeyState(controls::HitchhikeKey()) & 0x8000) != 0;
    const bool key_edge = key && !g_prev_key;
    g_prev_key = key;

    if (g_s != S::Idle && (g_veh == 0 || !ENTITY::DOES_ENTITY_EXIST(g_veh) || g_driver == 0 || !ENTITY::DOES_ENTITY_EXIST(g_driver) || ENTITY::IS_ENTITY_DEAD(g_driver))) {
        Release();  // araç ya da şoför kayboldu/öldü
        return;
    }

    if (g_s == S::Idle) {
        if (!key_edge || now < g_next_ok || PED::IS_PED_IN_ANY_VEHICLE(player, false) || ENTITY::IS_ENTITY_DEAD(player)) return;
        g_next_ok = now + g_cooldown_ms;
        Vehicle veh = 0;
        Ped drv = 0;
        if (!PickCar(pos, veh, drv)) {
            karma::Toast("Otostop: yakında geçen araç yok", 4);
            return;
        }
        if (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) >= g_chance) {
            karma::Toast("Otostop: sürücü durmadı, geçip gitti", 4);
            return;
        }
        g_veh = veh;
        g_driver = drv;
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(g_veh, true, true);  // yolculuk boyunca oyun silmesin
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(g_driver, true, true);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(g_driver, true);
        AI::TASK_VEHICLE_TEMP_ACTION(g_driver, g_veh, kBrakeAndStop, 20000);
        AI::TASK_ENTER_VEHICLE(player, g_veh, static_cast<int>(kBoardTimeoutMs), kFrontPassengerSeat, 1.0f, 1, 0);
        g_s = S::Boarding;
        g_t = now;
        karma::Toast("Otostop: sürücü durdu, arabaya bin", 5);
        return;
    }

    if (g_s == S::Boarding) {
        if (PED::IS_PED_IN_VEHICLE(player, g_veh, false)) {
            g_s = S::Riding;
            g_t = now;
            g_boarded = true;  // sohbet başlayacak
            g_chat_dest = false;
            Vector3 wp{};
            if (Waypoint(wp)) {
                DriveTo(wp);
                karma::Toast("Sürücü haritadaki işarete gidiyor", 5);
            } else {
                g_has_dest = false;
                AI::TASK_VEHICLE_TEMP_ACTION(g_driver, g_veh, kBrakeAndStop, 120000);  // hedef söylenene kadar bekler
            }
        } else if (now - g_t > kBoardTimeoutMs) {
            Release();  // binmedin: sürücü yoluna devam eder
        }
        return;
    }

    // Riding
    if (!PED::IS_PED_IN_VEHICLE(player, g_veh, false)) {
        Release();  // indin ya da düştün
        return;
    }
    if (now - g_wp_check > 2000ULL) {
        g_wp_check = now;
        Vector3 wp{};
        if (!g_arrived && !g_chat_dest && Waypoint(wp) && (!g_has_dest || D2(wp, g_dest) > 15.f)) DriveTo(wp);
    }
    if (g_has_dest && !g_arrived && D2(ENTITY::GET_ENTITY_COORDS(g_veh, true), g_dest) < 30.f) {
        g_arrived = true;
        AI::TASK_VEHICLE_TEMP_ACTION(g_driver, g_veh, kBrakeAndStop, 15000);
        karma::Toast("Vardık. E: in", 6);
        g_arrival = {{"kind", "hitch"}, {"arrived", true}, {"destination", g_dest_name}};
        g_arrival_ready = true;
    }
    ShowHelp("~INPUT_PICKUP~ Arabadan in");
    if (e_pressed) {
        AI::TASK_VEHICLE_TEMP_ACTION(g_driver, g_veh, kBrakeAndStop, 4000);
        AI::TASK_LEAVE_VEHICLE(player, g_veh, 0);
    }
}

bool Riding() { return g_s == S::Riding; }
Ped Driver() { return g_driver; }

bool ConsumeBoarded() {
    if (!g_boarded) return false;
    g_boarded = false;
    return true;
}

bool ConsumeArrival(nlohmann::json& facts) {
    if (!g_arrival_ready) return false;
    g_arrival_ready = false;
    facts = g_arrival;
    return true;
}

// Karar kodda: mesafe sınırı ve "zaten yakınız" denetimi. Kabulde sürücü hedefe gider.
nlohmann::json SetDestination(const Vector3& pos, const std::string& name) {
    using nlohmann::json;
    if (g_s != S::Riding) return json{{"kind", "hitch"}, {"accepted", false}, {"reason", "not_riding"}};
    const float dist = D2(ENTITY::GET_ENTITY_COORDS(g_veh, true), pos);
    if (dist < 80.f) return json{{"kind", "hitch"}, {"accepted", false}, {"reason", "already_there"}, {"destination", name}};
    if (dist > g_max_distance_m) return json{{"kind", "hitch"}, {"accepted", false}, {"reason", "too_far"}, {"destination", name}};
    g_chat_dest = true;
    g_dest_name = name;
    DriveTo(pos);
    return json{{"kind", "hitch"}, {"accepted", true}, {"destination", name}, {"distance_m", static_cast<int>(dist)}};
}

nlohmann::json UseWaypoint() {
    Vector3 wp{};
    if (!Waypoint(wp)) return nlohmann::json{{"kind", "hitch"}, {"accepted", false}, {"reason", "no_waypoint"}};
    return SetDestination(wp, "haritadaki işaret");
}

}  // namespace hikaye::hitchhike
