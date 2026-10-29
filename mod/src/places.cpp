#include "places.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "json.hpp"

using nlohmann::json;

namespace hikaye {

namespace {
json ReadJson(const std::string& name) {
    std::ifstream in(std::string(kBaseDir) + "/data/places/" + name);
    if (!in) {
        Log("places: dosya yok: " + name);
        return json::object();
    }
    try {
        return json::parse(in);
    } catch (const std::exception& e) {
        Log("places: " + name + " okunamadi: " + e.what());
        return json::object();
    }
}

Vector3 V3(const json& p) { return {p[0].get<float>(), 0, p[1].get<float>(), 0, p[2].get<float>(), 0}; }
}  // namespace

float Places::Dist2D(const Vector3& a, const Vector3& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

std::string Places::Fold(const std::string& s) {
    // Küçük harf + Türkçe harfleri ASCII'ye çevir + harf/rakam dışını boşluk yap.
    static const char* const map[][2] = {
        {"ç", "c"}, {"Ç", "c"}, {"ğ", "g"}, {"Ğ", "g"}, {"ı", "i"}, {"İ", "i"},
        {"ö", "o"}, {"Ö", "o"}, {"ş", "s"}, {"Ş", "s"}, {"ü", "u"}, {"Ü", "u"},
    };
    std::string out;
    for (size_t i = 0; i < s.size();) {
        bool hit = false;
        for (const auto& m : map) {
            const size_t n = std::char_traits<char>::length(m[0]);
            if (s.compare(i, n, m[0]) == 0) {
                out += m[1];
                i += n;
                hit = true;
                break;
            }
        }
        if (hit) continue;
        const unsigned char c = static_cast<unsigned char>(s[i++]);
        if (c >= 'A' && c <= 'Z') out += static_cast<char>(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += static_cast<char>(c);
        else out += ' ';
    }
    // Ardışık boşlukları teke indir.
    std::string squeezed;
    for (char c : out) {
        if (c == ' ' && (squeezed.empty() || squeezed.back() == ' ')) continue;
        squeezed += c;
    }
    while (!squeezed.empty() && squeezed.back() == ' ') squeezed.pop_back();
    return squeezed;
}

void Places::AddKey(const std::string& raw, Entry::Kind kind, size_t index) {
    const std::string key = Fold(raw);
    if (key.size() >= 2) entries_.push_back({kind, key, index});
}

void Places::Load() {
    zones_.clear();
    groups_.clear();
    entries_.clear();

    const json zones = ReadJson("zones.json");
    if (zones.is_array()) {
        for (const auto& z : zones) {
            Zone zone;
            zone.id = z.value("id", "");
            zone.name = z.value("name", zone.id);
            for (const auto& b : z.at("bounds")) {
                zone.bounds.push_back({b[0].get<float>(), b[1].get<float>(), b[2].get<float>(), b[3].get<float>()});
            }
            zones_.push_back(std::move(zone));
            AddKey(zones_.back().name, Entry::Kind::Zone, zones_.size() - 1);  // "Davis", "Vespucci Beach"...
        }
    }
    auto zone_index = [&](const std::string& id) -> int {
        for (size_t i = 0; i < zones_.size(); ++i) {
            if (zones_[i].id == id) return static_cast<int>(i);
        }
        return -1;
    };

    const json aliases = ReadJson("aliases_tr.json");
    // NOT: value(...) KOPYA döndürür; .items() geçici kopyaya bakar ve döngüden önce silinir (çökme sebebi).
    // Bu yüzden kopyalar önce yerel değişkende tutulur.
    const json zone_aliases = aliases.value("zone_aliases", json::object());
    const json poi_aliases = aliases.value("poi_aliases", json::object());
    const json services = aliases.value("services", json::object());
    for (const auto& [alias, id] : zone_aliases.items()) {
        const int zi = zone_index(id.get<std::string>());
        if (zi >= 0) AddKey(alias, Entry::Kind::Zone, static_cast<size_t>(zi));
    }

    // Yakın nokta türleri (ATM, benzinlik...)
    const json pois = ReadJson("pois.json");
    static const std::pair<const char*, const char*> kPoiLabels[] = {
        {"atm", "ATM"}, {"gas_station", "Benzinlik"}, {"bus_stop", "Otobüs durağı"}, {"phone_booth", "Telefon kulübesi"}};
    std::vector<std::pair<std::string, size_t>> poi_group;  // tür -> groups_ indeksi
    for (const auto& [type, label] : kPoiLabels) {
        if (!pois.contains(type)) continue;
        Group g;
        g.label = label;
        for (const auto& p : pois[type]) g.points.push_back({label, V3(p.at("pos"))});
        groups_.push_back(std::move(g));
        poi_group.push_back({type, groups_.size() - 1});
    }
    for (const auto& [alias, type] : poi_aliases.items()) {
        for (const auto& [t, gi] : poi_group) {
            if (t == type.get<std::string>()) AddKey(alias, Entry::Kind::Group, gi);
        }
    }

    // Hizmetler (hastane, karakol, itfaiye)
    for (const auto& [svc, def] : services.items()) {
        Group g;
        g.label = def.value("name", svc);
        for (const auto& p : def.value("points", json::array())) g.points.push_back({p.value("name", g.label), V3(p.at("pos"))});
        groups_.push_back(std::move(g));
        for (const auto& k : def.value("keys", json::array())) AddKey(k.get<std::string>(), Entry::Kind::Group, groups_.size() - 1);
    }
    Log("places yuklendi: " + std::to_string(zones_.size()) + " bolge, " + std::to_string(groups_.size()) + " grup");
}

void Places::AddNamed(const std::string& name, const Vector3& pos) {
    named_.push_back({name, pos});
    AddKey(name, Entry::Kind::Named, named_.size() - 1);
}

bool Places::ZoneCenter(const Zone& z, Place& out) const {
    // En geniş dikdörtgenin merkezi (bölgeler birden çok parçadan oluşabilir).
    const std::array<float, 4>* best = nullptr;
    float best_area = -1.f;
    for (const auto& b : z.bounds) {
        const float area = (b[2] - b[0]) * (b[3] - b[1]);
        if (area > best_area) {
            best_area = area;
            best = &b;
        }
    }
    if (!best) return false;
    out.name = z.name;
    out.pos = {((*best)[0] + (*best)[2]) / 2, 0, ((*best)[1] + (*best)[3]) / 2, 0, 0, 0};
    return true;
}

bool Places::Resolve(const std::string& query, const Vector3& from, Place& out) const {
    const std::string q = " " + Fold(query) + " ";
    const Entry* best = nullptr;
    for (const auto& e : entries_) {
        // Kelime sınırıyla eşleştir ("durak" -> "otobus duragi" değil); Türkçe ekler için başlangıç eşleşmesi yeter:
        // "havaalanina" anahtarı "havaalani" ile başlar, bu yüzden anahtarın solunda sınır, sağında ek olabilir.
        const size_t pos = q.find(" " + e.key);
        if (pos == std::string::npos) continue;
        if (!best || e.key.size() > best->key.size()) best = &e;  // en uzun anahtar kazanır
    }
    if (!best) return false;

    switch (best->kind) {
        case Entry::Kind::Zone:
            return ZoneCenter(zones_[best->index], out);
        case Entry::Kind::Named:
            out = named_[best->index];
            return true;
        case Entry::Kind::Group: {
            const Group& g = groups_[best->index];
            const Place* nearest = nullptr;
            float nd = 1e12f;
            for (const auto& p : g.points) {
                const float d = Dist2D(from, p.pos);
                if (d < nd) {
                    nd = d;
                    nearest = &p;
                }
            }
            if (!nearest) return false;
            out = *nearest;  // "en yakın X"
            return true;
        }
    }
    return false;
}

std::string Places::ZoneNameAt(const Vector3& p) const {
    const Zone* best = nullptr;
    float best_area = 1e30f;
    for (const auto& z : zones_) {
        for (const auto& b : z.bounds) {
            if (p.x >= b[0] && p.x <= b[2] && p.y >= b[1] && p.y <= b[3]) {
                const float area = (b[2] - b[0]) * (b[3] - b[1]);
                if (area < best_area) {  // en küçük kapsayan bölge en ayrıntılısıdır
                    best_area = area;
                    best = &z;
                }
            }
        }
    }
    return best ? best->name : "";
}

std::string Places::Compass(const Vector3& from, const Vector3& to) {
    // GTA: +X doğu, +Y kuzey. Açı 0 = kuzey, saat yönünde artar.
    const float deg = std::atan2(to.x - from.x, to.y - from.y) * 57.29578f;
    static const char* const dirs[] = {"kuzey", "kuzeydoğu", "doğu", "güneydoğu", "güney", "güneybatı", "batı", "kuzeybatı"};
    const int idx = static_cast<int>(std::floor((deg + 360.f + 22.5f) / 45.f)) % 8;
    return dirs[idx];
}

}  // namespace hikaye
