#pragma once
#include <string>
#include <utility>
#include <vector>

namespace hikaye::overlay {

// Oyun içi arayüz katmanı (Dear ImGui + D3D11). ScriptHookV'nin IDXGISwapChain::Present kancasına bağlanır ve
// her karede altyazıları ile telefonu çizer. Çizim RENDER thread'inde yapılır; oyun mantığı (script thread) sadece
// "ne çizilecek" verisini yayınlar (mutex'li çift tampon). Native çağrıları script thread'inde kalır.
struct PhoneRow {
    std::string label;
    std::string status;
    int tone = 0;  // 0 normal, 1 bekleniyor (sarı), 2 olumlu (yeşil), 3 hata (kırmızı)
};

struct WebShape {
    int type = 0;  // 0 rect, 1 circle, 2 line, 3 polyline, 4 polygon, 5 text
    float x = 0, y = 0, w = 0, h = 0, r = 0, sw = 1, size = 14;
    int fill = -1, stroke = -1, align = 0;
    std::vector<std::pair<float, float>> pts;
    std::string text;
};

struct WebItem {
    int kind = 0;  // hml::Kind sırası: 0 metin, 1 h1, 2 h2, 3 çizgi, 4 bağlantı, 5 kart, 6 gönderi, 7 madde
    std::string text, sub, meta;
    bool selectable = false;
    int color = -1, bg = -1, border = -1;  // 0xRRGGBB, -1: varsayılan
    float size = 0.f, pad = -1.f, radius = -1.f;
    int align = 0;  // 0 sol, 1 orta, 2 sağ
    float gw = 0, gh = 0;  // kind 8 (çizim): çizim alanı
    int gbg = -1;
    std::vector<WebShape> shapes;
    bool spin = false;
};

struct ChatLine {  // arama SMS gibi bir mesaj dizisi olarak gösterilir
    bool mine = false;
    std::string text;
    bool separator = false;
};

struct PhoneView {
    bool open = false;
    int selected = 0;
    std::string clock;  // "14:05"
    std::string cash;   // "Nakit: $5.000" (boşsa gösterilmez)
    std::string battery;  // "%87" ya da "%87 şarjda" (boşsa gösterilmez)
    bool battery_low = false;  // %15 ve altı: kırmızı
    std::string signal;   // sinyal çubukları (örn. "▃▂▁░"); boşsa gösterilmez
    std::vector<PhoneRow> rows;
    bool in_call = false;
    std::string caller, caller_line, player_line;
    bool listening = false, speaking = false, typing = false;
    std::string hint;    // alt satır: tuş ipuçları
    std::string notice;  // geçici bilgi (örn. "Taksi çağrıldı")
    // Tarayıcı modu: açıksa telefon ekranında rows yerine sayfa çizilir.
    bool web = false;
    std::string web_url, web_title;
    std::vector<WebItem> web_items;
    int web_selected = -1;  // web_items içindeki seçili öğe (-1: yok)
    int web_bg = -1, web_color = -1, web_accent = -1;  // sayfa teması (0xRRGGBB)
    bool cursor = false;       // fare imleci etkin: üstündeki öğe HoverItem(), tıklama TakeWebClick() ile script thread'ine döner
    float cursor_x = 0, cursor_y = 0;  // 0..1 ekran konumu
    int click_seq = 0;         // her sol tıklamada artar
    std::vector<ChatLine> chat;  // arama sırasında mesaj geçmişi
    bool thinking = false;       // cevap hazırlanıyor: sohbette tek "düşünüyor..." balonu
    bool assistant = false;      // Haviland: asistan arayüzü (yuvarlak ışık + büyük yazı), SMS balonları yok
    bool pc = false;             // bilgisayar: 16:9 ekranda Facade masaüstü + pencere (özel tıklamalar: -3 pencereyi kapat/geri, -4 masaüstü)
    bool cef_browser = false;    // CEF OSR karelerini bilgisayar tarayıcısının içerik alanında göster
};

struct BrowserRect {
    float x = 0, y = 0, width = 0, height = 0;  // normalize edilmiş ekran koordinatları
};

// Soldaki menü paneli (mod menüsü ve karakter oluşturucu): başlık, sekmeler ve satırlar.
struct MenuRow {
    std::string label;
    std::string value;   // boşsa sadece etiket (eylem satırı)
    bool header = false; // bölüm başlığı (seçilemez)
};
struct MenuView {
    bool open = false;
    std::string title, subtitle;
    std::vector<std::string> tabs;
    int tab = 0;
    std::vector<MenuRow> rows;
    int selected = 0;
    std::string hint;
};
void SetMenu(const MenuView& view);

// Yazı kutusu (T ile yazma): alt ortada "Yaz › metin▌" olarak çizilir.
void SetInput(const std::string& text, bool active);

// Yükleme kapağı: başlatıcıyla (HIKAYE_LAUNCHER=1) açılınca HikayeTurkRP/data/loading.png oyun açılır açılmaz tam ekran gösterilir
// (oyunun kendi ana menüsü ve kayıt listesi görünmez) ve mod hikâyeye girene kadar kalır (en fazla 3 dk; sonra kendiliğinden kalkar).
// Kapatınca 0,6 sn'de kararır.
void SetLoadingCover(bool on);
bool LoadingCoverActive();

void Init();      // Present kancasını kaydeder. DLL attach sırasında çağrılmalı (SDK kuralı)
void Shutdown();  // kancayı kaldırır (DLL kapanırken)
bool Ready();     // ImGui + D3D11 hazır mı
bool HasTurkishFont();  // ş ı ğ İ çizebilen yazı tipi yüklendi mi

// Bu kare için ortalanmış bir satır (y: 0..1). Commit() çağrılana kadar render thread'ine geçmez.
void Text(const std::string& utf8, float y, int r, int g, int b);
void SetPhone(const PhoneView& view);
void SetTvQuad(const float* normalized_points);  // dört köşe: sol üst, sağ üst, sağ alt, sol alt; null kapatır
BrowserRect GetCefBrowserRect();
// Kalıcı HUD durum çubuğu (sağ üst): saat + sinyal çubukları + pil. Boş alanlar çizilmez.
void SetStatusBar(const std::string& clock, const std::string& signal, const std::string& battery, bool battery_low);
// Araç dashboard'u (sağ alt): hız (km/s), kilometre sayacı (odometer) ve yakıt çubuğu. Aktif=false ile kapanır.
void SetDash(float kmh, float km, float fuel_pct, bool active = true);
// Script thread kare sonunda çağırır: hazırlanan satırlar ve telefon render thread'ine geçer.
void Commit();
// Fare: telefon/bilgisayar sayfasında imlecin altındaki öğe (-1: yok) ve son tıklanan öğe (bir kez; -1: yok)
int HoverItem();
int TakeWebClick();

}  // namespace hikaye::overlay
