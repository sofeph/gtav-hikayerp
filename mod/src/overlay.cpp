#include <atomic>
#include "overlay.h"

#include <d3d11.h>
#include <wincodec.h>
#include "videocover.h"
#include "xpad.h"
#include <dxgi.h>

#include <algorithm>
#include <cmath>
#include <atomic>
#include <chrono>
#include <mutex>

#include "common.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"

#pragma comment(lib, "windowscodecs.lib")

namespace hikaye::overlay {

namespace {

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

std::atomic<bool> g_ready{false};
std::atomic<bool> g_failed{false};
std::atomic<bool> g_turkish{false};

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
bool g_init = false;
std::chrono::steady_clock::time_point g_last;

ImU32 Col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

// Metni ortalayarak (isteğe bağlı kenarlıkla) çizer. wrap: satır kaydırma genişliği (piksel).
ImVec2 DrawCentered(ImDrawList* dl, ImFont* font, float size, const std::string& text, float cx, float y, float wrap,
                    ImU32 color, bool outline) {
    const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, wrap, text.c_str());
    const ImVec2 pos(cx - sz.x * 0.5f, y);
    if (outline) {
        const float o = std::max(1.f, size / 18.f);
        for (const ImVec2 d : {ImVec2(-o, 0), ImVec2(o, 0), ImVec2(0, -o), ImVec2(0, o)}) {
            dl->AddText(font, size, ImVec2(pos.x + d.x, pos.y + d.y), IM_COL32(0, 0, 0, 230), text.c_str(), nullptr, wrap);
        }
    }
    dl->AddText(font, size, pos, color, text.c_str(), nullptr, wrap);
    return sz;
}

ImU32 ToneColor(int tone) {
    switch (tone) {
        case 1: return Col(255, 205, 90);
        case 2: return Col(120, 220, 140);
        case 3: return Col(255, 120, 120);
        default: return Col(225, 228, 235);
    }
}

ImU32 Rgb(int c, int a = 255) { return Col((c >> 16) & 255, (c >> 8) & 255, c & 255, a); }

// Telefon tarayıcısı: adres çubuğu + kaydırmalı sayfa. Seçili öğe her zaman görünür kalır (üstten kırpılır).
void DrawWeb(ImDrawList* dl, ImFont* font, const PhoneView& p, float W, float H, float s, const float* geo = nullptr) {
    float pw = 460.f * s, pad = 18.f * s;
    float x = W - pw - 36.f * s;
    float y = 24.f * s, h = H - 48.f * s;
    if (geo) {  // bilgisayar penceresinin içeriği: verilen dikdörtgen (x, y, genişlik, yükseklik)
        x = geo[0]; y = geo[1]; pw = geo[2]; h = geo[3]; pad = 12.f * s;
    }
    const float inner = pw - 2 * pad;
    const ImU32 page_col = p.web_color >= 0 ? Rgb(p.web_color) : Col(225, 228, 235);
    const ImU32 accent = p.web_accent >= 0 ? Rgb(p.web_accent) : Col(120, 180, 255);
    const float fs = 22.f * s, fs_small = 18.f * s;
    if (!geo) {
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + pw, y + h), p.web_bg >= 0 ? Rgb(p.web_bg, 248) : Col(18, 20, 28, 242), 26.f * s);
    dl->AddRect(ImVec2(x, y), ImVec2(x + pw, y + h), p.web_accent >= 0 ? accent : Col(90, 100, 130, 255), 26.f * s, 0, 2.f * s);
    // adres çubuğu
    dl->AddRectFilled(ImVec2(x + pad, y + pad), ImVec2(x + pw - pad, y + pad + 36.f * s), Col(40, 46, 62), 18.f * s);
    dl->AddText(font, fs_small, ImVec2(x + pad + 14.f * s, y + pad + 8.f * s), Col(150, 200, 255), p.web_url.c_str());
    const ImVec2 csz = font->CalcTextSizeA(fs_small, FLT_MAX, 0.f, p.clock.c_str());
    dl->AddText(font, fs_small, ImVec2(x + pw - pad - 12.f * s - csz.x, y + pad + 8.f * s), Col(200, 205, 220), p.clock.c_str());
    float right_edge = x + pw - pad - 12.f * s - csz.x;  // saat sol kenarı
    if (!p.battery.empty()) {  // pil: saatin solunda
        const ImVec2 bsz = font->CalcTextSizeA(fs_small, FLT_MAX, 0.f, p.battery.c_str());
        right_edge -= 16.f * s + bsz.x;
        dl->AddText(font, fs_small, ImVec2(right_edge, y + pad + 8.f * s), p.battery_low ? Col(255, 110, 110) : Col(130, 225, 150), p.battery.c_str());
    }
    if (!p.signal.empty()) {  // sinyal çubukları: pilin solunda (her sayfada üst çubuk)
        const ImVec2 ssz = font->CalcTextSizeA(fs_small, FLT_MAX, 0.f, p.signal.c_str());
        right_edge -= 12.f * s + ssz.x;
        const int bars = static_cast<int>(std::count_if(p.signal.begin(), p.signal.end(), [](char c) { return c != '\xB0'; }));  // ░ dışı karakter sayısı kabaca çubuk
        dl->AddText(font, fs_small, ImVec2(right_edge, y + pad + 8.f * s), bars <= 1 ? Col(255, 120, 120) : Col(150, 220, 255), p.signal.c_str());
    }
    }
    const float top = geo ? y + pad : y + pad + 48.f * s, bottom = geo ? y + h - pad : y + h - 36.f * s;
    const float avail = bottom - top;
    if (!p.notice.empty()) dl->AddText(font, fs_small, ImVec2(x + pad, bottom - 24.f * s), Col(255, 205, 90), p.notice.c_str());

    auto size_of = [&](const WebItem& w) {
        if (w.size > 0.f) return w.size * s;
        return w.kind == 1 ? 32.f * s : w.kind == 2 ? 26.f * s : w.kind == 5 ? 23.f * s : fs;
    };
    // kutucuk ızgarası: ardışık Tile öğeleri 3'lü satırlar oluşturur
    const float tgap = 10.f * s, tile_w = (inner - 2.f * tgap) / 3.f, tile_h = tile_w * 0.92f;
    std::vector<int> tcol(p.web_items.size(), -1);
    {
        int run = -1;
        for (size_t i = 0; i < p.web_items.size(); ++i) {
            if (p.web_items[i].kind == 9) {
                run = run < 0 ? 0 : run + 1;
                tcol[i] = run % 3;
            } else {
                run = -1;
            }
        }
    }
    std::vector<float> rects(p.web_items.size() * 4, 0.f);  // fare isabeti: x, y, w, h
    // her öğenin yüksekliği
    std::vector<float> hs(p.web_items.size());
    for (size_t i = 0; i < p.web_items.size(); ++i) {
        const WebItem& it = p.web_items[i];
        const float wrap = inner - (it.kind == 5 || it.kind == 6 ? 2.f * (it.pad >= 0.f ? it.pad : 14.f) * s : 0.f);
        float v = 0.f;
        switch (it.kind) {
            case 3: v = 16.f * s; break;
            case 9: v = tcol[i] == 0 ? tile_h + tgap : 0.f; break;
            case 8: v = it.gw > 0.f ? it.gh * (geo ? std::min(inner / it.gw, (bottom - top - 12.f * s) / it.gh) : inner / it.gw) + 10.f * s : 0.f; break;
            case 5: v = 16.f * s + font->CalcTextSizeA(size_of(it), FLT_MAX, wrap, it.text.c_str()).y +
                        (it.sub.empty() ? 0.f : 4.f * s + font->CalcTextSizeA(fs_small, FLT_MAX, wrap, it.sub.c_str()).y) + 8.f * s; break;
            case 6: v = 14.f * s + fs_small + 4.f * s + font->CalcTextSizeA(size_of(it), FLT_MAX, wrap, it.text.c_str()).y + 6.f * s; break;
            default: v = font->CalcTextSizeA(size_of(it), FLT_MAX, wrap, it.text.c_str()).y + (it.kind == 1 ? 10.f : 8.f) * s; break;
        }
        hs[i] = v + (it.kind == 5 || it.kind == 6 ? 8.f * s : 0.f);
    }
    // seçili öğe görünsün: ondan geriye doğru sığdır
    size_t first = 0;
    if (p.web_selected >= 0 && p.web_selected < static_cast<int>(hs.size())) {
        float used = 0.f;
        for (int i = 0; i <= p.web_selected; ++i) used += hs[static_cast<size_t>(i)];
        while (used > avail && first < static_cast<size_t>(p.web_selected)) used -= hs[first++];
    }
    dl->PushClipRect(ImVec2(x, top), ImVec2(x + pw, bottom - (p.notice.empty() ? 0.f : 26.f * s)), true);
    float cy = top;
    // Hizalama: tek satıra sığan metni sola/ortaya/sağa yaslar (kaydırmalı çok satırlı metin sola yaslı kalır).
    auto aligned_x = [&](const WebItem& it, const std::string& txt, float sz, float left, float width) {
        if (it.align == 0) return left;
        const float tw = font->CalcTextSizeA(sz, FLT_MAX, 0.f, txt.c_str()).x;
        if (tw >= width) return left;
        return it.align == 1 ? left + (width - tw) * 0.5f : left + (width - tw);
    };
    for (size_t i = first; i < p.web_items.size() && cy < bottom; ++i) {
        const WebItem& it = p.web_items[i];
        const bool sel = static_cast<int>(i) == p.web_selected;
        const float ix = x + pad;
        const float sz = size_of(it);
        const float rad = (it.radius >= 0.f ? it.radius : 12.f) * s;
        const float ipad = (it.pad >= 0.f ? it.pad : 14.f) * s;
        auto text_col = [&](ImU32 fallback) { return it.color >= 0 ? Rgb(it.color) : fallback; };
        if (it.kind == 9) {
            const float tx0 = ix + static_cast<float>(tcol[i]) * (tile_w + tgap);
            rects[i * 4] = tx0; rects[i * 4 + 1] = cy; rects[i * 4 + 2] = tile_w; rects[i * 4 + 3] = tile_h;
        } else if (it.selectable) {
            rects[i * 4] = ix - 6.f * s; rects[i * 4 + 1] = cy; rects[i * 4 + 2] = inner + 12.f * s; rects[i * 4 + 3] = hs[i];
        }
        switch (it.kind) {
            case 9: {  // uygulama kutucuğu
                const float tx0 = ix + static_cast<float>(tcol[i]) * (tile_w + tgap);
                const ImU32 base = it.bg >= 0 ? Rgb(it.bg) : Col(60, 90, 160);
                dl->AddRectFilled(ImVec2(tx0, cy), ImVec2(tx0 + tile_w, cy + tile_h), base, 18.f * s);
                if (sel) dl->AddRect(ImVec2(tx0 - 2.f * s, cy - 2.f * s), ImVec2(tx0 + tile_w + 2.f * s, cy + tile_h + 2.f * s), Col(255, 255, 255), 20.f * s, 0, 3.f * s);
                const float isz = 46.f * s;
                const ImVec2 iw = font->CalcTextSizeA(isz, FLT_MAX, 0.f, it.sub.c_str());
                dl->AddText(font, isz, ImVec2(tx0 + (tile_w - iw.x) * 0.5f, cy + tile_h * 0.14f), Col(255, 255, 255), it.sub.c_str());
                const float lsz = 17.f * s;
                const ImVec2 lw = font->CalcTextSizeA(lsz, FLT_MAX, tile_w - 10.f * s, it.text.c_str());
                dl->AddText(font, lsz, ImVec2(tx0 + (tile_w - lw.x) * 0.5f, cy + tile_h * 0.66f), Col(255, 255, 255), it.text.c_str(), nullptr, tile_w - 10.f * s);
                break;
            }
            case 1:
                dl->AddText(font, sz, ImVec2(aligned_x(it, it.text, sz, ix, inner), cy), text_col(Col(255, 255, 255)), it.text.c_str(), nullptr, inner);
                break;
            case 2:
                dl->AddText(font, sz, ImVec2(aligned_x(it, it.text, sz, ix, inner), cy), text_col(p.web_accent >= 0 ? accent : Col(190, 210, 255)), it.text.c_str(), nullptr, inner);
                break;
            case 3: dl->AddLine(ImVec2(ix, cy + 7.f * s), ImVec2(ix + inner, cy + 7.f * s), it.color >= 0 ? Rgb(it.color) : Col(70, 78, 100), 1.5f * s); break;
            case 4: {
                if (sel) dl->AddRectFilled(ImVec2(ix - 6.f * s, cy - 2.f * s), ImVec2(ix + inner + 6.f * s, cy + hs[i] - 4.f * s), it.bg >= 0 ? Rgb(it.bg) : Col(52, 70, 120), 10.f * s);
                const std::string txt = std::string("› ") + it.text;
                dl->AddText(font, sz, ImVec2(aligned_x(it, txt, sz, ix, inner), cy), sel ? Col(255, 255, 255) : text_col(accent), txt.c_str(), nullptr, inner);
                break;
            }
            case 5: {
                const float wrap = inner - 2 * ipad;
                dl->AddRectFilled(ImVec2(ix, cy), ImVec2(ix + inner, cy + hs[i] - 8.f * s), sel ? Col(52, 70, 120) : (it.bg >= 0 ? Rgb(it.bg) : Col(32, 37, 52)), rad);
                if (it.border >= 0 || sel) dl->AddRect(ImVec2(ix, cy), ImVec2(ix + inner, cy + hs[i] - 8.f * s), sel ? accent : Rgb(it.border), rad, 0, 1.5f * s);
                float ty = cy + 8.f * s;
                dl->AddText(font, sz, ImVec2(ix + ipad, ty), text_col(Col(255, 255, 255)), it.text.c_str(), nullptr, wrap);
                if (!it.meta.empty()) {
                    const ImVec2 m = font->CalcTextSizeA(fs_small, FLT_MAX, 0.f, it.meta.c_str());
                    dl->AddText(font, fs_small, ImVec2(ix + inner - 12.f * s - m.x, ty + 2.f * s), p.web_accent >= 0 ? accent : Col(130, 225, 150), it.meta.c_str());
                }
                ty += font->CalcTextSizeA(sz, FLT_MAX, wrap, it.text.c_str()).y + 4.f * s;
                if (!it.sub.empty()) dl->AddText(font, fs_small, ImVec2(ix + ipad, ty), Col(185, 190, 205), it.sub.c_str(), nullptr, wrap);
                break;
            }
            case 6: {
                const float wrap = inner - 2 * ipad;
                dl->AddRectFilled(ImVec2(ix, cy), ImVec2(ix + inner, cy + hs[i] - 8.f * s), sel ? Col(52, 70, 120) : (it.bg >= 0 ? Rgb(it.bg) : Col(32, 37, 52)), rad);
                if (it.border >= 0 || sel) dl->AddRect(ImVec2(ix, cy), ImVec2(ix + inner, cy + hs[i] - 8.f * s), sel ? accent : Rgb(it.border), rad, 0, 1.5f * s);
                float ty = cy + 8.f * s;
                dl->AddText(font, fs_small, ImVec2(ix + ipad, ty), text_col(Col(255, 255, 255)), it.meta.c_str());
                const ImVec2 nm = font->CalcTextSizeA(fs_small, FLT_MAX, 0.f, it.meta.c_str());
                dl->AddText(font, fs_small, ImVec2(ix + ipad + nm.x + 8.f * s, ty), Col(130, 140, 165), it.sub.c_str());
                ty += fs_small + 4.f * s;
                dl->AddText(font, sz, ImVec2(ix + ipad, ty), Col(228, 232, 240), it.text.c_str(), nullptr, wrap);
                break;
            }
            case 8: {  // vektör çizim: svg alt kümesi ve grafikler; alana sığacak şekilde ölçeklenir
                if (it.gw <= 0.f || it.gh <= 0.f) break;
                const float k = geo ? std::min(inner / it.gw, (bottom - top - 12.f * s) / it.gh) : inner / it.gw;
                const ImVec2 o(ix + (geo ? (inner - it.gw * k) * 0.5f : 0.f), cy);
                auto P = [&](float px, float py) { return ImVec2(o.x + px * k, o.y + py * k); };
                if (it.gbg >= 0) dl->AddRectFilled(o, P(it.gw, it.gh), Rgb(it.gbg), rad);
                if (it.border >= 0) dl->AddRect(o, P(it.gw, it.gh), Rgb(it.border), rad, 0, 1.5f * s);
                dl->PushClipRect(o, P(it.gw, it.gh), true);
                for (const auto& sh : it.shapes) {
                    const float sw = std::max(1.f, sh.sw * k);
                    switch (sh.type) {
                        case 0:
                            if (sh.fill >= 0) dl->AddRectFilled(P(sh.x, sh.y), P(sh.x + sh.w, sh.y + sh.h), Rgb(sh.fill), sh.r * k);
                            if (sh.stroke >= 0) dl->AddRect(P(sh.x, sh.y), P(sh.x + sh.w, sh.y + sh.h), Rgb(sh.stroke), sh.r * k, 0, sw);
                            break;
                        case 1:
                            if (sh.fill >= 0) dl->AddCircleFilled(P(sh.x, sh.y), sh.r * k, Rgb(sh.fill), 32);
                            if (sh.stroke >= 0) dl->AddCircle(P(sh.x, sh.y), sh.r * k, Rgb(sh.stroke), 32, sw);
                            break;
                        case 2:
                            if (sh.stroke >= 0) dl->AddLine(P(sh.x, sh.y), P(sh.w, sh.h), Rgb(sh.stroke), sw);
                            break;
                        case 3:
                        case 4: {
                            std::vector<ImVec2> pts;
                            for (const auto& q : sh.pts) pts.push_back(P(q.first, q.second));
                            if (pts.size() < 2) break;
                            if (sh.type == 4 && sh.fill >= 0 && pts.size() >= 3) {
                                // dışbükey olmayan çokgenler için üçgen yelpazesi (merkez = ilk nokta; pasta dilimleri bu yapıda)
                                for (size_t q = 1; q + 1 < pts.size(); ++q) dl->AddTriangleFilled(pts[0], pts[q], pts[q + 1], Rgb(sh.fill));
                            }
                            if (sh.stroke >= 0) dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), Rgb(sh.stroke), sh.type == 4 ? ImDrawFlags_Closed : ImDrawFlags_None, sw);
                            break;
                        }
                        case 5: {
                            const float tsz = std::max(8.f, sh.size * k);
                            float tx = o.x + sh.x * k;
                            const float tw = font->CalcTextSizeA(tsz, FLT_MAX, 0.f, sh.text.c_str()).x;
                            if (sh.align == 1) tx -= tw * 0.5f;
                            else if (sh.align == 2) tx -= tw;
                            dl->AddText(font, tsz, ImVec2(tx, o.y + sh.y * k), sh.fill >= 0 ? Rgb(sh.fill) : Col(255, 255, 255), sh.text.c_str());
                            break;
                        }
                        default: break;
                    }
                }
                dl->PopClipRect();
                break;
            }
            case 7: {
                const std::string txt = std::string("• ") + it.text;
                dl->AddText(font, sz, ImVec2(aligned_x(it, txt, sz, ix, inner), cy), text_col(page_col), txt.c_str(), nullptr, inner);
                break;
            }
            default:
                dl->AddText(font, sz, ImVec2(aligned_x(it, it.text, sz, ix, inner), cy), text_col(page_col), it.text.c_str(), nullptr, inner);
                break;
        }
        cy += hs[i];
    }
    dl->PopClipRect();
    // fare: imlecin altındaki seçilebilir öğe; sol tıklama script thread'ine bildirilir
    if (p.cursor) {
        const float mx = p.cursor_x * W, my = p.cursor_y * H;
        int hit = -1;
        for (size_t i = first; i < p.web_items.size(); ++i) {
            if (rects[i * 4 + 2] <= 0.f) continue;
            if (mx >= rects[i * 4] && mx <= rects[i * 4] + rects[i * 4 + 2] && my >= rects[i * 4 + 1] && my <= rects[i * 4 + 1] + rects[i * 4 + 3]) hit = static_cast<int>(i);
        }
        g_hover = hit;
        if (p.click_seq != g_seen_click_seq) {
            g_seen_click_seq = p.click_seq;
            if (hit >= 0) g_click = hit;
        }
    } else {
        g_hover = -1;
    }
    if (!geo) dl->AddText(font, fs_small * 0.9f, ImVec2(x + pad, y + h - 28.f * s), Col(140, 148, 170), p.hint.c_str());
}

// Bilgisayar: 16:9 ekranda Windows benzeri ama FACADE sistemi: duvar kağıdı, masaüstü simgeleri, görev çubuğu (Facade düğmesi, saat) ve
// pencereli uygulamalar. Fare isabeti render thread'inde hesaplanır: simge/öğe tıklaması g_click, pencere kapat -3, Facade düğmesi -4.
void DrawPc(ImDrawList* dl, ImFont* font, const PhoneView& p, float W, float H, float s) {
    const float sw = std::min(W * 0.70f, H * 0.86f * 16.f / 9.f), sh = sw * 9.f / 16.f;
    const float sx = (W - sw) * 0.5f, sy = (H - sh) * 0.5f;
    const float tb_h = 38.f * s;
    // gövde + ekran
    dl->AddRectFilled(ImVec2(sx - 14.f * s, sy - 14.f * s), ImVec2(sx + sw + 14.f * s, sy + sh + 14.f * s), Col(12, 14, 20, 255), 14.f * s);
    dl->AddRectFilledMultiColor(ImVec2(sx, sy), ImVec2(sx + sw, sy + sh), Col(24, 62, 128), Col(52, 38, 110), Col(14, 28, 60), Col(12, 44, 92));
    const float lsz = 58.f * s;
    const char* brand = "Facade";
    const ImVec2 bsz = font->CalcTextSizeA(lsz, FLT_MAX, 0.f, brand);
    dl->AddText(font, lsz, ImVec2(sx + sw - bsz.x - 26.f * s, sy + sh - tb_h - bsz.y - 18.f * s), Col(255, 255, 255, 46), brand);
    const float mx = p.cursor_x * W, my = p.cursor_y * H;
    bool click_new = p.cursor && p.click_seq != g_seen_click_seq;
    const auto inside = [&](float x0, float y0, float x1, float y1) { return p.cursor && mx >= x0 && mx <= x1 && my >= y0 && my <= y1; };
    const bool desktop = p.web_url == "bilgisayar://home";
    int hover = -1;

    if (desktop) {  // masaüstü simgeleri: sol sütun, yukarıdan aşağı
        const float isz = 60.f * s, cell_h = isz + 40.f * s;
        float cx = sx + 22.f * s, cy = sy + 18.f * s;
        for (size_t i = 0; i < p.web_items.size(); ++i) {
            const WebItem& it = p.web_items[i];
            if (it.kind != 9) continue;
            if (cy + cell_h > sy + sh - tb_h) {  // sütun doldu: yandaki sütuna
                cy = sy + 18.f * s;
                cx += isz + 44.f * s;
            }
            const bool sel = static_cast<int>(i) == p.web_selected;
            const bool over = inside(cx - 10.f * s, cy - 6.f * s, cx + isz + 10.f * s, cy + cell_h - 6.f * s);
            if (over) hover = static_cast<int>(i);
            if (sel || over) dl->AddRectFilled(ImVec2(cx - 10.f * s, cy - 6.f * s), ImVec2(cx + isz + 10.f * s, cy + cell_h - 6.f * s), Col(255, 255, 255, sel ? 70 : 40), 8.f * s);
            dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + isz, cy + isz), it.bg >= 0 ? Rgb(it.bg) : Col(60, 90, 160), 12.f * s);
            const float gsz = 36.f * s;
            const ImVec2 gw = font->CalcTextSizeA(gsz, FLT_MAX, 0.f, it.sub.c_str());
            dl->AddText(font, gsz, ImVec2(cx + (isz - gw.x) * 0.5f, cy + (isz - gsz) * 0.5f - 2.f * s), Col(255, 255, 255), it.sub.c_str());
            const float tsz = 15.f * s;
            const ImVec2 tw = font->CalcTextSizeA(tsz, FLT_MAX, isz + 30.f * s, it.text.c_str());
            dl->AddText(font, tsz, ImVec2(cx + (isz - tw.x) * 0.5f, cy + isz + 6.f * s), Col(255, 255, 255), it.text.c_str(), nullptr, isz + 30.f * s);
            cy += cell_h;
        }
    } else {  // uygulama penceresi
        const float wx = sx + sw * 0.05f, wy = sy + sh * 0.06f, ww = sw * 0.90f, wh = sh - tb_h - sh * 0.10f;
        const float title_h = 30.f * s;
        dl->AddRectFilled(ImVec2(wx + 4.f * s, wy + 5.f * s), ImVec2(wx + ww + 4.f * s, wy + wh + 5.f * s), Col(0, 0, 0, 70), 8.f * s);  // gölge
        dl->AddRectFilled(ImVec2(wx, wy), ImVec2(wx + ww, wy + wh), Col(20, 24, 36, 255), 8.f * s);
        dl->AddRectFilledMultiColor(ImVec2(wx, wy), ImVec2(wx + ww, wy + title_h), Col(44, 84, 160), Col(60, 70, 150), Col(52, 62, 130), Col(36, 72, 140));
        dl->AddText(font, 17.f * s, ImVec2(wx + 12.f * s, wy + 5.f * s), Col(255, 255, 255), p.web_title.empty() ? p.web_url.c_str() : p.web_title.c_str());
        const float cb = 26.f * s;
        const float cx0 = wx + ww - cb - 4.f * s, cy0 = wy + 2.f * s;
        const bool over_close = inside(cx0, cy0, cx0 + cb, cy0 + cb);
        dl->AddRectFilled(ImVec2(cx0, cy0), ImVec2(cx0 + cb, cy0 + cb), over_close ? Col(220, 60, 60) : Col(150, 50, 50), 5.f * s);
        dl->AddText(font, 17.f * s, ImVec2(cx0 + 8.f * s, cy0 + 3.f * s), Col(255, 255, 255), "x");
        if (click_new && over_close) {  // pencereyi kapat = geri / masaüstü
            g_click = -3;
            g_seen_click_seq = p.click_seq;
            click_new = false;
        }
        const float geo[4] = {wx + 2.f * s, wy + title_h, ww - 4.f * s, wh - title_h - 2.f * s};
        dl->AddRect(ImVec2(wx, wy), ImVec2(wx + ww, wy + wh), Col(90, 110, 170), 8.f * s, 0, 1.5f * s);
        DrawWeb(dl, font, p, W, H, s, geo);
    }

    // görev çubuğu
    dl->AddRectFilled(ImVec2(sx, sy + sh - tb_h), ImVec2(sx + sw, sy + sh), Col(14, 18, 30, 235));
    const float sb_w = 108.f * s;
    const bool over_start = inside(sx + 6.f * s, sy + sh - tb_h + 4.f * s, sx + 6.f * s + sb_w, sy + sh - 4.f * s);
    dl->AddRectFilled(ImVec2(sx + 6.f * s, sy + sh - tb_h + 4.f * s), ImVec2(sx + 6.f * s + sb_w, sy + sh - 4.f * s), over_start ? Col(70, 120, 220) : Col(44, 84, 160), 6.f * s);
    dl->AddText(font, 19.f * s, ImVec2(sx + 18.f * s, sy + sh - tb_h + 8.f * s), Col(255, 255, 255), "Facade");
    if (!desktop) {  // açık uygulama düğmesi
        const std::string ttl = p.web_title.empty() ? p.web_url : p.web_title;
        dl->AddRectFilled(ImVec2(sx + sb_w + 16.f * s, sy + sh - tb_h + 5.f * s), ImVec2(sx + sb_w + 190.f * s, sy + sh - 5.f * s), Col(52, 62, 96), 5.f * s);
        dl->AddText(font, 16.f * s, ImVec2(sx + sb_w + 26.f * s, sy + sh - tb_h + 10.f * s), Col(230, 236, 255), ttl.c_str(), nullptr, 150.f * s);
    }
    const ImVec2 csz = font->CalcTextSizeA(17.f * s, FLT_MAX, 0.f, p.clock.c_str());
    dl->AddText(font, 17.f * s, ImVec2(sx + sw - csz.x - 14.f * s, sy + sh - tb_h + 9.f * s), Col(230, 236, 255), p.clock.c_str());
    if (!p.signal.empty()) {  // görev çubuğunda saat solunda sinyal çubukları
        const ImVec2 ssz = font->CalcTextSizeA(17.f * s, FLT_MAX, 0.f, p.signal.c_str());
        dl->AddText(font, 17.f * s, ImVec2(sx + sw - csz.x - 26.f * s - ssz.x, sy + sh - tb_h + 9.f * s), Col(150, 220, 255), p.signal.c_str());
    }
    if (click_new && over_start) {
        g_click = -4;  // Facade düğmesi: masaüstüne dön
        g_seen_click_seq = p.click_seq;
        click_new = false;
    }
    if (desktop) {
        g_hover = hover;
        if (click_new) {
            g_seen_click_seq = p.click_seq;
            if (hover >= 0) g_click = hover;
        }
    }
}

// Arama: SMS benzeri mesaj dizisi (eski telefon liste arayüzü kaldırıldı; ana ekran ve uygulamalar telefon tarayıcısındadır)
void DrawChat(ImDrawList* dl, ImFont* font, const PhoneView& p, float W, float H, float s) {
    const float pw = 430.f * s, pad = 16.f * s;
    const float x = W - pw - 36.f * s;
    const float fs = 20.f * s, fs_small = 16.f * s;
    const float wrap = (pw - 2 * pad) * 0.72f;
    // balonlar (en yeni altta): sığanlar sondan geriye
    struct B { bool mine; std::string text; float h; };
    std::vector<B> bubbles;
    for (const auto& c : p.chat) bubbles.push_back({c.mine, c.text, 0.f});
    const char* state = p.listening ? "..." : (p.typing ? "yazıyorsun..." : "");
    if (p.thinking) bubbles.push_back({false, std::string("düşünüyor") + std::string(1 + static_cast<size_t>(ImGui::GetTime() * 2.5) % 3, '.'), 0.f});
    else if (p.speaking) bubbles.push_back({false, "...", 0.f});
    else if (p.listening) bubbles.push_back({true, "...", 0.f});
    else if (p.typing) bubbles.push_back({true, "yazıyorsun...", 0.f});
    (void)state;
    const float max_h = H * 0.7f;
    float used = 0.f;
    size_t first = bubbles.size();
    for (size_t i = bubbles.size(); i-- > 0;) {
        bubbles[i].h = font->CalcTextSizeA(fs, FLT_MAX, wrap, bubbles[i].text.c_str()).y + 20.f * s;
        if (used + bubbles[i].h + 8.f * s > max_h - 110.f * s) break;
        used += bubbles[i].h + 8.f * s;
        first = i;
    }
    const float h = pad + 56.f * s + used + 40.f * s;
    const float y = std::max(24.f * s, (H - h) * 0.5f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + pw, y + h), Col(14, 16, 24, 244), 26.f * s);
    dl->AddRect(ImVec2(x, y), ImVec2(x + pw, y + h), Col(90, 100, 130, 255), 26.f * s, 0, 2.f * s);
    // başlık: kişi adı + saat
    dl->AddText(font, fs * 1.1f, ImVec2(x + pad, y + pad), Col(255, 255, 255), p.caller.c_str());
    const ImVec2 csz = font->CalcTextSizeA(fs_small, FLT_MAX, 0.f, p.clock.c_str());
    dl->AddText(font, fs_small, ImVec2(x + pw - pad - csz.x, y + pad + 4.f * s), Col(200, 205, 220), p.clock.c_str());
    if (!p.signal.empty()) {  // arama ekranında başlıkta saat solunda sinyal
        const ImVec2 ssz = font->CalcTextSizeA(fs_small, FLT_MAX, 0.f, p.signal.c_str());
        dl->AddText(font, fs_small, ImVec2(x + pw - pad - csz.x - 12.f * s - ssz.x, y + pad + 4.f * s), Col(150, 220, 255), p.signal.c_str());
    }
    dl->AddText(font, fs_small, ImVec2(x + pad, y + pad + 30.f * s), Col(120, 220, 140), "Mesajlar");
    dl->AddLine(ImVec2(x + pad, y + pad + 54.f * s), ImVec2(x + pw - pad, y + pad + 54.f * s), Col(60, 68, 90), 1.5f * s);
    float cy = y + pad + 66.f * s;
    for (size_t i = first; i < bubbles.size(); ++i) {
        const B& b = bubbles[i];
        const ImVec2 tsz = font->CalcTextSizeA(fs, FLT_MAX, wrap, b.text.c_str());
        const float bw = tsz.x + 24.f * s;
        const float bx = b.mine ? x + pw - pad - bw : x + pad;
        dl->AddRectFilled(ImVec2(bx, cy), ImVec2(bx + bw, cy + b.h), b.mine ? Col(46, 104, 210) : Col(48, 54, 72), 16.f * s);
        dl->AddText(font, fs, ImVec2(bx + 12.f * s, cy + 10.f * s), Col(240, 244, 255), b.text.c_str(), nullptr, wrap);
        cy += b.h + 8.f * s;
    }
    if (!p.notice.empty()) dl->AddText(font, fs_small, ImVec2(x + pad, y + h - 54.f * s), Col(255, 205, 90), p.notice.c_str());
    dl->AddText(font, fs_small * 0.9f, ImVec2(x + pad, y + h - 28.f * s), Col(140, 148, 170), p.hint.c_str());
}

// Fare imleci: oyunun kendi imleci bizim ekranın ALTINDA kalıyordu (görünmüyordu): okumuz en üstte çizilir
void DrawCursor(ImDrawList* dl, const PhoneView& p, float W, float H, float s) {
    if (!p.cursor) return;
    const float x = p.cursor_x * W, y = p.cursor_y * H, k = 22.f * s;
    const ImVec2 a(x, y), b(x, y + k), c(x + k * 0.62f, y + k * 0.78f);
    dl->AddTriangleFilled(a, b, c, IM_COL32(255, 255, 255, 255));
    dl->AddTriangle(a, b, c, IM_COL32(0, 0, 0, 255), 2.f * s);
}

// Haviland: sesli asistan arayüzü. Üstte duruma göre renk değiştiren/nabız atan ışık, altında son sözün büyük yazısı (SMS balonu yok).
void DrawAssistant(ImDrawList* dl, ImFont* font, const PhoneView& p, float W, float H, float s) {
    const float pw = 420.f * s, x = W - pw - 36.f * s, h = std::min(H * 0.62f, 560.f * s), y = std::max(24.f * s, (H - h) * 0.5f);
    dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + pw, y + h), Col(18, 14, 38, 244), Col(18, 14, 38, 244), Col(8, 8, 20, 244), Col(8, 8, 20, 244));
    dl->AddRect(ImVec2(x, y), ImVec2(x + pw, y + h), Col(120, 100, 220, 200), 26.f * s, 0, 2.f * s);
    const float t = static_cast<float>(ImGui::GetTime());
    const float cx = x + pw * 0.5f, cy = y + 120.f * s;
    int r = 130, g = 110, b = 255;                                   // boşta: mor
    const char* state = "Dinliyorum";
    float pulse = 0.5f + 0.5f * std::sin(t * 2.0f);
    if (p.listening) { r = 255; g = 90; b = 140; state = "Seni dinliyorum..."; pulse = 0.5f + 0.5f * std::sin(t * 7.0f); }
    else if (p.thinking) { r = 170; g = 120; b = 255; state = "Düşünüyorum..."; pulse = 0.5f + 0.5f * std::sin(t * 4.0f); }
    else if (p.speaking) { r = 80; g = 220; b = 255; state = "Konuşuyorum"; pulse = 0.5f + 0.5f * std::sin(t * 9.0f); }
    else if (p.typing) { r = 255; g = 200; b = 90; state = "Yazıyorsun..."; }
    for (int i = 4; i >= 1; --i) dl->AddCircleFilled(ImVec2(cx, cy), (44.f + i * 12.f * (0.6f + pulse * 0.6f)) * s, Col(r, g, b, 18 + (4 - i) * 6), 40);
    dl->AddCircleFilled(ImVec2(cx, cy), (40.f + pulse * 6.f) * s, Col(r, g, b, 235), 40);
    dl->AddCircle(ImVec2(cx, cy), (50.f + pulse * 10.f) * s, Col(255, 255, 255, 110), 40, 2.f * s);
    const ImVec2 nsz = font->CalcTextSizeA(26.f * s, FLT_MAX, 0.f, "Haviland");
    dl->AddText(font, 26.f * s, ImVec2(cx - nsz.x * 0.5f, y + 196.f * s), Col(255, 255, 255), "Haviland");
    const ImVec2 ssz = font->CalcTextSizeA(18.f * s, FLT_MAX, 0.f, state);
    dl->AddText(font, 18.f * s, ImVec2(cx - ssz.x * 0.5f, y + 230.f * s), Col(r, g, b), state);
    // son söz: Haviland'ın cevabı büyük; oyuncunun son cümlesi küçük ve soluk
    std::string mine, hers;
    for (auto it = p.chat.rbegin(); it != p.chat.rend(); ++it) {
        if (it->mine && mine.empty()) mine = it->text;
        if (!it->mine && hers.empty() && it->text != "düşündü") hers = it->text;
        if (!mine.empty() && !hers.empty()) break;
    }
    const float wrap = pw - 48.f * s;
    float ty = y + 268.f * s;
    if (!mine.empty()) {
        dl->AddText(font, 16.f * s, ImVec2(x + 24.f * s, ty), Col(150, 150, 190), ("Sen: " + mine).c_str(), nullptr, wrap);
        ty += font->CalcTextSizeA(16.f * s, FLT_MAX, wrap, mine.c_str()).y + 20.f * s;
    }
    if (!hers.empty()) dl->AddText(font, 22.f * s, ImVec2(x + 24.f * s, ty), Col(240, 244, 255), hers.c_str(), nullptr, wrap);
    if (!p.notice.empty()) dl->AddText(font, 16.f * s, ImVec2(x + 24.f * s, y + h - 56.f * s), Col(255, 205, 90), p.notice.c_str());
    dl->AddText(font, 14.f * s, ImVec2(x + 24.f * s, y + h - 30.f * s), Col(130, 130, 170), p.hint.c_str());
}

void DrawPhone(ImDrawList* dl, ImFont* font, const PhoneView& p, float W, float H, float s) {
    if (p.web) {
        if (p.pc) DrawPc(dl, font, p, W, H, s);
        else DrawWeb(dl, font, p, W, H, s);
        DrawCursor(dl, p, W, H, s);
        return;
    }
    if (p.in_call) {  // eski liste arayüzü yok: Haviland asistan arayüzü, diğerleri SMS gibi mesaj dizisi
        if (p.assistant) DrawAssistant(dl, font, p, W, H, s);
        else DrawChat(dl, font, p, W, H, s);
    }
}

void DrawMenu(ImDrawList* dl, ImFont* font, const MenuView& m, float W, float H, float s) {
    (void)W;
    const float pw = 620.f * s, pad = 22.f * s;
    const float x = 40.f * s;
    const float fs = 26.f * s, fs_small = 21.f * s, row_h = 38.f * s;
    const int visible = 12;

    // Kaydırma: seçili satır görünür pencerede kalsın.
    int first = 0;
    const int n = static_cast<int>(m.rows.size());
    if (n > visible) first = std::max(0, std::min(m.selected - visible / 2, n - visible));
    const int shown = std::min(n, visible);

    float h = pad + 40.f * s + (m.subtitle.empty() ? 0.f : 30.f * s) + (m.tabs.empty() ? 0.f : 44.f * s) +
              static_cast<float>(shown) * row_h + pad + 34.f * s;
    const float y = std::max(24.f * s, (H - h) * 0.5f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + pw, y + h), Col(14, 16, 24, 242), 22.f * s);
    dl->AddRect(ImVec2(x, y), ImVec2(x + pw, y + h), Col(110, 160, 255), 22.f * s, 0, 2.f * s);

    float cy = y + pad;
    dl->AddText(font, fs * 1.1f, ImVec2(x + pad, cy), Col(255, 255, 255), m.title.c_str());
    cy += 40.f * s;
    if (!m.subtitle.empty()) {
        dl->AddText(font, fs_small, ImVec2(x + pad, cy), Col(150, 158, 180), m.subtitle.c_str());
        cy += 30.f * s;
    }
    if (!m.tabs.empty()) {  // sekmeler
        float tx = x + pad;
        for (size_t i = 0; i < m.tabs.size(); ++i) {
            const bool cur = static_cast<int>(i) == m.tab;
            const ImVec2 ts = font->CalcTextSizeA(fs_small, FLT_MAX, 0.f, m.tabs[i].c_str());
            if (cur) dl->AddRectFilled(ImVec2(tx - 6.f * s, cy), ImVec2(tx + ts.x + 6.f * s, cy + 32.f * s), Col(52, 70, 120), 10.f * s);
            dl->AddText(font, fs_small, ImVec2(tx, cy + 4.f * s), cur ? Col(255, 255, 255) : Col(150, 158, 180), m.tabs[i].c_str());
            tx += ts.x + 22.f * s;
        }
        cy += 44.f * s;
    }
    for (int i = 0; i < shown; ++i) {
        const MenuRow& r = m.rows[static_cast<size_t>(first + i)];
        const bool sel = (first + i) == m.selected && !r.header;
        if (sel) dl->AddRectFilled(ImVec2(x + 8.f * s, cy), ImVec2(x + pw - 8.f * s, cy + row_h - 4.f * s), Col(52, 70, 120), 10.f * s);
        if (r.header) {
            dl->AddText(font, fs_small, ImVec2(x + pad, cy + 6.f * s), Col(120, 160, 255), r.label.c_str());
        } else {
            dl->AddText(font, fs, ImVec2(x + pad, cy + 4.f * s), sel ? Col(255, 255, 255) : Col(215, 218, 228), r.label.c_str());
            if (!r.value.empty()) {
                const std::string v = sel ? "‹ " + r.value + " ›" : r.value;
                const ImVec2 vs = font->CalcTextSizeA(fs, FLT_MAX, 0.f, v.c_str());
                dl->AddText(font, fs, ImVec2(x + pw - pad - vs.x, cy + 4.f * s), sel ? Col(255, 220, 130) : Col(170, 178, 200), v.c_str());
            }
        }
        cy += row_h;
    }
    if (n > visible) {  // kaydırma çubuğu
        const float top = y + h - pad - 34.f * s - static_cast<float>(shown) * row_h;
        const float th = static_cast<float>(shown) * row_h;
        dl->AddRectFilled(ImVec2(x + pw - 10.f * s, top), ImVec2(x + pw - 6.f * s, top + th), Col(40, 46, 66), 2.f * s);
        const float bar = th * static_cast<float>(visible) / static_cast<float>(n);
        const float bt = top + (th - bar) * static_cast<float>(first) / static_cast<float>(n - visible);
        dl->AddRectFilled(ImVec2(x + pw - 10.f * s, bt), ImVec2(x + pw - 6.f * s, bt + bar), Col(110, 160, 255), 2.f * s);
    }
    dl->AddText(font, fs_small * 0.9f, ImVec2(x + pad, y + h - 30.f * s), Col(140, 148, 170), m.hint.c_str());
}

void DrawInput(ImDrawList* dl, ImFont* font, const InputView& in, float W, float H, float s) {
    const float bw = W * 0.6f, bh = 64.f * s, pad = 20.f * s;
    const float x = (W - bw) * 0.5f, y = H * 0.93f - bh * 0.5f;
    const float fs = 28.f * s;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw, y + bh), Col(14, 16, 24, 240), 16.f * s);
    dl->AddRect(ImVec2(x, y), ImVec2(x + bw, y + bh), Col(110, 160, 255), 16.f * s, 0, 2.f * s);

    // Uzun metinde sondaki kısım görünür (baştan UTF-8 karakter kırp).
    std::string shown = in.text;
    const float room = bw - 2 * pad - 230.f * s;
    while (!shown.empty() && font->CalcTextSizeA(fs, FLT_MAX, 0.f, shown.c_str()).x > room) {
        size_t cut = 1;
        while (cut < shown.size() && (static_cast<unsigned char>(shown[cut]) & 0xC0) == 0x80) ++cut;
        shown.erase(0, cut);
    }
    const bool blink = static_cast<int>(ImGui::GetTime() * 2.0) % 2 == 0;
    const std::string line = "Yaz › " + shown + (blink ? "▌" : " ");
    dl->AddText(font, fs, ImVec2(x + pad, y + (bh - fs) * 0.5f), Col(255, 255, 255), line.c_str());
    const char* hint = "Enter: gönder   Esc: iptal";
    const ImVec2 hs = font->CalcTextSizeA(fs * 0.62f, FLT_MAX, 0.f, hint);
    dl->AddText(font, fs * 0.62f, ImVec2(x + bw - pad - hs.x, y + (bh - hs.y) * 0.5f), Col(140, 148, 170), hint);
}


// ---- Yükleme kapağı (loading.png): oyun açılır açılmaz tam ekran; mod hikâyeye girince kararıp kalkar ----
std::atomic<bool> g_cover_on{false};
std::chrono::steady_clock::time_point g_cover_start, g_cover_off_at;
bool g_cover_fading = false, g_cover_tried = false;
ID3D11ShaderResourceView* g_cover_srv = nullptr;
int g_cover_w = 0, g_cover_h = 0;
bool g_cover_video = false, g_cover_video_pending = false;  // video kapağı (intro bir kez, sonra loop)
ID3D11Texture2D* g_vid_tex = nullptr;
ID3D11ShaderResourceView* g_vid_srv = nullptr;
int g_vid_w = 0, g_vid_h = 0;

ID3D11ShaderResourceView* LoadTexture(ID3D11Device* dev, const wchar_t* path, int& w, int& h) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // zaten başlatılmışsa hata döner, zararsız
    IWICImagingFactory* f = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* fr = nullptr;
    IWICFormatConverter* cv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) &&
        SUCCEEDED(f->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)) && SUCCEEDED(dec->GetFrame(0, &fr)) &&
        SUCCEEDED(f->CreateFormatConverter(&cv)) &&
        SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        UINT uw = 0, uh = 0;
        cv->GetSize(&uw, &uh);
        std::vector<BYTE> px(static_cast<size_t>(uw) * uh * 4);
        if (SUCCEEDED(cv->CopyPixels(nullptr, uw * 4, static_cast<UINT>(px.size()), px.data()))) {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = uw;
            td.Height = uh;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sd{px.data(), uw * 4, 0};
            ID3D11Texture2D* tex = nullptr;
            if (SUCCEEDED(dev->CreateTexture2D(&td, &sd, &tex)) && tex) {
                dev->CreateShaderResourceView(tex, nullptr, &srv);
                tex->Release();
                w = static_cast<int>(uw);
                h = static_cast<int>(uh);
            }
        }
    }
    if (cv) cv->Release();
    if (fr) fr->Release();
    if (dec) dec->Release();
    if (f) f->Release();
    return srv;
}

// Kapak çizimi: ekranı kaplar (en-boy oranı korunur, taşan kırpılır). Dönüş: çizildi mi.
bool DrawCover(ImDrawList* dl, float W, float H) {
    if (!g_cover_on.load() && !g_cover_fading) return false;
    const auto now = std::chrono::steady_clock::now();
    if (g_cover_on.load() && std::chrono::duration<float>(now - g_cover_start).count() > 180.f) g_cover_on = false;  // güvenlik: sonsuza kadar kalmasın
    if (!g_cover_on.load() && !g_cover_fading) return false;
    if (g_cover_video_pending) {  // video ilk karede başlar (oyun penceresi görünürken; DLL yüklenirken değil)
        g_cover_video_pending = false;
        if (!videocover::Start(L"HikayeTurkRP/data/loading_intro.vid", L"HikayeTurkRP/data/loading_loop.vid")) g_cover_video = false;
    }
    if (g_cover_video && !videocover::Running() && !videocover::HasFrame()) {  // video açılamadı: resim kapağı
        g_cover_video = false;
        Log("overlay: kapak videosu acilamadi, resim kapagi kullaniliyor");
    }
    if (!g_cover_tried && !g_cover_video) {
        g_cover_tried = true;
        g_cover_srv = LoadTexture(g_dev, L"HikayeTurkRP/data/loading.png", g_cover_w, g_cover_h);
        if (!g_cover_srv) Log("overlay: loading.png yuklenemedi, kapak yok");
    }
    ID3D11ShaderResourceView* srv = g_cover_srv;
    int tw = g_cover_w, th = g_cover_h;
    if (g_cover_video) {
        std::vector<uint8_t> px;
        int fw = 0, fh = 0;
        if (videocover::TakeFrame(px, fw, fh) && fw > 0 && fh > 0 && px.size() >= static_cast<size_t>(fw) * fh * 4) {
            if (!g_vid_tex || fw != g_vid_w || fh != g_vid_h) {
                if (g_vid_srv) { g_vid_srv->Release(); g_vid_srv = nullptr; }
                if (g_vid_tex) { g_vid_tex->Release(); g_vid_tex = nullptr; }
                D3D11_TEXTURE2D_DESC td{};
                td.Width = static_cast<UINT>(fw);
                td.Height = static_cast<UINT>(fh);
                td.MipLevels = 1;
                td.ArraySize = 1;
                td.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
                td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_DEFAULT;
                td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                if (SUCCEEDED(g_dev->CreateTexture2D(&td, nullptr, &g_vid_tex)) && g_vid_tex) {
                    g_dev->CreateShaderResourceView(g_vid_tex, nullptr, &g_vid_srv);
                    g_vid_w = fw;
                    g_vid_h = fh;
                }
            }
            if (g_vid_tex) g_ctx->UpdateSubresource(g_vid_tex, 0, nullptr, px.data(), static_cast<UINT>(fw) * 4, 0);
        }
        srv = g_vid_srv;
        tw = g_vid_w;
        th = g_vid_h;
    }
    if (!srv) {
        if (g_cover_video) {  // ilk kare henüz çözülmedi: siyah ekran
            dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 255));
            return true;
        }
        g_cover_on = false;
        g_cover_fading = false;
        return false;
    }
    float alpha = 1.f;
    if (!g_cover_on.load()) {  // kapanış: 0,6 sn solma
        if (!g_cover_fading) {
            g_cover_fading = true;
            g_cover_off_at = now;
        }
        alpha = 1.f - std::chrono::duration<float>(now - g_cover_off_at).count() / 0.6f;
        if (alpha <= 0.f) {
            g_cover_fading = false;
            if (g_cover_video) {
                videocover::Stop();
                g_cover_video = false;
                if (g_vid_srv) { g_vid_srv->Release(); g_vid_srv = nullptr; }
                if (g_vid_tex) { g_vid_tex->Release(); g_vid_tex = nullptr; }
            }
            return false;
        }
    }
    const float ia = static_cast<float>(tw) / static_cast<float>(std::max(1, th)), sa = W / H;
    float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
    if (sa > ia) {  // ekran daha geniş: üst-alt kırp
        const float vis = ia / sa;
        v0 = (1.f - vis) * 0.5f;
        v1 = 1.f - v0;
    } else {
        const float vis = sa / ia;
        u0 = (1.f - vis) * 0.5f;
        u1 = 1.f - u0;
    }
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, static_cast<int>(255 * alpha)));
    dl->AddImage((ImTextureID)srv, ImVec2(0, 0), ImVec2(W, H), ImVec2(u0, v0), ImVec2(u1, v1), IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)));
    return true;
}

void Render(IDXGISwapChain* sc) {
    xpad::TickSignal();  // "save menüsü yüklendi" sinyali (companion pyautogui ile Backspace basar)
    if (!g_init) {
        if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_dev))) || !g_dev) {
            g_failed = true;
            return;
        }
        g_dev->GetImmediateContext(&g_ctx);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;  // giriş script thread'inde işlenir; ImGui fare/klavye almaz
        // Türkçe harfler (ş ı ğ İ) için Windows'un Segoe UI yazı tipi.
        ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 28.0f);
        g_turkish = font != nullptr;
        if (!font) io.Fonts->AddFontDefault();
        ImGui_ImplDX11_Init(g_dev, g_ctx);
        g_init = true;
        xpad::Arm();  // oyun ilk kez çizildi (pencere göründü): sayaç başlar
        g_last = std::chrono::steady_clock::now();
        g_ready = true;
        Log(std::string("overlay: ImGui hazir (Turkce font: ") + (g_turkish ? "var" : "yok, ASCII'ye doner") + ")");
    }

    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&bb))) || !bb) return;
    D3D11_TEXTURE2D_DESC desc{};
    bb->GetDesc(&desc);
    ID3D11RenderTargetView* rtv = nullptr;
    g_dev->CreateRenderTargetView(bb, nullptr, &rtv);
    bb->Release();
    if (!rtv) return;

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
        if (phone.open) DrawPhone(dl, font, phone, W, H, s);
        if (menu.open) DrawMenu(dl, font, menu, W, H, s);
        if (input.active) DrawInput(dl, font, input, W, H, s);
        if (dash.active) {  // araç dashboard'u: sağ alt köşede yuvarlak kadran + km sayacı + yakıt çubuğu
            const float dw = 300.f * s, dh = 108.f * s;
            const float dx = W - dw - 24.f * s, dy = H - dh - 24.f * s;
            dl->AddRectFilled(ImVec2(dx, dy), ImVec2(dx + dw, dy + dh), Col(10, 12, 18, 215), 14.f * s);
            dl->AddRect(ImVec2(dx, dy), ImVec2(dx + dw, dy + dh), Col(70, 80, 105, 255), 14.f * s, 0, 1.5f * s);
            // hız kadranı: yarım daire (180°..360°), ibre açısı hıza göre
            const float cx = dx + 62.f * s, cy = dy + 66.f * s, r = 44.f * s;
            const float a0 = 3.14159265f, a1 = 2.f * 3.14159265f;  // soldan sağa üst yarım çember
            const int seg = 24;
            for (int i = 0; i < seg; ++i) {
                const float t0 = a0 + (a1 - a0) * i / seg, t1 = a0 + (a1 - a0) * (i + 1) / seg;
                const float v0 = (a1 - t0) / (a1 - a0) * 300.f;  // 0..300 km/s ölçek
                const ImU32 c = v0 < 120.f ? Col(90, 200, 120) : (v0 < 220.f ? Col(240, 200, 80) : Col(240, 90, 80));
                dl->AddLine(ImVec2(cx + std::cos(t0) * (r - 7.f * s), cy + std::sin(t0) * (r - 7.f * s)),
                            ImVec2(cx + std::cos(t0) * r, cy + std::sin(t0) * r), c, 2.5f * s);
                (void)t1;
            }
            const float frac = std::clamp(dash.kmh / 300.f, 0.f, 1.f);
            const float ang = a0 + (a1 - a0) * frac;
            dl->AddLine(ImVec2(cx, cy), ImVec2(cx + std::cos(ang) * (r - 10.f * s), cy + std::sin(ang) * (r - 10.f * s)), Col(255, 255, 255), 3.f * s);
            dl->AddCircleFilled(ImVec2(cx, cy), 5.f * s, Col(255, 255, 255));
            char spd[16];
            std::snprintf(spd, sizeof(spd), "%d", static_cast<int>(dash.kmh + 0.5f));
            const ImVec2 ssz = font->CalcTextSizeA(22.f * s, FLT_MAX, 0.f, spd);
            dl->AddText(font, 22.f * s, ImVec2(cx - ssz.x * 0.5f, cy - 2.f * s), Col(255, 255, 255), spd);
            dl->AddText(font, 12.f * s, ImVec2(cx - 14.f * s, cy + 8.f * s), Col(160, 168, 190), "km/s");
            // kilometre sayacı (odometer)
            char kmb[32];
            std::snprintf(kmb, sizeof(kmb), "%.1f km", dash.km);
            dl->AddText(font, 17.f * s, ImVec2(dx + 118.f * s, dy + 22.f * s), Col(230, 236, 255), kmb);
            // yakıt çubuğu (yatay bar, sağ taraf)
            const float bx = dx + 118.f * s, bw = dw - 118.f * s - 16.f * s, by = dy + 58.f * s, bh = 14.f * s;
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), Col(45, 50, 66), 4.f * s);
            const float fw = bw * std::clamp(dash.fuel / 100.f, 0.f, 1.f);
            const ImU32 fc = dash.fuel < 15.f ? Col(240, 90, 80) : (dash.fuel < 40.f ? Col(240, 200, 80) : Col(90, 200, 120));
            if (fw > 0.f) dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + fw, by + bh), fc, 4.f * s);
            dl->AddRect(ImVec2(bx, by), ImVec2(bx + bw, by + bh), Col(90, 100, 130), 4.f * s, 0, 1.f * s);
            char fb[16];
            std::snprintf(fb, sizeof(fb), "Yakit %%:%d", static_cast<int>(dash.fuel + 0.5f));
            dl->AddText(font, 12.f * s, ImVec2(bx, by + bh + 4.f * s), Col(160, 168, 190), fb);
        }
        // Kalıcı durum çubuğu (sağ üst): telefon kapalıyken de saat + sinyal + pil görünsün
        if (!status.clock.empty() || !status.signal.empty() || !status.battery.empty()) {
            const float fs_sb = 20.f * s;
            float rx = W - 18.f * s;
            if (!status.clock.empty()) {
                const ImVec2 csz = font->CalcTextSizeA(fs_sb, FLT_MAX, 0.f, status.clock.c_str());
                rx -= csz.x;
                dl->AddText(font, fs_sb, ImVec2(rx, 14.f * s), Col(235, 238, 245), status.clock.c_str());
                rx -= 14.f * s;
            }
            if (!status.battery.empty()) {
                const ImVec2 bsz = font->CalcTextSizeA(fs_sb, FLT_MAX, 0.f, status.battery.c_str());
                rx -= bsz.x;
                dl->AddText(font, fs_sb, ImVec2(rx, 14.f * s), status.battery_low ? Col(255, 110, 110) : Col(130, 225, 150), status.battery.c_str());
                rx -= 12.f * s;
            }
            if (!status.signal.empty()) {
                const ImVec2 ssz = font->CalcTextSizeA(fs_sb, FLT_MAX, 0.f, status.signal.c_str());
                rx -= ssz.x;
                const int bars = static_cast<int>(std::count_if(status.signal.begin(), status.signal.end(), [](char c) { return c != '\xB0'; }));
                dl->AddText(font, fs_sb, ImVec2(rx, 14.f * s), bars <= 1 ? Col(255, 120, 120) : Col(150, 220, 255), status.signal.c_str());
            }
        }
        DrawCover(dl, W, H);  // en üstte: oyunun ana menüsü/kayıt listesi görünmesin
    }
    ImGui::Render();
    g_ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    rtv->Release();
}

void OnPresent(void* swap_chain) {
    if (g_failed || !swap_chain) return;
    try {
        Render(static_cast<IDXGISwapChain*>(swap_chain));
    } catch (...) {
        g_failed = true;  // çizim hatası oyunu düşürmesin: katman kapanır, native altyazıya dönülür
        g_ready = false;
    }
}

}  // namespace

void Init() {
    xpad::Install();  // Xbox Geri tuşu enjeksiyonu (başlatıcı isterse): oyunun ana menüsü/kayıt listesi için
    char env[8] = {};
    if (GetEnvironmentVariableA("HIKAYE_LAUNCHER", env, sizeof(env)) > 0 && env[0] == '1') {  // başlatıcıyla açıldı: kapak oyun açılır açılmaz
        g_cover_start = std::chrono::steady_clock::now();
        g_cover_on = true;
        // video varsa (intro bir kez, sonra loop) önce o; yoksa/açılamazsa loading.png
        if (GetFileAttributesW(L"HikayeTurkRP/data/loading_intro.vid") != INVALID_FILE_ATTRIBUTES && GetFileAttributesW(L"HikayeTurkRP/data/loading_loop.vid") != INVALID_FILE_ATTRIBUTES) {
            g_cover_video = true;
            g_cover_video_pending = true;
        }
    }
    presentCallbackRegister(OnPresent);
}

void SetLoadingCover(bool on) { g_cover_on = on; }
bool LoadingCoverActive() { return g_cover_on.load(); }

void Shutdown() {
    presentCallbackUnregister(OnPresent);
    g_failed = true;
    g_ready = false;
}

bool Ready() { return g_ready && !g_failed; }
bool HasTurkishFont() { return g_turkish; }

void Text(const std::string& utf8, float y, int r, int g, int b) { g_lines_next.push_back({utf8, y, r, g, b}); }

void SetPhone(const PhoneView& view) { g_phone_next = view; }
void SetStatusBar(const std::string& clock, const std::string& signal, const std::string& battery, bool battery_low) {
    g_status_next.clock = clock;
    g_status_next.signal = signal;
    g_status_next.battery = battery;
    g_status_next.battery_low = battery_low;
}
void SetDash(float kmh, float km, float fuel_pct, bool active) {
    g_dash_next.active = active;
    g_dash_next.kmh = kmh;
    g_dash_next.km = km;
    g_dash_next.fuel = fuel_pct;
}
int HoverItem() { return g_hover.load(); }
int TakeWebClick() { return g_click.exchange(-1); }

void SetMenu(const MenuView& view) { g_menu_next = view; }

void SetInput(const std::string& text, bool active) {
    g_input_next.active = active;
    g_input_next.text = text;
}

void Commit() {
    std::lock_guard<std::mutex> lock(g_mu);
    g_lines_cur = std::move(g_lines_next);
    g_lines_next.clear();
    g_phone_cur = g_phone_next;
    g_input_cur = g_input_next;
    g_menu_cur = g_menu_next;
    g_status_cur = g_status_next;
    g_dash_cur = g_dash_next;
}

}  // namespace hikaye::overlay
