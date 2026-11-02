#pragma once
#include <string>

#include "common.h"

namespace hikaye {

// Karakterin sana telefon açması (örn. Tracey). Zil çalar ve bizim telefonun ekranında "Gelen arama" görünür
// (ekranı main.cpp açar ve yayınlar). Enter / gamepad seç: cevapla, Backspace / gamepad geri: reddet;
// 25 sn cevapsız kalırsa kaçırılır. Cevaplanınca görüşmeyi Conversation sürdürür.
class IncomingCall {
public:
    enum class Result { None, Answered, Declined, Missed };

    void Ring(const std::string& caller_name);
    Result Update();       // her frame
    void Hangup();         // görüşme bitti
    bool ringing() const { return state_ == State::Ringing; }
    bool in_call() const { return state_ == State::InCall; }
    bool active() const { return state_ != State::Idle; }
    const std::string& caller() const { return name_; }

private:
    enum class State { Idle, Ringing, InCall };
    void StartRingSound();
    void StopRingSound();

    State state_ = State::Idle;
    std::string name_;
    unsigned long long t_ = 0, last_vibrate_ = 0;
    bool ringing_sound_ = false;
    bool enter_was_ = false, back_was_ = false;
};

}  // namespace hikaye
