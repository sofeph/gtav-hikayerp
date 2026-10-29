#pragma once
#include "common.h"

namespace hikaye::passenger {

// Yolcu NPC mekaniği (hangout daveti ve görevlerde NPC taşıma için ortak).
void Board(Ped ped, Vehicle veh);    // NPC serbest kalır, yürüyüp ön yolcu koltuğuna biner
void Release(Ped ped, Vehicle veh);  // iner, yürüyerek uzaklaşır, bir süre sonra silinir
void Tick();                         // her frame (mod aktifken): inen yolcuları yönetir
void Clear();                        // moddan çıkarken hepsini sil

}  // namespace hikaye::passenger
