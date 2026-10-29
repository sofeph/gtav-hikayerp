#include "passenger.h"

#include <windows.h>

#include <vector>

namespace hikaye::passenger {

namespace {
struct Leaving {
    Ped ped;
    Vehicle veh;
    unsigned long long until;
    bool wandering = false;
};
std::vector<Leaving> g_leaving;

void Delete(Ped p) {
    if (!ENTITY::DOES_ENTITY_EXIST(p)) return;
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, true, true);
    PED::DELETE_PED(&p);
}
}  // namespace

void Board(Ped ped, Vehicle veh) {
    if (!ENTITY::DOES_ENTITY_EXIST(ped) || !ENTITY::DOES_ENTITY_EXIST(veh)) return;
    ENTITY::FREEZE_ENTITY_POSITION(ped, false);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, true);
    PED::SET_PED_CAN_BE_DRAGGED_OUT(ped, false);
    PED::SET_PED_STAY_IN_VEHICLE_WHEN_JACKED(ped, true);
    AI::TASK_ENTER_VEHICLE(ped, veh, 20000, 0, 1.0f, 1, 0);  // seat 0: ön yolcu koltuğu
}

void Release(Ped ped, Vehicle veh) {
    if (!ENTITY::DOES_ENTITY_EXIST(ped)) return;
    AI::TASK_LEAVE_VEHICLE(ped, veh, 0);
    g_leaving.push_back({ped, veh, GetTickCount64() + 15000ULL});
}

void Tick() {
    const unsigned long long now = GetTickCount64();
    for (size_t i = 0; i < g_leaving.size();) {
        Leaving& l = g_leaving[i];
        if (!ENTITY::DOES_ENTITY_EXIST(l.ped)) {
            g_leaving.erase(g_leaving.begin() + i);
            continue;
        }
        if (!l.wandering && !PED::IS_PED_IN_VEHICLE(l.ped, l.veh, true)) {
            AI::TASK_WANDER_STANDARD(l.ped, 10.0f, 10);
            l.wandering = true;
        }
        if (now > l.until) {
            Delete(l.ped);
            g_leaving.erase(g_leaving.begin() + i);
            continue;
        }
        ++i;
    }
}

void Clear() {
    for (auto& l : g_leaving) Delete(l.ped);
    g_leaving.clear();
}

}  // namespace hikaye::passenger
