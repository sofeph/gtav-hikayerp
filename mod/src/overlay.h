#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <chrono>

namespace hikaye::overlay {

enum class WallpaperTheme : int {
    AMOLED_NIGHT = 0,    // Standart Saf Siyah & Kömür
    NEON_SUNSET = 1,     // Turuncu & Magenta Gradient
    CYBER_PULSE = 2,     // Neon Mavi & Mor Gradient
    DEEP_EMERALD = 3     // Zümrüt Yeşili & Koyu Petrol
};

struct PhoneNotification {
    int id = 0;
    std::string title;
    std::string body;
    std::string icon = "message"; // "message", "cart", "bank", "shield", "settings"
    int duration_ms = 4500;
    std::chrono::steady_clock::time_point spawn_time;
    bool active = false;
    std::string action_payload;  // Örneğin: "app:missions" veya "mission_id:101"
};

struct PhoneSettings {
    WallpaperTheme wallpaper = WallpaperTheme::AMOLED_NIGHT;
    int ringtone_id = 0;        // 0: Classic, 1: Digital, 2: Cyber
    int notification_id = 0;    // 0: Pop, 1: Chime, 2: Minimal
    float master_volume = 1.0f; // 0.0f - 1.0f
    bool vibrate = true;
};

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
    bool external_browser = false;
    bool gtml_mode = false;      // GTML (WebV2) modu
    std::string gtml_json;       // GTML Display List JSON (WebSocket'ten gelen)
        float chat_scroll = 0.f;       // Sohbet kaydırma ofseti (yukarı negatif)
    
        // FAZ 3: Material UI / Android device frame
        bool material_ui = false;           // Material 3 tasarım dili aktif
    int material_theme = 0;             // 0=light, 1=dark, 2=system
    float corner_radius = 0.f;          // Cihaz köşe yuvarlatma (dp)
    bool show_status_bar = true;        // Android durum çubuğu
    bool show_gesture_pill = true;      // Gezinme çubuğu (gesture navigation)
    bool show_camera_cutout = false;    // Punch-hole / notch
    int elevation_level = 0;            // Gölge seviyesi (0-5)
    int primary_color = 0x6750A4;       // Material primary (M3 baseline)
    int on_primary_color = 0xFFFFFF;
    int primary_container = 0xEADDFF;
    int on_primary_container = 0x21005D;
    int secondary_color = 0x625B71;
    int on_secondary_color = 0xFFFFFF;
    int surface_color = 0xFFFBFE;
    int on_surface_color = 0x1C1B1F;
    int outline_color = 0x79747E;
    int error_color = 0xBA1A1A;

    // YENİ: Duvar kağıdı, ayarlar ve bildirim banner'ı
    PhoneSettings settings;
    PhoneNotification active_banner;
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

void Init();      // Present kancasını kaydeder. DLL attach sırasında çağrılmalı (SDK kuralı)
void Shutdown();  // kancayı kaldırır (DLL kapanırken)
bool Ready();     // ImGui + D3D11 hazır mı
bool HasTurkishFont();  // ş ı ğ İ çizebilen yazı tipi yüklendi mi

// Bu kare için ortalanmış bir satır (y: 0..1). Commit() çağrılana kadar render thread'ine geçmez.
void Text(const std::string& utf8, float y, int r, int g, int b);
void SetPhone(const PhoneView& view);
// Kalıcı HUD durum çubuğu (sağ üst): saat + sinyal çubukları + pil. Boş alanlar çizilmez.
void SetStatusBar(const std::string& clock, const std::string& signal, const std::string& battery, bool battery_low);
// Araç dashboard'u (sağ alt): hız (km/s), kilometre sayacı (odometer) ve yakıt çubuğu. Aktif=false ile kapanır.
void SetDash(float kmh, float km, float fuel_pct, bool active = true);
void SetRadio(bool active, const std::string& station, const std::string& genre, const std::string& status);
// Script thread kare sonunda çağırır: hazırlanan satırlar ve telefon render thread'ine geçer.
void Commit();
// Fare: telefon/bilgisayar sayfasında imlecin altındaki öğe (-1: yok) ve son tıklanan öğe (bir kez; -1: yok)
int HoverItem();
int TakeWebClick();
int TakeMouseWheelDelta();

// TV overlay: render video texture onto TV screen in world (called from render thread)
void SetTVVisible(bool visible, const std::string& channel_info);
void SetTVScreen(bool visible, const std::array<float, 8>& normalized_corners);
void SetTVSignalLost(bool signal_lost);
void SetTVVideoFrame(const std::uint8_t* pixels, unsigned width, unsigned height);
void ClearTVVideoFrame();

// YENİ: Bildirim banner'ı (telefon kapalıyken de çizilir)
void SetNotification(const PhoneNotification& notif);
PhoneNotification TakeNotification();  // render thread okur, tek seferlik
void PushMissionNotification(const std::string& title, const std::string& description, const std::string& action = "app:missions");

}  // namespace hikaye::overlay
