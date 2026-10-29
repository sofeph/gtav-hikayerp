#pragma once
#include <string>

#include "missions.h"

namespace hikaye {

// GTA V "hangout" mantığı: rehberdeki bir arkadaşı arabana davet et. Arkadaş yürüyüp ön koltuğa biner, birlikte gezip
// sohbet edersiniz (E ile konuş), sen araçtan inince o da iner. Kabul/ret ve koşullar KODDA belirlenir.
class Hangout {
public:
    // Davet isteği. Dönüş "ok" ya da ret nedeni: busy, cooldown, player_not_in_car, no_free_seat, wanted_level,
    // no_spawn_point, model_missing
    std::string Request(const Missions::Talk& who);
    void Update();
    bool Seated() const { return state_ == State::Seated; }
    bool Active() const { return state_ != State::Idle; }
    const Missions::Talk& Who() const { return who_; }  // ped dolu (binince)
    void Shutdown();

private:
    enum class State { Idle, Coming, Seated };
    void Finish();
    State state_ = State::Idle;
    Missions::Talk who_;
    Vehicle veh_ = 0;
    unsigned long long since_ = 0, out_since_ = 0, cooldown_until_ = 0;
};

}  // namespace hikaye
