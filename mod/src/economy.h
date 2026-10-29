#pragma once
#include <vector>

#include "common.h"
#include "json.hpp"

namespace hikaye::economy {

// Hikaye RP modunun cüzdanını bağlar (nullptr: mod kapalı, hiçbir para işlemi yapılmaz). Hikaye parasına dokunulmaz.
void SetWallet(int* cash);
// Modun karakterinin nakdi. Mod kapalıysa -1.
int GetCash();
// Yeterli nakit varsa düşer ve true döner (CLAUDE.md §14: para işlemleri tek yerden geçer).
bool Spend(int amount);
// Nakit ekler (iade vb.). Karakter tanınmazsa false.
bool Earn(int amount);
// Maze Bank hesap özeti: son işlemler (eksi: harcama). En yeni sonda.
struct Tx {
    int amount = 0;
    int hour = 0, minute = 0;
};
std::vector<Tx> History();
// Dükkan envanterindeki `effect` alanını uygular. `spawn_at` araç için doğma noktası.
// Dönüş: etki uygulandı mı.
// Satın alınan araç doğunca çağrılır (garaj sahipliği için): kanca(araç, model adı).
void SetVehicleBoughtHook(void (*hook)(Vehicle, const std::string&));
bool ApplyEffect(const nlohmann::json& effect, const Vector3& spawn_at, float spawn_heading);

}  // namespace hikaye::economy
