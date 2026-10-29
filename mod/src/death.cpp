#include "death.h"

#include <windows.h>
#include <algorithm>

#include "common.h"
#include "economy.h"
#include "karma.h"

namespace hikaye::death {

namespace {
constexpr int kHospitalBill = 400;

struct Hospital {
    float x, y, z, heading;
    const char* name;
};

// Hastanelerin hemen önündeki ana cadde / yol koordinatları
static const Hospital kHospitals[] = {
    { 341.0f,  -583.0f, 28.80f, 248.0f, "Pillbox Hill Tip Merkezi" },
    { 356.0f, -1598.0f, 29.30f, 320.0f, "Merkez Los Santos Tip Merkezi" },
    { -470.0f, -340.0f, 34.50f, 260.0f, "Mount Zonah Tip Merkezi" },
    { 1833.0f, 3680.0f, 34.30f, 210.0f, "Sandy Shores Saglik Klinigi" },
    { -240.0f, 6325.0f, 32.40f, 225.0f, "Paleto Bay Acil Bakim Merkezi" },
};

std::function<void()> g_restore;

enum class State {
    kAlive,
    kJustDied,
    kTeleportAndLoad,
    kWakingUp
};

State g_state = State::kAlive;
unsigned long long g_state_time = 0;
Hospital g_target_hosp = kHospitals[0];
Vector3 g_spawn_pos = {};
float g_spawn_heading = 0.f;
bool g_frozen = false;

inline float DistSq(float x1, float y1, float x2, float y2) {
    float dx = x1 - x2;
    float dy = y1 - y2;
    return dx * dx + dy * dy;
}

const Hospital& FindNearestHospital(const Vector3& pos) {
    int best = 0;
    float best_dist = DistSq(pos.x, pos.y, kHospitals[0].x, kHospitals[0].y);
    const int count = static_cast<int>(sizeof(kHospitals) / sizeof(kHospitals[0]));
    for (int i = 1; i < count; ++i) {
        float d = DistSq(pos.x, pos.y, kHospitals[i].x, kHospitals[i].y);
        if (d < best_dist) {
            best_dist = d;
            best = i;
        }
    }
    return kHospitals[best];
}
}  // namespace

void SetLoadoutRestorer(std::function<void()> f) { g_restore = std::move(f); }

void Reset() {
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (g_frozen) {
        ENTITY::FREEZE_ENTITY_POSITION(player, false);
        g_frozen = false;
    }
    g_state = State::kAlive;
    g_state_time = 0;
}

void Update(bool mod_active) {
    if (!mod_active) {
        if (g_state != State::kAlive || g_frozen) Reset();
        return;
    }

    // Oyunun native Wasted/Busted ve sonsuz yükleme ekranını tamamen devre dışı bırak
    GAMEPLAY::SET_FADE_OUT_AFTER_DEATH(false);
    GAMEPLAY::SET_FADE_IN_AFTER_DEATH_ARREST(false);
    GAMEPLAY::SET_FADE_IN_AFTER_LOAD(false);
    GAMEPLAY::IGNORE_NEXT_RESTART(true);
    SCRIPT::SET_NO_LOADING_SCREEN(true);

    const unsigned long long now = GetTickCount64();
    const Player player_id = PLAYER::PLAYER_ID();
    const Ped player = PLAYER::PLAYER_PED_ID();

    switch (g_state) {
        case State::kAlive: {
            // Oyuncu öldü mü kontrol et
            const bool dead = PLAYER::IS_PLAYER_DEAD(player_id) || ENTITY::IS_ENTITY_DEAD(player);
            if (dead) {
                Vector3 p = ENTITY::GET_ENTITY_COORDS(player, true);
                g_target_hosp = FindNearestHospital(p);

                // Hastane koordinatlarında dünya altına spawn et (oyun motoru güvenle yüzeye toparlar)
                g_spawn_pos.x = g_target_hosp.x;
                g_spawn_pos.y = g_target_hosp.y;
                g_spawn_pos.z = -50.0f;
                g_spawn_heading = g_target_hosp.heading;

                g_state = State::kJustDied;
                g_state_time = now;
                CAM::DO_SCREEN_FADE_OUT(600);
                Log("olum: ozel olum sistemi tetiklendi -> " + std::string(g_target_hosp.name) + " dunya alti spawn (-50z)");
            }
            break;
        }

        case State::kJustDied: {
            // Kendi özel ölüm arayüzümüzü çiz
            DrawLine("BILINCINI KAYBETTIN", 0.44f, 220, 40, 40);
            DrawLine("Hastaneye naklediliyorsun...", 0.50f, 235, 235, 235);

            // Kararma tamamlandığında veya 800ms geçtiğinde dirilt ve yol ortasına ışınla
            if (CAM::IS_SCREEN_FADED_OUT() || (now - g_state_time >= 800)) {
                // Ped'i dirilt ve temizle
                PED::RESURRECT_PED(player);
                ENTITY::SET_ENTITY_HEALTH(player, 200);
                AI::CLEAR_PED_TASKS_IMMEDIATELY(player);
                PED::CLEAR_PED_BLOOD_DAMAGE(player);
                PLAYER::RESET_PLAYER_STAMINA(player_id);
                PLAYER::CLEAR_PLAYER_WANTED_LEVEL(player_id);

                // Hastanenin altında dünya altına yerleştir (oyun otomatik yüzeye toparlar)
                ENTITY::SET_ENTITY_COORDS_NO_OFFSET(player, g_spawn_pos.x, g_spawn_pos.y, -50.0f, false, false, true);
                ENTITY::SET_ENTITY_HEADING(player, g_spawn_heading);
                ENTITY::SET_ENTITY_VELOCITY(player, 0.f, 0.f, 0.f);
                ENTITY::FREEZE_ENTITY_POSITION(player, false);
                g_frozen = false;

                // Bölgenin çarpışma ve sahnesini yükle
                STREAMING::REQUEST_COLLISION_AT_COORD(g_spawn_pos.x, g_spawn_pos.y, g_target_hosp.z);
                STREAMING::LOAD_SCENE(g_spawn_pos.x, g_spawn_pos.y, g_target_hosp.z);

                g_state = State::kTeleportAndLoad;
                g_state_time = now;
            }
            break;
        }

        case State::kTeleportAndLoad: {
            STREAMING::REQUEST_COLLISION_AT_COORD(g_spawn_pos.x, g_spawn_pos.y, g_target_hosp.z);
            // Oyunun dünya altından yüzeye toparlaması için kısa süre bekle
            if (now - g_state_time >= 1000) {
                // Eğer oyun motoru henüz yukarı atmadıysa zemin koordinatıyla güvene al
                const Vector3 cur = ENTITY::GET_ENTITY_COORDS(player, true);
                if (cur.z < -20.0f) {
                    float gz = 0.f;
                    if (GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(g_spawn_pos.x, g_spawn_pos.y, g_target_hosp.z + 15.f, &gz, false)) {
                        ENTITY::SET_ENTITY_COORDS(player, g_spawn_pos.x, g_spawn_pos.y, gz + 0.9f, false, false, false, true);
                    } else {
                        ENTITY::SET_ENTITY_COORDS(player, g_spawn_pos.x, g_spawn_pos.y, g_target_hosp.z, false, false, false, true);
                    }
                }
                ENTITY::SET_ENTITY_VELOCITY(player, 0.f, 0.f, 0.f);

                g_state = State::kWakingUp;
                g_state_time = now;
            }
            break;
        }

        case State::kWakingUp: {
            // Yükleme ekranı kilidini kaldır
            SCRIPT::SHUTDOWN_LOADING_SCREEN();

            // Hastane masrafı ve saat ilerletme
            const int cash = economy::GetCash();
            const int bill = cash > 0 ? std::min(kHospitalBill, cash) : 0;
            if (bill > 0) economy::Spend(bill);

            PLAYER::CLEAR_PLAYER_WANTED_LEVEL(player_id);
            TIME::ADD_TO_CLOCK_TIME(2, 0, 0);

            // RP silah ve envanterini geri yükle
            if (g_restore) g_restore();

            // Ekranı aç
            CAM::DO_SCREEN_FADE_IN(1200);

            // Bilgilendirme mesajı
            std::string toast = std::string(g_target_hosp.name) + ": Tedavi edildin";
            if (bill > 0) toast += " (-$" + std::to_string(bill) + ")";
            karma::Toast(toast, 6);

            Log("olum: hastane yakininda yol ortasinda ayildi (" + toast + ")");
            g_state = State::kAlive;
            break;
        }
    }
}

}  // namespace hikaye::death
