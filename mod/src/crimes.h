#pragma once
#include <functional>
#include <string>
#include <vector>

#include "common.h"
#include "hml.h"

namespace hikaye::crimes {

// Suç kaydı -> haber. Oyuncunun işlediği (veya yakınında olan) suçlar Weazel News "Suç haberleri"nde ve Bleeter/LLM konularında
// görünür. Hem moda ait suçlar (yayaya saldırı, cinayet) hem oyunun kendi suçları (araç gasbı, silah atışı, patlama, polis
// öldürme, yüksek arama seviyesi) algılanır. Kayıt karaktere özeldir (saves/crimes_<slot>.json, son 30 olay).
// OTORİTE KODDA: haberin ne olduğunu, nerede/ne zaman geçtiğini ve tanık sayısını kod belirler; LLM sadece bu olayları yorumlar.
void Load(int slot);
// Araç hırsızlığı tespiti için: bu araç oyuncunun kendi aracı mı? (garaj/vale/görev araçları suç sayılmaz)
// Suçu gören bir tanık polisi arar (tacizdeki gibi: telefona sarılır, sonra arama seviyesi verilir). caller: (tanık, yıldız, suç) -> başladı mı.
// Tanık yoksa polis çağrılmaz (sadece haber olur). Oyunun kendi suç-yıldız sistemi kapatılır; tüm suçları bu script yönetir.
void SetPoliceCaller(std::function<bool(Ped, int, const std::string&)> caller);
// Tanık olamayacak NPC'ler (dükkân sahipleri/satıcılar): polisi onlar aramaz, başka bir yoldan geçen vatandaş tanık olur.
void SetWitnessExclusion(std::function<bool(Ped)> excluded);
void SetOwnedPredicate(std::function<bool(Vehicle)> is_owned);
// Kendi aracının yakınında araç hırsızlığı tanıklarının olayı fark etmemesi için.
void SetCarTheftWitnessBlindSpot(std::function<bool(const Vector3&)> near_owned_vehicle);
// Çağrılan hizmetler (tamirci, taksi): aracı alma/vurma suç sayılmaz. exempt(ped, araç): hizmetin mürettebatı ya da aracı mı; near(konum, yarıçap): yakında var mı.
void SetServiceExemption(std::function<bool(Ped, Vehicle)> exempt, std::function<bool(const Vector3&, float)> near_service);
bool IsExempt(Ped p, Vehicle v);
void Save();
void Shutdown();
void Update();  // her frame (mod aktifken): oyunun kendi suçlarını algılar
// Bir suç kaydet. kind: murder | assault | cop_killed | carjack | car_theft | shooting | explosion | wanted. pos: olay yeri.
void Record(const std::string& kind, const Vector3& pos, int witnesses = -1, bool player_offense = true);
std::string Title(const std::string& kind);
// Aracı oyuncuya ait say: bu kayıt, tekrar binme suç kaydından bağımsızdır ve araç kimliği yaşadığı sürece korunur.
void MarkKnown(Vehicle v);
std::vector<hml::Item> NewsItems(int max_items = 6);  // Weazel News kartları (en yeni üstte)
std::vector<std::string> Topics(int max_items = 5);   // LLM içeriklerine konu olarak verilir

}  // namespace hikaye::crimes
