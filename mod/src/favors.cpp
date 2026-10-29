#include "favors.h"

#include <windows.h>

#include <cmath>

#include "economy.h"
#include "karma.h"

namespace hikaye {

namespace {
float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
}  // namespace

bool RideFavor::Start(Ped ped, const Vector3& dest, const std::string& dest_name, std::string& reason) {
    if (active()) {
        reason = "busy";
        return false;
    }
    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(player, false);
    if (v == 0 || VEHICLE::GET_PED_IN_VEHICLE_SEAT(v, -1) != player) {
        reason = "no_vehicle";
        return false;
    }
    if (!VEHICLE::IS_VEHICLE_SEAT_FREE(v, 0) || ENTITY::GET_ENTITY_SPEED(v) > 4.f) {
        reason = "no_free_seat";
        return false;
    }
    ped_ = ped;
    veh_ = v;
    dest_ = dest;
    dest_name_ = dest_name;
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped, true, true);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, true);
    AI::CLEAR_PED_TASKS(ped);
    AI::TASK_ENTER_VEHICLE(ped, v, 10000, 0, 1.0f, 1, 0);
    state_ = State::Boarding;
    t_ = GetTickCount64();
    Log("iyilik: yolculuk basladi -> " + dest_name);
    return true;
}

void RideFavor::Release() {
    if (ped_ != 0 && ENTITY::DOES_ENTITY_EXIST(ped_)) {
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped_, false);
        Ped p = ped_;
        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&p);
    }
    ped_ = 0;
    veh_ = 0;
    state_ = State::Idle;
}

void RideFavor::Abort() {
    if (!active()) return;
    if (ped_ != 0 && ENTITY::DOES_ENTITY_EXIST(ped_) && PED::IS_PED_IN_ANY_VEHICLE(ped_, false)) AI::TASK_LEAVE_VEHICLE(ped_, PED::GET_VEHICLE_PED_IS_IN(ped_, false), 0);
    Release();
}

void RideFavor::Update() {
    if (!active()) return;
    const unsigned long long now = GetTickCount64();
    if (!ENTITY::DOES_ENTITY_EXIST(ped_) || ENTITY::IS_ENTITY_DEAD(ped_) || !ENTITY::DOES_ENTITY_EXIST(veh_)) {
        Release();
        return;
    }
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (state_ == State::Boarding) {
        if (PED::IS_PED_IN_VEHICLE(ped_, veh_, false)) {
            state_ = State::Riding;
            t_ = now;
            Vector3 d = dest_;
            UI::SET_NEW_WAYPOINT(d.x, d.y);
            karma::Toast("Yolcuyu " + dest_name_ + " noktasına götür (haritada işaretli)", 6);
        } else if (now - t_ > 14000ULL) {
            Log("iyilik: yaya binemedi");
            Release();
        }
        return;
    }
    if (state_ == State::Riding) {
        if (PED::GET_VEHICLE_PED_IS_IN(player, false) != veh_ || !PED::IS_PED_IN_VEHICLE(ped_, veh_, false)) {
            if (now - t_ > 4000ULL) Abort();  // oyuncu indi veya yolcu çıktı: iyilik yarıda kaldı
            return;
        }
        if (now - t_ > 900000ULL) {
            Abort();
            return;
        }
        const Vector3 vp = ENTITY::GET_ENTITY_COORDS(veh_, true);
        if (D2(vp, dest_) < 40.f && ENTITY::GET_ENTITY_SPEED(veh_) < 4.f) {
            AI::TASK_LEAVE_VEHICLE(ped_, veh_, 0);
            state_ = State::Leaving;
            t_ = now;
        }
        return;
    }
    if (state_ == State::Leaving && (!PED::IS_PED_IN_VEHICLE(ped_, veh_, false) || now - t_ > 6000ULL)) {
        karma::Add(8, "Birini yerine bıraktın", "iyilik_yolculuk");
        economy::Earn(30);
        karma::Toast("Yolcu teşekkür etti ve $30 bahşiş bıraktı", 5);
        AI::TASK_WANDER_STANDARD(ped_, 10.0f, 10);
        Release();
    }
}

}  // namespace hikaye
