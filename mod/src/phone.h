#pragma once
#include <string>

#include <vector>

#include "missions.h"
#include "overlay.h"
#include "services.h"

namespace hikaye {

// Oyun içi telefon (P). Tuşlar: ↑↓ seç, Enter hızlı arama (hizmeti hemen çağırır), C veya "Santrali ara" sesli
// görüşme (B ses / T yazı), Backspace veya ESC kapatır. Hizmet kararları yine Services'te (kodda); bu sınıf sadece
// girişi okur ve overlay'e çizilecek veriyi hazırlar. Çizim render thread'inde overlay.cpp'dedir.
class Phone {
public:
    struct CallView {  // aktif görüşmenin telefonda gösterilen hali
        bool in_call = false;
        std::string caller, caller_line, player_line;
        std::vector<overlay::ChatLine> previous_history;
        bool listening = false, speaking = false, typing = false;
        bool thinking = false;  // NPC cevabı hazırlanıyor: tek "düşünüyor..." balonu
        bool assistant = false;  // Haviland (telefonun yapay zekâsı): SMS balonları yerine asistan arayüzü
    };

    bool open() const { return open_; }
    bool cursor() const { return state_.cursor; }
        bool in_call() const { return state_.in_call; }
    void UpdateCursorToggle();
    void Close() {
        open_ = false;
        state_.open = false;
        state_.cursor = false;
        state_.web_selected = -1;
    }
    void Open() {  // örn. oyunun telefonundaki "Süper Rehber" seçilince
        open_ = true;
        selected_ = 0;
        state_.open = true;
        state_.web_url = "phone/home";
        state_.web_selected = 0;
        state_.cursor = false;
    }
    std::string TakeAppLaunchUrl();
    // Her frame. Dönüş: true = oyuncu "santrali ara" dedi (sesli görüşme başlatılmalı).
    // call_contact: rehberden bir kişi aranırsa kişinin kimliği (aksi hâlde boş).
    bool Update(bool conversation_active, bool typing, bool escape_pressed, Services& services,
                const std::vector<Missions::Contact>& contacts, std::string& call_contact);
    // Telefon açıksa overlay'e yayınlar (kapalıysa kapalı görünüm).
    void Publish(const CallView& call, const Services& services, const std::vector<Missions::Contact>& contacts,
                 bool allow_input = true);
        const overlay::PhoneView& current_view() const { return state_; }

private:
    static constexpr int kServiceCount = 6;  // sonra rehber kişileri, en sonda "Santrali ara"

    bool Edge(int vk, bool& was_down) const;

    bool open_ = false;
    int selected_ = 0;
    overlay::PhoneView state_{};
    std::string pending_app_url_;
    std::string notice_;
    unsigned long long notice_until_ = 0;
    bool was_up_ = false, was_down_ = false, was_enter_ = false, was_back_ = false, was_c_ = false;
    bool was_cursor_toggle_down_ = false;
};

}  // namespace hikaye
