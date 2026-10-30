#include "businesses.h"

#include <windows.h>

#include <cmath>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "blips.h"
#include "economy.h"
#include "fuel.h"
#include "json.hpp"

namespace hikaye::businesses {

using nlohmann::json;

namespace {
struct Place {
    std::string type, name;
    Vector3 pos{};
    Blip blip = 0;
};
std::vector<Place> g_places;
json g_types = json::object();
bool g_blips_made = false;
unsigned long long g_toast_until = 0;
std::string g_toast;

float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
}  // namespace

void Load() {
    g_places.clear();
    try {
        std::ifstream tin(std::string(kBaseDir) + "/data/business_types.json");
        if (tin) g_types = json::parse(tin);
        std::ifstream hin(std::string(kBaseDir) + "/data/places/hangout_spots.json");
        if (hin) {
            const json hj = json::parse(hin);
            for (const auto& it : hj.value("items", json::array())) {
                Place p;
                p.type = it.value("type", "");
                p.name = it.value("name", "");
                if (!it.contains("pos") || !it["pos"].is_array() || it["pos"].size() < 3) continue;
                const auto& v = it["pos"];
                p.pos = {v[0].get<float>(), 0, v[1].get<float>(), 0, v[2].get<float>(), 0};
                if (!p.type.empty() && !p.name.empty()) g_places.push_back(std::move(p));
            }
        } else {
            Log("hangout_spots.json yok; restoran/kafe bulusma noktasi kullanilamaz");
        }
        std::ifstream in(std::string(kBaseDir) + "/data/places/businesses.json");
        if (!in) {
            Log("businesses.json yok (tools/fetch_businesses.py ile uretilir)");
        } else {
            const json j = json::parse(in);
            for (const auto& it : j.value("items", json::array())) {
                Place p;
                p.type = it.value("type", "");
                p.name = it.value("name", "");
                const auto& v = it["pos"];
                p.pos = {v[0].get<float>(), 0, v[1].get<float>(), 0, v[2].get<float>(), 0};
                g_places.push_back(std::move(p));
            }
        }
        // İşimize yarayan hizmet noktaları: hastane (ölünce uyandığın yer), karakol (tutuklanınca götürüldüğün yer), itfaiye (hizmet çağrısı)
        std::ifstream sin(std::string(kBaseDir) + "/data/places/aliases_tr.json");
        if (sin) {
            const json aj = json::parse(sin);
            for (const char* svc : {"hospital", "police", "fire"}) {
                const json def = aj.value("services", json::object()).value(svc, json::object());
                for (const auto& pt : def.value("points", json::array())) {
                    if (!pt.contains("pos") || !pt["pos"].is_array() || pt["pos"].size() < 3) continue;
                    Place p;
                    p.type = svc;
                    p.name = pt.value("name", def.value("name", std::string(svc)));
                    const auto& v = pt["pos"];
                    p.pos = {v[0].get<float>(), 0, v[1].get<float>(), 0, v[2].get<float>(), 0};
                    g_places.push_back(std::move(p));
                }
            }
        }
    } catch (const std::exception& e) {
        Log(std::string("businesses.Load HATA: ") + e.what());
    }
    Log("isletme: " + std::to_string(g_places.size()) + " konum");
}

bool OwnsBlip(Blip b) {
    if (b == 0) return false;
    for (const auto& p : g_places) {
        if (p.blip == b) return true;
    }
    return false;
}

std::vector<Spot> Spots(const std::string& type) {
    std::vector<Spot> out;
    for (const auto& p : g_places) {
        if (p.type == type) out.push_back({p.name, p.pos});
    }
    return out;
}

bool NearType(const std::string& type, const Vector3& pos, float radius) {
    for (const auto& p : g_places) {
        if (p.type == type && D2(pos, p.pos) <= radius) return true;
    }
    return false;
}

void Reset() {
    for (auto& p : g_places) blips::Remove(p.blip);
    g_blips_made = false;
}

void Update(const Vector3& pos, bool e_pressed) {
    const unsigned long long now = GetTickCount64();
    if (!g_blips_made) {  // ikonlar: harita kapağında her zaman, mini haritada yakınken
        for (auto& p : g_places) {
            const json t = g_types.value(p.type, json::object());
            if (t.empty() || !t.value("enabled", true)) continue;
            p.blip = blips::Add(p.pos, t.value("sprite", 52), t.value("colour", 0), t.value("name", p.name), t.value("scale", 0.8f), true);
        }
        g_blips_made = true;
    }
    if (now < g_toast_until) DrawLine(g_toast, 0.86f, 150, 235, 170);
}

}  // namespace hikaye::businesses
