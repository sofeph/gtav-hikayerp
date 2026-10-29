#pragma once
#include <string>

#include "common.h"

namespace hikaye {

// Karakterin sana telefon açması (örn. Tracey). Zil çalar, oyunun KENDİ telefon ekranı (iFruit/Badger/Facade) elde açılır
// ve arayan adıyla "gelen arama" gösterilir. Enter: cevapla, Backspace: reddet; 25 sn cevapsız kalırsa kaçırılır.
// Cevaplayınca oyunun telefonunda "bağlandı" görünür ve görüşmeyi Conversation sürdürür; bitince telefon cebe girer.
// Telefonun iç yapısı oyun sürümüne bağlıdır (bkz. nativephone.h): ekran açılamazsa yalnızca zil ve ekran yazısı kalır.
class IncomingCall {
public:
    enum class Result { None, Answered, Declined, Missed };

    void Ring(const std::string& caller_name);
    Result Update();       // her frame
    void Hangup();         // görüşme bitti: telefonu cebe koy
    void Draw() const;     // "Gelen arama: ..." yazısı
    bool ringing() const { return state_ == State::Ringing; }
    bool in_call() const { return state_ == State::InCall; }
    bool active() const { return state_ != State::Idle; }

private:
    enum class State { Idle, Ringing, InCall };
    int Scaleform();
    void ShowCallScreen(int handle, const char* status);
    void StartRingSound();
    void StopRingSound();
    void PutAway();

    State state_ = State::Idle;
    std::string name_;
    unsigned long long t_ = 0, last_ring_ = 0;
    int sound_id_ = -1;
    bool enter_was_ = false, back_was_ = false;
};

}  // namespace hikaye
