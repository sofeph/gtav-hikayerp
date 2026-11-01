#pragma once
#include "main.h"  // ScriptHookV SDK uses main.h, not script.h
#include <Windows.h>
#include <string>

namespace hikaye {
namespace safe {

// SEH wrapper - TÜM native çağrıları BU İLE YAPILACAK
// C++ try/catch SEH exception (access violation) YAKALAMAZ, __try/__except gerekir.
// Hash SDK'da DWORD (32-bit): 64-bit native hash'i Hash'e daraltmak üst yarıyı atar
// (0x9F47B058362C84B5 -> 0x9F47B058) ve Script Hook V "Can't find native" ile oyunu kapatır.
// Bu yüzden hash her zaman tam 64-bit UINT64 olarak taşınır.
template<typename Ret, typename... Args>
bool TryCall(Ret& out, UINT64 hash, Args... args) {
    __try {
        out = invoke<Ret>(hash, args...);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Void return type için özel wrapper (SDK Void = DWORD typedef)
template<typename... Args>
bool TryVoid(UINT64 hash, Args... args) {
    __try {
        invoke<Void>(hash, args...);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Entity doğrulama - HER entity kullanımından önce çağrılmalı
inline bool Valid(Entity e) {
    if (!e || e == 0) return false;  // 0 = invalid entity in GTA V
    BOOL exists = FALSE;
    __try {
        exists = ENTITY::DOES_ENTITY_EXIST(e);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return exists != 0;
}

inline bool ValidPed(Ped p) { return Valid(p) && ENTITY::IS_ENTITY_A_PED(p); }
inline bool ValidVehicle(Vehicle v) { return Valid(v) && ENTITY::IS_ENTITY_A_VEHICLE(v); }
inline bool ValidObject(Object o) { return Valid(o) && ENTITY::IS_ENTITY_AN_OBJECT(o); }

// Güvenli koordinat alma
inline bool GetCoords(Entity e, Vector3& out) {
    if (!Valid(e)) return false;
    __try {
        out = ENTITY::GET_ENTITY_COORDS(e, true);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Güvenli model alma
inline bool GetModel(Entity e, Hash& out) {
    if (!Valid(e)) return false;
    return TryCall(out, 0x9F47B058362C84B5ULL, e); // GET_ENTITY_MODEL
}

// Güvenli heading alma
inline bool GetHeading(Entity e, float& out) {
    if (!Valid(e)) return false;
    return TryCall(out, 0xE83D4F9BA2A38914ULL, e); // GET_ENTITY_HEADING
}

// Güvenli health alma
inline bool GetHealth(Entity e, int& out) {
    if (!Valid(e)) return false;
    return TryCall(out, 0xEEF059FAD016D209ULL, e); // GET_ENTITY_HEALTH
}

// Güvenli max health alma
inline bool GetMaxHealth(Entity e, int& out) {
    if (!Valid(e)) return false;
    return TryCall(out, 0x15D757606D170C3CULL, e); // GET_ENTITY_MAX_HEALTH
}

// Güvenli velocity alma
inline bool GetVelocity(Entity e, Vector3& out) {
    if (!Valid(e)) return false;
    return TryCall(out, 0x4805D2B1D8CF94A9ULL, e); // GET_ENTITY_VELOCITY
}

// Güvenli speed alma
inline bool GetSpeed(Entity e, float& out) {
    if (!Valid(e)) return false;
    return TryCall(out, 0xD5037BA82E12416FULL, e); // GET_ENTITY_SPEED
}

// Ped-specific güvenli çağrılar
inline bool IsPedDeadOrDying(Ped p) {
    if (!ValidPed(p)) return true;
    BOOL dead = FALSE;
    __try {
        dead = PED::IS_PED_DEAD_OR_DYING(p, true);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
    return dead != 0;
}

inline bool IsPedInAnyVehicle(Ped p) {
    if (!ValidPed(p)) return false;
    BOOL inVeh = FALSE;
    __try {
        inVeh = PED::IS_PED_IN_ANY_VEHICLE(p, false);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return inVeh != 0;
}

inline bool GetVehiclePedIsIn(Ped p, Vehicle& out, bool lastVehicle) {
    if (!ValidPed(p)) return false;
    return TryCall(out, 0x9A9112A0FE9A4713ULL, p, lastVehicle); // GET_VEHICLE_PED_IS_IN
}

// Vehicle-specific güvenli çağrılar
inline bool IsVehicleDriveable(Vehicle v) {
    if (!ValidVehicle(v)) return false;
    BOOL driveable = FALSE;
    __try {
        driveable = VEHICLE::IS_VEHICLE_DRIVEABLE(v, FALSE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return driveable != 0;
}

inline bool GetVehicleEngineHealth(Vehicle v, float& out) {
    if (!ValidVehicle(v)) return false;
    return TryCall(out, 0xC45D23BAF168AAB8ULL, v); // GET_VEHICLE_ENGINE_HEALTH
}

inline bool GetVehicleBodyHealth(Vehicle v, float& out) {
    if (!ValidVehicle(v)) return false;
    return TryCall(out, 0xF271147EB7B40F12ULL, v); // GET_VEHICLE_BODY_HEALTH
}

inline bool GetVehiclePetrolTankHealth(Vehicle v, float& out) {
    if (!ValidVehicle(v)) return false;
    return TryCall(out, 0x7D5DABE888D2D074ULL, v); // GET_VEHICLE_PETROL_TANK_HEALTH
}

// Player-specific güvenli çağrılar
inline bool GetPlayerPed(Player player, Ped& out) {
    return TryCall(out, 0x43A66C31C68491C0ULL, player); // GET_PLAYER_PED
}

inline bool GetPlayerWantedLevel(Player player, int& out) {
    return TryCall(out, 0xE28E54788CE8F12DULL, player); // GET_PLAYER_WANTED_LEVEL
}

inline bool ClearPlayerWantedLevel(Player player) {
    return TryVoid(0xB302540597885499ULL, player); // CLEAR_PLAYER_WANTED_LEVEL
}

inline bool SetPlayerWantedLevelMultiplier(float multiplier) {
    return TryVoid(0x020E5F00CDA207BAULL, multiplier); // SET_WANTED_LEVEL_MULTIPLIER
}

// World/entity pool güvenli çağrılar
inline bool GetClosestVehicle(Vector3 pos, float radius, Hash model, int flags, Vehicle& out) {
    return TryCall(out, 0xF73EB622C4F1689BULL, pos.x, pos.y, pos.z, radius, model, flags); // GET_CLOSEST_VEHICLE
}

// GET_CLOSEST_PED imzası (x,y,z,radius,p4,p5,outPed*,p7,p8,pedType) şeklindedir.
// pedType son argümandır; ignoreFlags geriye dönük uyumluluk için p4 olarak geçer.
inline bool GetClosestPed(Vector3 pos, float radius, int pedType, int ignoreFlags, Ped& out) {
    return TryCall(out, 0xC33AB876A77F8164ULL, pos.x, pos.y, pos.z, radius,
                   ignoreFlags ? TRUE : FALSE, TRUE, &out, TRUE, FALSE, pedType);
}

// Script thread güvenliği
inline bool IsScriptThreadActive() {
    // Script thread'deysek true döner
    return true; // ScriptMain zaten script thread'de çalışır
}

// Güvenli WAIT wrapper
inline void SafeWait(int ms) {
    __try {
        WAIT(ms);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // WAIT crash etmez genelde, ama yine de
    }
}

} // namespace safe
} // namespace hikaye