#include "hangout.h"

#include <windows.h>

#include <cmath>

#include "blips.h"

#include "karma.h"
#include "passenger.h"

namespace hikaye {

namespace {
constexpr unsigned long long kComeTimeoutMs = 70000, kCooldownMs = 120000, kExitGraceMs = 2000;
float D2(const Vector3& a, const Vector3& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}
}

std::string Hangout::Request(const Missions::Talk& who, const std::string& destination_name, const Vector3& destination) {
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

    STREAMING::REQUEST_COLLISION_AT_COORD(destination.x, destination.y, destination.z);
    const bool female = who.npc_json.value("gender", "female") == "female";
    const Ped ped = PED::CREATE_PED(female ? 5 : 4, model, destination.x, destination.y, destination.z, 0.f, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
    if (!ENTITY::DOES_ENTITY_EXIST(ped)) return "no_spawn_point";
    ENTITY::SET_ENTITY_INVINCIBLE(ped, true);
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped, true, true);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, true);
    AI::TASK_STAND_STILL(ped, -1);
    blip_ = blips::AddEntityBlip(ped, 480, 3, who.name, 0.9f);
    dest_blip_ = blips::Add(destination, 1, 7, destination_name, 1.0f, false);

    who_ = who;
    who_.model = chosen;
    who_.ped = ped;
    veh_ = v;
    destination_name_ = destination_name;
    destination_ = destination;
    state_ = State::Coming;
    since_ = GetTickCount64();
    Log("hangout: " + who.name + " bulusma noktasinda olusturuldu; hedef " + destination_name);
    return "ok";
}

void Hangout::Finish() {
    if (who_.ped != 0 && ENTITY::DOES_ENTITY_EXIST(who_.ped)) passenger::Release(who_.ped, veh_);
    who_.ped = 0;
    veh_ = 0;
    state_ = State::Idle;
    destination_name_.clear();
    cooldown_until_ = GetTickCount64() + kCooldownMs;
    if (blip_ != 0 && UI::DOES_BLIP_EXIST(blip_)) UI::REMOVE_BLIP(&blip_);
    blip_ = 0;
    if (dest_blip_ != 0) blips::Remove(dest_blip_);
    dest_blip_ = 0;
}

void Hangout::CompleteMeetup() {
    if (state_ == State::Arrived) Finish();
}

void Hangout::Update() {
    if (state_ == State::Idle) return;
    const unsigned long long now = GetTickCount64();
    if (!ENTITY::DOES_ENTITY_EXIST(who_.ped) || ENTITY::IS_ENTITY_DEAD(who_.ped) || !ENTITY::DOES_ENTITY_EXIST(veh_)) {
        who_.ped = 0;
        veh_ = 0;
        state_ = State::Idle;
        if (blip_ != 0) blips::Remove(blip_);
        if (dest_blip_ != 0) blips::Remove(dest_blip_);
        return;
    }
    const Ped player = PLAYER::PLAYER_PED_ID();
    const bool player_in = PED::IS_PED_IN_VEHICLE(player, veh_, false);
    if (state_ == State::Coming) {
        // Oyuncu bulusma noktasina yaklasti mi?
        const Vector3 player_pos = ENTITY::GET_ENTITY_COORDS(player, true);
        if (D2(player_pos, destination_) <= 15.f) {
            // Oyuncu yaklasti, NPC arabaya bin
            passenger::Board(who_.ped, veh_);
            state_ = State::Seated;
            out_since_ = 0;
            Log("hangout: " + who_.name + " arabaya bindi");
        } else if (now - since_ > kComeTimeoutMs) {
            Log("hangout: gelmedi (zaman asimi)");
            Finish();
        }
        return;
    }
    if (state_ == State::Seated && !destination_name_.empty()) {
        const Vector3 vehicle_pos = ENTITY::GET_ENTITY_COORDS(veh_, true);
        if (D2(vehicle_pos, destination_) <= 28.f && (!player_in || ENTITY::GET_ENTITY_SPEED(veh_) <= 1.5f)) {
            state_ = State::Arrived;
            AI::TASK_LEAVE_VEHICLE(who_.ped, veh_, 0);
            since_ = now;
            karma::Add(2, "Arkadaşınla buluştun", "iyilik_bulusma");
            karma::Toast(destination_name_ + " noktasına ulaştınız", 5);
            Log("hangout: hedefe varildi: " + destination_name_);
            return;
        }
    }
    if (state_ == State::Arrived) {
        if (D2(ENTITY::GET_ENTITY_COORDS(player, true), destination_) > 100.f) Finish();
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
    if (blip_ != 0 && UI::DOES_BLIP_EXIST(blip_)) UI::REMOVE_BLIP(&blip_);
    blip_ = 0;
    if (dest_blip_ != 0) blips::Remove(dest_blip_);
    dest_blip_ = 0;
}

}  // namespace hikaye
