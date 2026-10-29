#pragma once
#include <functional>
#include <string>
#include <vector>

#include "common.h"

namespace hikaye::admin {

// Hile/yönetici menüsü. Telefon > Telefon > "Numara çevir" kutusuna phpmyadmin yazılıp Enter'a basılınca açılır (harf dizisi dinlenmez: yürüyüş tuşlarıyla çakışmasın).
//  Işınlan: modda ne varsa (dükkânlar, işletmeler, duraklar, evler, görev hedefi), Ölümsüzlük, Yıldız düşür, Noclip (WASD + fare,
//  Shift hızlı, Space yukarı, Ctrl aşağı). "Güvenli çıkış": tüm etkileri kapatır (noclip, ölümsüzlük) ve menüyü kapatır.
// Moddan çıkarken (Reset) tüm etkiler kendiliğinden kapanır.
struct Dest {
    std::string cat, name;
    Vector3 pos{};
};
void Update(bool blocked, const std::function<std::vector<Dest>()>& build_dests);  // her frame (mod aktifken)
void Open();     // ana menüyü aç (telefondan çağrılır)
bool MenuOpen();
bool Active();   // herhangi bir etki açık mı
void Reset();    // tüm etkileri kapat

}  // namespace hikaye::admin
