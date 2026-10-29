#pragma once
#include <functional>

#include "common.h"

namespace hikaye::street_events {

bool Chance(double p);  // [0,1] olasılıkla true

// Konuşma gerginleşince (LLM 'confront' önerir, sonucu KOD belirler): zarla silahlı saldırı veya yumruk dövüşü.
// armed_chance: data/jail.json -> confront.armed_attack_chance. Dönüş: silahlı mı saldırdı.
bool Confront(Ped ped, double armed_chance);

// NPC telefona sarılıp polisi arar (kısa animasyon), bitince `on_done` çağrılır (3 yıldız jail.* tarafından verilir).
class PoliceCallEvent {
public:
    using DoneFn = std::function<void(bool player_armed)>;
    bool active() const { return active_; }
    void Abort() { active_ = false; }
    bool Start(Ped ped, DoneFn on_done);
    void Update();  // her frame

private:
    bool active_ = false;
    Ped ped_ = 0;
    unsigned long long t_ = 0;
    bool player_armed_ = false;
    DoneFn done_;
};

// Flörtöz bir kadının oyuncunun arabasına binmesi. Sahne ana oyundaki gibi ima edilir:
// ekran kararır, kısa bir süre sonra açılır ve kadın araçtan iner. Hiçbir şey çizilmez.
class RideEvent {
public:
    bool active() const { return state_ != State::Idle; }
    Ped ped() const { return ped_; }
    // Koşullar: oyuncu araçta, araç neredeyse duruyor, ön yolcu koltuğu boş. Sağlanmazsa false (hiçbir şey yapılmaz).
    bool Start(Ped ped);
    void Update();  // her frame
    void Abort() {  // moddan çıkarken sahneyi kes
        if (active()) Finish();
    }

private:
    enum class State { Idle, Boarding, FadeOut, Scene, FadeIn, Leaving };
    void Finish();  // kontrolü geri verir, ped'i serbest bırakır

    State state_ = State::Idle;
    Ped ped_ = 0;
    Vehicle veh_ = 0;
    unsigned long long t_ = 0;
};

}  // namespace hikaye::street_events
