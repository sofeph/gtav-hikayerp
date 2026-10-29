#pragma once
#include <string>
#include <vector>

namespace hikaye::addons {

// Eklenti (addon) sistemi: oyun dosyalarına dokunmadan yeni içerik. HikayeTurkRP/addons/<kimlik>/ klasörü, data/ ile aynı düzende:
//   addon.json      {"id","name","version","enabled":true}   (enabled=false ise yok sayılır)
//   shops/*.json    ek dükkanlar (spawn_vehicle ile yeni araç modelleri satılabilir)
//   items.json      ek envanter eşyaları ({"items":{...}} biçimi, data/items.json ile aynı)
//   vehicles.json   GeneralSale ikinci el pazarına araç modelleri {"vehicles":[{"model","name","base"}]}
//   missions/*.json ek görevler
//   web/<site>/*.hml ek telefon siteleri (Eyefind'da aranabilir, adresle açılır)
// Araç modelleri (addon/DLC araba) oyunda yüklü olmalıdır; model yoksa satış güvenli biçimde "teslimat başarısız" olur.
// Ayrıntı: HikayeTurkRP/addons/README.md
void Scan();                                   // addons/*/addon.json (enabled) -> Dirs()
const std::vector<std::string>& Dirs();        // etkin eklenti klasörleri ("HikayeTurkRP/addons/<kimlik>")
// Eklentilerin <alt>/ klasöründeki dosyaları (uzantı: ".json" gibi), sıralı
std::vector<std::string> Files(const std::string& sub, const std::string& ext);

}  // namespace hikaye::addons
