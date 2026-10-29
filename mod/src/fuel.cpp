#include "fuel.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <vector>

#include "blips.h"
#include "economy.h"
#include "json.hpp"

namespace hikaye::fuel {

using nlohmann::json;

namespace {
std::vector<Vector3> g_stations;
std::vector<Blip> g_blips;
std::map<Vehicle, float> g_fuel;       // araç tutamacı -> %
std::map<Vehicle, bool> g_warned;      // düşük yakıt uyarısı verildi mi
float g_price_per_pct = 1.2f;          // 1% yakıt = $1,2 (depo ~ $120)
float g_station_radius = 14.f;
unsigned long long g_last_ms = 0, g_clean_ms = 0, g_toast_until = 0;
std::string g_toast;

bool IsRoadVehicle(Vehicle v) {
    const Hash m = ENTITY::GET_ENTITY_MODEL(v);
    return VEHICLE::IS_THIS_MODEL_A_CAR(m) || VEHICLE::IS_THIS_MODEL_A_BIKE(m) || VEHICLE::IS_THIS_MODEL_A_QUADBIKE(m);
}

float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
}  // namespace

void Load() {
    g_stations.clear();
    try {
        std::ifstream cfgin(std::string(kBaseDir) + "/data/fuel.json");
        if (cfgin) {
            const json c = json::parse(cfgin);
            g_price_per_pct = c.value("price_per_pct", g_price_per_pct);
            g_station_radius = c.value("station_radius_m", g_station_radius);
        }
        std::ifstream in(std::string(kBaseDir) + "/data/places/pois.json");
        if (in) {
            const json j = json::parse(in);
            for (const auto& g : j.value("gas_station", json::array())) {
                const auto& p = g["pos"];
                g_stations.push_back({p[0].get<float>(), 0, p[1].get<float>(), 0, p[2].get<float>(), 0});
            }
        }
    } catch (const std::exception& e) {
        Log(std::string("fuel.Load HATA: ") + e.what());
    }
    Log("benzin sistemi: " + std::to_string(g_stations.size()) + " istasyon");
}

float Get(Vehicle v) {
    const auto it = g_fuel.find(v);
    return it == g_fuel.end() ? -1.f : it->second;
}

void Set(Vehicle v, float pct) {
    g_fuel[v] = pct < 0.f ? 0.f : (pct > 100.f ? 100.f : pct);
    g_warned[v] = false;
}

bool OwnsBlip(Blip b) {
    if (b == 0) return false;
    for (Blip x : g_blips) {
        if (x == b) return true;
    }
    return false;
}

void Reset() {
    for (Blip& b : g_blips) blips::Remove(b);
    g_blips.clear();
    g_fuel.clear();
    g_warned.clear();
    g_last_ms = 0;
}

void Update(const Vector3& pos, bool e_pressed, bool& e_used) {
    e_used = false;
    if (g_blips.empty()) {  // benzin istasyonu ikonları (sprite 361)
        for (const auto& s : g_stations) g_blips.push_back(blips::Add(s, 361, 1, "Benzin Istasyonu", 0.7f, true));
    }
    const unsigned long long now = GetTickCount64();
    const float dt = g_last_ms == 0 ? 0.f : std::min(0.25f, static_cast<float>(now - g_last_ms) / 1000.f);
    g_last_ms = now;

    if (now - g_clean_ms > 10000) {  // silinmiş araçları listeden at
        g_clean_ms = now;
        for (auto it = g_fuel.begin(); it != g_fuel.end();) {
            if (!ENTITY::DOES_ENTITY_EXIST(it->first)) {
                g_warned.erase(it->first);
                it = g_fuel.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (now < g_toast_until) DrawLine(g_toast, 0.86f, 255, 120, 90);

    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(player, false);
    if (v == 0 || !IsRoadVehicle(v) || VEHICLE::GET_PED_IN_VEHICLE_SEAT(v, -1) != player) return;

    float f = Get(v);
    if (f < 0.f) {  // yeni araç: dolu sayılmaz, rastgele ama makul bir seviye (satın alınan/çağrılan araçlar zaten Set edilir)
        f = 35.f + static_cast<float>((static_cast<unsigned>(v) * 2654435761u) % 5000u) / 100.f;
        Set(v, f);
    }
    const float speed = ENTITY::GET_ENTITY_SPEED(v);  // m/s
    if (VEHICLE::_IS_VEHICLE_ENGINE_ON(v) && f > 0.f) {
        f -= (0.008f + speed * 0.0035f) * dt;  // rölantide çok az, hızla artar (30 m/s ≈ 14 dk'da depo biter)
        if (f < 0.f) f = 0.f;
        g_fuel[v] = f;
    }
    if (f <= 0.f) {
        VEHICLE::SET_VEHICLE_ENGINE_ON(v, false, true, true);  // yakıt bitti: motor çalışmaz
        g_toast = "Yakıt bitti! Telefondan tamirciyi ara ya da benzin istasyonuna it.";
        g_toast_until = now + 2000;
    } else if (f < 15.f && !g_warned[v]) {
        g_warned[v] = true;
        g_toast = "Yakıt azalıyor: %" + std::to_string(static_cast<int>(f)) + ". Benzin istasyonu ara.";
        g_toast_until = now + 6000;
    }
    // HUD
    const int pct = static_cast<int>(std::ceil(f));
    DrawLine("Yakıt: %" + std::to_string(pct), 0.93f, f < 15.f ? 255 : 230, f < 15.f ? 120 : 230, f < 15.f ? 90 : 230);

    // İstasyonda dur ve E: depoyu doldur
    if (speed < 2.5f && f < 99.f) {
        for (const auto& s : g_stations) {
            if (D2(pos, s) > g_station_radius) continue;
            const int cost = static_cast<int>(std::ceil((100.f - f) * g_price_per_pct));
            ShowHelp(("~INPUT_PICKUP~ Depoyu doldur ($" + std::to_string(cost) + ")").c_str());
            if (e_pressed) {
                e_used = true;
                const int cash = economy::GetCash();
                if (cash < 0) break;
                int pay = std::min(cost, cash);
                if (pay <= 0) {
                    g_toast = "Yetersiz bakiye";
                    g_toast_until = now + 3000;
                    break;
                }
                const float added = pay >= cost ? (100.f - f) : static_cast<float>(pay) / g_price_per_pct;
                economy::Spend(pay);
                Set(v, f + added);
                VEHICLE::SET_VEHICLE_ENGINE_ON(v, true, true, false);
                g_toast = pay >= cost ? "Depo dolduruldu: $" + std::to_string(pay) : "Yarım dolduruldu (bakiye yetmedi): $" + std::to_string(pay);
                g_toast_until = now + 4000;
            }
            break;
        }
    }
}

}  // namespace hikaye::fuel
