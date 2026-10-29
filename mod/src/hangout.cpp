#include "hangout.h"

#include <windows.h>

#include <cmath>

#include "passenger.h"

namespace hikaye {

namespace {
constexpr unsigned long long kComeTimeoutMs = 70000, kCooldownMs = 120000, kExitGraceMs = 2000;
}

std::string Hangout::Request(const Missions::Talk& who) {
    if (state_ != State::Idle) return "busy";
    if (GetTickCount64() < cooldown_until_) return "cooldown";
    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(player, false);
    if (v == 0 || !VEHICLE::IS_THIS_MODEL_A_CAR(ENTITY::GET_ENTITY_MODEL(v))) return "player_not_in_car";
    if (!VEHICLE::IS_VEHICLE_SEAT_FREE(v, 0)) return "no_free_seat";
    if (PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) > 0) return "wanted_level";

    std::vector<std::string> candidates{who.model};
    candidates.insert(candidates.end(), who.fallback_models.begin(), who.fallback_models.end());
    std::string chosen;
    for (const auto& c : candidates) {
        const Hash h = HashKey(c.c_str());
        if (STREAMING::IS_MODEL_IN_CDIMAGE(h) && STREAMING::IS_MODEL_VALID(h)) {
            chosen = c;
            break;
        }
    }
    if (chosen.empty()) return "model_missing";
    const Hash model = HashKey(chosen.c_str());
    STREAMING::REQUEST_MODEL(model);
    for (int i = 0; i < 100 && !STREAMING::HAS_MODEL_LOADED(model); ++i) WAIT(0);
    if (!STREAMING::HAS_MODEL_LOADED(model)) return "model_missing";

    // Aracın ~30 m arkasında, kaldırımda uygun bir noktaya doğar.
    const Vector3 pp = ENTITY::GET_ENTITY_COORDS(v, true);
    const float h = ENTITY::GET_ENTITY_HEADING(v) * 3.14159265f / 180.f;
    Vector3 spawn{};
    if (!PATHFIND::GET_SAFE_COORD_FOR_PED(pp.x + std::sin(h) * 30.f, pp.y - std::cos(h) * 30.f, pp.z, true, &spawn, 16)) {
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
        return "no_spawn_point";
    }
    const bool female = who.npc_json.value("gender", "female") == "female";
    const Ped ped = PED::CREATE_PED(female ? 5 : 4, model, spawn.x, spawn.y, spawn.z, 0.f, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
    if (!ENTITY::DOES_ENTITY_EXIST(ped)) return "no_spawn_point";
    ENTITY::SET_ENTITY_INVINCIBLE(ped, true);
    who_ = who;
    who_.ped = ped;
    veh_ = v;
    state_ = State::Coming;
    since_ = GetTickCount64();
    passenger::Board(ped, v);
    Log("hangout: " + who.name + " arabaya geliyor");
    return "ok";
}

void Hangout::Finish() {
    if (who_.ped != 0 && ENTITY::DOES_ENTITY_EXIST(who_.ped)) passenger::Release(who_.ped, veh_);
    who_.ped = 0;
    veh_ = 0;
    state_ = State::Idle;
    cooldown_until_ = GetTickCount64() + kCooldownMs;
}

void Hangout::Update() {
    if (state_ == State::Idle) return;
    const unsigned long long now = GetTickCount64();
    if (!ENTITY::DOES_ENTITY_EXIST(who_.ped) || ENTITY::IS_ENTITY_DEAD(who_.ped) || !ENTITY::DOES_ENTITY_EXIST(veh_)) {
        who_.ped = 0;
        veh_ = 0;
        state_ = State::Idle;
        return;
    }
    const Ped player = PLAYER::PLAYER_PED_ID();
    const bool player_in = PED::IS_PED_IN_VEHICLE(player, veh_, false);
    if (state_ == State::Coming) {
        if (PED::IS_PED_IN_VEHICLE(who_.ped, veh_, false)) {
            state_ = State::Seated;
            out_since_ = 0;
            Log("hangout: " + who_.name + " arabada");
        } else if (now - since_ > kComeTimeoutMs) {
            Log("hangout: gelmedi (zaman asimi)");
            Finish();
        }
        return;
    }
    // Seated: sen inince o da iner.
    if (!player_in) {
        if (out_since_ == 0) out_since_ = now;
        if (now - out_since_ > kExitGraceMs) Finish();
    } else {
        out_since_ = 0;
        who_.pos = ENTITY::GET_ENTITY_COORDS(who_.ped, true);
    }
}

void Hangout::Shutdown() {
    if (who_.ped != 0 && ENTITY::DOES_ENTITY_EXIST(who_.ped)) {
        Ped p = who_.ped;
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, true, true);
        PED::DELETE_PED(&p);
    }
    who_.ped = 0;
    veh_ = 0;
    state_ = State::Idle;
}

}  // namespace hikaye
