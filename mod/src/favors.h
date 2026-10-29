#pragma once
#include <string>

#include "common.h"

namespace hikaye {

// İyilik: bir yayayı arabayla bir yerden bir yere götürmek. LLM sadece niyeti ('give_ride') ve gidilecek yeri önerir;
// koşulları (araç, boş koltuk, hedef), yolculuğu, varışı ve ödülü (onur + küçük bahşiş) kod belirler.
class RideFavor {
public:
    bool active() const { return state_ != State::Idle; }
    // Başarısızsa reason: no_vehicle | no_free_seat | busy. Başarılıysa yaya arabaya biner.
    bool Start(Ped ped, const Vector3& dest, const std::string& dest_name, std::string& reason);
    void Update();  // her frame (mod aktifken)
    void Abort();

private:
    enum class State { Idle, Boarding, Riding, Leaving };
    void Release();
    State state_ = State::Idle;
    Ped ped_ = 0;
    Vehicle veh_ = 0;
    Vector3 dest_{};
    std::string dest_name_;
    unsigned long long t_ = 0;
};

}  // namespace hikaye
