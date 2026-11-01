#include <atomic>
#include "overlay.h"
#include "gtml_renderer.h"
#include "hls_player.h"

#include <d3d11.h>
#include <wincodec.h>
#include "xpad.h"
#include <dxgi.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include <algorithm>
#include <cmath>
#include <chrono>
#include <mutex>

#include "common.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"

#pragma comment(lib, "windowscodecs.lib")

namespace hikaye::overlay {

struct Line {
    std::string text;
    float y;
    int r, g, b;
};

std::mutex g_mu;
std::vector<Line> g_lines_next, g_lines_cur;
PhoneView g_phone_next, g_phone_cur;
std::atomic<int> g_hover{-1}, g_click{-1};
int g_seen_click_seq = 0;

struct InputView {
    bool active = false;
    std::string text;
};
InputView g_input_next, g_input_cur;
MenuView g_menu_next, g_menu_cur;

struct StatusBar {
    std::string clock, signal, battery;
    bool battery_low = false;
};
StatusBar g_status_next, g_status_cur;

struct Dash {
    bool active = false;
    float kmh = 0.f, km = 0.f, fuel = 100.f;
};
Dash g_dash_next, g_dash_cur;

struct TVState {
    bool visible = false;
    std::string channel_info;
};
TVState g_tv_next, g_tv_cur;

std::atomic<bool> g_ready{false};
std::atomic<bool> g_failed{false};
std::atomic<bool> g_turkish{false};

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
ID3D11RenderTargetView* g_main_rtv = nullptr;

bool g_init = false;
std::chrono::steady_clock::time_point g_last;

namespace {

// Forward declarations
void DrawMaterialIcon(ImDrawList* dl, const std::string& name, float x, float y, float size, ImU32 color);

inline ImU32 Col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
inline ImU32 Rgb(int c, int a = 255) { return Col((c >> 16) & 255, (c >> 8) & 255, c & 255, a); }

// Sinematik yumuşak gölgeli metin çizici
ImVec2 DrawCentered(ImDrawList* dl, ImFont* font, float size, const std::string& text, float cx, float y, float wrap,
                    ImU32 color, bool outline) {
    const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, wrap, text.c_str());
    const ImVec2 pos(cx - sz.x * 0.5f, y);
    if (outline) {
        // Yumuşak çift katmanlı gölge efekti
        dl->AddText(font, size, ImVec2(pos.x + 2.f, pos.y + 2.f), IM_COL32(0, 0, 0, 180), text.c_str(), nullptr, wrap);
        dl->AddText(font, size, ImVec2(pos.x - 1.f, pos.y), IM_COL32(0, 0, 0, 140), text.c_str(), nullptr, wrap);
        dl->AddText(font, size, ImVec2(pos.x + 1.f, pos.y), IM_COL32(0, 0, 0, 140), text.c_str(), nullptr, wrap);
    }
    dl->AddText(font, size, pos, color, text.c_str(), nullptr, wrap);
    return sz;
}

// Hücresel Sinyal Çubukları
void DrawSignalBars(ImDrawList* dl, float x_right, float y, float s, int bars) {
    const float bw = 4.0f * s, gap = 2.5f * s, base_h = 5.f * s, step = 3.5f * s;
    bars = std::clamp(bars, 0, 4);
    const ImU32 on = Col(70, 220, 125);
    const ImU32 off = Col(80, 85, 95, 120);
    for (int i = 0; i < 4; ++i) {
        const float h = base_h + step * i;
        const float x0 = x_right - (4 - i) * (bw + gap);
        dl->AddRectFilled(ImVec2(x0, y + base_h + 3.f * step - h), 
                          ImVec2(x0 + bw, y + base_h + 3.f * step), 
                          i < bars ? on : off, 1.5f * s);
    }
}

int SignalBarsFromText(const std::string& t) {
    int n = 0;
    for (size_t i = 0; i < t.size();) {
        const unsigned char c = static_cast<unsigned char>(t[i]);
        if (c >= 0xE0 && i + 2 < t.size()) {
            const bool inactive = c == 0xE2 && static_cast<unsigned char>(t[i + 1]) == 0x96 &&
                                  static_cast<unsigned char>(t[i + 2]) == 0x91;  // ░
            if (!inactive) ++n;
            i += 3;
            continue;
        }
        if (c >= 0x80) { i += (c >= 0xF0) ? 4 : 2; ++n; continue; }
        ++i; ++n;
    }
    return n;
}

// -----------------------------------------------------------------------------
// Vektörel Material 3 İkon Motoru
// -----------------------------------------------------------------------------
void DrawMaterialIcon(ImDrawList* dl, const std::string& name, float x, float y, float size, ImU32 color) {
    const float r = size * 0.5f;
    if (name == "menu") {
        for (int i = 0; i < 3; ++i) {
            dl->AddRectFilled(ImVec2(x - r * 0.8f, y - r * 0.6f + i * r * 0.6f), 
                              ImVec2(x + r * 0.8f, y - r * 0.35f + i * r * 0.6f), color, 1.5f);
        }
    } else if (name == "arrow_back") {
        dl->AddLine(ImVec2(x + r * 0.4f, y), ImVec2(x - r * 0.4f, y), color, 2.0f);
        dl->AddLine(ImVec2(x - r * 0.4f, y), ImVec2(x - r * 0.05f, y - r * 0.4f), color, 2.0f);
        dl->AddLine(ImVec2(x - r * 0.4f, y), ImVec2(x - r * 0.05f, y + r * 0.4f), color, 2.0f);
    } else if (name == "close") {
        dl->AddLine(ImVec2(x - r * 0.45f, y - r * 0.45f), ImVec2(x + r * 0.45f, y + r * 0.45f), color, 2.0f);
        dl->AddLine(ImVec2(x + r * 0.45f, y - r * 0.45f), ImVec2(x - r * 0.45f, y + r * 0.45f), color, 2.0f);
    } else if (name == "mic") {
        dl->AddRectFilled(ImVec2(x - r * 0.25f, y - r * 0.65f), ImVec2(x + r * 0.25f, y + r * 0.15f), color, r * 0.25f);
        dl->PathArcTo(ImVec2(x, y + r * 0.05f), r * 0.45f, 0.f, 3.14159f, 10);
        dl->PathStroke(color, false, 2.0f);
        dl->AddLine(ImVec2(x, y + r * 0.5f), ImVec2(x, y + r * 0.75f), color, 2.0f);
    } else if (name == "phone") {
        dl->AddRectFilled(ImVec2(x - r * 0.45f, y - r * 0.7f), ImVec2(x + r * 0.45f, y + r * 0.7f), color, 4.0f);
        dl->AddCircleFilled(ImVec2(x, y + r * 0.45f), 1.5f, IM_COL32(0, 0, 0, 180));
    } else if (name == "person") {
        dl->AddCircleFilled(ImVec2(x, y - r * 0.3f), r * 0.35f, color);
        dl->PathArcTo(ImVec2(x, y + r * 0.8f), r * 0.6f, 3.14159f, 6.28318f, 12);
        dl->PathStroke(color, false, 2.2f);
    } else if (name == "globe") {
        dl->AddCircle(ImVec2(x, y), r * 0.7f, color, 16, 2.0f);
        dl->AddLine(ImVec2(x - r * 0.7f, y), ImVec2(x + r * 0.7f, y), color, 1.5f);
        dl->AddLine(ImVec2(x, y - r * 0.7f), ImVec2(x, y + r * 0.7f), color, 1.5f);
    } else if (name == "bank") {
        dl->AddTriangleFilled(ImVec2(x, y - r * 0.6f), ImVec2(x - r * 0.6f, y - r * 0.2f), ImVec2(x + r * 0.6f, y - r * 0.2f), color);
        dl->AddRectFilled(ImVec2(x - r * 0.5f, y - r * 0.1f), ImVec2(x - r * 0.3f, y + r * 0.4f), color);
        dl->AddRectFilled(ImVec2(x - r * 0.1f, y - r * 0.1f), ImVec2(x + r * 0.1f, y + r * 0.4f), color);
        dl->AddRectFilled(ImVec2(x + r * 0.3f, y - r * 0.1f), ImVec2(x + r * 0.5f, y + r * 0.4f), color);
        dl->AddRectFilled(ImVec2(x - r * 0.65f, y + r * 0.4f), ImVec2(x + r * 0.65f, y + r * 0.6f), color, 1.0f);
    } else if (name == "search") {
        dl->AddCircle(ImVec2(x - r * 0.15f, y - r * 0.15f), r * 0.45f, color, 12, 2.0f);
        dl->AddLine(ImVec2(x + r * 0.2f, y + r * 0.2f), ImVec2(x + r * 0.65f, y + r * 0.65f), color, 2.5f);
    } else if (name == "cart") {
        dl->AddCircleFilled(ImVec2(x - r * 0.3f, y + r * 0.55f), 2.5f, color);
        dl->AddCircleFilled(ImVec2(x + r * 0.35f, y + r * 0.55f), 2.5f, color);
        dl->AddLine(ImVec2(x - r * 0.65f, y - r * 0.4f), ImVec2(x - r * 0.4f, y - r * 0.4f), color, 2.0f);
        dl->AddLine(ImVec2(x - r * 0.4f, y - r * 0.4f), ImVec2(x - r * 0.2f, y + r * 0.3f), color, 2.0f);
        dl->AddLine(ImVec2(x - r * 0.2f, y + r * 0.3f), ImVec2(x + r * 0.5f, y + r * 0.3f), color, 2.0f);
        dl->AddLine(ImVec2(x + r * 0.5f, y + r * 0.3f), ImVec2(x + r * 0.6f, y - r * 0.2f), color, 2.0f);
    } else if (name == "shield") {
        dl->AddTriangleFilled(ImVec2(x, y - r * 0.6f), ImVec2(x - r * 0.55f, y - r * 0.1f), ImVec2(x + r * 0.55f, y - r * 0.1f), color);
        dl->PathArcTo(ImVec2(x, y - r * 0.1f), r * 0.55f, 0.f, 3.14159f, 12);
        dl->PathFillConvex(color);
    } else if (name == "settings") {
        for (int i = 0; i < 6; ++i) {
            float a = i * 3.14159f / 3.f;
            dl->AddLine(ImVec2(x + cosf(a) * r * 0.3f, y + sinf(a) * r * 0.3f),
                        ImVec2(x + cosf(a) * r * 0.65f, y + sinf(a) * r * 0.65f), color, 2.5f);
        }
        dl->AddCircle(ImVec2(x, y), r * 0.35f, color, 12, 2.0f);
    } else {
        dl->AddCircleFilled(ImVec2(x, y), r * 0.5f, color);
    }
}

// -----------------------------------------------------------------------------
// Telefon Tarayıcısı ve Material You Arayüzü
// -----------------------------------------------------------------------------
// Telefon tarayıcısı ve Android 14 Material You arayüzü (Ok tuşları + İmleç Destekli)
void DrawWeb(ImDrawList* dl, ImFont* font, const PhoneView& p, float W, float H, float s, const float* geo = nullptr) {
    float pw = 360.f * s, pad = 12.f * s;
    float x = W - pw - 35.f * s;
    float y = H - 760.f * s;
    float h = 740.f * s;
    if (geo) {
        x = geo[0]; y = geo[1]; pw = geo[2]; h = geo[3]; pad = 10.f * s;
    }
    const float inner = pw - 2 * pad;
    
    const ImU32 surface_container = IM_COL32(0x1C, 0x20, 0x2A, 255);
    const ImU32 surface_high = IM_COL32(0x28, 0x2C, 0x38, 255);
    const ImU32 primary = IM_COL32(0xA8, 0xC7, 0xFA, 255);
    const ImU32 on_surface = IM_COL32(0xE8, 0xEE, 0xFC, 255);
    const ImU32 on_surface_variant = IM_COL32(0x9A, 0xA2, 0xB4, 255);
    const ImU32 outline = IM_COL32(0x3E, 0x42, 0x4E, 180);
    const ImU32 focus_glow = IM_COL32(0xA8, 0xC7, 0xFA, 255); // Ok tuşları odak çerçevesi
    
    const ImU32 page_col = p.web_color >= 0 ? Rgb(p.web_color) : on_surface;
    const float fs = 20.f * s, fs_small = 14.f * s;
    const bool is_home = p.web_url.empty() || p.web_url == "phone/home" || p.web_url == "telefon://home" || p.web_url == "home";

    float mx = p.cursor_x * W, my = p.cursor_y * H;

    // -------------------------------------------------------------
    // MOD 1: ANDROID 14+ MATERIAL YOU LAUNCHER (ANA EKRAN)
    // -------------------------------------------------------------
    if (is_home) {
        float cy = y + 10.f * s;

        // 1) Dijital Saat
        std::string clock_str = p.clock.empty() ? "14:05" : p.clock;
        const float fs_big_clock = 42.f * s;
        const ImVec2 csz = font->CalcTextSizeA(fs_big_clock, FLT_MAX, 0.f, clock_str.c_str());
        dl->AddText(font, fs_big_clock, ImVec2(x + (pw - csz.x) * 0.5f, cy), IM_COL32(255, 255, 255, 255), clock_str.c_str());
        cy += fs_big_clock + 2.f * s;

        // 2) Tarih & Hava Durumu
        const std::string date_str = "Los Santos • 24°C Açık";
        const ImVec2 dsz = font->CalcTextSizeA(fs_small, FLT_MAX, 0.f, date_str.c_str());
        dl->AddText(font, fs_small, ImVec2(x + (pw - dsz.x) * 0.5f, cy), on_surface_variant, date_str.c_str());
        cy += fs_small + 18.f * s;

        // 3) Arama Çubuğu (Search Pill) - Ok tuşu indeksi: -5
        const float pill_h = 42.f * s;
        const float pill_r = 21.f * s;
        const float pill_x0 = x + pad + 6.f * s;
        const float pill_x1 = x + pw - pad - 6.f * s;
        
        bool pill_focused = (!p.cursor && p.web_selected == -5);
        bool pill_hover = p.cursor && (mx >= pill_x0 && mx <= pill_x1 && my >= cy && my <= cy + pill_h);
        
        dl->AddRectFilled(ImVec2(pill_x0, cy), ImVec2(pill_x1, cy + pill_h), 
                          (pill_hover || pill_focused) ? surface_high : surface_container, pill_r);
        dl->AddRect(ImVec2(pill_x0, cy), ImVec2(pill_x1, cy + pill_h), 
                    pill_focused ? focus_glow : outline, pill_r, 0, pill_focused ? 2.5f * s : 1.0f * s);

        dl->AddText(font, 18.f * s, ImVec2(pill_x0 + 14.f * s, cy + 11.f * s), primary, "G");
        dl->AddText(font, fs_small, ImVec2(pill_x0 + 36.f * s, cy + 12.f * s), on_surface_variant, "Eyefind'da Ara...");
        DrawMaterialIcon(dl, "mic", pill_x1 - 20.f * s, cy + pill_h * 0.5f, 16.f * s, primary);

        if (pill_hover && p.click_seq != g_seen_click_seq) {
            g_seen_click_seq = p.click_seq;
            g_click.store(-5);
        }
        cy += pill_h + 26.f * s;

        // 4) Uygulama Izgarası (App Grid)
        struct LauncherApp {
            std::string name;
            std::string icon;
            ImU32 bg_color;
            int target_idx;
        };

        std::vector<LauncherApp> apps;
        if (!p.web_items.empty()) {
            static const std::vector<ImU32> palette = {
                IM_COL32(0x2E, 0x7D, 0x32, 255), IM_COL32(0x19, 0x76, 0xD2, 255), IM_COL32(0xD3, 0x2F, 0x2F, 255),
                IM_COL32(0x9B, 0x6B, 0xFF, 255), IM_COL32(0x2A, 0xA7, 0xC9, 255), IM_COL32(0xD4, 0xA0, 0x17, 255),
                IM_COL32(0x7B, 0x1F, 0xA2, 255), IM_COL32(0x2A, 0x8F, 0x8A, 255), IM_COL32(0xD9, 0x53, 0x4F, 255),
                IM_COL32(0x3E, 0x9B, 0x4F, 255), IM_COL32(0xC9, 0xA2, 0x27, 255), IM_COL32(0xE8, 0xB2, 0x3A, 255)
            };
            for (size_t i = 0; i < std::min<size_t>(12, p.web_items.size()); ++i) {
                const auto& it = p.web_items[i];
                std::string name = it.text.empty() ? (it.meta.empty() ? "Uygulama" : it.meta) : it.text;
                if (name.size() > 12) name = name.substr(0, 12);
                std::string icon = "phone";
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (lower.find("rehber") != std::string::npos) icon = "person";
                else if (lower.find("internet") != std::string::npos || lower.find("web") != std::string::npos || lower.find("site") != std::string::npos) icon = "globe";
                else if (lower.find("haviland") != std::string::npos) icon = "mic";
                else if (lower.find("maps") != std::string::npos || lower.find("ara") != std::string::npos) icon = "search";
                else if (lower.find("maze") != std::string::npos || lower.find("bank") != std::string::npos) icon = "bank";
                else if (lower.find("store") != std::string::npos || lower.find("app") != std::string::npos) icon = "cart";
                else if (lower.find("saat") != std::string::npos || lower.find("clock") != std::string::npos) icon = "phone";
                else if (lower.find("alarm") != std::string::npos) icon = "mic";
                else if (lower.find("takvim") != std::string::npos) icon = "phone";
                else if (lower.find("not") != std::string::npos) icon = "menu";
                else if (lower.find("yap") != std::string::npos || lower.find("list") != std::string::npos) icon = "menu";
                apps.push_back({name, icon, it.color >= 0 ? Rgb(it.color) : palette[i % palette.size()], static_cast<int>(i)});
            }
        } else {
            const LauncherApp defaults[] = {
                {"Telefon", "phone", IM_COL32(0x2E, 0x7D, 0x32, 255), 0},
                {"Rehber", "person", IM_COL32(0x19, 0x76, 0xD2, 255), 1},
                {"İnternet", "globe", IM_COL32(0xD3, 0x2F, 0x2F, 255), 2},
                {"Haviland", "mic", IM_COL32(0x9B, 0x6B, 0xFF, 255), 3},
                {"Maps", "search", IM_COL32(0x2A, 0xA7, 0xC9, 255), 4},
                {"Maze Bank", "bank", IM_COL32(0xD4, 0xA0, 0x17, 255), 5},
                {"App Store", "cart", IM_COL32(0x7B, 0x1F, 0xA2, 255), 6},
                {"Saat", "clock", IM_COL32(0x2A, 0x8F, 0x8A, 255), 7},
                {"Alarm", "alarm", IM_COL32(0xD9, 0x53, 0x4F, 255), 8},
                {"Takvim", "calendar", IM_COL32(0x3E, 0x9B, 0x4F, 255), 9},
                {"Notlar", "note", IM_COL32(0xC9, 0xA2, 0x27, 255), 10},
                {"Yapılacaklar", "list", IM_COL32(0xE8, 0xB2, 0x3A, 255), 11}
            };
            for (size_t i = 0; i < std::size(defaults); ++i) apps.push_back(defaults[i]);
        }

        const int cols = 4;
        const float grid_w = inner - 6.f * s;
        const float col_w = grid_w / cols;
        const float icon_sz = 46.f * s;
        const float app_row_h = 74.f * s;

        int hovered_app = -1;

        for (size_t i = 0; i < std::min<size_t>(8, apps.size()); ++i) {
            int row = static_cast<int>(i / cols);
            int col = static_cast<int>(i % cols);
            float ax = x + pad + 3.f * s + col * col_w + (col_w - icon_sz) * 0.5f;
            float ay = cy + row * app_row_h;

            bool is_focused = (!p.cursor && p.web_selected == static_cast<int>(i));
            bool is_hovered = p.cursor && (mx >= ax && mx <= ax + icon_sz && my >= ay && my <= ay + icon_sz + 18.f * s);
            if (is_hovered) hovered_app = static_cast<int>(i);

            dl->AddRectFilled(ImVec2(ax, ay), ImVec2(ax + icon_sz, ay + icon_sz), apps[i].bg_color, 15.f * s);
            if (is_focused || is_hovered) {
                dl->AddRect(ImVec2(ax - 2.f * s, ay - 2.f * s), 
                            ImVec2(ax + icon_sz + 2.f * s, ay + icon_sz + 2.f * s), 
                            is_focused ? focus_glow : IM_COL32(255, 255, 255, 230), 16.f * s, 0, 2.5f * s);
            }

            DrawMaterialIcon(dl, apps[i].icon, ax + icon_sz * 0.5f, ay + icon_sz * 0.5f, 22.f * s, IM_COL32(255, 255, 255, 255));
            const ImVec2 nsz = font->CalcTextSizeA(fs_small * 0.85f, FLT_MAX, 0.f, apps[i].name.c_str());
            dl->AddText(font, fs_small * 0.85f, ImVec2(ax + (icon_sz - nsz.x) * 0.5f, ay + icon_sz + 4.f * s), 
                        (is_focused || is_hovered) ? primary : on_surface, apps[i].name.c_str());
        }

        if (hovered_app >= 0) {
            g_hover.store(hovered_app);
            if (p.click_seq != g_seen_click_seq) {
                g_seen_click_seq = p.click_seq;
                g_click.store(hovered_app);
            }
        }

        // 5) Alt Sabit Dock (Ok tuşları: 8, 9, 10, 11)
        const float dock_h = 66.f * s;
        const float dock_y = y + h - dock_h - 24.f * s;
        dl->AddRectFilled(ImVec2(x + pad, dock_y), ImVec2(x + pw - pad, dock_y + dock_h), 
                          IM_COL32(0x18, 0x1C, 0x26, 220), 22.f * s);
        dl->AddRect(ImVec2(x + pad, dock_y), ImVec2(x + pw - pad, dock_y + dock_h), 
                    outline, 22.f * s, 0, 1.0f * s);

        const LauncherApp dock_apps[] = {
            {"Telefon", "phone", IM_COL32(0x2E, 0x7D, 0x32, 255), 0},
            {"Rehber", "person", IM_COL32(0x19, 0x76, 0xD2, 255), 1},
            {"İnternet", "globe", IM_COL32(0xD3, 0x2F, 0x2F, 255), 2},
            {"Ayarlar", "settings", IM_COL32(0x54, 0x6E, 0x7A, 255), 7}
        };

        const float dock_col_w = (inner - 8.f * s) / 4.f;
        for (int i = 0; i < 4; ++i) {
            float dax = x + pad + 4.f * s + i * dock_col_w + (dock_col_w - 44.f * s) * 0.5f;
            float day = dock_y + 11.f * s;
            
            bool d_focused = (!p.cursor && p.web_selected == (8 + i));
            bool d_hover = p.cursor && (mx >= dax && mx <= dax + 44.f * s && my >= day && my <= day + 44.f * s);

            dl->AddRectFilled(ImVec2(dax, day), ImVec2(dax + 44.f * s, day + 44.f * s), 
                              dock_apps[i].bg_color, 14.f * s);
            if (d_focused || d_hover) {
                dl->AddRect(ImVec2(dax - 2.f * s, day - 2.f * s), 
                            ImVec2(dax + 44.f * s + 2.f * s, day + 44.f * s + 2.f * s), 
                            d_focused ? focus_glow : IM_COL32(255, 255, 255, 255), 15.f * s, 0, 2.0f * s);
                if (d_hover && p.click_seq != g_seen_click_seq) {
                    g_seen_click_seq = p.click_seq;
                    g_click.store(dock_apps[i].target_idx);
                }
            }
            DrawMaterialIcon(dl, dock_apps[i].icon, dax + 22.f * s, day + 22.f * s, 20.f * s, IM_COL32(255, 255, 255, 255));
        }

        return;
    }

    // -------------------------------------------------------------
    // MOD 2: WEB SAYFASI VEYA UYGULAMA İÇİ (DÜZENLİ LİSTE)
    // -------------------------------------------------------------
    const float url_bar_h = 36.f * s;
    const float url_bar_r = 18.f * s;
    const float url_bar_y = y + 2.f * s;

    dl->AddRectFilled(ImVec2(x + pad, url_bar_y), ImVec2(x + pw - pad, url_bar_y + url_bar_h), surface_container, url_bar_r);
    dl->AddRect(ImVec2(x + pad, url_bar_y), ImVec2(x + pw - pad, url_bar_y + url_bar_h), outline, url_bar_r, 0, 1.0f * s);

    const float lock_x = x + pad + 18.f * s;
    const float lock_y = url_bar_y + url_bar_h * 0.5f;
    DrawMaterialIcon(dl, "arrow_back", lock_x, lock_y, 16.f * s, primary);

    std::string display_url = p.web_url;
    if (display_url.rfind("telefon://", 0) == 0) display_url = display_url.substr(10);
    dl->AddText(font, fs_small, ImVec2(lock_x + 14.f * s, url_bar_y + 9.f * s), primary, display_url.c_str());

    const float close_btn_x = x + pw - pad - 18.f * s;
    const float close_btn_y = url_bar_y + url_bar_h * 0.5f;
    DrawMaterialIcon(dl, "close", close_btn_x, close_btn_y, 16.f * s, on_surface_variant);

    if (p.cursor) {
        if (mx >= close_btn_x - 14.f * s && mx <= close_btn_x + 14.f * s && 
            my >= url_bar_y && my <= url_bar_y + url_bar_h) {
            dl->AddCircleFilled(ImVec2(close_btn_x, close_btn_y), 12.f * s, IM_COL32(255, 255, 255, 40));
            if (p.click_seq != g_seen_click_seq) {
                g_seen_click_seq = p.click_seq;
                g_click.store(-3);
            }
        }
        if (mx >= lock_x - 14.f * s && mx <= lock_x + 14.f * s && 
            my >= url_bar_y && my <= url_bar_y + url_bar_h) {
            dl->AddCircleFilled(ImVec2(lock_x, lock_y), 12.f * s, IM_COL32(255, 255, 255, 40));
            if (p.click_seq != g_seen_click_seq) {
                g_seen_click_seq = p.click_seq;
                g_click.store(-4);
            }
        }
    }

    const float top = url_bar_y + url_bar_h + 8.f * s;
    const float bottom = y + h - pad - 6.f * s;
    const float avail = bottom - top;
    if (!p.notice.empty()) dl->AddText(font, fs_small, ImVec2(x + pad, bottom - 20.f * s), IM_COL32(0xFF, 0xCD, 0x5A, 255), p.notice.c_str());

    auto size_of = [&](const WebItem& w) {
        if (w.size > 0.f) return w.size * s;
        return w.kind == 1 ? 26.f * s : w.kind == 2 ? 22.f * s : w.kind == 5 ? 20.f * s : fs;
    };

    std::vector<float> hs(p.web_items.size());
    for (size_t i = 0; i < p.web_items.size(); ++i) {
        const WebItem& it = p.web_items[i];
        const float wrap = inner - 16.f * s;
        float v = 0.f;
        switch (it.kind) {
            case 3: v = 14.f * s; break;
            case 5: v = 14.f * s + font->CalcTextSizeA(size_of(it), FLT_MAX, wrap, it.text.c_str()).y + (it.sub.empty() ? 0.f : 4.f * s + font->CalcTextSizeA(fs_small, FLT_MAX, wrap, it.sub.c_str()).y) + 10.f * s; break;
            case 6: v = 12.f * s + fs_small + 4.f * s + font->CalcTextSizeA(size_of(it), FLT_MAX, wrap, it.text.c_str()).y + 6.f * s; break;
            default: v = font->CalcTextSizeA(size_of(it), FLT_MAX, wrap, it.text.c_str()).y + 8.f * s; break;
        }
        hs[i] = v;
    }

    size_t first = 0;
    if (p.web_selected >= 0 && p.web_selected < static_cast<int>(hs.size())) {
        float used = 0.f;
        for (int i = 0; i <= p.web_selected; ++i) used += hs[static_cast<size_t>(i)];
        while (used > avail && first < static_cast<size_t>(p.web_selected)) used -= hs[first++];
    }

    dl->PushClipRect(ImVec2(x, top), ImVec2(x + pw, bottom - (p.notice.empty() ? 0.f : 24.f * s)), true);
    float cy = top;
    std::vector<float> rects(p.web_items.size() * 4, 0.f);

    for (size_t i = first; i < p.web_items.size() && cy < bottom; ++i) {
        const WebItem& it = p.web_items[i];
        const bool sel = static_cast<int>(i) == p.web_selected;
        const float ix = x + pad;
        const float sz = size_of(it);
        const float rad = (it.radius >= 0.f ? it.radius : 14.f) * s;
        auto text_col = [&](ImU32 fallback) { return it.color >= 0 ? Rgb(it.color) : fallback; };
        
        if (it.kind == 5 || it.kind == 4 || it.selectable) {
            rects[i * 4] = ix; rects[i * 4 + 1] = cy; rects[i * 4 + 2] = inner; rects[i * 4 + 3] = hs[i];

            const ImU32 card_bg = it.bg >= 0 ? Rgb(it.bg) : (sel ? surface_high : surface_container);
            dl->AddRectFilled(ImVec2(ix, cy + 2.f * s), ImVec2(ix + inner, cy + hs[i] + 2.f * s), IM_COL32(0, 0, 0, 35), rad);
            dl->AddRectFilled(ImVec2(ix, cy), ImVec2(ix + inner, cy + hs[i]), card_bg, rad);
            
            // Ok tuşlarıyla veya fareyle seçilen kartta parlayan çerçeve
            if (sel) {
                dl->AddRect(ImVec2(ix, cy), ImVec2(ix + inner, cy + hs[i]), focus_glow, rad, 0, 2.2f * s);
            } else {
                dl->AddRect(ImVec2(ix, cy), ImVec2(ix + inner, cy + hs[i]), outline, rad, 0, 1.0f * s);
            }

            dl->AddText(font, sz, ImVec2(ix + 10.f * s, cy + 6.f * s), text_col(sel ? primary : on_surface), it.text.c_str(), nullptr, inner - 20.f * s);
            if (!it.sub.empty()) {
                dl->AddText(font, fs_small, ImVec2(ix + 10.f * s, cy + 8.f * s + sz), on_surface_variant, it.sub.c_str(), nullptr, inner - 20.f * s);
            }
        } else if (it.kind == 3) {
            dl->AddLine(ImVec2(ix, cy + 7.f * s), ImVec2(ix + inner, cy + 7.f * s), outline, 1.0f * s);
        } else {
            dl->AddText(font, sz, ImVec2(ix, cy + 2.f * s), text_col(it.kind == 1 ? IM_COL32(255, 255, 255, 255) : page_col), it.text.c_str(), nullptr, inner);
        }
        cy += hs[i] + 4.f * s;
    }
    dl->PopClipRect();

    if (p.cursor) {
        int hover = -1;
        for (size_t i = 0; i < p.web_items.size(); ++i) {
            if (!p.web_items[i].selectable) continue;
            const float rx0 = rects[i * 4], ry0 = rects[i * 4 + 1];
            const float rw0 = rects[i * 4 + 2], rh0 = rects[i * 4 + 3];
            if (rw0 > 0 && rh0 > 0 && mx >= rx0 && mx <= rx0 + rw0 && my >= ry0 && my <= ry0 + rh0) {
                hover = static_cast<int>(i);
                break;
            }
        }
        g_hover.store(hover);
        if (hover >= 0 && p.click_seq != g_seen_click_seq) {
            g_seen_click_seq = p.click_seq;
            g_click.store(hover);
        }
    }
}
// -----------------------------------------------------------------------------
// GTML & Bilgisayar & Chat Arayüzleri
// -----------------------------------------------------------------------------
void DrawGTML(ImDrawList* dl, ImFont* font, const PhoneView& p, float W, float H, float s) {
    if (p.gtml_json.empty()) return;
    gtml::GTMLDisplayList display_list;
    std::string error;
    if (!gtml::ParseDisplayList(p.gtml_json, display_list, error)) {
        const float pw = 460.f * s, pad = 18.f * s;
        float x = W - pw - 36.f * s, y = 24.f * s;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + pw, y + 100.f * s), Col(40, 20, 20, 240), 16.f * s);
        dl->AddText(font, 18.f * s, ImVec2(x + pad, y + pad), Col(255, 120, 120), "GTML Parse Error:");
        dl->AddText(font, 14.f * s, ImVec2(x + pad, y + pad + 24.f * s), Col(200, 200, 200), error.c_str());
        return;
    }
    const float pw = 460.f * s;
    float x = W - pw - 36.f * s, y = 24.f * s, h = H - 48.f * s;
    if (display_list.bg_color >= 0) {
        int bg = display_list.bg_color;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + pw, y + h), Col((bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF, 248), 26.f * s);
    } else {
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + pw, y + h), Col(18, 20, 28, 242), 26.f * s);
    }
    gtml::RenderDisplayList(display_list, s, x, y);
}

void DrawPc(ImDrawList* dl, ImFont* font, const PhoneView& p, float W, float H, float s) {
    const float sw = std::min(W * 0.70f, H * 0.86f * 16.f / 9.f), sh = sw * 9.f / 16.f;
    const float sx = (W - sw) * 0.5f, sy = (H - sh) * 0.5f;
    const float tb_h = 38.f * s;
    dl->AddRectFilled(ImVec2(sx - 14.f * s, sy - 14.f * s), ImVec2(sx + sw + 14.f * s, sy + sh + 14.f * s), Col(12, 14, 20, 255), 14.f * s);
    dl->AddRectFilledMultiColor(ImVec2(sx, sy), ImVec2(sx + sw, sy + sh), Col(24, 62, 128), Col(52, 38, 110), Col(14, 28, 60), Col(12, 44, 92));

    const bool desktop = p.web_url == "bilgisayar://home";
    if (!desktop) {
        const float wx = sx + sw * 0.05f, wy = sy + sh * 0.06f, ww = sw * 0.90f, wh = sh - tb_h - sh * 0.10f;
        const float title_h = 30.f * s;
        dl->AddRectFilled(ImVec2(wx, wy), ImVec2(wx + ww, wy + wh), Col(20, 24, 36, 255), 8.f * s);
        dl->AddRectFilledMultiColor(ImVec2(wx, wy), ImVec2(wx + ww, wy + title_h), Col(44, 84, 160), Col(60, 70, 150), Col(52, 62, 130), Col(36, 72, 140));
        dl->AddText(font, 17.f * s, ImVec2(wx + 12.f * s, wy + 5.f * s), Col(255, 255, 255), p.web_title.empty() ? p.web_url.c_str() : p.web_title.c_str());
        const float geo[4] = {wx + 2.f * s, wy + title_h, ww - 4.f * s, wh - title_h - 2.f * s};
        DrawWeb(dl, font, p, W, H, s, geo);
    }
}

void DrawChat(ImDrawList* dl, ImFont* font, const PhoneView& p, float W, float H, float s) {
    const float pw = 430.f * s, pad = 16.f * s;
    const float x = W - pw - 36.f * s, fs = 20.f * s, y = 24.f * s, h = H - 48.f * s;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + pw, y + h), Col(14, 16, 24, 244), 26.f * s);
    dl->AddText(font, fs * 1.1f, ImVec2(x + pad, y + pad), Col(255, 255, 255), p.caller.c_str());
}

void DrawCursor(ImDrawList* dl, const PhoneView& p, float W, float H, float s) {
    if (!p.cursor) return;
    const float x = p.cursor_x * W, y = p.cursor_y * H, k = 22.f * s;
    const ImVec2 a(x, y), b(x, y + k), c(x + k * 0.62f, y + k * 0.78f);
    dl->AddTriangleFilled(a, b, c, IM_COL32(255, 255, 255, 255));
    dl->AddTriangle(a, b, c, IM_COL32(0, 0, 0, 255), 2.f * s);
}

void DrawAssistant(ImDrawList* dl, ImFont* font, const PhoneView& p, float W, float H, float s) {
    const float pw = 480.f * s, ph = 180.f * s;
    const float x = (W - pw) * 0.5f, y = H - ph - 48.f * s;

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + pw, y + ph), Col(16, 20, 32, 235), 24.f * s);
    dl->AddRect(ImVec2(x, y), ImVec2(x + pw, y + ph), Col(90, 110, 150, 255), 24.f * s, 0, 2.f * s);

    ImU32 status_col = Col(120, 220, 140);
    std::string status_txt = p.caller;
    if (p.listening) {
        status_col = Col(255, 205, 90);
        status_txt = "Dinleniyor...";
    } else if (p.thinking) {
        status_col = Col(100, 180, 255);
        status_txt = "Düşünüyor...";
    } else if (p.speaking) {
        status_col = Col(220, 120, 255);
        status_txt = "Konuşuyor...";
    }

    dl->AddCircleFilled(ImVec2(x + 30.f * s, y + 30.f * s), 8.f * s, status_col);
    dl->AddText(font, 18.f * s, ImVec2(x + 50.f * s, y + 19.f * s), Col(200, 210, 230), status_txt.c_str());

    if (!p.notice.empty()) {
        dl->AddText(font, 22.f * s, ImVec2(x + 24.f * s, y + 70.f * s), Col(255, 255, 255), p.notice.c_str(), nullptr, pw - 48.f * s);
    }
}

// -----------------------------------------------------------------------------
// Menü Çizimi (Modern Glassmorphism UI)
// -----------------------------------------------------------------------------
void DrawMenu(ImDrawList* dl, ImFont* font, const MenuView& m, float W, float H, float s) {
    (void)W;
    const float pw = 580.f * s, pad = 20.f * s;
    const float x = 36.f * s;
    const float fs = 24.f * s, fs_small = 18.f * s, row_h = 40.f * s;
    const int visible = 12;

    int first = 0;
    const int n = static_cast<int>(m.rows.size());
    if (n > visible) first = std::max(0, std::min(m.selected - visible / 2, n - visible));
    const int shown = std::min(n, visible);

    float h = pad + 40.f * s + (m.subtitle.empty() ? 0.f : 26.f * s) + (m.tabs.empty() ? 0.f : 42.f * s) +
              static_cast<float>(shown) * row_h + pad + 30.f * s;
    const float y = std::max(24.f * s, (H - h) * 0.5f);

    // Dış gölge
    dl->AddRectFilled(ImVec2(x - 4.f * s, y - 4.f * s), ImVec2(x + pw + 4.f * s, y + h + 4.f * s), IM_COL32(0, 0, 0, 70), 22.f * s);
    // Panel arka planı
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + pw, y + h), IM_COL32(16, 20, 28, 245), 20.f * s);
    dl->AddRect(ImVec2(x, y), ImVec2(x + pw, y + h), IM_COL32(90, 130, 210, 180), 20.f * s, 0, 1.5f * s);

    float cy = y + pad;
    dl->AddText(font, fs * 1.15f, ImVec2(x + pad, cy), IM_COL32(255, 255, 255, 255), m.title.c_str());
    cy += 38.f * s;

    if (!m.subtitle.empty()) {
        dl->AddText(font, fs_small, ImVec2(x + pad, cy), IM_COL32(150, 160, 185, 255), m.subtitle.c_str());
        cy += 26.f * s;
    }
    if (!m.tabs.empty()) {
        float tx = x + pad;
        for (size_t i = 0; i < m.tabs.size(); ++i) {
            const bool cur = static_cast<int>(i) == m.tab;
            const ImVec2 ts = font->CalcTextSizeA(fs_small, FLT_MAX, 0.f, m.tabs[i].c_str());
            if (cur) {
                dl->AddRectFilled(ImVec2(tx - 6.f * s, cy), ImVec2(tx + ts.x + 6.f * s, cy + 30.f * s), IM_COL32(65, 105, 225, 230), 8.f * s);
            }
            dl->AddText(font, fs_small, ImVec2(tx, cy + 4.f * s), cur ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 158, 180, 255), m.tabs[i].c_str());
            tx += ts.x + 20.f * s;
        }
        cy += 40.f * s;
    }

    for (int i = 0; i < shown; ++i) {
        const MenuRow& r = m.rows[static_cast<size_t>(first + i)];
        const bool sel = (first + i) == m.selected && !r.header;
        if (sel) {
            dl->AddRectFilled(ImVec2(x + 10.f * s, cy), ImVec2(x + pw - 10.f * s, cy + row_h - 4.f * s), IM_COL32(65, 105, 225, 200), 10.f * s);
            dl->AddRect(ImVec2(x + 10.f * s, cy), ImVec2(x + pw - 10.f * s, cy + row_h - 4.f * s), IM_COL32(160, 200, 255, 230), 10.f * s, 0, 1.2f * s);
        }
        if (r.header) {
            dl->AddText(font, fs_small, ImVec2(x + pad, cy + 6.f * s), IM_COL32(140, 180, 255, 255), r.label.c_str());
        } else {
            dl->AddText(font, fs, ImVec2(x + pad, cy + 5.f * s), sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(215, 220, 235, 255), r.label.c_str());
            if (!r.value.empty()) {
                const std::string v = sel ? "‹ " + r.value + " ›" : r.value;
                const ImVec2 vs = font->CalcTextSizeA(fs, FLT_MAX, 0.f, v.c_str());
                dl->AddText(font, fs, ImVec2(x + pw - pad - vs.x, cy + 5.f * s), sel ? IM_COL32(255, 220, 130, 255) : IM_COL32(170, 180, 200, 255), v.c_str());
            }
        }
        cy += row_h;
    }
    dl->AddText(font, fs_small * 0.9f, ImVec2(x + pad, y + h - 26.f * s), IM_COL32(140, 150, 175, 220), m.hint.c_str());
}

// -----------------------------------------------------------------------------
// Yazı Giriş Kutusu (Modern Input)
// -----------------------------------------------------------------------------
void DrawInput(ImDrawList* dl, ImFont* font, const InputView& in, float W, float H, float s) {
    const float bw = W * 0.55f, bh = 60.f * s, pad = 20.f * s;
    const float x = (W - bw) * 0.5f, y = H * 0.92f - bh * 0.5f;
    const float fs = 24.f * s;

    // Arka plan & Parlak kenarlık
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw, y + bh), IM_COL32(14, 17, 25, 240), 16.f * s);
    dl->AddRect(ImVec2(x, y), ImVec2(x + bw, y + bh), IM_COL32(100, 150, 255, 220), 16.f * s, 0, 1.8f * s);

    std::string shown = in.text;
    const float room = bw - 2 * pad - 220.f * s;
    while (!shown.empty() && font->CalcTextSizeA(fs, FLT_MAX, 0.f, shown.c_str()).x > room) {
        size_t cut = 1;
        while (cut < shown.size() && (static_cast<unsigned char>(shown[cut]) & 0xC0) == 0x80) ++cut;
        shown.erase(0, cut);
    }
    const bool blink = static_cast<int>(ImGui::GetTime() * 2.5) % 2 == 0;
    const std::string line = "Yaz › " + shown + (blink ? "▌" : " ");
    dl->AddText(font, fs, ImVec2(x + pad, y + (bh - fs) * 0.5f), IM_COL32(255, 255, 255, 255), line.c_str());

    const char* hint = "Enter: gönder   Esc: iptal";
    const ImVec2 hs = font->CalcTextSizeA(fs * 0.65f, FLT_MAX, 0.f, hint);
    dl->AddText(font, fs * 0.65f, ImVec2(x + bw - pad - hs.x, y + (bh - hs.y) * 0.5f), IM_COL32(150, 160, 180, 230), hint);
}

// -----------------------------------------------------------------------------
// Android 14+ Material You Telefon Donanım Çerçevesi
// -----------------------------------------------------------------------------
void DrawMaterialFrame(ImDrawList* dl, ImFont* font, const PhoneView& p, float W, float H, float s) {
    const float device_w = 380.0f * s;
    const float device_h = std::min(H - 35.0f * s, 810.0f * s);
    const float dx = W - device_w - 32.0f * s;
    const float dy = H - device_h - 18.0f * s;
    const float corner = 34.0f * s;
    const float bezel = 5.0f * s;

    // 1) Çok Katmanlı Ortam Gölgesi (Elevation Blur)
    dl->AddRectFilled(ImVec2(dx - 14.0f * s, dy - 8.0f * s), 
                      ImVec2(dx + device_w + 14.0f * s, dy + device_h + 18.0f * s), 
                      IM_COL32(0, 0, 0, 45), corner + 14.0f * s);
    dl->AddRectFilled(ImVec2(dx - 6.0f * s, dy - 3.0f * s), 
                      ImVec2(dx + device_w + 6.0f * s, dy + device_h + 10.0f * s), 
                      IM_COL32(0, 0, 0, 95), corner + 6.0f * s);

    // 2) Yan Tuşlar
    const ImU32 button_color = IM_COL32(42, 45, 52, 255);
    dl->AddRectFilled(ImVec2(dx + device_w + 1.0f * s, dy + 130.0f * s), 
                      ImVec2(dx + device_w + 4.0f * s, dy + 175.0f * s), button_color, 2.0f * s);
    dl->AddRectFilled(ImVec2(dx + device_w + 1.0f * s, dy + 205.0f * s), 
                      ImVec2(dx + device_w + 4.0f * s, dy + 295.0f * s), button_color, 2.0f * s);

    // 3) Kasa & Metalik Çerçeve
    const ImU32 casing_color = IM_COL32(0x1B, 0x1C, 0x1E, 255);
    dl->AddRectFilled(ImVec2(dx - bezel, dy - bezel), 
                      ImVec2(dx + device_w + bezel, dy + device_h + bezel), 
                      casing_color, corner + bezel);
    dl->AddRect(ImVec2(dx - bezel, dy - bezel), 
                ImVec2(dx + device_w + bezel, dy + device_h + bezel), 
                IM_COL32(75, 80, 90, 180), corner + bezel, 0, 1.5f * s);

    // 4) Hoparlör Izgarası
    const float spk_w = 46.0f * s;
    dl->AddRectFilled(ImVec2(dx + device_w * 0.5f - spk_w * 0.5f, dy - 2.5f * s),
                      ImVec2(dx + device_w * 0.5f + spk_w * 0.5f, dy - 0.5f * s),
                      IM_COL32(10, 12, 15, 255), 1.0f * s);

    // 5) AMOLED Ekran Zemini
    const ImU32 screen_bg = IM_COL32(0x0B, 0x0D, 0x11, 255);
    const float scr_corner = corner - 2.0f * s;
    dl->AddRectFilled(ImVec2(dx, dy), ImVec2(dx + device_w, dy + device_h), screen_bg, scr_corner);
    dl->AddRect(ImVec2(dx, dy), ImVec2(dx + device_w, dy + device_h), 
                IM_COL32(0x35, 0x38, 0x42, 100), scr_corner, 0, 1.0f * s);

    // 6) Kamera Deliği (Punch-Hole)
    if (p.show_camera_cutout) {
        const ImVec2 cam_center(dx + device_w * 0.5f, dy + 16.0f * s);
        dl->AddCircleFilled(cam_center, 7.0f * s, IM_COL32(18, 22, 28, 255));
        dl->AddCircleFilled(cam_center, 5.0f * s, IM_COL32(3, 4, 6, 255));
        dl->AddCircleFilled(ImVec2(cam_center.x - 1.5f * s, cam_center.y - 1.5f * s), 
                            1.5f * s, IM_COL32(50, 90, 155, 180));
    }

    // 7) Durum Çubuğu
    const float sb_h = 28.0f * s;
    if (p.show_status_bar) {
        const float fs_sb = 14.0f * s;
        std::string clock = p.clock.empty() ? "14:05" : p.clock;
        dl->AddText(font, fs_sb, ImVec2(dx + 18.0f * s, dy + 6.0f * s), 
                    IM_COL32(0xE8, 0xEE, 0xFC, 255), clock.c_str());

        float rx = dx + device_w - 18.0f * s;

        const float bat_w = 20.0f * s, bat_h = 10.0f * s;
        rx -= bat_w + 3.0f * s;
        const float bat_y = dy + 8.5f * s;
        const ImU32 bat_col = p.battery_low ? IM_COL32(0xE2, 0x3B, 0x3B, 255) : IM_COL32(0xE8, 0xEE, 0xFC, 255);
        
        dl->AddRect(ImVec2(rx, bat_y), ImVec2(rx + bat_w, bat_y + bat_h), bat_col, 2.5f * s, 0, 1.2f * s);
        dl->AddRectFilled(ImVec2(rx + bat_w, bat_y + 2.5f * s), ImVec2(rx + bat_w + 2.0f * s, bat_y + bat_h - 2.5f * s), bat_col, 1.0f * s);
        
        float fill_pct = 0.85f;
        if (!p.battery.empty()) {
            int val = 0;
            if (std::sscanf(p.battery.c_str(), "%%%d", &val) == 1 || std::sscanf(p.battery.c_str(), "%d", &val) == 1) {
                fill_pct = std::clamp(val / 100.0f, 0.05f, 1.0f);
            }
        }
        dl->AddRectFilled(ImVec2(rx + 2.0f * s, bat_y + 2.0f * s), 
                          ImVec2(rx + 2.0f * s + (bat_w - 4.0f * s) * fill_pct, bat_y + bat_h - 2.0f * s), 
                          p.battery_low ? IM_COL32(0xE2, 0x3B, 0x3B, 255) : IM_COL32(0x46, 0xDC, 0x7D, 255), 1.5f * s);

        if (!p.battery.empty()) {
            const ImVec2 bsz = font->CalcTextSizeA(fs_sb * 0.85f, FLT_MAX, 0.f, p.battery.c_str());
            rx -= bsz.x + 6.0f * s;
            dl->AddText(font, fs_sb * 0.85f, ImVec2(rx, dy + 7.0f * s), bat_col, p.battery.c_str());
        }

        rx -= 20.0f * s;
        const float wifi_x = rx + 8.0f * s, wifi_y = dy + 18.0f * s;
        dl->AddCircleFilled(ImVec2(wifi_x, wifi_y - 2.0f * s), 1.5f * s, IM_COL32(0xE8, 0xEE, 0xFC, 255));
        dl->PathArcTo(ImVec2(wifi_x, wifi_y), 5.5f * s, -2.4f, -0.74f, 8);
        dl->PathStroke(IM_COL32(0xE8, 0xEE, 0xFC, 255), false, 1.5f * s);
        dl->PathArcTo(ImVec2(wifi_x, wifi_y), 9.0f * s, -2.4f, -0.74f, 8);
        dl->PathStroke(IM_COL32(0xE8, 0xEE, 0xFC, 255), false, 1.5f * s);

        rx -= 22.0f * s;
        if (!p.signal.empty()) {
            DrawSignalBars(dl, rx + 18.0f * s, dy + 8.0f * s, s * 0.65f, SignalBarsFromText(p.signal));
        } else {
            DrawSignalBars(dl, rx + 18.0f * s, dy + 8.0f * s, s * 0.65f, 4);
        }
    }

    // 8) Android 14+ Gesture Navigation Pill Bar (Hatasız ve düzgün AddLine / AddRectFilled)
    const float pill_h = 4.5f * s;
    const float pill_w = 80.0f * s;
    const float pill_x = dx + (device_w - pill_w) * 0.5f;
    const float pill_y = dy + device_h - 12.0f * s;
    if (p.show_gesture_pill) {
        dl->AddRectFilled(ImVec2(pill_x, pill_y), ImVec2(pill_x + pill_w, pill_y + pill_h), 
                          IM_COL32(0xF0, 0xF4, 0xFF, 190), pill_h * 0.5f);
    }

    if (p.cursor) {
        const float mx = p.cursor_x * W, my = p.cursor_y * H;
        const bool in_pill = (mx >= dx && mx <= dx + device_w && my >= dy + device_h - 26.0f * s && my <= dy + device_h + 5.0f * s);
        if (in_pill) {
            dl->AddRectFilled(ImVec2(pill_x - 6.0f * s, pill_y - 2.0f * s), 
                              ImVec2(pill_x + pill_w + 6.0f * s, pill_y + pill_h + 2.0f * s), 
                              IM_COL32(0xFF, 0xFF, 0xFF, 255), pill_h);
        }
    }

    // 9) İçerik Alanı
    const float content_y = dy + (p.show_status_bar ? sb_h + 4.0f * s : 10.0f * s);
    const float content_h = device_h - (p.show_status_bar ? sb_h + 4.0f * s : 10.0f * s) - 22.0f * s;
    const float content_x = dx + 6.0f * s;
    const float content_w = device_w - 12.0f * s;

    if (!p.web_url.empty() && p.web_url.rfind("bilgisayar://", 0) != 0 && p.web_url.rfind("chat://", 0) != 0) {
        const float geo[4] = {content_x, content_y, content_w, content_h};
        DrawWeb(dl, font, p, W, H, s, geo);
    }
}

// -----------------------------------------------------------------------------
// Dijital & Analog Hibrit Spor Araç Dashboard'u (Estetik Olarak Yenilendi)
// -----------------------------------------------------------------------------
void DrawDash(ImDrawList* dl, ImFont* font, const Dash& dash, float W, float H, float s) {
    const float dw = 320.f * s, dh = 115.f * s;
    const float dx = W - dw - 24.f * s, dy = H - dh - 24.f * s;

    // Yumuşak Panel Gövdesi
    dl->AddRectFilled(ImVec2(dx, dy), ImVec2(dx + dw, dy + dh), IM_COL32(10, 12, 18, 230), 16.f * s);
    dl->AddRect(ImVec2(dx, dy), ImVec2(dx + dw, dy + dh), IM_COL32(50, 70, 100, 180), 16.f * s, 0, 1.5f * s);

    // Kavisli Hız Kadranı
    const float cx = dx + 68.f * s, cy = dy + 62.f * s, r = 44.f * s;
    const float a0 = 2.35619f;       // 135 derece
    const float a1 = 5.49778f;       // 315 derece
    const float frac = std::clamp(dash.kmh / 280.f, 0.f, 1.f);

    // Arka plan rayı
    dl->PathArcTo(ImVec2(cx, cy), r, a0, a1, 32);
    dl->PathStroke(IM_COL32(30, 36, 48, 255), false, 5.0f * s);

    // Hıza göre renkli LED yay
    const ImU32 arc_col = dash.kmh < 120.f ? IM_COL32(70, 220, 125, 255) : 
                          (dash.kmh < 200.f ? IM_COL32(255, 205, 90, 255) : IM_COL32(255, 75, 75, 255));
    if (frac > 0.01f) {
        dl->PathArcTo(ImVec2(cx, cy), r, a0, a0 + (a1 - a0) * frac, 32);
        dl->PathStroke(arc_col, false, 5.5f * s);
    }

    // Dijital Hız Metni
    char spd[16];
    std::snprintf(spd, sizeof(spd), "%d", static_cast<int>(dash.kmh + 0.5f));
    const ImVec2 ssz = font->CalcTextSizeA(28.f * s, FLT_MAX, 0.f, spd);
    dl->AddText(font, 28.f * s, ImVec2(cx - ssz.x * 0.5f, cy - 18.f * s), IM_COL32(255, 255, 255, 255), spd);
    
    const char* unit_str = "KM/H";
    const ImVec2 usz = font->CalcTextSizeA(11.f * s, FLT_MAX, 0.f, unit_str);
    dl->AddText(font, 11.f * s, ImVec2(cx - usz.x * 0.5f, cy + 12.f * s), IM_COL32(150, 160, 185, 240), unit_str);

    // Sağ Bilgi Bölümü (KM Sayacı ve Yakıt)
    char kmb[32];
    std::snprintf(kmb, sizeof(kmb), "%.1f km", dash.km);
    dl->AddText(font, 18.f * s, ImVec2(dx + 130.f * s, dy + 20.f * s), IM_COL32(230, 238, 255, 255), kmb);

    // Yakıt Barı
    const float bx = dx + 130.f * s, bw = dw - 146.f * s, by = dy + 56.f * s, bh = 10.f * s;
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(30, 36, 48, 255), 5.f * s);

    const float fuel_frac = std::clamp(dash.fuel / 100.f, 0.f, 1.f);
    const ImU32 fc = dash.fuel < 15.f ? IM_COL32(245, 75, 75, 255) : 
                     (dash.fuel < 35.f ? IM_COL32(255, 195, 70, 255) : IM_COL32(70, 220, 125, 255));
    if (fuel_frac > 0.02f) {
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * fuel_frac, by + bh), fc, 5.f * s);
    }
    dl->AddRect(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(70, 80, 100, 160), 5.f * s, 0, 1.f * s);

    char fb[32];
    std::snprintf(fb, sizeof(fb), "Yakıt  %%%d", static_cast<int>(dash.fuel + 0.5f));
    dl->AddText(font, 12.f * s, ImVec2(bx, by + bh + 6.f * s), IM_COL32(160, 170, 195, 255), fb);
}

// -----------------------------------------------------------------------------
// Sağ Üst Kalıcı Durum Çubuğu (Estetik Pill Badge)
// -----------------------------------------------------------------------------
void DrawStatusBar(ImDrawList* dl, ImFont* font, const StatusBar& status, float W, float s) {
    if (status.clock.empty() && status.signal.empty() && status.battery.empty()) return;
    const float fs_sb = 18.f * s;
    const float ph = 34.f * s;
    
    // Sağ üstte şık, yarı saydam AMOLED kapsül
    float rx = W - 20.f * s;

    // Geçici ölçüm
    float total_w = 20.f * s;
    if (!status.clock.empty()) total_w += font->CalcTextSizeA(fs_sb, FLT_MAX, 0.f, status.clock.c_str()).x + 16.f * s;
    if (!status.battery.empty()) total_w += font->CalcTextSizeA(fs_sb, FLT_MAX, 0.f, status.battery.c_str()).x + 16.f * s;
    if (!status.signal.empty()) total_w += 32.f * s;

    const float pill_x0 = rx - total_w;
    const float pill_y0 = 14.f * s;

    dl->AddRectFilled(ImVec2(pill_x0, pill_y0), ImVec2(rx, pill_y0 + ph), IM_COL32(12, 15, 22, 210), ph * 0.5f);
    dl->AddRect(ImVec2(pill_x0, pill_y0), ImVec2(rx, pill_y0 + ph), IM_COL32(60, 75, 105, 180), ph * 0.5f, 0, 1.2f * s);

    float cx_cur = rx - 12.f * s;
    if (!status.signal.empty()) {
        const float bw_total = 4.f * (4.f + 2.5f) * s;
        cx_cur -= bw_total;
        DrawSignalBars(dl, cx_cur + bw_total, pill_y0 + 10.f * s, s * 0.7f, SignalBarsFromText(status.signal));
        cx_cur -= 10.f * s;
    }
    if (!status.battery.empty()) {
        const ImVec2 bsz = font->CalcTextSizeA(fs_sb, FLT_MAX, 0.f, status.battery.c_str());
        cx_cur -= bsz.x;
        dl->AddText(font, fs_sb, ImVec2(cx_cur, pill_y0 + 7.f * s), 
                    status.battery_low ? IM_COL32(255, 100, 100, 255) : IM_COL32(130, 225, 150, 255), status.battery.c_str());
        cx_cur -= 12.f * s;
    }
    if (!status.clock.empty()) {
        const ImVec2 csz = font->CalcTextSizeA(fs_sb, FLT_MAX, 0.f, status.clock.c_str());
        cx_cur -= csz.x;
        dl->AddText(font, fs_sb, ImVec2(cx_cur, pill_y0 + 7.f * s), IM_COL32(240, 245, 255, 255), status.clock.c_str());
    }
}

// -----------------------------------------------------------------------------
// DirectX11 Kurulum & Render Döngüsü
// -----------------------------------------------------------------------------
bool EnsureInit(IDXGISwapChain* sc) {
    if (g_init) return true;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_dev))) || !g_dev) {
        g_failed = true;
        return false;
    }
    g_dev->GetImmediateContext(&g_ctx);
    hikaye::hls::HLSPlayer::SetD3D11Device(g_dev, g_ctx);
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 28.0f);
    g_turkish = font != nullptr;
    if (!font) io.Fonts->AddFontDefault();
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    g_init = true;
    xpad::Arm();
    g_last = std::chrono::steady_clock::now();
    g_ready = true;
    Log(std::string("overlay: ImGui hazir (Turkce font: ") + (g_turkish ? "var" : "yok, ASCII'ye doner") + ")");
    return true;
}

void Render(IDXGISwapChain* sc) {
    xpad::TickSignal();
    if (!EnsureInit(sc)) return;

    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&bb))) || !bb) return;
    D3D11_TEXTURE2D_DESC desc{};
    bb->GetDesc(&desc);
    bb->Release();

    if (!g_main_rtv) {
        if (FAILED(g_dev->CreateRenderTargetView(bb, nullptr, &g_main_rtv)) || !g_main_rtv) {
            return;
        }
    }

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - g_last).count();
    g_last = now;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(desc.Width), static_cast<float>(desc.Height));
    io.DeltaTime = std::clamp(dt, 0.001f, 0.1f);

    std::vector<Line> lines;
    PhoneView phone;
    InputView input;
    MenuView menu;
    StatusBar status;
    Dash dash;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        lines = g_lines_cur;
        phone = g_phone_cur;
        input = g_input_cur;
        menu = g_menu_cur;
        status = g_status_cur;
        dash = g_dash_cur;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();
        const float W = io.DisplaySize.x, H = io.DisplaySize.y;
        const float s = H / 1080.f;

        const float size = 30.f * s;
        const float wrap = W * 0.62f;
        for (const auto& l : lines) {
            DrawCentered(dl, font, size, l.text, W * 0.5f, l.y * H, wrap, Col(l.r, l.g, l.b), true);
        }

        if (phone.open) {
            if (!phone.web_url.empty()) {
                if (phone.web_url.rfind("bilgisayar://", 0) == 0) {
                    DrawPc(dl, font, phone, W, H, s);
                } else if (phone.web_url.rfind("chat://", 0) == 0) {
                    DrawChat(dl, font, phone, W, H, s);
                } else if (phone.gtml_mode) {
                    DrawGTML(dl, font, phone, W, H, s);
                } else if (!phone.material_ui) {
                    DrawWeb(dl, font, phone, W, H, s);
                }
            }
            if (phone.material_ui) DrawMaterialFrame(dl, font, phone, W, H, s);
            if (phone.assistant) DrawAssistant(dl, font, phone, W, H, s);
            DrawCursor(dl, phone, W, H, s);
        }

        if (menu.open) DrawMenu(dl, font, menu, W, H, s);
        if (input.active) DrawInput(dl, font, input, W, H, s);
        if (dash.active) DrawDash(dl, font, dash, W, H, s);
        DrawStatusBar(dl, font, status, W, s);
        
        if (g_tv_cur.visible && !g_tv_cur.channel_info.empty()) {
            const float fs = 24.f * s;
            const float pad = 20.f * s;
            const ImVec2 tsz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, g_tv_cur.channel_info.c_str());
            const float x = (W - tsz.x) * 0.5f;
            const float y = 80.f * s;
            dl->AddRectFilled(ImVec2(x - pad, y - pad * 0.5f), ImVec2(x + tsz.x + pad, y + fs + pad * 0.5f), 
                              IM_COL32(0x0B, 0x0D, 0x11, 220), 12.f * s);
            dl->AddRect(ImVec2(x - pad, y - pad * 0.5f), ImVec2(x + tsz.x + pad, y + fs + pad * 0.5f), 
                        IM_COL32(0xA8, 0xC7, 0xFA, 255), 12.f * s, 0, 2.f * s);
            dl->AddText(font, fs, ImVec2(x, y), IM_COL32(0xE8, 0xEE, 0xFC, 255), g_tv_cur.channel_info.c_str());
        }
    }
    ImGui::Render();
    g_ctx->OMSetRenderTargets(1, &g_main_rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void OnPresent(void* swap_chain) {
    if (g_failed || !swap_chain) return;
    try {
        Render(static_cast<IDXGISwapChain*>(swap_chain));
    } catch (...) {
        g_failed = true;
        g_ready = false;
    }
}

}  // namespace

void Init() {
    xpad::Install();
    presentCallbackRegister(OnPresent);
}

void Shutdown() {
    presentCallbackUnregister(OnPresent);
    if (g_main_rtv) { g_main_rtv->Release(); g_main_rtv = nullptr; }
    g_failed = true;
    g_ready = false;
    g_init = false;
}

bool Ready() { return g_ready && !g_failed; }
bool HasTurkishFont() { return g_turkish; }

void Text(const std::string& utf8, float y, int r, int g, int b) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_lines_next.push_back({utf8, y, r, g, b});
}

void SetPhone(const PhoneView& view) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_phone_next = view;
}

void SetInput(const std::string& text, bool active) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_input_next.text = text;
    g_input_next.active = active;
}

void SetMenu(const MenuView& menu) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_menu_next = menu;
}

void SetStatusBar(const std::string& clock, const std::string& signal, const std::string& battery, bool battery_low) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_status_next.clock = clock;
    g_status_next.signal = signal;
    g_status_next.battery = battery;
    g_status_next.battery_low = battery_low;
}

void SetDash(float kmh, float km, float fuel, bool active) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_dash_next.kmh = kmh;
    g_dash_next.km = km;
    g_dash_next.fuel = fuel;
    g_dash_next.active = active;
}

void SetTVVisible(bool visible, const std::string& channel_info) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_tv_next.visible = visible;
    g_tv_next.channel_info = channel_info;
}

void Commit() {
    std::lock_guard<std::mutex> lock(g_mu);
    g_lines_cur = std::move(g_lines_next);
    g_lines_next.clear();
    g_phone_cur = g_phone_next;
    g_menu_cur = g_menu_next;
    g_input_cur = g_input_next;
    g_status_cur = g_status_next;
    g_dash_cur = g_dash_next;
    g_tv_cur = g_tv_next;
}

int HoverItem() { return g_hover.load(); }

int TakeWebClick() { return g_click.exchange(-1); }

}  // namespace hikaye::overlay