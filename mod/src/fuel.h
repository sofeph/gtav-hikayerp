#pragma once
#include "common.h"

namespace hikaye::fuel {

// Benzin sistemi (sadece mod aktifken). Her araç %0-100 yakıtla çalışır: sürüş hızıyla azalır, bitince motor kapanır.
// Benzin istasyonlarında (data/places/pois.json "gas_station") E ile depo doldurulur; tamirci yakıt da ekler.
void Load();                                                    // istasyon konumları + ayarlar
void Update(const Vector3& player_pos, bool e_pressed, bool& e_used);  // her frame; e_used: E bu sistem tarafından kullanıldı
float Get(Vehicle v);                                           // %; bilinmiyorsa -1
void Set(Vehicle v, float pct);
bool OwnsBlip(Blip b);  // istasyon ikonları story blip gizlemesinden muaf
void Reset();                                                   // moddan çıkarken durumu temizle

}  // namespace hikaye::fuel
