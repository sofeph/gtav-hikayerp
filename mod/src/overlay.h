#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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
    std::string icon = "message"; // "message", "cart", "bank", "shield", "settings", "phone", "tv"
    int duration_ms = 4500;
    std::chrono::steady_clock::time_point spawn_time;
    bool active = false;
    std::string action_payload;  // Örneğin: "app:missions" veya "mission_id:101"
};

struct PhoneSettings {
    WallpaperTheme wallpaper = WallpaperTheme::AMOLED_NIGHT;  // resim yoksa çizilen gradyan
    std::string wallpaper_image;  // data/wallpapers/phone/ içindeki dosya adı; boş = tema gradyanı
    float master_volume = 1.0f; // 0.0f - 1.0f
    bool vibrate = true;
};

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
    int kind = 0;  // hml::Kind: 0 metin, 1 h1, 2 h2, 3 çizgi, 4 bağlantı, 5 kart, 6 gönderi, 7 madde
    std::string text, sub, meta;
    bool selectable = false;
    int color = -1, bg = -1, border = -1;  // 0xRRGGBB, -1: varsayılan
    float size = 0.f, pad = -1.f, radius = -1.f;
    int align = 0;  // 0 sol, 1 orta, 2 sağ
    float gw = 0, gh = 0;
    int gbg = -1;
    std::vector<WebShape> shapes;
    bool spin = false;
    bool dock = false;  // telefon ana ekranı: alttaki sabit dock'ta durur (sayfalanmaz, taşınmaz)
};

struct ChatLine {
    bool mine = false;
    std::string text;
    bool separator = false;
};

struct PhoneView {
    bool open = false;
    int selected = 0;
    std::string clock;
    std::string cash;
    std::string battery;
    bool battery_low = false;
    std::string signal;
    std::vector<PhoneRow> rows;
    bool in_call = false;
    std::string caller, caller_line, player_line;
    bool listening = false, speaking = false, typing = false;
    std::string hint;
    std::string notice;
    bool web = false;
    std::string web_url, web_title;
    std::vector<WebItem> web_items;
    int web_selected = -1;
    int web_bg = -1, web_color = -1, web_accent = -1;
    bool cursor = false;
    float cursor_x = 0, cursor_y = 0;
    int click_seq = 0;
    std::vector<ChatLine> chat;
    bool thinking = false;
    bool assistant = false;
    bool pc = false;
    bool external_browser = false;
    bool gtml_mode = false;
    std::string gtml_json;
    float chat_scroll = 0.f;

    // FAZ 3: Material UI / Android donanım çerçevesi
    bool material_ui = false;
    int material_theme = 0;
    float corner_radius = 0.f;
    bool show_status_bar = true;
    bool show_gesture_pill = true;
    bool show_camera_cutout = false;
    int elevation_level = 0;
    int primary_color = 0x6750A4;
    int on_primary_color = 0xFFFFFF;
    int primary_container = 0xEADDFF;
    int on_primary_container = 0x21005D;
    int secondary_color = 0x625B71;
    int on_secondary_color = 0xFFFFFF;
    int surface_color = 0xFFFBFE;
    int on_surface_color = 0x1C1B1F;
    int outline_color = 0x79747E;
    int error_color = 0xBA1A1A;

    PhoneSettings settings;
    PhoneNotification active_banner;
};

struct MenuRow {
    std::string label;
    std::string value;
    bool header = false;
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
void SetInput(const std::string& text, bool active);

void Init();
void Shutdown();
bool Ready();
bool HasTurkishFont();

void Text(const std::string& utf8, float y, int r, int g, int b);
void SetPhone(const PhoneView& view);
void SetStatusBar(const std::string& clock, const std::string& signal, const std::string& battery, bool battery_low);
void SetDash(float kmh, float km, float fuel_pct, bool active = true);
void SetRadio(bool active, const std::string& station, const std::string& genre, const std::string& status, int volume = 50, bool muted = false);
void Commit();

int HoverItem();
int TakeWebClick();
int TakeMouseWheelDelta();
int TakeReorder();  // Returns app index being reordered, or -2 if none
void SetReorder(int index);  // Set which app is being reordered (-2 = none)

// TV Arayüzü
void SetTVVisible(bool visible, const std::string& channel_info);
void SetTVScreen(bool visible, const std::array<float, 8>& normalized_corners);
void SetTVSignalLost(bool signal_lost);
void SetTVVideoFrame(const std::uint8_t* pixels, unsigned width, unsigned height);
void ClearTVVideoFrame();

// Bildirim Banner Sistemi
void SetNotification(const PhoneNotification& notif);
PhoneNotification TakeNotification();
void PushMissionNotification(const std::string& title, const std::string& description, const std::string& action = "app:missions");
bool PopNotificationSoundRequest(); // Script thread'in güvenle ses çalabilmesi için kontrol bayrağı

}  // namespace hikaye::overlay