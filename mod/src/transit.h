#pragma once
#include <string>
#include <vector>

#include "common.h"
#include "json.hpp"

namespace hikaye::transit {

// Toplu taşıma (sadece mod aktifken). data/transit.json (tools/gen_transit.py): hatlar ve duraklar.
//  OTOBÜS: hat üzerinde gerçek bir otobüs doğar, duraklar arasında sürücüsüyle gider, her durakta bekler. Durakta beklerken E ile binersin
//          (ücret düşer), durakta E ile inersin. Durakta bir sonraki otobüsün tahmini varış süresi yazılır.
//  LST METRO (The Arrow): oyunun gerçek metrosu da kendi kendine çalışır; burada ayrıca istasyon ikonları ve tarifeye bağlı hızlı geçiş vardır. E ile binersin; ekran kararır, bir sonraki istasyona varırsın.
//          Durakta E ile yolculuğa devam edersin ya da uzaklaşarak inersin.
// Telefon navigasyonu: 'from'dan 'dest'e toplu taşıma rotası. En kısa tahmini süreli (yürü + bekle + yolculuk + yürü) hat/durak çifti
// seçilir; yürümek daha kısaysa bunu söyler. Biniş durağını haritaya işaretler. Dönüş LLM'e gerçek bilgi olarak gider.
nlohmann::json PlanRoute(const Vector3& from, const Vector3& dest, const std::string& dest_name);
void Load();
// Durakta yaya iken geçen hatlar listelenir; altında 'Telefonla QR okut [J]' yazar. J'ye basınca ConsumeQr durağın sitesini verir:
//   şehir otobüsü lst.com/citybus/<durak>, metro lst.com/metro/<durak>, bölgesel dashound.com/otogar/<durak>.
// input_blocked: yazı yazarken/menüdeyken tuşlar okunmaz.
void Update(const Vector3& player_pos, bool e_pressed, bool input_blocked = false);
bool ConsumeQr(std::string& url);
// LST / Dashound sayfaları (durak ve hat listeleri) oyun verisinden üretilir. url: 'lst.com/citybus/pillbox-hill'. Bilinmiyorsa "".
std::string BuildPage(const std::string& url);
bool OnTransit();  // otobüste ya da metroda mı (telefon şarjı)
struct MapLine {
    std::string id, type;
    std::vector<Vector3> pts;
};
std::vector<MapLine> MapLines();  // telefon haritası: hat çizgileri
bool OwnsBlip(Blip b);  // durak ikonları story blip gizlemesinden muaf
void Reset();           // moddan çıkarken otobüsleri ve ikonları kaldır

}  // namespace hikaye::transit
