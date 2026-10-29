#pragma once

namespace hikaye::xpad {

// Oyunun ana menüsü / kayıt listesi için giriş enjeksiyonu (iki yol birlikte):
//  1) XInput: GTA5.exe XINPUT1_4.dll'den XInputGetState'i içe aktarır; içe aktarma tablosundaki (IAT) işaretçi sarmalayıcıya çevrilir ve 0 numaralı
//     kumandada Geri (Back) düğmesi basılı görünür (kumanda yoksa "bağlı" gösterilir).
//  2) DirectInput: DirectInput8Create -> CreateDevice -> klavye GetDeviceState/GetDeviceData sarmalanır; klavye durumunda Backspace (varsayılan) basılı görünür.
// Zaman: oyun görünür olduktan N sn sonra, 2 sn arayla en fazla 10 kez, her basış 150 ms. Başlatıcı HIKAYE_AUTOBACK=1 (HIKAYE_AUTOBACK_DELAY=sn,
// HIKAYE_AUTOBACK_BTN, HIKAYE_AUTOBACK_KEY) ile açarsa devreye girer. Hikâye yüklenince (mod betiği başlayınca) durur. Her adım mod.log'a "xpad:" ile yazılır.
void Install();  // DLL yüklenirken: IAT yaması (HIKAYE_AUTOBACK=1 ise)
void Arm();      // oyun ilk kez çizim yaptığında (pencere göründü): zaman sayacı başlar
void Disarm();   // hikâye yüklendi: enjeksiyon durur
// (artık boş — sinyal dosyası ve Python automenu kaldırıldı; save menüsü geçişi SaveSkipThread tarafından ASI içinde yapılıyor)
void TickSignal();  // her çizim karesinde çağrılır (şu an no-op)

}  // namespace hikaye::xpad
