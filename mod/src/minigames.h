#pragma once
#include <string>
#include <vector>

#include "overlay.h"

namespace hikaye::minigames {

// Bilgisayardaki oyunlar. Çizim, telefon/bilgisayar ekranındaki vektör çizim alanına (WebShape) yapılır.
//  Yılan: ok tuşları / WASD. Tepeden Suç: GTA 1-2 tarzı tepeden bakışlı şehir (W/S gaz-fren, A/D direksiyon):
//  trafik, yayalar, arama seviyesi, polis kovalamacası ve paket teslim görevi. Kod bağımsızdır, gerçek oyunu etkilemez.
// Çıkış: Backspace. Yeniden başlat: Enter (oyun bitince).
enum class Id { None = 0, Snake = 1, TopDown = 2 };

struct Frame {
    int w = 400, h = 400;
    int bg = 0x10141a;
    std::vector<overlay::WebShape> shapes;
    std::string title;
};

void Start(Id id);
bool Active();
bool Update();           // her frame; true: oyun kapatıldı (Backspace)
Frame Draw();
Id Current();
int Best(Id id);         // en yüksek skor (kayıtlı)
void SetBest(Id id, int score);

}  // namespace hikaye::minigames
