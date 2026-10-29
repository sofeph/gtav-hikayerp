#pragma once
#include <functional>
#include <string>

#include "common.h"

namespace hikaye {
class JailSystem;
}

namespace hikaye::weaponwatch {

// Silah ihbarı, rastgele çevirme ve polis araması.
//  1) Silah ihbarı: oyuncu halka açık yerde elinde ateşli silahla görülürse (çevrede polis olmayan, görüş hattı olan biri
//     2,5 sn boyunca) vatandaş polise bildirir; devriye gelir.
//  2) Rastgele çevirme: ara sıra (7-18 dk) devriye seni durdurur (silah ihbarı olmadan, rutin kontrol).
//  Polis yanına gelince seninle DİYALOĞA girer (LLM: 'police' bağlamı; sesle/yazıyla konuşursun):
//   - iş birliği ('comply') -> üstün aranır: temiz çıkarsa SERBEST; yasadışı silah (RP profilinde olmayan, gasp edilmiş/yerden alınmış)
//     bulunursa müsadere + YAKALAMA KARARI (tutuklama akışı JailSystem'de);
//   - kaçmak ('flee'), aramadan uzaklaşmak, araçla kaçmak, ateş etmek, ikinci hakaret -> yakalama kararı;
//   - yalvarmak ('plead') hiçbir şeyi değiştirmez; hakaret onur düşürür.
// OTORİTE KODDA: ihbarı, çevirmeyi, aramayı ve sonucu kod belirler; LLM sadece polisin cümlelerini söyler ve niyeti önerir.
void SetJail(JailSystem* jail);
// Güvenli bölge: dükkân çevresi ve iç mekanlarda silah ihbarı ve rastgele çevirme olmaz (dükkâna girince polis gelmesin).
void SetSafeZone(std::function<bool(const Vector3&)> safe);
// Diyalog başlatıcı: (polis, sebep: "weapon_report" | "random_check") -> true: diyalog açıldı. Verilmezse sessiz arama yapılır.
void SetDialogueStarter(std::function<bool(Ped, const std::string&)> starter);
void OnPoliceIntent(const std::string& intent);  // 'police' bağlamında LLM'in önerdiği niyet (comply/flee/insult/plead/chat)
bool InDialogue();                                // polis konuşması bekleniyor mu
void SnapshotLegal();  // şu an elde olan ateşli silahlar yasal sayılır (RP envanteri verildikten sonra çağrılır)
void Update();         // her frame (mod aktifken)
void Reset();          // moddan çıkarken / karakter değişirken

}  // namespace hikaye::weaponwatch
