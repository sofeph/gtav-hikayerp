#pragma once
#include <string>

#include <vector>

#include "missions.h"
#include "overlay.h"
#include "services.h"

namespace hikaye {

// Oyun içi telefon (P). Tuşlar: ↑↓ seç, Enter hızlı arama (hizmeti hemen çağırır), C veya "Santrali ara" sesli
// görüşme (B ses / T yazı), Backspace veya P kapatır. Hizmet kararları yine Services'te (kodda); bu sınıf sadece
// girişi okur ve overlay'e çizilecek veriyi hazırlar. Çizim render thread'inde overlay.cpp'dedir.
class Phone {
public:
    struct CallView {  // aktif görüşmenin telefonda gösterilen hali
        bool in_call = false;
        std::string caller, caller_line, player_line;
        bool listening = false, speaking = false, typing = false;
        bool thinking = false;  // NPC cevabı hazırlanıyor: tek "düşünüyor..." balonu
        bool assistant = false;  // Haviland (telefonun yapay zekâsı): SMS balonları yerine asistan arayüzü
    };

    bool open() const { return open_; }
    void Close() { open_ = false; }
    void Open() {  // örn. oyunun telefonundaki "Süper Rehber" seçilince
        open_ = true;
        selected_ = 0;
    }
    // Her frame. Dönüş: true = oyuncu "santrali ara" dedi (sesli görüşme başlatılmalı).
    // call_contact: rehberden bir kişi aranırsa kişinin kimliği (aksi hâlde boş).
    bool Update(bool conversation_active, bool typing, bool p_pressed, Services& services,
                const std::vector<Missions::Contact>& contacts, std::string& call_contact);
    // Telefon açıksa overlay'e yayınlar (kapalıysa kapalı görünüm).
    void Publish(const CallView& call, const Services& services, const std::vector<Missions::Contact>& contacts);

private:
    static constexpr int kServiceCount = 6;  // sonra rehber kişileri, en sonda "Santrali ara"

    bool Edge(int vk, bool& was_down) const;

    bool open_ = false;
    int selected_ = 0;
    std::string notice_;
    unsigned long long notice_until_ = 0;
    bool was_up_ = false, was_down_ = false, was_enter_ = false, was_back_ = false, was_c_ = false;
};

}  // namespace hikaye
