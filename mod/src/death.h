#pragma once
#include <functional>

namespace hikaye::death {

// Ölme mekaniği (Hikaye RP modu açıkken). Oyunun kendi yeniden doğuşu (hastane) çalışır; mod şunları üstlenir:
//  - ölürken "ÖLDÜN" ekranı, ayılınca hastane faturası ($400'e kadar), arama seviyesi silinir, 2 saat geçer;
//  - ZEMİNE GÖMÜLME düzeltmesi: yeniden doğduktan sonra (karakter modeli mod tarafından yeniden uygulanırken) çarpışma yüklenene
//    kadar oyuncu dondurulur, zeminin altındaysa zemine çıkarılır;
//  - RP silahları (profil envanteri) yeniden verilir; oyun öldükten sonra silahları siler.
// OTORİTE KODDA: fatura ve süre burada, LLM'e bağlı değildir.
void SetLoadoutRestorer(std::function<void()> f);
void Update(bool mod_active);  // her frame
void Reset();

}  // namespace hikaye::death
