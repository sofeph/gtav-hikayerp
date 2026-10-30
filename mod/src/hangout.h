#pragma once
#include <string>

#include "missions.h"
#include "common.h"

namespace hikaye {

// GTA V "hangout" mantığı: rehberdeki bir arkadaşı arabana davet et. Arkadaş bulusma noktasinda bekler,
// oyuncu oraya gelince araba binir, birlikte gezip sohbet edersiniz (E ile konus), sen aractan inince o da iner.
// Kabul/ret ve kosullar KODDA belirlenir.
class Hangout {
public:
    // Davet istegi. Donus "ok" ya da ret nedeni: busy, cooldown, player_not_in_car, no_free_seat, wanted_level,
    // no_spawn_point, model_missing
    std::string Request(const Missions::Talk& who, const std::string& destination_name, const Vector3& destination);
    void Update();
    bool Seated() const { return state_ == State::Seated; }
    bool Arrived() const { return state_ == State::Arrived; }
    bool Active() const { return state_ != State::Idle; }
    const Missions::Talk& Who() const { return who_; }  // ped dolu (binince)
    const std::string& DestinationName() const { return destination_name_; }
    void CompleteMeetup();
    void Shutdown();

private:
    enum class State { Idle, Coming, Seated, Arrived };
    void Finish();
    State state_ = State::Idle;
    Missions::Talk who_;
    Vehicle veh_ = 0;
    std::string destination_name_;
    Vector3 destination_{};
    unsigned long long since_ = 0, out_since_ = 0, cooldown_until_ = 0;
    Blip blip_ = 0;
    Blip dest_blip_ = 0;
};

}  // namespace hikaye
