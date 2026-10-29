#pragma once
#include <cstdint>
#include <vector>

namespace hikaye::videocover {

// Yükleme kapağı videosu (Media Foundation + waveOut; DLL içinde, dış oynatıcı yok). Ayrı bir iş parçacığında çalışır:
// önce `intro` bir kez, bitince `loop` Stop() çağrılana kadar tekrar tekrar oynar. Ses (varsa) waveOut ile çalınır.
// Dosyalar uzantısız içerik türüyle (video/mp4) açılır, bu yüzden `.vid` adı verilebilir (NSIS kurulum arşivi .mp4'ü dışlıyor).
bool Start(const wchar_t* intro, const wchar_t* loop);
void Stop();
bool Running();    // iş parçacığı canlı mı
bool HasFrame();   // en az bir kare çözüldü mü (açılamazsa false kalır: resim kapağına dönülür)
// Yeni kare varsa true: out = B8G8R8X8 piksel (w*h*4)
bool TakeFrame(std::vector<uint8_t>& out, int& w, int& h);

}  // namespace hikaye::videocover
