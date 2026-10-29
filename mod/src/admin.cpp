#include "admin.h"
#include "economy.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "input.h"
#include "karma.h"
#include "overlay.h"
#include "weaponwatch.h"

namespace hikaye::admin {

namespace {
constexpr const char* kCode = "PHPMYADMIN";
std::string g_buf;
unsigned long long g_last_letter = 0, g_tick = 0, g_god_check = 0;
bool g_down[256] = {};

enum class Level { Closed, Main, Cats, Dests, Money } g_level = Level::Closed;
int g_sel = 0, g_cat_sel = 0;
bool g_god = false, g_noclip = false;
std::vector<Dest> g_dests;
std::vector<std::string> g_cats;
std::string g_cat;
Keys g_keys;
std::string g_notice;
unsigned long long g_notice_until = 0;

void Say(const std::string& m) {
    g_notice = m;
    g_notice_until = GetTickCount64() + 3500ULL;
}

Entity Target() {
    const Ped p = PLAYER::PLAYER_PED_ID();
    if (PED::IS_PED_IN_ANY_VEHICLE(p, false)) return PED::GET_VEHICLE_PED_IS_IN(p, false);
    return p;
}

void SetGod(bool on) {
    g_god = on;
    PLAYER::SET_PLAYER_INVINCIBLE(PLAYER::PLAYER_ID(), on);
    ENTITY::SET_ENTITY_INVINCIBLE(PLAYER::PLAYER_PED_ID(), on);
}

void StopNoclip() {
    if (!g_noclip) return;
    g_noclip = false;
    const Ped p = PLAYER::PLAYER_PED_ID();
    for (Entity e : {static_cast<Entity>(p), PED::IS_PED_IN_ANY_VEHICLE(p, false) ? static_cast<Entity>(PED::GET_VEHICLE_PED_IS_IN(p, false)) : static_cast<Entity>(p)}) {
        ENTITY::FREEZE_ENTITY_POSITION(e, false);
        ENTITY::SET_ENTITY_COLLISION(e, true, true);
        ENTITY::SET_ENTITY_VISIBLE(e, true, false);
    }
    ENTITY::SET_ENTITY_VISIBLE(p, true, false);
}

void Teleport(const Dest& d) {
    const Entity e = Target();
    STREAMING::LOAD_SCENE(d.pos.x, d.pos.y, d.pos.z);
    ENTITY::SET_ENTITY_COORDS_NO_OFFSET(e, d.pos.x, d.pos.y, d.pos.z + 1.0f, false, false, false);
    Say("Işınlandın: " + d.name);
}

void SafeExit() {
    StopNoclip();
    ENTITY::SET_ENTITY_VISIBLE(PLAYER::PLAYER_PED_ID(), true, false);
    SetGod(false);
    g_level = Level::Closed;
    overlay::SetMenu({});
    karma::Toast("Hile etkileri kapatıldı", 3);
}

void NoclipTick(float dt) {
    const Ped p = PLAYER::PLAYER_PED_ID();
    const Entity e = Target();
    ENTITY::FREEZE_ENTITY_POSITION(e, true);
    ENTITY::SET_ENTITY_COLLISION(e, false, false);
    ENTITY::SET_ENTITY_VISIBLE(p, false, false);
    if (e != p) ENTITY::SET_ENTITY_VISIBLE(e, false, false);
    SetGod(true);
    for (int c : {21, 22, 23, 24, 25, 36}) CONTROLS::DISABLE_CONTROL_ACTION(0, c, true);  // koşu, zıplama, giriş, saldırı, nişan, çömelme
    const Vector3 rot = CAM::GET_GAMEPLAY_CAM_ROT(2);
    const float yaw = rot.z * 3.14159265f / 180.f, pitch = rot.x * 3.14159265f / 180.f;
    const float fx = -std::sin(yaw) * std::cos(pitch), fy = std::cos(yaw) * std::cos(pitch), fz = std::sin(pitch);
    const float rx = std::cos(yaw), ry = std::sin(yaw);
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const float speed = (shift ? 60.f : 14.f) * dt;
    float mx = 0, my = 0, mz = 0;
    if (GetAsyncKeyState('W') & 0x8000) { mx += fx; my += fy; mz += fz; }
    if (GetAsyncKeyState('S') & 0x8000) { mx -= fx; my -= fy; mz -= fz; }
    if (GetAsyncKeyState('D') & 0x8000) { mx += rx; my += ry; }
    if (GetAsyncKeyState('A') & 0x8000) { mx -= rx; my -= ry; }
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) mz += 1.f;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mz -= 1.f;
    const Vector3 pos = ENTITY::GET_ENTITY_COORDS(e, true);
    ENTITY::SET_ENTITY_COORDS_NO_OFFSET(e, pos.x + mx * speed, pos.y + my * speed, pos.z + mz * speed, false, false, false);
    if (e != p) ENTITY::SET_ENTITY_HEADING(e, -rot.z);
}

void OpenMain() {
    g_level = Level::Main;
    g_sel = 0;
}
}  // namespace

void Open() { OpenMain(); }
bool MenuOpen() { return g_level != Level::Closed; }
bool Active() { return g_god || g_noclip; }

void Reset() {
    StopNoclip();
    ENTITY::SET_ENTITY_VISIBLE(PLAYER::PLAYER_PED_ID(), true, false);
    if (g_god) SetGod(false);
    g_level = Level::Closed;
    overlay::SetMenu({});
    g_buf.clear();
}

void Update(bool blocked, const std::function<std::vector<Dest>()>& build_dests) {
    const unsigned long long now = GetTickCount64();
    if (g_tick == 0) g_tick = now;
    const float dt = std::min(0.05f, static_cast<float>(now - g_tick) / 1000.f);
    g_tick = now;

    // Etkiler
    if (g_noclip) NoclipTick(dt);
    else if (g_god && now - g_god_check > 1000ULL) {  // başka sistemler (hapis) kapatmış olabilir
        g_god_check = now;
        SetGod(true);
    }

    if (g_level == Level::Closed) return;

    // Menü
    CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
    overlay::MenuView mv;
    mv.open = true;
    mv.title = "Yönetici";
    std::vector<overlay::MenuRow> rows;
    std::vector<std::string> acts;
    if (g_level == Level::Main) {
        const int cash = economy::GetCash();
        const std::string cash_str = (cash >= 0) ? (" ($" + std::to_string(cash) + ")") : "";
        rows = {{"Işınlan", "", false},
                {"Ölümsüzlük", g_god ? "AÇIK" : "kapalı", false},
                {"Yıldız düşür", "", false},
                {"Noclip (Görünmez)", g_noclip ? "AÇIK" : "kapalı", false},
                {"Para İşlemleri" + cash_str, "", false},
                {"Güvenli çıkış (tüm etkileri kapat)", "", false},
                {"Kapat (etkiler açık kalır)", "", false}};
        mv.subtitle = "phpmyadmin";
    } else if (g_level == Level::Money) {
        rows = {
            {"+$1.000", "", false},
            {"+$5.000", "", false},
            {"+$10.000", "", false},
            {"+$50.000", "", false},
            {"+$100.000", "", false},
            {"-$1.000", "", false},
            {"-$5.000", "", false},
            {"-$10.000", "", false},
            {"-$50.000", "", false},
            {"-$100.000", "", false},
            {"Parayı Sıfırla ($0)", "", false},
            {"Zengin Ol ($1.000.000)", "", false}
        };
        mv.subtitle = "Para Düzenle   Nakit: $" + std::to_string(std::max(0, economy::GetCash()));
    } else if (g_level == Level::Cats) {
        for (const auto& c : g_cats) rows.push_back({c, "", false});
        mv.subtitle = "Işınlan: kategori";
    } else {
        for (const auto& d : g_dests) {
            if (d.cat == g_cat) rows.push_back({d.name, "", false});
        }
        mv.subtitle = "Işınlan: " + g_cat;
    }
    if (now < g_notice_until) mv.subtitle += "   " + g_notice;
    const int n = static_cast<int>(rows.size());
    if (n == 0) g_sel = 0;
    else if (g_sel >= n) g_sel = n - 1;
    if (g_keys.Repeat(VK_DOWN) && n > 0) g_sel = (g_sel + 1) % n;
    if (g_keys.Repeat(VK_UP) && n > 0) g_sel = (g_sel + n - 1) % n;
    const bool enter = g_keys.Pressed(VK_RETURN), back = g_keys.Pressed(VK_BACK);
    if (back) {
        if (g_level == Level::Main) {
            g_level = Level::Closed;
            overlay::SetMenu({});
        } else if (g_level == Level::Money) {
            g_level = Level::Main;
            g_sel = 4;
        } else {
            g_level = g_level == Level::Dests ? Level::Cats : Level::Main;
            g_sel = 0;
        }
        return;
    }
    if (enter && n > 0) {
        if (g_level == Level::Main) {
            switch (g_sel) {
                case 0:
                    g_dests = build_dests ? build_dests() : std::vector<Dest>();
                    g_cats.clear();
                    for (const auto& d : g_dests) {
                        if (std::find(g_cats.begin(), g_cats.end(), d.cat) == g_cats.end()) g_cats.push_back(d.cat);
                    }
                    g_level = Level::Cats;
                    g_sel = 0;
                    break;
                case 1: SetGod(!g_god); Say(g_god ? "Ölümsüzlük açık" : "Ölümsüzlük kapalı"); break;
                case 2:
                    PLAYER::CLEAR_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID());
                    weaponwatch::Reset();
                    Say("Aranma seviyesi sıfırlandı");
                    break;
                case 3:
                    if (g_noclip) StopNoclip();
                    else g_noclip = true;
                    Say(g_noclip ? "Noclip açık (Karakter görünmez): menüyü kapatıp uç (WASD, Shift hızlı, Space/Ctrl)" : "Noclip kapalı");
                    break;
                case 4:
                    g_level = Level::Money;
                    g_sel = 0;
                    break;
                case 5: SafeExit(); return;
                default:
                    g_level = Level::Closed;
                    overlay::SetMenu({});
                    return;
            }
        } else if (g_level == Level::Money) {
            switch (g_sel) {
                case 0: economy::Earn(1000); break;
                case 1: economy::Earn(5000); break;
                case 2: economy::Earn(10000); break;
                case 3: economy::Earn(50000); break;
                case 4: economy::Earn(100000); break;
                case 5: economy::Spend(1000); break;
                case 6: economy::Spend(5000); break;
                case 7: economy::Spend(10000); break;
                case 8: economy::Spend(50000); break;
                case 9: economy::Spend(100000); break;
                case 10: {
                    const int c = economy::GetCash();
                    if (c > 0) economy::Spend(c);
                    break;
                }
                case 11: {
                    const int c = economy::GetCash();
                    if (c < 1000000) economy::Earn(1000000 - c);
                    break;
                }
            }
            Say("Güncel Nakit: $" + std::to_string(std::max(0, economy::GetCash())));
        } else if (g_level == Level::Cats) {
            g_cat = g_cats[static_cast<size_t>(g_sel)];
            g_cat_sel = g_sel;
            g_level = Level::Dests;
            g_sel = 0;
        } else {
            int idx = 0;
            for (const auto& d : g_dests) {
                if (d.cat != g_cat) continue;
                if (idx++ == g_sel) {
                    Teleport(d);
                    break;
                }
            }
        }
    }
    mv.rows = rows;
    mv.selected = g_sel;
    mv.hint = "↑↓ seç   Enter onay   Backspace geri";
    overlay::SetMenu(mv);
}

}  // namespace hikaye::admin
