#pragma once
#include <string>
#include <vector>

#include "common.h"
#include "json.hpp"
#include "missions.h"

namespace hikaye {
class Services;
class Places;
class ShopManager;
class JailSystem;
}  // namespace hikaye

namespace hikaye::phoneos {

// Telefon işletim sistemi: ana ekran + uygulamalar (Telefon, Rehber, İnternet, Haviland, Eyefind Maps, Maze Bank, App Store),
// pil/şarj ve numara sistemi. Ekranlar HML sayfası olarak (phone/<sayfa>) üretilir ve telefon tarayıcısında gösterilir;
// eylemler tarayıcıdaki "act:ph:<komut>" bağlantılarıyla gelir. Yukarı ok tuşu (↑) telefonu açar (main.cpp).
// OTORİTE KODDA: arama sonucu, numara doğrulama, şarj ve pil burada hesaplanır; LLM sadece konuşur.
struct Deps {
    Services* services = nullptr;
    Missions* missions = nullptr;
    Places* places = nullptr;
    ShopManager* shops = nullptr;
    JailSystem* jail = nullptr;
};
void Bind(const Deps& deps);
void Load();                 // data/phone.json (varsayılan evler)
void LoadSave(int slot);     // saves/phone_<slot>.json: pil, kişiler, kısayollar
void Shutdown();             // kaydet

// Her frame (mod aktifken). screen_on: telefon ekranı açık (daha hızlı tükenir). homes: sahip olunan evlerin kapıları.
void Update(const Vector3& pos, bool screen_on, const std::vector<Vector3>& homes);
bool Dead();                 // pil bitti: telefon açılmaz
int Battery();               // 0..100
bool Charging();
bool AtHome(const Vector3& pos, const std::vector<Vector3>& homes);  // karakter evi ya da sahip olunan ev yakınında (35 m)
std::string HavilandVoice();       // Haviland'ın seçili sesi (XTTS konuşmacı adı)
bool HavilandVoiceFemale();
std::string ChargeSource();  // "Araç çakmaklığı", "Market", "Otobüs/metro (USB)", "Ev" ya da ""
std::string BatteryText();   // "%87" / "%87 şarjda"

// Tarayıcı kancaları
std::string BuildPage(const std::string& url);  // "phone/home" ... bilinmiyorsa ""
std::string Action(const std::string& cmd);     // "dial:3105550100", "call:id", ... bildirim metni; "@goto:url|metin" sayfa değiştirir
// Aramalar main'e iletilir: hikaye karakteri kimliği | "mechanic_line" | "parking_line" | "haviland" | "santral" | "bar_lawyer" | "person:<id>"
bool ConsumeCall(std::string& id);
bool ConsumeAdminRequest();  // arama kutusuna hile kodu yazıldı (phpmyadmin)
bool PersonNpc(const std::string& id, nlohmann::json& npc_json);  // kayıtlı kişinin (rastgele NPC) kişilik verisi

// Numara sistemi: ABD (NANP) biçimi (NXX) NXX-XXXX; doğrulama + sabit rastgele üretim
bool IsValidNanp(const std::string& digits10);
std::string GenerateNumber(unsigned long long seed);  // seed aynıysa numara aynı: NPC'ye bir kez verilince sabit kalır
std::string NumberFor(const std::string& key);        // anahtar -> 10 haneli numara
std::string Format(const std::string& digits10);      // "3104821957" -> "(310) 482-1957"
std::string NormalizeDialed(const std::string& typed);  // sadece rakamlar; 7 hane -> 310 alan kodu; 1 + 10 hane -> 10 hane; "" geçersiz

// Şebeke & operatör
int SignalBars();          // 0-4 çubuk
std::string SignalText();  // çubuk simgeleri (örn. "▃▂▁░") — arayüz üst çubuğunda gösterilir
std::string OperatorName();  // "Whiz" | "Graham Telecom" | "Modatel"
bool HasSignal();          // SignalBars() > 0
bool InternetSlow();       // SignalBars() <= 2 (internet yükleme uyarısı)
int PkgPagesLeft();        // kalan veri MB; -1 = sınırsız; 0 = bitti
int PkgMinutesLeft();      // kalan arama dakikası; -1 = sınırsız; 0 = bitti
void ConsumeInternetPage(const std::string& url = "", int src_bytes = 0);  // internet sayfası yüklenince kota düşür (operatör siteleri intranet = ücretsiz); src_bytes: sayfa kaynağı baytı — yazı yoğunluğuna göre MB hesaplanır (2 KB kaynak ≈ 1 MB, en az 1 MB)
bool InternetAllowed(const std::string& url = "");      // erişim var mı? (sinyal + kota; operatör sitesi: her zaman evet)

// Operatör geçiş sistemi
bool ConsumeIncomingCall(Missions::Talk& out);  // web sitesinden geçiş talebi sonrası operatör geri arar
void ConfirmSwitch();                           // arama cevaplanınca/tamamlanınca geçişi resmileştir
void RetrySwitchCall();                         // arama kaçırıldıysa/meşgulse tekrar planla
bool HasPendingSwitch();

// Sokak NPC'si numarasını verir mi (kod karar verir). Kabul edilirse rehbere kaydedilir. Dönüş: LLM'e giden gerçek bilgi.
bool AddNote(const std::string& text);  // telefon notlarına ekler (aynısı varsa eklemez); eklendiyse true. NPC bir sırrı (ürün anahtarı vb.) söyleyince otomatik kullanılır
nlohmann::json OfferNumber(const nlohmann::json& npc_json, int karma_value);

}  // namespace hikaye::phoneos
