#pragma once
#include "common.h"

namespace hikaye::binco {

// Binco (kendi giyim sistemimiz): oyunun kendi kıyafet dükkanları mod aktifken kapalıdır. Beş Binco noktasında yaya iken E:
// üst, alt, ayakkabı, şapka ve gözlük. Sol/sağ ok model ve rengi gezdirir (canlı önizleme), Enter satın alır/giyer. Alınan parçalar
// karaktere özel gardıropta (saves/wardrobe_<slot>.json) saklanır; sahip olunan parça tekrar ücretsiz giyilir. Ücretler modun cüzdanından.
// Kaydedilen kıyafet her açılışta ve profil uygulanınca geri yüklenir. Satın almadan çıkılırsa önizleme geri alınır.
void LoadSave(int slot);
void Shutdown();
void Update(const Vector3& pos, bool e_pressed, bool blocked);
bool MenuOpen();
bool OwnsBlip(Blip b);
void Reset();
// Gardırop menüsü: Binco dışında da (örn. ev içinde) açılabilir. Sadece sahip olunan parçalar ücretsiz giyilir.
void OpenWardrobe(const char* title = "Gardirop");
void CloseWardrobe(bool revert);

}  // namespace hikaye::binco
