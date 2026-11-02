#include "incomingcall.h"

#include <windows.h>

#include "phone.h"

namespace hikaye {

namespace {
constexpr unsigned long long kRingTimeoutMs = 25000, kVibrateEveryMs = 1500;
constexpr int kPhoneSelectControl = 176, kPhoneCancelControl = 177;  // INPUT_CELLPHONE_SELECT / CANCEL (gamepad)
}  // namespace

void IncomingCall::StartRingSound() {
    phone::StartRingtone();
    ringing_sound_ = true;
}

void IncomingCall::StopRingSound() {
    if (!ringing_sound_) return;
    phone::StopRingtone();
    CONTROLS::STOP_PAD_SHAKE(0);
    ringing_sound_ = false;
}

void IncomingCall::Ring(const std::string& caller_name) {
    if (state_ != State::Idle) return;
    name_ = caller_name;
    state_ = State::Ringing;
    t_ = GetTickCount64();
    last_vibrate_ = 0;
    enter_was_ = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;  // basılı tutulan tuş kendiliğinden cevaplamasın
    back_was_ = (GetAsyncKeyState(VK_BACK) & 0x8000) != 0;
    StartRingSound();
    Log("gelen arama: " + caller_name);
}

IncomingCall::Result IncomingCall::Update() {
    if (state_ != State::Ringing) return Result::None;
    const unsigned long long now = GetTickCount64();

    if (phone::GetVibrate() && now - last_vibrate_ > kVibrateEveryMs) {
        last_vibrate_ = now;
        CONTROLS::SET_PAD_SHAKE(0, 600, 200);
    }

    // Telefon açıkken 176/177 devre dışıdır: gamepad basışı "disabled" sürümüyle okunur
    const bool enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0, back = (GetAsyncKeyState(VK_BACK) & 0x8000) != 0;
    const bool answer = (enter && !enter_was_) || CONTROLS::IS_DISABLED_CONTROL_JUST_PRESSED(0, kPhoneSelectControl);
    const bool reject = (back && !back_was_) || CONTROLS::IS_DISABLED_CONTROL_JUST_PRESSED(0, kPhoneCancelControl);
    enter_was_ = enter;
    back_was_ = back;

    if (answer) {
        StopRingSound();
        state_ = State::InCall;
        Log("arama cevaplandi");
        return Result::Answered;
    }
    if (reject || now - t_ > kRingTimeoutMs) {
        StopRingSound();
        state_ = State::Idle;
        Log(reject ? "arama reddedildi" : "arama kacirildi");
        return reject ? Result::Declined : Result::Missed;
    }
    return Result::None;
}

void IncomingCall::Hangup() {
    if (state_ == State::Idle) return;
    StopRingSound();
    state_ = State::Idle;
    Log("arama bitti");
}

}  // namespace hikaye
