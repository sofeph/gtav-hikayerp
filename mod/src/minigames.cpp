#include "minigames.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>

namespace hikaye::minigames {

namespace {
using overlay::WebShape;

Id g_id = Id::None;
int g_best[3] = {0, 0, 0};
unsigned long long g_last = 0;
bool g_over = false;

float Rnd() { return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX); }
bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
bool Edge(int vk) {
    static bool was[256] = {};
    const bool d = Down(vk);
    const bool e = d && !was[vk & 255];
    was[vk & 255] = d;
    return e;
}

// ---- çizim yardımcıları ----
WebShape R(float x, float y, float w, float h, int fill, float r = 0) {
    WebShape s;
    s.type = 0;
    s.x = x; s.y = y; s.w = w; s.h = h; s.r = r;
    s.fill = fill;
    return s;
}
WebShape C(float x, float y, float r, int fill) {
    WebShape s;
    s.type = 1;
    s.x = x; s.y = y; s.r = r;
    s.fill = fill;
    return s;
}
WebShape L(float x1, float y1, float x2, float y2, int col, float w = 1.f) {
    WebShape s;
    s.type = 2;
    s.x = x1; s.y = y1; s.w = x2; s.h = y2;
    s.stroke = col;
    s.sw = w;
    return s;
}
WebShape T(float x, float y, const std::string& text, float size, int col, int align = 0) {
    WebShape s;
    s.type = 5;
    s.x = x; s.y = y; s.size = size; s.fill = col; s.align = align;
    s.text = text;
    return s;
}
// Dönük dikdörtgen (araç): merkez, uzunluk, genişlik, açı (radyan)
WebShape Car(float cx, float cy, float len, float wid, float ang, int fill) {
    WebShape s;
    s.type = 4;
    s.fill = fill;
    const float c = std::cos(ang), sn = std::sin(ang);
    const float hl = len / 2, hw = wid / 2;
    const float pts[4][2] = {{-hl, -hw}, {hl, -hw}, {hl, hw}, {-hl, hw}};
    for (const auto& p : pts) s.pts.push_back({cx + p[0] * c - p[1] * sn, cy + p[0] * sn + p[1] * c});
    return s;
}

// ================= Yılan =================
constexpr int kN = 20, kCell = 18, kOx = 20, kOy = 44;
struct Snake {
    std::deque<std::pair<int, int>> body;
    int dx = 1, dy = 0, ndx = 1, ndy = 0;
    std::pair<int, int> food{10, 10};
    int score = 0;
    unsigned long long next = 0;
} g_snake;

void SnakeReset() {
    g_snake = Snake{};
    g_snake.body = {{8, 10}, {7, 10}, {6, 10}};
    g_snake.food = {14, 10};
    g_snake.next = GetTickCount64() + 400;
    g_over = false;
}

void SnakeUpdate() {
    Snake& s = g_snake;
    if (g_over) {
        if (Edge(VK_RETURN)) SnakeReset();
        return;
    }
    if (Down('W') && s.dy != 1) { s.ndx = 0; s.ndy = -1; }
    if (Down('S') && s.dy != -1) { s.ndx = 0; s.ndy = 1; }
    if (Down('A') && s.dx != 1) { s.ndx = -1; s.ndy = 0; }
    if (Down('D') && s.dx != -1) { s.ndx = 1; s.ndy = 0; }
    const unsigned long long now = GetTickCount64();
    if (now < s.next) return;
    s.next = now + static_cast<unsigned long long>(std::max(60, 140 - s.score * 3));
    s.dx = s.ndx;
    s.dy = s.ndy;
    const std::pair<int, int> head{s.body.front().first + s.dx, s.body.front().second + s.dy};
    bool hit = head.first < 0 || head.second < 0 || head.first >= kN || head.second >= kN;
    for (const auto& b : s.body) hit = hit || b == head;
    if (hit) {
        g_over = true;
        if (s.score > g_best[1]) g_best[1] = s.score;
        return;
    }
    s.body.push_front(head);
    if (head == s.food) {
        ++s.score;
        for (int tries = 0; tries < 200; ++tries) {
            const std::pair<int, int> f{std::rand() % kN, std::rand() % kN};
            bool free_cell = true;
            for (const auto& b : s.body) free_cell = free_cell && b != f;
            if (free_cell) {
                s.food = f;
                break;
            }
        }
    } else {
        s.body.pop_back();
    }
}

Frame SnakeDraw() {
    Frame f;
    f.title = "Yılan";
    f.w = 400;
    f.h = 420;
    f.bg = 0x0e1a12;
    f.shapes.push_back(T(20, 10, "Skor: " + std::to_string(g_snake.score) + "   En iyi: " + std::to_string(g_best[1]), 18, 0xdfffe0));
    f.shapes.push_back(R(kOx - 2, kOy - 2, kN * kCell + 4, kN * kCell + 4, 0x16281b, 4));
    for (size_t i = 0; i < g_snake.body.size(); ++i) {
        const auto& b = g_snake.body[i];
        f.shapes.push_back(R(kOx + b.first * kCell + 1, kOy + b.second * kCell + 1, kCell - 2, kCell - 2, i == 0 ? 0x9cf06a : 0x4fb04a, 4));
    }
    f.shapes.push_back(C(kOx + g_snake.food.first * kCell + kCell / 2.f, kOy + g_snake.food.second * kCell + kCell / 2.f, kCell / 2.f - 2, 0xff5c5c));
    if (g_over) {
        f.shapes.push_back(R(60, 170, 280, 80, 0x000000, 10));
        f.shapes.push_back(T(200, 184, "Oyun bitti", 24, 0xffffff, 1));
        f.shapes.push_back(T(200, 218, "Enter: yeniden   Backspace: çık", 14, 0xcfe0ff, 1));
    } else {
        f.shapes.push_back(T(20, 404, "WASD   Backspace: çık", 12, 0x8fb39a));
    }
    return f;
}

// ================= Tepeden Suç (GTA 1-2 tarzı) =================
constexpr float kBlock = 240.f, kRoad = 70.f, kView = 400.f;
constexpr float kPi = 3.14159265f;

struct Vehicle {
    float x = 0, y = 0, ang = 0, speed = 0;
    int dir = 0;      // trafik: 0 doğu, 1 güney, 2 batı, 3 kuzey
    int color = 0xcccccc;
    bool cop = false;
};
struct Ped {
    float x = 0, y = 0, a = 0;
    bool dead = false;
    unsigned long long dead_at = 0;
    int color = 0xe0c090;
};
struct TopDown {
    Vehicle me;
    std::vector<Vehicle> traffic, cops;
    std::vector<Ped> peds;
    int health = 100, wanted = 0, score = 0, delivered = 0;
    float mx = 0, my = 0;  // paket teslim noktası
    unsigned long long wanted_at = 0, last_hit = 0;
    std::string msg;
    unsigned long long msg_until = 0;
} g_td;

bool OnRoad(float x, float y) {
    float fx = std::fmod(x, kBlock), fy = std::fmod(y, kBlock);
    if (fx < 0) fx += kBlock;
    if (fy < 0) fy += kBlock;
    return fx < kRoad || fy < kRoad;
}
float RoadCenter(float v) {  // v'ye en yakın yol ekseni (x ya da y)
    return std::floor(v / kBlock + 0.5f) * kBlock + kRoad / 2 - (v / kBlock + 0.5f - std::floor(v / kBlock + 0.5f) < 0.f ? 0.f : 0.f);
}
void Say(const std::string& m) {
    g_td.msg = m;
    g_td.msg_until = GetTickCount64() + 2500;
}

void NewTarget() {
    const int bx = static_cast<int>(std::floor(g_td.me.x / kBlock)) + (std::rand() % 7 - 3);
    const int by = static_cast<int>(std::floor(g_td.me.y / kBlock)) + (std::rand() % 7 - 3);
    g_td.mx = bx * kBlock + kRoad / 2;
    g_td.my = by * kBlock + kRoad / 2;
    if (std::abs(g_td.mx - g_td.me.x) + std::abs(g_td.my - g_td.me.y) < 400.f) g_td.mx += 2 * kBlock;
}

Vehicle SpawnTraffic() {
    Vehicle v;
    const float ang = Rnd() * 2 * kPi;
    const float dist = 380.f + Rnd() * 160.f;
    float x = g_td.me.x + std::cos(ang) * dist, y = g_td.me.y + std::sin(ang) * dist;
    const bool horizontal = std::rand() % 2 == 0;
    if (horizontal) {
        y = std::floor(y / kBlock) * kBlock + kRoad / 2;
        v.dir = std::rand() % 2 ? 0 : 2;
    } else {
        x = std::floor(x / kBlock) * kBlock + kRoad / 2;
        v.dir = std::rand() % 2 ? 1 : 3;
    }
    const float lane = (v.dir == 0 || v.dir == 1) ? 12.f : -12.f;
    if (horizontal) y += lane;
    else x -= lane;
    v.x = x;
    v.y = y;
    v.speed = 55.f + Rnd() * 25.f;
    v.ang = v.dir * kPi / 2;
    static const int cols[] = {0x4c8dff, 0xf0a82a, 0x9b9b9b, 0x56c271, 0xd9d9d9, 0xc0504d, 0x7a5cff};
    v.color = cols[std::rand() % 7];
    return v;
}

void TopDownReset() {
    g_td = TopDown{};
    g_td.me.x = kRoad / 2 + kBlock * 2;
    g_td.me.y = kRoad / 2 + kBlock * 2;
    g_td.me.color = 0xf5d142;
    for (int i = 0; i < 9; ++i) g_td.traffic.push_back(SpawnTraffic());
    for (int i = 0; i < 18; ++i) {
        Ped p;
        const float ang = Rnd() * 2 * kPi, d = 60.f + Rnd() * 380.f;
        p.x = g_td.me.x + std::cos(ang) * d;
        p.y = g_td.me.y + std::sin(ang) * d;
        p.a = Rnd() * 2 * kPi;
        static const int cols[] = {0xe0c090, 0xc58f6a, 0xf2d0b0, 0x8d5a3b};
        p.color = cols[std::rand() % 4];
        g_td.peds.push_back(p);
    }
    NewTarget();
    g_over = false;
    g_td.wanted_at = GetTickCount64();
    Say("Paketi sarı noktaya götür");
}

void TdUpdate(float dt) {
    TopDown& g = g_td;
    if (g_over) {
        if (Edge(VK_RETURN)) TopDownReset();
        return;
    }
    Vehicle& me = g.me;
    const bool gas = Down('W'), brake = Down('S');
    const float steer = (Down('D') ? 1.f : 0.f) - (Down('A') ? 1.f : 0.f);
    if (gas) me.speed += 240.f * dt;
    if (brake) me.speed -= 300.f * dt;
    me.speed *= (1.f - 0.9f * dt);  // sürtünme
    me.speed = std::max(-90.f, std::min(250.f, me.speed));
    me.ang += steer * 2.6f * dt * std::min(1.f, std::abs(me.speed) / 70.f) * (me.speed >= 0 ? 1.f : -1.f);
    const float nx = me.x + std::cos(me.ang) * me.speed * dt, ny = me.y + std::sin(me.ang) * me.speed * dt;
    if (!OnRoad(nx, ny) && !OnRoad(nx + std::cos(me.ang) * 9, ny + std::sin(me.ang) * 9)) {  // bina: çarpma
        if (std::abs(me.speed) > 50.f) {
            g.health -= static_cast<int>(std::abs(me.speed) / 12.f);
            Say("Duvara çarptın!");
        }
        me.speed *= -0.3f;
    } else {
        me.x = nx;
        me.y = ny;
    }

    // trafik
    for (auto& v : g.traffic) {
        const float dx = std::cos(v.dir * kPi / 2), dy = std::sin(v.dir * kPi / 2);
        // durmuş (önünde araç/oyuncu): yavaşla
        float spd = v.speed;
        const float ax = v.x + dx * 40, ay = v.y + dy * 40;
        if (std::hypot(ax - me.x, ay - me.y) < 26.f) spd = 0;
        for (const auto& o : g.traffic) {
            if (&o != &v && std::hypot(ax - o.x, ay - o.y) < 22.f) spd = 0;
        }
        v.x += dx * spd * dt;
        v.y += dy * spd * dt;
        // kavşakta ara sıra dön
        float fx = std::fmod(v.x, kBlock), fy = std::fmod(v.y, kBlock);
        if (fx < 0) fx += kBlock;
        if (fy < 0) fy += kBlock;
        if (fx < kRoad && fy < kRoad && std::rand() % 400 == 0) {
            v.dir = (v.dir + (std::rand() % 2 ? 1 : 3)) % 4;
            v.ang = v.dir * kPi / 2;
            const float cx = std::floor(v.x / kBlock) * kBlock + kRoad / 2, cy = std::floor(v.y / kBlock) * kBlock + kRoad / 2;
            if (v.dir == 0 || v.dir == 2) { v.y = cy + (v.dir == 0 ? 12.f : -12.f); }
            else { v.x = cx - (v.dir == 1 ? 12.f : -12.f); }
        }
        v.ang = v.dir * kPi / 2;
        // oyuncuyla çarpışma
        if (std::hypot(v.x - me.x, v.y - me.y) < 17.f) {
            const unsigned long long now = GetTickCount64();
            if (now - g.last_hit > 600) {
                g.last_hit = now;
                g.health -= 4 + static_cast<int>(std::abs(me.speed) / 25.f);
                if (std::abs(me.speed) > 90.f && g.wanted < 1) g.wanted = 1;
                Say("Çarpıştın!");
            }
            me.speed *= 0.5f;
            const float ang = std::atan2(me.y - v.y, me.x - v.x);
            me.x += std::cos(ang) * 4.f;
            me.y += std::sin(ang) * 4.f;
        }
    }
    for (auto& v : g.traffic) {  // uzaklaşan arabayı yeniden doğur
        if (std::hypot(v.x - me.x, v.y - me.y) > 700.f) v = SpawnTraffic();
    }

    // yayalar
    const unsigned long long now = GetTickCount64();
    for (auto& p : g.peds) {
        if (p.dead) {
            if (now - p.dead_at > 4000) {
                const float ang = Rnd() * 2 * kPi;
                p.x = me.x + std::cos(ang) * (380.f + Rnd() * 100.f);
                p.y = me.y + std::sin(ang) * (380.f + Rnd() * 100.f);
                p.dead = false;
            }
            continue;
        }
        if (std::rand() % 90 == 0) p.a += (Rnd() - 0.5f) * 2.f;
        const float fl = std::hypot(p.x - me.x, p.y - me.y) < 70.f ? 55.f : 14.f;  // oyuncu yaklaşınca kaçar
        if (fl > 20.f) p.a = std::atan2(p.y - me.y, p.x - me.x);
        p.x += std::cos(p.a) * fl * dt;
        p.y += std::sin(p.a) * fl * dt;
        if (std::hypot(p.x - me.x, p.y - me.y) < 11.f && std::abs(me.speed) > 40.f) {
            p.dead = true;
            p.dead_at = now;
            g.score += 10;
            g.wanted = std::min(3, g.wanted + 1);
            g.wanted_at = now;
            Say("Yayaya çarptın! Aranıyorsun");
        }
        if (std::hypot(p.x - me.x, p.y - me.y) > 700.f) {
            const float ang = Rnd() * 2 * kPi;
            p.x = me.x + std::cos(ang) * (380.f + Rnd() * 100.f);
            p.y = me.y + std::sin(ang) * (380.f + Rnd() * 100.f);
        }
    }

    // polis: arama seviyesi kadar araç kovalar
    while (static_cast<int>(g.cops.size()) < g.wanted) {
        Vehicle c = SpawnTraffic();
        c.cop = true;
        c.color = 0x2a4cff;
        c.speed = 0;
        const float ang = Rnd() * 2 * kPi;
        c.x = me.x + std::cos(ang) * 420.f;
        c.y = me.y + std::sin(ang) * 420.f;
        g.cops.push_back(c);
    }
    for (auto& c : g.cops) {
        const float want = std::atan2(me.y - c.y, me.x - c.x);
        float d = want - c.ang;
        while (d > kPi) d -= 2 * kPi;
        while (d < -kPi) d += 2 * kPi;
        c.ang += std::max(-2.2f * dt, std::min(2.2f * dt, d));
        c.speed = std::min(175.f + 25.f * g.wanted, c.speed + 200.f * dt);
        c.x += std::cos(c.ang) * c.speed * dt;
        c.y += std::sin(c.ang) * c.speed * dt;
        if (std::hypot(c.x - me.x, c.y - me.y) < 17.f) {
            if (std::abs(me.speed) < 90.f) {
                g.health -= static_cast<int>(60.f * dt) + 1;  // yakalama: yavaşsan hızla can kaybedersin
                if (g.health <= 0) Say("Yakalandın!");
            }
            me.speed *= 0.7f;
        }
    }
    if (g.wanted > 0 && now - g.wanted_at > 22000) {  // saklanırsan arama düşer
        bool near_cop = false;
        for (const auto& c : g.cops) near_cop = near_cop || std::hypot(c.x - me.x, c.y - me.y) < 220.f;
        if (!near_cop) {
            --g.wanted;
            g.wanted_at = now;
            while (static_cast<int>(g.cops.size()) > g.wanted) g.cops.pop_back();
            Say(g.wanted ? "Arama seviyesi düştü" : "Kurtuldun");
        } else {
            g.wanted_at = now - 12000;
        }
    }

    // görev
    if (std::hypot(g.mx - me.x, g.my - me.y) < 26.f) {
        g.score += 100;
        ++g.delivered;
        Say("Paket teslim edildi: +$100");
        NewTarget();
    }
    if (g.health <= 0) {
        g.health = 0;
        g_over = true;
        if (g.score > g_best[2]) g_best[2] = g.score;
    }
}

Frame TdDraw() {
    const TopDown& g = g_td;
    Frame f;
    f.title = "Tepeden Suç";
    f.w = 400;
    f.h = 400;
    f.bg = 0x3a3d44;  // asfalt
    const float cx = g.me.x - kView / 2, cy = g.me.y - kView / 2;
    const auto sx = [&](float x) { return x - cx; };
    const auto sy = [&](float y) { return y - cy; };
    // bloklar (binalar) + kaldırım
    const int b0x = static_cast<int>(std::floor(cx / kBlock)) - 1, b1x = static_cast<int>(std::floor((cx + kView) / kBlock)) + 1;
    const int b0y = static_cast<int>(std::floor(cy / kBlock)) - 1, b1y = static_cast<int>(std::floor((cy + kView) / kBlock)) + 1;
    for (int by = b0y; by <= b1y; ++by) {
        for (int bx = b0x; bx <= b1x; ++bx) {
            const float x = bx * kBlock + kRoad, y = by * kBlock + kRoad, s = kBlock - kRoad;
            const unsigned h = static_cast<unsigned>(bx * 73856093) ^ static_cast<unsigned>(by * 19349663);
            const int tone = 0x70 + static_cast<int>(h % 60);
            const int col = (tone << 16) | ((tone - 10) << 8) | (tone - 20 + static_cast<int>((h >> 8) % 20));
            f.shapes.push_back(R(sx(x), sy(y), s, s, 0x8a8d94));                  // kaldırım
            f.shapes.push_back(R(sx(x) + 8, sy(y) + 8, s - 16, s - 16, col));     // bina
            f.shapes.push_back(R(sx(x) + 16, sy(y) + 16, s - 32, s - 32, (col & 0xfefefe) >> 1 | 0x303030));  // çatı
        }
    }
    // şerit çizgileri
    for (int by = b0y; by <= b1y; ++by) {
        for (int bx = b0x; bx <= b1x; ++bx) {
            const float x = bx * kBlock, y = by * kBlock;
            f.shapes.push_back(L(sx(x), sy(y) + kRoad / 2, sx(x) + kBlock, sy(y) + kRoad / 2, 0x6b6e76, 1.f));
            f.shapes.push_back(L(sx(x) + kRoad / 2, sy(y), sx(x) + kRoad / 2, sy(y) + kBlock, 0x6b6e76, 1.f));
        }
    }
    // görev noktası
    f.shapes.push_back(C(sx(g.mx), sy(g.my), 20.f, 0xf5d142));
    f.shapes.push_back(T(sx(g.mx), sy(g.my) - 8, "$", 16, 0x222222, 1));
    // yayalar
    for (const auto& p : g.peds) {
        if (p.dead) f.shapes.push_back(C(sx(p.x), sy(p.y), 4.f, 0xa01818));
        else f.shapes.push_back(C(sx(p.x), sy(p.y), 3.f, p.color));
    }
    for (const auto& v : g.traffic) f.shapes.push_back(Car(sx(v.x), sy(v.y), 24, 12, v.ang, v.color));
    const unsigned long long now = GetTickCount64();
    for (const auto& c : g.cops) {
        f.shapes.push_back(Car(sx(c.x), sy(c.y), 26, 13, c.ang, ((now / 200) % 2) ? 0xff3030 : 0x3050ff));
    }
    f.shapes.push_back(Car(sx(g.me.x), sy(g.me.y), 26, 13, g.me.ang, g.me.color));
    // hedef ok: ekran dışındaysa kenarda nokta
    {
        const float tx = sx(g.mx), ty = sy(g.my);
        if (tx < 6 || tx > kView - 6 || ty < 6 || ty > kView - 6) {
            const float a = std::atan2(g.my - g.me.y, g.mx - g.me.x);
            f.shapes.push_back(C(kView / 2 + std::cos(a) * 170.f, kView / 2 + std::sin(a) * 170.f, 6.f, 0xf5d142));
        }
    }
    // HUD
    f.shapes.push_back(R(0, 0, 400, 26, 0x000000));
    f.shapes.push_back(T(8, 5, "$" + std::to_string(g.score) + "  En iyi $" + std::to_string(g_best[2]) + "  Teslim " + std::to_string(g.delivered), 14, 0xffffff));
    std::string stars;
    for (int i = 0; i < 3; ++i) stars += i < g.wanted ? "*" : "-";
    f.shapes.push_back(T(392, 5, "Aranma " + stars, 14, g.wanted ? 0xff7070 : 0x9aa4b4, 2));
    f.shapes.push_back(R(8, 30, 100, 8, 0x401010));
    f.shapes.push_back(R(8, 30, static_cast<float>(std::max(0, g.health)), 8, 0x56c271));
    if (now < g.msg_until) f.shapes.push_back(T(200, 48, g.msg, 16, 0xffe08a, 1));
    if (g_over) {
        f.shapes.push_back(R(60, 150, 280, 100, 0x000000, 10));
        f.shapes.push_back(T(200, 164, g.health <= 0 ? "Bitti" : "Oyun bitti", 26, 0xff6060, 1));
        f.shapes.push_back(T(200, 202, "Skor $" + std::to_string(g.score), 18, 0xffffff, 1));
        f.shapes.push_back(T(200, 226, "Enter: yeniden   Backspace: çık", 13, 0xcfe0ff, 1));
    } else {
        f.shapes.push_back(T(8, 384, "W/S gaz-fren   A/D direksiyon   Backspace: çık", 12, 0xd0d6e0));
    }
    return f;
}
}  // namespace

void Start(Id id) {
    g_id = id;
    g_last = GetTickCount64();
    if (id == Id::Snake) SnakeReset();
    else if (id == Id::TopDown) TopDownReset();
}

bool Active() { return g_id != Id::None; }
Id Current() { return g_id; }
int Best(Id id) { return g_best[static_cast<int>(id) & 3 % 3]; }
void SetBest(Id id, int score) {
    const int i = static_cast<int>(id);
    if (i >= 1 && i <= 2) g_best[i] = std::max(g_best[i], score);
}

bool Update() {
    if (g_id == Id::None) return false;
    const unsigned long long now = GetTickCount64();
    float dt = static_cast<float>(now - g_last) / 1000.f;
    g_last = now;
    dt = std::min(dt, 0.05f);
    if (Edge(VK_BACK)) {
        g_id = Id::None;
        return true;
    }
    if (g_id == Id::Snake) SnakeUpdate();
    else TdUpdate(dt);
    return false;
}

Frame Draw() {
    if (g_id == Id::Snake) return SnakeDraw();
    if (g_id == Id::TopDown) return TdDraw();
    return Frame{};
}

}  // namespace hikaye::minigames
