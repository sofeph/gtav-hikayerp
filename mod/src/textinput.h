#pragma once
#include <string>

namespace hikaye::textinput {

// Oyun içi yazı kutusu (T). Oyunun ekran klavyesi ş ı ğ gibi harfleri desteklemediği için tuş olayları
// ScriptHookV klavye kancasından alınır ve aktif klavye düzeninin (Türkçe Q vb.) karşılığı Unicode'a çevrilir.
// Çizimi overlay yapar. Kanca DllMain'de kaydedilir (SDK kuralı).

void Init();      // DLL attach: klavye kancasını kaydeder
void Shutdown();  // DLL detach

void Begin();     // yazmayı başlat (script thread)
void Cancel();    // yazmayı sessizce bırak (oturum kapandı vb.)
bool Active();
void Pump();      // script thread her frame: kancadan gelen tuşları metne uygular
std::string Current();  // UTF-8, şu ana kadar yazılan
// Enter'a basıldıysa true ve metni verir (bir kez). Esc: iptal.
bool ConsumeSubmitted(std::string& out);
bool ConsumeCancelled();

}  // namespace hikaye::textinput
