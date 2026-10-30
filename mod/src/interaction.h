#pragma once
#include "shop_manager.h"

namespace hikaye {

class Interaction {
public:
    // Her N frame'de bir çağrılır (CLAUDE.md §11: yakınlık her frame kontrol edilmez).
    // Etkileşilebilir dükkanı döndürür (yoksa nullptr) ve ekrana "E: Konuş" ipucunu basar.
    Shop* Update(ShopManager& shops, const Vector3& player_pos);
    // Sokaktaki yaya NPC'lerden oyuncuya yakın olanı bulur ve "E: Konuş" ipucunu basar (yoksa 0).
    // Polis/asker/hayvan hariç; dükkan NPC'leri hariç. Tarama birkaç frame'de bir yapılır.
    Ped UpdateStreet(ShopManager& shops, const Vector3& player_pos);
    // E tuşuna yeni basıldıysa true (kenar algılama).
        bool InteractPressed() const;

private:
    mutable bool was_down_ = false;
    Ped street_candidate_ = 0;
    int scan_tick_ = 0;
};

}  // namespace hikaye
