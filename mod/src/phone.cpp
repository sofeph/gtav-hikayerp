#include "phone.h"

#include <windows.h>

#include <cstdio>

#include "controls.h"
#include "economy.h"
#include "phoneos.h"

namespace hikaye {

namespace {
// Hizmetin neden verilemediğini oyuncuya Türkçe söyler (kararı veren Services::Dispatch).
std::string ReasonText(const std::string& reason) {
    if (reason == "already_on_the_way") return "Zaten yolda";
    if (reason == "cooldown") return "Biraz bekle, yakın zamanda çağırdın";
    if (reason == "wanted_level") return "Arandığın için hizmet verilemiyor";
    if (reason == "no_road_nearby") return "Yakında uygun yol yok";
    if (reason == "busy_in_ride") return "Şu an yolculuktasın";
    return "Şu an hizmet verilemiyor";
}

const ServiceKind kKinds[] = {ServiceKind::Taxi, ServiceKind::HeliTaxi, ServiceKind::Police, ServiceKind::Ambulance, ServiceKind::Fire, ServiceKind::Mechanic};
}  // namespace

bool Phone::Edge(int vk, bool& was_down) const {
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool pressed = down && !was_down;
    was_down = down;
    return pressed;
}

bool Phone::Update(bool conversation_active, bool typing, bool p_pressed, Services& services,
                   const std::vector<Missions::Contact>& contacts, std::string& call_contact) {
    call_contact.clear();
    if (typing) return false;  // ekran klavyesi açıkken yazılan harfler telefonu tetiklemesin
    bool want_call = false;
    // Satırlar: hizmetler (5) + rehberdeki kişiler + "Santrali ara".
    const int n_contacts = static_cast<int>(contacts.size());
    const int call_row = kServiceCount + n_contacts;
    const int total = call_row + 1;
    if (selected_ >= total) selected_ = 0;  // kişi sayısı değiştiyse

    // Telefon açıkken oyunun kendi telefonu (yukarı ok) ve ilgili tuşlar çalışmasın.
    if (open_) {
        for (int c : {27 /*INPUT_PHONE*/, 172, 173, 174, 175, 176, 177}) CONTROLS::DISABLE_CONTROL_ACTION(0, c, true);
    }

    const bool up = Edge(VK_UP, was_up_), down = Edge(VK_DOWN, was_down_), enter = Edge(VK_RETURN, was_enter_),
               back = Edge(VK_BACK, was_back_), c = Edge('C', was_c_);

    if (p_pressed && !conversation_active) {  // görüşme sırasında P görüşmeyi kapatır (Conversation), telefonu değil
        open_ = !open_;
        if (open_) selected_ = 0;
        return false;
    }
    if (!open_ || conversation_active) return false;

    if (back) {
        open_ = false;
        return false;
    }
    if (up) selected_ = (selected_ + total - 1) % total;
    if (down) selected_ = (selected_ + 1) % total;
    if (c || (enter && selected_ == call_row)) {
        want_call = true;  // sesli görüşme: santral görevlisiyle konuş
    } else if (enter && selected_ >= kServiceCount && selected_ < call_row) {
        call_contact = contacts[static_cast<size_t>(selected_ - kServiceCount)].id;  // rehberdeki kişiyi ara
    } else if (enter && selected_ < kServiceCount) {
        const ServiceKind kind = kKinds[selected_];
        const nlohmann::json facts = services.Dispatch(kind);  // hızlı arama: karar yine kodda
        if (facts.value("accepted", false)) {
            const int eta = facts.value("eta_seconds", 0);
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s yola çıktı (~%d dk)", Services::Label(kind), (eta + 59) / 60);
            notice_ = buf;
        } else {
            notice_ = std::string(Services::Label(kind)) + ": " + ReasonText(facts.value("reason", ""));
        }
        notice_until_ = GetTickCount64() + 5000;
    }
    return want_call;
}

void Phone::Publish(const CallView& call, const Services& services, const std::vector<Missions::Contact>& contacts) {
    (void)services;
    (void)contacts;
    static std::vector<overlay::ChatLine> hist;
    static std::string last_caller, last_player;
    static bool was_thinking = false;
    overlay::PhoneView v;
    v.open = open_;
    if (!call.in_call) {
        hist.clear();
        last_caller.clear();
        last_player.clear();
        was_thinking = false;
        overlay::SetPhone(v);
        return;
    }
    // Arama SMS gibi gösterilir: her yeni satır bir balon
    if (call.thinking) was_thinking = true;
    if (!call.caller_line.empty() && call.caller_line != last_caller) {
        if (was_thinking) {  // düşünme bitti: "düşündü", ardından cevap
            hist.push_back({false, "düşündü"});
            was_thinking = false;
        }
        hist.push_back({false, call.caller_line});
        last_caller = call.caller_line;
    }
    if (!call.player_line.empty() && call.player_line != last_player) {
        hist.push_back({true, call.player_line});
        last_player = call.player_line;
    }
    if (hist.size() > 12) hist.erase(hist.begin());
    char clock[8];
    std::snprintf(clock, sizeof(clock), "%02d:%02d", TIME::GET_CLOCK_HOURS(), TIME::GET_CLOCK_MINUTES());
    v.clock = clock;
    v.signal = phoneos::SignalText();
    v.open = true;
    v.in_call = true;
    v.caller = call.caller;
    v.listening = call.listening;
    v.speaking = call.speaking;
    v.typing = call.typing;
    v.thinking = call.thinking;
    v.assistant = call.assistant;
    v.chat = call.previous_history;
    if (!v.chat.empty() && !hist.empty()) v.chat.push_back({false, "YENI GORUSME", true});
    v.chat.insert(v.chat.end(), hist.begin(), hist.end());
    if (GetTickCount64() < notice_until_) v.notice = notice_;
    const std::string talk = controls::KeyName(controls::TalkKey()), phone = controls::KeyName(controls::PhoneKey());
    v.hint = talk + ": konuş   T: yaz   " + phone + ": görüşmeyi bitir";
    overlay::SetPhone(v);
}

}  // namespace hikaye
