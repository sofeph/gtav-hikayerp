#include "street_events.h"

#include <windows.h>

#include <random>

namespace hikaye::street_events {

bool Chance(double p) {
    static std::mt19937 rng{std::random_device{}()};
    return std::uniform_real_distribution<double>(0.0, 1.0)(rng) < p;
}

namespace {
constexpr unsigned long long kPhoneCallMs = 3500;
constexpr int kFrontPassengerSeat = 0;
constexpr unsigned long long kBoardingTimeoutMs = 10000;
constexpr unsigned long long kSceneMs = 5000;
constexpr unsigned long long kFadeMs = 1200;
}  // namespace

bool PoliceCallEvent::Start(Ped ped, DoneFn on_done) {
    if (active_ || !ENTITY::DOES_ENTITY_EXIST(ped)) return false;
    const Ped player = PLAYER::PLAYER_PED_ID();
    Hash weapon = 0;
    WEAPON::GET_CURRENT_PED_WEAPON(player, &weapon, true);
    player_armed_ = weapon != 0 && weapon != 0xA2719263;  // WEAPON_UNARMED dışında bir şey elinde
    ped_ = ped;
    done_ = std::move(on_done);
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped_, true, true);
    AI::CLEAR_PED_TASKS(ped_);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped_, true);
    AI::TASK_START_SCENARIO_IN_PLACE(ped_, const_cast<char*>("WORLD_HUMAN_STAND_MOBILE"), 0, true);
    active_ = true;
    t_ = GetTickCount64();
    Log("olay: npc polisi ariyor");
    return true;
}

void PoliceCallEvent::Update() {
    if (!active_) return;
    // Telefon konuşması bitince veya NPC öldürülünce olay tamamlanır.
    const bool dead = !ENTITY::DOES_ENTITY_EXIST(ped_) || ENTITY::IS_ENTITY_DEAD(ped_);
    if (!dead && GetTickCount64() - t_ < kPhoneCallMs) return;
    active_ = false;
    if (dead) return;  // aramayı bitiremedi: polis çağrılmadı
    AI::CLEAR_PED_TASKS(ped_);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped_, false);
    AI::TASK_SMART_FLEE_PED(ped_, PLAYER::PLAYER_PED_ID(), 80.0f, -1, false, false);
    Ped p = ped_;
    ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&p);
    if (done_) done_(player_armed_);
}

bool Confront(Ped ped, double armed_chance) {
    const Ped player = PLAYER::PLAYER_PED_ID();
    const bool armed = Chance(armed_chance);
    AI::CLEAR_PED_TASKS(ped);  // "ayakta bekle" görevini kaldır
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, false);
    if (armed) {
        WEAPON::GIVE_WEAPON_TO_PED(ped, HashKey("WEAPON_PISTOL"), 60, false, true);
        PED::SET_PED_ACCURACY(ped, 25);  // sinirli ama ölümcül olmayan nişan
    } else {
        WEAPON::REMOVE_ALL_PED_WEAPONS(ped, true);  // silahsızsa yumrukla dövüşür
    }
    PED::SET_PED_COMBAT_ATTRIBUTES(ped, 46, true);  // always fight: kaçmasın
    PED::SET_PED_COMBAT_ATTRIBUTES(ped, 5, true);   // silahsızken de silahlıya karşı savaşsın
    PED::SET_PED_COMBAT_ABILITY(ped, 1);
    AI::TASK_COMBAT_PED(ped, player, 0, 16);
    PED::SET_PED_KEEP_TASK(ped, true);
    ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&ped);  // oyun yönetsin, görev sürer
    Log(armed ? "olay: silahli saldiri" : "olay: yumruk dovusu");
    return armed;
}

bool RideEvent::Start(Ped ped) {
    if (active() || !ENTITY::DOES_ENTITY_EXIST(ped)) return false;
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (!PED::IS_PED_IN_ANY_VEHICLE(player, false)) return false;
    const Vehicle veh = PED::GET_VEHICLE_PED_IS_IN(player, false);
    if (veh == 0 || ENTITY::GET_ENTITY_SPEED(veh) > 2.0f) return false;
    if (!VEHICLE::IS_VEHICLE_SEAT_FREE(veh, kFrontPassengerSeat)) return false;

    ped_ = ped;
    veh_ = veh;
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped_, true, true);
    AI::CLEAR_PED_TASKS(ped_);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped_, true);
    AI::TASK_ENTER_VEHICLE(ped_, veh_, static_cast<int>(kBoardingTimeoutMs), kFrontPassengerSeat, 1.0f, 1, 0);
    state_ = State::Boarding;
    t_ = GetTickCount64();
    Log("olay: kadin arabaya biniyor");
    return true;
}

void RideEvent::Finish() {
    PLAYER::SET_PLAYER_CONTROL(PLAYER::PLAYER_ID(), true, 0);
    if (CAM::IS_SCREEN_FADED_OUT()) CAM::DO_SCREEN_FADE_IN(static_cast<int>(kFadeMs));
    if (ENTITY::DOES_ENTITY_EXIST(ped_)) {
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped_, false);
        AI::TASK_WANDER_STANDARD(ped_, 10.0f, 10);
        Ped p = ped_;
        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&p);
    }
    state_ = State::Idle;
    ped_ = 0;
    veh_ = 0;
}

void RideEvent::Update() {
    if (!active()) return;
    const unsigned long long now = GetTickCount64();
    const Ped player = PLAYER::PLAYER_PED_ID();
    // Oyuncu araçtan indi/öldü veya ped yok oldu: sahneyi iptal et.
    if (!ENTITY::DOES_ENTITY_EXIST(ped_) || ENTITY::IS_ENTITY_DEAD(ped_) || ENTITY::IS_ENTITY_DEAD(player) ||
        (state_ != State::Leaving && !PED::IS_PED_IN_ANY_VEHICLE(player, false))) {
        return Finish();
    }
    switch (state_) {
        case State::Boarding:
            if (PED::IS_PED_IN_VEHICLE(ped_, veh_, false)) {
                PLAYER::SET_PLAYER_CONTROL(PLAYER::PLAYER_ID(), false, 0);  // sahne boyunca araç sürülmez
                CAM::DO_SCREEN_FADE_OUT(static_cast<int>(kFadeMs));
                state_ = State::FadeOut;
                t_ = now;
            } else if (now - t_ > kBoardingTimeoutMs) {
                Finish();
            }
            break;
        case State::FadeOut:
            if (CAM::IS_SCREEN_FADED_OUT() || now - t_ > kFadeMs + 1500) {
                state_ = State::Scene;
                t_ = now;
            }
            break;
        case State::Scene:  // ekran karanlık: sahne ima edilir, hiçbir şey gösterilmez
            if (now - t_ > kSceneMs) {
                CAM::DO_SCREEN_FADE_IN(static_cast<int>(kFadeMs));
                state_ = State::FadeIn;
                t_ = now;
            }
            break;
        case State::FadeIn:
            if (CAM::IS_SCREEN_FADED_IN() || now - t_ > kFadeMs + 1500) {
                PLAYER::SET_PLAYER_CONTROL(PLAYER::PLAYER_ID(), true, 0);
                AI::TASK_LEAVE_VEHICLE(ped_, veh_, 0);
                state_ = State::Leaving;
                t_ = now;
            }
            break;
        case State::Leaving:
            if (!PED::IS_PED_IN_VEHICLE(ped_, veh_, false) || now - t_ > 6000) Finish();
            break;
        default:
            break;
    }
}

}  // namespace hikaye::street_events
