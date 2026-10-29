#pragma once
#include <functional>

#include "common.h"

namespace hikaye::barber {

// Berber sistemi (oyunun kendi berberi mod aktifken kapalıdır). Sakal kendiliğinden uzar: her 10 dakikada (oynanan gerçek süre) bir kademe,
// en fazla 7 kademe (0 temiz, 1-3 kirli sakal, 4-5 kısa sakal, 6-7 bakımsız sakal). Berber SADECE KISALTIR: bir kademe kısalt ya da tamamen
// tıraş et; sakal uzatma/stil seçme yoktur. Altı berber noktası: yaya iken yaklaş, E. Ücretler modun cüzdanından (economy::Spend).
// Kademe karaktere özeldir (saves/beard_<slot>.json) ve freemode ped'in sakal katmanına (head overlay 1) uygulanır.
// Sakal rengi saç rengiyle aynı olur (karakter oluşturucudaki saç rengi/vurgusu): sağlayıcı (renk, vurgu) döner.
void SetHairColorProvider(std::function<void(int&, int&)> provider);
void LoadSave(int slot);
void Shutdown();
void Update(const Vector3& pos, bool e_pressed, bool blocked);  // her frame (mod aktifken)
bool MenuOpen();
bool OwnsBlip(Blip b);
void Reset();
int Stage();

}  // namespace hikaye::barber
