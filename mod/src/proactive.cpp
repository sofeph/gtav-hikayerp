#include "proactive.h"

#include <windows.h>

#include <cmath>
#include <fstream>
#include <random>

#include "karma.h"

namespace hikaye {

using nlohmann::json;

namespace {
std::mt19937& Rng() {
    static std::mt19937 r(static_cast<unsigned>(GetTickCount64() * 7u));
    return r;
}
int Rand(int lo, int hi) { return std::uniform_int_distribution<int>(lo, std::max(lo, hi))(Rng()); }
float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

const char* const kDestinations[] = {"Legion Square", "Del Perro İskelesi", "Vinewood", "Havalimanı", "Vespucci Plajı", "Mission Row"};
}  // namespace

void ProactiveFavor::Load() {
    try {
        std::ifstream in(std::string(kBaseDir) + "/data/favors.json");
        if (!in) return;
        const json j = json::parse(in);
        enabled_ = j.value("enabled", true);
        min_s_ = j.value("min_interval_s", 240);
        max_s_ = j.value("max_interval_s", 600);
        money_lo_ = j.value("money_min", 20);
        money_hi_ = j.value("money_max", 80);
        ride_chance_ = j.value("ride_chance", 0.4f);
    } catch (const std::exception& e) {
        Log(std::string("favors.json HATA: ") + e.what());
    }
}

void ProactiveFavor::Release() {
    if (ped_ != 0 && ENTITY::DOES_ENTITY_EXIST(ped_)) {
        AI::CLEAR_PED_TASKS(ped_);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped_, false);
        Ped p = ped_;
        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&p);
    }
    ped_ = 0;
    state_ = State::Idle;
}

void ProactiveFavor::Abort() { Release(); }

void ProactiveFavor::Update(bool busy, const Vector3& pos, const Places* places,
                            const std::function<bool(Ped, const json&)>& start) {
    if (!enabled_) return;
    const unsigned long long now = GetTickCount64();
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (next_at_ == 0) next_at_ = now + static_cast<unsigned long long>(Rand(min_s_, max_s_)) * 1000ULL / 2;  // ilk istek daha erken

    if (state_ == State::Approaching) {
        if (busy || !ENTITY::DOES_ENTITY_EXIST(ped_) || ENTITY::IS_ENTITY_DEAD(ped_) || now - since_ > 28000ULL) {
            Release();
            next_at_ = now + 60000ULL;
            return;
        }
        const Vector3 pp = ENTITY::GET_ENTITY_COORDS(ped_, true);
        if (D2(pp, pos) < 3.4f) {
            const Ped ped = ped_;
            ped_ = 0;
            state_ = State::Idle;
            AI::CLEAR_PED_TASKS(ped);
            PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, true);  // konuşma bitince Conversation serbest bırakır
            if (!start(ped, facts_)) {
                PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, false);
                Ped p = ped;
                ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&p);
            }
            next_at_ = now + static_cast<unsigned long long>(Rand(min_s_, max_s_)) * 1000ULL;
        }
        return;
    }

    if (busy || now < next_at_) return;
    // Haydut ünlüler kimseye yaklaşılmaz; polis peşindeyken veya ölüyken istek yok.
    if (karma::Level() == "outlaw" || PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) > 0 || ENTITY::IS_ENTITY_DEAD(player)) {
        next_at_ = now + 30000ULL;
        return;
    }
    const Vehicle pv = PED::GET_VEHICLE_PED_IS_IN(player, false);
    const bool on_foot = pv == 0;
    const bool stopped_car = pv != 0 && VEHICLE::GET_PED_IN_VEHICLE_SEAT(pv, -1) == player && ENTITY::GET_ENTITY_SPEED(pv) < 1.5f &&
                             VEHICLE::IS_VEHICLE_SEAT_FREE(pv, 0);
    if (!on_foot && !stopped_car) {
        next_at_ = now + 20000ULL;
        return;
    }
    int arr[2 + 2 * 16];
    arr[0] = 16;
    const int n = PED::GET_PED_NEARBY_PEDS(player, arr, -1);
    Ped pick = 0;
    for (int i = 0; i < n && i < 16 && pick == 0; ++i) {
        const Ped p = arr[2 + i * 2];
        if (p == 0 || p == player || !ENTITY::DOES_ENTITY_EXIST(p) || !PED::IS_PED_HUMAN(p) || ENTITY::IS_ENTITY_DEAD(p)) continue;
        if (PED::IS_PED_IN_ANY_VEHICLE(p, false) || ENTITY::IS_ENTITY_A_MISSION_ENTITY(p) || PED::IS_PED_IN_COMBAT(p, player)) continue;
        if (PED::GET_PED_TYPE(p) == 6 || PED::GET_PED_TYPE(p) == 27 || PED::GET_PED_TYPE(p) == 29) continue;  // polis, SWAT, ordu
        const float d = D2(ENTITY::GET_ENTITY_COORDS(p, true), pos);
        if (d < 5.f || d > 30.f) continue;
        pick = p;
    }
    if (pick == 0) {
        next_at_ = now + 15000ULL;  // yakında uygun yaya yok: kısa süre sonra tekrar bak
        return;
    }
    json f = json::object();
    if (stopped_car && places) {
        // arabadaysan para yerine yolculuk istenir; hedef çözülebilmeli
        Place pl;
        const char* dest = kDestinations[Rand(0, 5)];
        if (places && places->Resolve(dest, pos, pl) && D2(pl.pos, pos) > 600.f) {
            f = {{"kind", "ride"}, {"destination", pl.name}};
        }
    }
    if (!f.contains("kind")) f = {{"kind", "money"}, {"amount", Rand(money_lo_, money_hi_) / 5 * 5}};
    f["role"] = "approached_player";
    facts_ = f;
    ped_ = pick;
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped_, true, true);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped_, true);
    AI::CLEAR_PED_TASKS(ped_);
    AI::TASK_GO_TO_ENTITY(ped_, player, 28000, 2.0f, 1.0f, 1073741824.f, 0);
    state_ = State::Approaching;
    since_ = now;
    Log("proaktif iyilik: yaya yaklasiyor (" + f.value("kind", "") + ")");
}

}  // namespace hikaye
