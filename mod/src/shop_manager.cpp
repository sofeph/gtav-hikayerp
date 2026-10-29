#include "shop_manager.h"

#include <cmath>
#include <filesystem>
#include <random>
#include <fstream>

#include "blips.h"
#include "addons.h"
#include "json.hpp"
#include "profile_gen.h"
#include "vendors.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace hikaye {

namespace {
constexpr float kSpawnDist = 80.f;
constexpr float kDespawnDist = 120.f;

float Dist(const Vector3& a, const Vector3& b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
}  // namespace

void ShopManager::LoadAll() {
    shops_.clear();
    const fs::path dir = fs::path(kBaseDir) / "data" / "shops";
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        Log("shops klasörü yok: " + dir.string());
        return;
    }
    json pool = json::object();  // rastgele NPC havuzu; yoksa sadece elle yazılmış NPC'ler çalışır
    try {
        std::ifstream pin(fs::path(kBaseDir) / "data" / "profile_pool.json");
        if (pin) pool = json::parse(pin);
        else Log("profile_pool.json yok: rastgele NPC uretilemez");
    } catch (const std::exception& e) {
        Log(std::string("profile_pool.json okunamadi: ") + e.what());
    }
    pool_ = pool;
    std::vector<fs::path> shop_files;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() == ".json") shop_files.push_back(entry.path());
    }
    for (const auto& f : addons::Files("shops", ".json")) shop_files.push_back(f);  // eklenti dükkanları (oyun dosyalarına dokunmadan)
    for (const auto& file : shop_files) {
        try {
            std::ifstream in(file);
            json j = json::parse(in);
            Shop s;
            s.id = j.at("id").get<std::string>();
            s.name = j.value("name", s.id);
            s.type = j.value("type", "");
            const auto& p = j.at("pos");
            s.pos = {p[0].get<float>(), 0, p[1].get<float>(), 0, p[2].get<float>(), 0};
            s.interact_radius = j.value("interact_radius", 2.5f);
            const json npc = profile_gen::Resolve(j.at("npc"), s.id, pool);
            s.npc_json = npc;
            s.inventory = j.value("inventory", json::array());
            s.npc_model = npc.at("model").get<std::string>();
            s.npc_heading = npc.value("heading", 0.f);
            s.npc_gender = npc.value("gender", "male");
            if (npc.contains("profile")) s.npc_name = npc["profile"].value("name", "");
            if (j.contains("vehicle_spawn")) {  // [x, y, z, heading]
                const auto& v = j["vehicle_spawn"];
                s.vehicle_spawn = {v[0].get<float>(), 0, v[1].get<float>(), 0, v[2].get<float>(), 0};
                s.vehicle_heading = v[3].get<float>();
                s.has_vehicle_spawn = true;
            }
            shops_.push_back(std::move(s));
            Log("dukkan yuklendi: " + shops_.back().id + " (" + shops_.back().npc_name + ", " +
                shops_.back().npc_gender + ", " + shops_.back().npc_model + ")");
        } catch (const std::exception& e) {
            Log("dükkan okunamadı (" + file.string() + "): " + e.what());
        }
    }
}

bool ShopManager::Spawn(Shop& shop) {
    const Hash model = HashKey(shop.npc_model.c_str());
    if (!STREAMING::IS_MODEL_IN_CDIMAGE(model) || !STREAMING::IS_MODEL_VALID(model)) {
        Log("geçersiz NPC modeli: " + shop.npc_model);
        shop.npc_model.clear();  // tekrar tekrar denemesin
        return false;
    }
    STREAMING::REQUEST_MODEL(model);
    // Bloklama yok: model yüklenmediyse sonraki frame'de tekrar denenir.
    if (!STREAMING::HAS_MODEL_LOADED(model)) return false;

    // Zemine oturt: dükkan koordinatları yaklaşık olabilir (çarpışma yüklenene kadar kısa süre bekler).
    float z = shop.pos.z;
    STREAMING::REQUEST_COLLISION_AT_COORD(shop.pos.x, shop.pos.y, z);
    float gz = 0.f;
    // Önce verideki yüksekliğin hemen üstünden ara (iç mekanda çatıya/üst kata değil, tezgâhın zeminine otursun); bulunamazsa biraz daha yukarıdan.
    // Bulunan zemin verilen yükseklikten 2 m'den fazla yüksekse çatı sayılır ve reddedilir (işletme sahibi tepede doğmasın).
    bool found_ground = GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(shop.pos.x, shop.pos.y, z + 1.2f, &gz, false) && std::fabs(gz - z) < 3.f;
    if (!found_ground && GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(shop.pos.x, shop.pos.y, z + 3.0f, &gz, false) && gz <= z + 2.0f && gz >= z - 6.f) found_ground = true;
    if (found_ground) {
        z = gz;
        shop.pos.z = gz;  // etkileşim mesafesi de gerçek zemine göre
    } else if (++shop.ground_tries < 90) {
        return false;  // çarpışma yüklenene kadar bekle; sonunda verideki yükseklik aynen kullanılır (çatıya doğmaz)
    }
    // NPC yol ortasında durmasın: nokta yolun üstündeyse en yakın kaldırım/yaya güvenli noktasına kaydır (etkileşim konumu da onunla gelir).
    if (PATHFIND::IS_POINT_ON_ROAD(shop.pos.x, shop.pos.y, z, 0) && shop.type != "market") {
        bool moved = false;
        for (float r = 3.f; r <= 22.f && !moved; r += 3.f) {
            for (int k = 0; k < 8 && !moved; ++k) {
                const float a = static_cast<float>(k) * 0.785398f;
                const float cx = shop.pos.x + r * std::cos(a), cy = shop.pos.y + r * std::sin(a);
                Vector3 out{};
                if (PATHFIND::GET_SAFE_COORD_FOR_PED(cx, cy, z, true, &out, 16) && !PATHFIND::IS_POINT_ON_ROAD(out.x, out.y, out.z, 0) &&
                    std::fabs(out.z - z) < 3.f) {
                    Log("dukkan yolda cikti, kaldirima kaydirildi: " + shop.id + " (" + std::to_string(static_cast<int>(r)) + " m)");
                    shop.pos.x = out.x;
                    shop.pos.y = out.y;
                    shop.pos.z = z = out.z;
                    moved = true;
                }
            }
        }
    }
    const int ped_type = shop.npc_gender == "female" ? 5 /*PED_TYPE_CIVFEMALE*/ : 4 /*PED_TYPE_CIVMALE*/;
    Ped ped = PED::CREATE_PED(ped_type, model, shop.pos.x, shop.pos.y, z,
                              shop.npc_heading, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
    if (!ENTITY::DOES_ENTITY_EXIST(ped)) return false;

    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, true);  // kaçmasın, panik yapmasın
    PED::SET_PED_CAN_RAGDOLL(ped, false);
    ENTITY::SET_ENTITY_INVINCIBLE(ped, true);
    ENTITY::FREEZE_ENTITY_POSITION(ped, true);
    shop.ped = ped;
    Log("NPC spawn: " + shop.id);
    return true;
}

void ShopManager::Despawn(Shop& shop) {
    if (shop.ped == 0) return;
    Ped ped = shop.ped;
    if (ENTITY::DOES_ENTITY_EXIST(ped)) {
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped, true, true);
        PED::DELETE_PED(&ped);
    }
    shop.ped = 0;
    shop.ground_tries = 0;
}

void ShopManager::EnsureBlips() {
    for (auto& s : shops_) {
        if (s.blip != 0 && UI::DOES_BLIP_EXIST(s.blip)) continue;
        if (s.type == "street_vendor" && !vendors::Discovered(s.id)) continue;  // gizli satıcı: keşfedilene kadar harita ikonu yok
        // Sprite: 225 araba bayisi, 226 motosiklet, 52 market. Harita kapağında her zaman görünür, mini haritada yakınken.
        int sprite = 52, colour = 2;
        if (s.type == "car_dealer") {
            sprite = s.id.find("motor") != std::string::npos ? 226 : 225;
            colour = 3;
        }
        if (s.type == "street_vendor") {
            s.blip = blips::Add(s.pos, 52, 1, s.name + " (keşfedildi)", 0.9f, false);
            continue;
        }
        s.blip = blips::Add(s.pos, sprite, colour, s.name, 0.9f, true);
    }
}

void ShopManager::RemoveBlips() {
    for (auto& s : shops_) blips::Remove(s.blip);
}

bool ShopManager::OwnsBlip(Blip b) const {
    if (b == 0) return false;
    for (const auto& s : shops_) {
        if (s.blip == b) return true;
    }
    return false;
}

// Nadir eşyalar: item.restock = {every_min, weights:[w0,w1,...]} -> belli aralıkla stok ağırlıklı rastgele yenilenir (0 = tükendi).
void ShopManager::Restock() {
    static std::mt19937 rng(static_cast<unsigned>(GetTickCount64()));
    const unsigned long long now = GetTickCount64();
    for (auto& s : shops_) {
        for (auto& it : s.inventory) {
            if (!it.contains("restock") || !it["restock"].is_object()) continue;
            if (it.value("_next_restock", 0ULL) > now) continue;
            const json& r = it["restock"];
            std::vector<int> w = r.value("weights", std::vector<int>{1});
            if (w.empty()) w = {1};
            std::discrete_distribution<int> dist(w.begin(), w.end());
            it["stock"] = dist(rng);
            it["_next_restock"] = now + static_cast<unsigned long long>(std::max(1, r.value("every_min", 30))) * 60000ULL;
            Log("stok yenilendi: " + s.id + "/" + it.value("id", "") + " = " + std::to_string(it["stock"].get<int>()));
        }
    }
}

void ShopManager::Update(const Vector3& player_pos) {
    Restock();
    EnsureBlips();
    for (auto& s : shops_) {
        if (s.npc_model.empty()) continue;  // geçersiz model
        const float d = Dist(player_pos, s.pos);
        if (s.ped == 0 && d < kSpawnDist) {
            Spawn(s);
        } else if (s.ped != 0) {
            // Oyun NPC'yi kendisi silmiş olabilir (ör. oyuncu ölünce).
            if (!ENTITY::DOES_ENTITY_EXIST(s.ped)) s.ped = 0;
            else if (d > kDespawnDist) Despawn(s);
        }
    }
}

void ShopManager::DespawnAll() {
    RemoveBlips();
    for (auto& s : shops_) Despawn(s);
}

}  // namespace hikaye
