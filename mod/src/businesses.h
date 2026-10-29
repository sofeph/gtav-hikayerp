#pragma once
#include <string>
#include <vector>

#include "common.h"

namespace hikaye::businesses {

// GitHub'dan çekilen işletme konumları (data/places/businesses.json; tools/fetch_businesses.py) haritada ikon olarak gösterilir
// (data/business_types.json: tür -> ikon). Modifiye atölyesine (customs) araçla girip E'ye basınca araç onarılır ve yıkanır.
void Load();
void Update(const Vector3& player_pos, bool e_pressed);  // her frame (mod aktifken): ikonlar + modifiye servisi
struct Spot {
    std::string name;
    Vector3 pos{};
};
std::vector<Spot> Spots(const std::string& type);  // bu türdeki tüm konumlar (customs, barber, clothing ...)
bool OwnsBlip(Blip b);                                   // story blip gizlemesinden muaf
// Oyuncu bu türden bir işletmeye (ör. "garage") radius içinde mi?
bool NearType(const std::string& type, const Vector3& pos, float radius);
void Reset();                                            // moddan çıkarken ikonları kaldır

}  // namespace hikaye::businesses
