#pragma once
#include <functional>
#include <string>

#include "common.h"
#include "json.hpp"
#include "places.h"

namespace hikaye {

// Proaktif iyilik isteyen NPC: ara sıra yoldaki bir yaya oyuncuya yürüyüp YAKLAŞIR ve konuşmayı İLK O başlatır
// (para ister ya da arabayla bir yere bırakılmak ister). Kabul/ret oyuncuya ait: kabul edilirse mevcut iyilik sistemi
// (give_money / give_ride) devreye girer, onur kazandırır. OTORİTE KODDA: kimin geleceğine, ne isteyeceğine ve ne zaman
// geleceğine kod karar verir; LLM sadece ilk cümleyi söyler. Ayarlar: data/favors.json.
class ProactiveFavor {
public:
    void Load();
    // Her frame (mod aktifken). busy: konuşma/telefon/hapis/görev sahnesi vb. varsa true (yeni istek başlamaz).
    // start: yaya yeterince yaklaşınca çağrılır (konuşmayı başlatır); true dönerse istek tamamlanmış sayılır.
    void Update(bool busy, const Vector3& player_pos, const Places* places,
                const std::function<bool(Ped, const nlohmann::json&)>& start);
    bool IsApproacher(Ped p) const { return state_ == State::Approaching && ped_ == p; }
    void Abort();

private:
    enum class State { Idle, Approaching };
    void Release();
    bool enabled_ = true;
    int min_s_ = 240, max_s_ = 600, money_lo_ = 20, money_hi_ = 80;
    float ride_chance_ = 0.4f;
    State state_ = State::Idle;
    Ped ped_ = 0;
    nlohmann::json facts_;
    unsigned long long next_at_ = 0, since_ = 0;
};

}  // namespace hikaye
