#pragma once
#include <string>
#include <vector>

#include "common.h"
#include "json.hpp"

namespace hikaye {

struct Shop {
    nlohmann::json npc_json;    // personality, profile, voice: session_start ile companion'a gider
    nlohmann::json inventory;  // fiyat/stok otoritesi modda; companion'a sadece id/ad/fiyat gider
    std::string id;
    std::string name;
    Vector3 pos{};
    float interact_radius = 2.5f;
    std::string npc_model;
    float npc_heading = 0.f;
    std::string npc_name;
    std::string npc_gender;  // "male" | "female"
    bool has_vehicle_spawn = false;  // yoksa araç oyuncunun önünde doğar
    Vector3 vehicle_spawn{};
    float vehicle_heading = 0.f;
    Ped ped = 0;  // 0: spawn edilmemiş
    int ground_tries = 0;
    std::string type;  // car_dealer | market ...
    Blip blip = 0;     // haritadaki işletme ikonu
};

class ShopManager {
public:
    // data/shops/*.json dosyalarını okur. Bozuk dosya atlanır, mod çökmez.
    void LoadAll();
    // Oyuncu yakınsa NPC'yi spawn eder, uzaklaşınca siler (streaming yükü için).
    void Update(const Vector3& player_pos);
    void DespawnAll();  // NPC'ler ve harita ikonları
    bool OwnsBlip(Blip b) const;  // story blip gizleme listesinden muaf tutmak için
    std::vector<Shop>& shops() { return shops_; }
    // Rastgele NPC havuzu (dükkan dışı NPC'ler için de kullanılır). Yoksa boş nesne.
    const nlohmann::json& pool() const { return pool_; }

private:
    nlohmann::json pool_ = nlohmann::json::object();
    void Restock();  // nadir eşyaların stok yenilemesi
    void EnsureBlips();
    void RemoveBlips();
    bool Spawn(Shop& shop);
    void Despawn(Shop& shop);

    std::vector<Shop> shops_;
};

}  // namespace hikaye
