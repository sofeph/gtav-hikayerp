#include "incomingcall.h"

#include <windows.h>

namespace hikaye {

namespace {
constexpr unsigned long long kRingTimeoutMs = 25000, kRingRepeatMs = 3500;
constexpr int kPhoneSelectControl = 176, kPhoneCancelControl = 177;  // INPUT_PHONE_SELECT / CANCEL

// Telefon yazı tipi Türkçe harfleri çizemeyebilir.
std::string Ascii(const std::string& s) {
    static const char* const map[][2] = {{"ç", "c"}, {"Ç", "C"}, {"ğ", "g"}, {"Ğ", "G"}, {"ı", "i"}, {"İ", "I"},
                                         {"ö", "o"}, {"Ö", "O"}, {"ş", "s"}, {"Ş", "S"}, {"ü", "u"}, {"Ü", "U"}};
    std::string out;
    for (size_t i = 0; i < s.size();) {
        bool hit = false;
        for (const auto& m : map) {
            const size_t n = std::char_traits<char>::length(m[0]);
            if (s.compare(i, n, m[0]) == 0) {
                out += m[1];
                i += n;
                hit = true;
                break;
            }
        }
        if (!hit) out += s[i++];
    }
    return out;
}

void PushText(const char* label, const char* text) {
    GRAPHICS::_BEGIN_TEXT_COMPONENT(const_cast<char*>(label));
    if (text) UI::_ADD_TEXT_COMPONENT_STRING(const_cast<char*>(text));
    GRAPHICS::_END_TEXT_COMPONENT();
}
}  // namespace

int IncomingCall::Scaleform() {
    // Her karakterin telefonu ayrı bir scaleform: Michael iFruit, Franklin Badger, Trevor Facade. Mod karakteri
    // freemode ped olduğu için varsayılan iFruit.
    const Hash model = ENTITY::GET_ENTITY_MODEL(PLAYER::PLAYER_PED_ID());
    const char* name = "cellphone_ifruit";
    if (model == HashKey("player_one")) name = "cellphone_badger";
    else if (model == HashKey("player_two")) name = "cellphone_facade";
    const int handle = GRAPHICS::REQUEST_SCALEFORM_MOVIE(const_cast<char*>(name));
    return GRAPHICS::HAS_SCALEFORM_MOVIE_LOADED(handle) ? handle : 0;
}

void IncomingCall::ShowCallScreen(int handle, const char* status) {
    // iFruitAddon2'nin DisplayCallUI tekniği (https://github.com/Bob74/iFruitAddon2, MIT): arama görünümü = SET_DATA_SLOT(4,...)
    const std::string name = Ascii(name_);
    GRAPHICS::_PUSH_SCALEFORM_MOVIE_FUNCTION(handle, const_cast<char*>("SET_DATA_SLOT"));
    GRAPHICS::_PUSH_SCALEFORM_MOVIE_FUNCTION_PARAMETER_INT(4);
    GRAPHICS::_PUSH_SCALEFORM_MOVIE_FUNCTION_PARAMETER_INT(0);
    GRAPHICS::_PUSH_SCALEFORM_MOVIE_FUNCTION_PARAMETER_INT(3);
    PushText("STRING", name.c_str());
    PushText("CELL_2000", "CHAR_DEFAULT");
    PushText("STRING", status);
    GRAPHICS::_POP_SCALEFORM_MOVIE_FUNCTION_VOID();
    GRAPHICS::_PUSH_SCALEFORM_MOVIE_FUNCTION(handle, const_cast<char*>("DISPLAY_VIEW"));
    GRAPHICS::_PUSH_SCALEFORM_MOVIE_FUNCTION_PARAMETER_INT(4);
    GRAPHICS::_POP_SCALEFORM_MOVIE_FUNCTION_VOID();
}

void IncomingCall::StartRingSound() {
    StopRingSound();
    sound_id_ = AUDIO::GET_SOUND_ID();
    AUDIO::PLAY_SOUND_FRONTEND(sound_id_, const_cast<char*>("Remote_Ring"), const_cast<char*>("Phone_SoundSet_Default"), true);
}

void IncomingCall::StopRingSound() {
    if (sound_id_ < 0) return;
    AUDIO::STOP_SOUND(sound_id_);
    AUDIO::RELEASE_SOUND_ID(sound_id_);
    sound_id_ = -1;
}

void IncomingCall::PutAway() { AI::TASK_USE_MOBILE_PHONE(PLAYER::PLAYER_PED_ID(), 0); }

void IncomingCall::Ring(const std::string& caller_name) {
    if (state_ != State::Idle) return;
    name_ = caller_name;
    state_ = State::Ringing;
    t_ = GetTickCount64();
    last_ring_ = 0;
    enter_was_ = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;  // basılı tutulan tuş kendiliğinden cevaplamasın
    back_was_ = (GetAsyncKeyState(VK_BACK) & 0x8000) != 0;
    AI::TASK_USE_MOBILE_PHONE(PLAYER::PLAYER_PED_ID(), 1);  // telefon ele gelir, ekran açılır
    Log("gelen arama: " + caller_name);
}

IncomingCall::Result IncomingCall::Update() {
    if (state_ == State::Idle) return Result::None;
    const unsigned long long now = GetTickCount64();
    const int handle = Scaleform();  // 0: telefon ekranı yok -> sadece zil ve ekran yazısı

    if (state_ == State::InCall) {
        if (handle) ShowCallScreen(handle, "BAGLANDI");  // her karede yazılmalı
        return Result::None;
    }

    if (now - last_ring_ > kRingRepeatMs) {
        StartRingSound();
        last_ring_ = now;
    }
    if (handle) ShowCallScreen(handle, "GELEN ARAMA");

    const bool enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0, back = (GetAsyncKeyState(VK_BACK) & 0x8000) != 0;
    const bool answer = (enter && !enter_was_) || CONTROLS::IS_CONTROL_JUST_PRESSED(0, kPhoneSelectControl);
    const bool reject = (back && !back_was_) || CONTROLS::IS_CONTROL_JUST_PRESSED(0, kPhoneCancelControl);
    enter_was_ = enter;
    back_was_ = back;

    if (answer) {
        StopRingSound();
        state_ = State::InCall;
        if (handle) ShowCallScreen(handle, "BAGLANDI");
        Log("arama cevaplandi");
        return Result::Answered;
    }
    if (reject || now - t_ > kRingTimeoutMs) {
        StopRingSound();
        PutAway();
        state_ = State::Idle;
        Log(reject ? "arama reddedildi" : "arama kacirildi");
        return reject ? Result::Declined : Result::Missed;
    }
    return Result::None;
}

void IncomingCall::Hangup() {
    if (state_ == State::Idle) return;
    StopRingSound();
    PutAway();
    state_ = State::Idle;
    Log("arama bitti");
}

void IncomingCall::Draw() const {
    if (state_ != State::Ringing) return;
    DrawLine("Gelen arama: " + name_ + "     Enter: cevapla    Backspace: reddet", 0.20f, 130, 235, 150);
}

}  // namespace hikaye
