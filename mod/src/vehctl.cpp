#include "vehctl.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "controls.h"
#include "fuel.h"

namespace hikaye::vehctl {

namespace {
enum Signal { kNone = 0, kLeft = 1, kRight = 2, kHazard = 3 };
struct State {
    Signal signal = kNone;
    float start_heading = 0.f;
    unsigned long long since = 0;
};
std::map<Vehicle, State> g_state;
struct Flash {
    Vehicle v;
    unsigned long long until;
};
std::vector<Flash> g_flash;       // kilitleme geri bildirimi: kısa dörtlü yanıp söner
Vehicle g_prev = 0;               // önceki karedeki aracımız
Vehicle g_pending_lock = 0;       // inip uzaklaşınca kilitlenecek sahip olunan araç
unsigned long long g_pending_since = 0, g_toast_until = 0;
std::string g_toast;
bool g_down[5] = {false, false, false, false, false};

bool Edge(int slot, int vk) {
    const bool d = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool e = d && !g_down[slot];
    g_down[slot] = d;
    return e;
}

void Toast(const std::string& t) {
    g_toast = t;
    g_toast_until = GetTickCount64() + 2500;
}

float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

float HeadingDiff(float a, float b) {
    float d = std::fabs(a - b);
    while (d > 360.f) d -= 360.f;
    return d > 180.f ? 360.f - d : d;
}

void ApplySignal(Vehicle v, Signal s) {
    VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(v, 1, s == kLeft || s == kHazard);   // 1: sol
    VEHICLE::SET_VEHICLE_INDICATOR_LIGHTS(v, 0, s == kRight || s == kHazard);  // 0: sağ
}

void Beep(Vehicle v, bool locked) {
    VEHICLE::START_VEHICLE_HORN(v, locked ? 90 : 60, HashKey("HELDDOWN"), false);
    const unsigned long long now = GetTickCount64();
    State& st = g_state[v];
    if (st.signal == kNone) {  // geri bildirim: dörtlü kısa yanıp söner
        ApplySignal(v, kHazard);
        g_flash.push_back({v, now + 700});
    }
}

void ToggleLock(Vehicle v) {
    const bool locked = VEHICLE::GET_VEHICLE_DOOR_LOCK_STATUS(v) == 2;
    VEHICLE::SET_VEHICLE_DOORS_LOCKED(v, locked ? 1 : 2);
    Beep(v, !locked);
    Toast(locked ? "Araç kilidi açıldı" : "Araç kilitlendi");
}
}  // namespace

void Reset() {
    AutoDrive(false);
    for (auto& [v, st] : g_state) {
        if (ENTITY::DOES_ENTITY_EXIST(v)) ApplySignal(v, kNone);
    }
    g_state.clear();
    g_flash.clear();
    g_prev = 0;
    g_pending_lock = 0;
}

void Update(bool blocked, const std::function<bool(Vehicle)>& owned, const std::function<std::vector<Vehicle>()>& owned_list) {
    const unsigned long long now = GetTickCount64();
    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vehicle cur = PED::GET_VEHICLE_PED_IS_IN(player, false);
    const Vector3 pos = ENTITY::GET_ENTITY_COORDS(player, true);
    if (now < g_toast_until) DrawLine(g_toast, 0.80f, 150, 235, 170);

    // Kilit geri bildirimi bitince dörtlüyü kapat
    for (size_t i = 0; i < g_flash.size();) {
        if (now > g_flash[i].until) {
            if (ENTITY::DOES_ENTITY_EXIST(g_flash[i].v) && g_state[g_flash[i].v].signal == kNone) ApplySignal(g_flash[i].v, kNone);
            g_flash.erase(g_flash.begin() + i);
        } else {
            ++i;
        }
    }

    // Sahip olunan araçtan inildi: uzaklaşınca kilitlenir
    if (g_prev != 0 && cur == 0 && owned(g_prev) && ENTITY::DOES_ENTITY_EXIST(g_prev)) {
        g_pending_lock = g_prev;
        g_pending_since = now;
    }
    if (cur != 0) g_pending_lock = 0;
    g_prev = cur;
    if (g_pending_lock != 0) {
        if (!ENTITY::DOES_ENTITY_EXIST(g_pending_lock) || now - g_pending_since > 90000ULL) {
            g_pending_lock = 0;
        } else if (D2(pos, ENTITY::GET_ENTITY_COORDS(g_pending_lock, true)) > 8.f) {
            if (VEHICLE::GET_VEHICLE_DOOR_LOCK_STATUS(g_pending_lock) != 2) {
                VEHICLE::SET_VEHICLE_DOORS_LOCKED(g_pending_lock, 2);
                Beep(g_pending_lock, true);
                Toast("Araç kendiliğinden kilitlendi");
            }
            g_pending_lock = 0;
        }
    }

    // Sinyaller kendiliğinden sönsün (dönüş bitti ya da süre doldu)
    for (auto it = g_state.begin(); it != g_state.end();) {
        const Vehicle v = it->first;
        State& st = it->second;
        if (!ENTITY::DOES_ENTITY_EXIST(v)) {
            it = g_state.erase(it);
            continue;
        }
        if ((st.signal == kLeft || st.signal == kRight) &&
            (HeadingDiff(ENTITY::GET_ENTITY_HEADING(v), st.start_heading) > 45.f || now - st.since > 25000ULL)) {
            st.signal = kNone;
            ApplySignal(v, kNone);
        }
        ++it;
    }

    // HUD: aktif sinyal
    if (cur != 0) {
        const State& st = g_state[cur];
        const char* t = st.signal == kLeft ? "◄ Sol sinyal" : st.signal == kRight ? "Sağ sinyal ►" : st.signal == kHazard ? "▲ Dörtlü" : nullptr;
        if (t) DrawLine(t, 0.90f, 255, 200, 80);
    }

    if (blocked) {  // yazı kutusu/konuşma açıkken tuşlar karışmasın
        for (bool& d : g_down) d = true;
        return;
    }

    // Kilit
    if (Edge(0, controls::LockKey())) {
        Vehicle target = cur;
        if (target == 0) {
            float best = 25.f;
            for (const Vehicle v : owned_list()) {
                if (!ENTITY::DOES_ENTITY_EXIST(v)) continue;
                const float d = D2(pos, ENTITY::GET_ENTITY_COORDS(v, true));
                if (d < best) {
                    best = d;
                    target = v;
                }
            }
        }
        if (target != 0) ToggleLock(target);
        else Toast("Yakında kilitleyecek aracın yok");
    }
    // Sinyaller ve kontak: sadece araçtayken
    const bool lp = Edge(1, controls::SignalLeftKey()), rp = Edge(2, controls::SignalRightKey()), hp = Edge(3, controls::HazardKey()),
               ep = Edge(4, controls::EngineKey());
    if (cur == 0) return;
    State& st = g_state[cur];
    auto set = [&](Signal s) {
        st.signal = st.signal == s ? kNone : s;
        st.start_heading = ENTITY::GET_ENTITY_HEADING(cur);
        st.since = now;
        ApplySignal(cur, st.signal);
    };
    if (lp) set(kLeft);
    if (rp) set(kRight);
    if (hp) set(kHazard);
    if (ep) {
        const bool running = VEHICLE::_IS_VEHICLE_ENGINE_ON(cur) != 0;
        if (!running && fuel::Get(cur) == 0.f) {
            Toast("Yakıt yok: motor çalışmıyor");
        } else {
            VEHICLE::SET_VEHICLE_ENGINE_ON(cur, !running, false, true);
            Toast(running ? "Motor kapatıldı" : "Motor çalıştırıldı");
        }
    }
}


namespace {
bool g_auto = false;
Vehicle g_auto_veh = 0;

void StopAuto() {
    if (!g_auto) return;
    g_auto = false;
    GAMEPLAY::SET_TIME_SCALE(1.0f);
    const Ped p = PLAYER::PLAYER_PED_ID();
    if (PED::IS_PED_IN_VEHICLE(p, g_auto_veh, false)) AI::CLEAR_PED_TASKS(p);  // araçta kalır, direksiyon yine oyuncuda
    g_auto_veh = 0;
}
}  // namespace

void AutoDrive(bool typing) {
    const Ped p = PLAYER::PLAYER_PED_ID();
    const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(p, false);
    const bool can = v != 0 && VEHICLE::GET_PED_IN_VEHICLE_SEAT(v, -1) == p && !ENTITY::IS_ENTITY_DEAD(p);
    if (g_auto && (!typing || !can || v != g_auto_veh)) {
        StopAuto();
        return;
    }
    if (!g_auto && typing && can && ENTITY::GET_ENTITY_SPEED(v) > 4.f && !ENTITY::IS_ENTITY_IN_AIR(v)) {
        const Hash vm = ENTITY::GET_ENTITY_MODEL(v);
        if (VEHICLE::IS_THIS_MODEL_A_CAR(vm) || VEHICLE::IS_THIS_MODEL_A_BIKE(vm)) {
            const float speed = std::min(ENTITY::GET_ENTITY_SPEED(v), 24.f);
            AI::TASK_VEHICLE_DRIVE_WANDER(p, v, speed, 786603);
            GAMEPLAY::SET_TIME_SCALE(0.6f);
            g_auto = true;
            g_auto_veh = v;
            Log("otomatik surus basladi");
        }
    }
}

}  // namespace hikaye::vehctl
