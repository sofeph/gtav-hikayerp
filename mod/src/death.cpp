#include "death.h"

#include <windows.h>

#include <algorithm>

#include "common.h"
#include "economy.h"
#include "karma.h"

namespace hikaye::death {

namespace {
constexpr int kHospitalBill = 400;
constexpr unsigned long long kGraceMs = 9000;  // yeniden doğuştan sonra zemin gözetimi
std::function<void()> g_restore;
bool g_was_dead = false, g_frozen = false, g_billed = true;
unsigned long long g_grace_until = 0, g_restore_at = 0, g_dead_since = 0;
}  // namespace

void SetLoadoutRestorer(std::function<void()> f) { g_restore = std::move(f); }

void Reset() {
    if (g_frozen) ENTITY::FREEZE_ENTITY_POSITION(PLAYER::PLAYER_PED_ID(), false);
    g_was_dead = false;
    g_frozen = false;
    g_billed = true;
    g_grace_until = g_restore_at = 0;
}

void Update(bool mod_active) {
    if (!mod_active) {
        if (g_was_dead || g_frozen) Reset();
        return;
    }
    const unsigned long long now = GetTickCount64();
    const bool dead = PLAYER::IS_PLAYER_DEAD(PLAYER::PLAYER_ID());

    if (dead) {
        if (!g_was_dead) {
            g_was_dead = true;
            g_dead_since = now;
            Log("olum: oyuncu oldu");
        }
        DrawLine("ÖLDÜN", 0.42f, 220, 40, 40);
        DrawLine("Hastaneye kaldırılıyorsun...", 0.48f, 235, 235, 235);
        return;
    }
    if (g_was_dead) {  // ayıldı: oyun yeniden doğurdu
        g_was_dead = false;
        g_grace_until = now + kGraceMs;
        g_restore_at = now + 4500ULL;  // karakter modeli yeniden uygulandıktan sonra
        g_billed = false;
        Log("olum: yeniden dogdu");
    }

    const Ped player = PLAYER::PLAYER_PED_ID();
    // Zemine gömülme düzeltmesi: yeniden doğuş + model değişimi sırasında çarpışma yüklenmeden düşmeyi önle
    if (now < g_grace_until) {
        const Vector3 p = ENTITY::GET_ENTITY_COORDS(player, true);
        STREAMING::REQUEST_COLLISION_AT_COORD(p.x, p.y, p.z);
        float gz = 0.f;
        if (GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(p.x, p.y, p.z + 60.f, &gz, false)) {
            if (p.z < gz - 0.8f) {
                ENTITY::SET_ENTITY_COORDS(player, p.x, p.y, gz + 0.9f, false, false, false, true);
                ENTITY::SET_ENTITY_VELOCITY(player, 0.f, 0.f, 0.f);
                Log("olum: zemine gomulme duzeltildi");
            }
            if (g_frozen) {
                ENTITY::FREEZE_ENTITY_POSITION(player, false);
                g_frozen = false;
            }
        } else if (!g_frozen) {
            ENTITY::FREEZE_ENTITY_POSITION(player, true);  // zemin henüz yok: düşme
            g_frozen = true;
        }
    } else if (g_frozen) {
        ENTITY::FREEZE_ENTITY_POSITION(player, false);
        g_frozen = false;
    }

    if (!g_billed && now >= g_restore_at && CAM::IS_SCREEN_FADED_IN()) {
        g_billed = true;
        const int cash = economy::GetCash();
        const int bill = cash > 0 ? std::min(kHospitalBill, cash) : 0;
        if (bill > 0) economy::Spend(bill);
        PLAYER::CLEAR_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID());
        TIME::ADD_TO_CLOCK_TIME(2, 0, 0);
        if (g_restore) g_restore();  // RP silahlarını geri ver
        karma::Toast(bill > 0 ? "Hastanede ayıldın: tedavi ücreti -$" + std::to_string(bill) : std::string("Hastanede ayıldın"), 6);
    }
}

}  // namespace hikaye::death
