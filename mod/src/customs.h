#pragma once
#include <functional>

#include "common.h"

namespace hikaye::customs {

// Los Santos Customs (kendi ekonomimiz): oyunun kendi modifiye dükkanı (carmod_shop) mod aktifken kapalıdır. Yerine altı noktada
// modun kendi menüsü açılır: araçla noktaya gir, E. Sadece SAHİP OLDUĞUN araç modifiye edilir; ücretler modun cüzdanından düşer
// (economy::Spend). Renk, performans (motor/fren/şanzıman/süspansiyon/zırh/turbo), görünüm parçaları, cam filmi, plaka, neon, xenon.
// Yapılan değişiklikler garajın "look" kaydıyla araçla birlikte saklanır.
void Load();
// Her frame (mod aktifken). e_pressed: E'ye yeni basıldı. blocked: konuşma/telefon vb. is_owned: araç oyuncunun mu.
void Update(const Vector3& pos, bool e_pressed, bool blocked, const std::function<bool(Vehicle)>& is_owned);
bool MenuOpen();
bool OwnsBlip(Blip b);
void Reset();  // moddan çıkarken ikonları ve menüyü kapat

}  // namespace hikaye::customs
