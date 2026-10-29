#pragma once
#include <string>

#include "json.hpp"

namespace hikaye::profile_gen {

// Dükkan/görevli NPC tanımındaki eksik alanları (cinsiyet, ad, model, ses, profil, kişilik)
// havuzdan (data/profile_pool.json) üretir. Elle yazılmış alanlara dokunmaz.
// Aynı `seed_key` her zaman aynı karakteri verir (rapport tutarlı kalsın diye).
// Havuz boşsa npc olduğu gibi döner.
nlohmann::json Resolve(const nlohmann::json& npc, const std::string& seed_key, const nlohmann::json& pool);

// Arkadaşlık için kişisel ayrıntılar: yaş, doğum günü, burç, hobi, sevdiği yemek ve içecek. `seed_key` aynıysa sonuç aynıdır;
// çağıran bunu bir kez üretip kaydetmelidir (havuz sonradan değişse de kişi değişmesin).
// Dönüş: {age, birthday ("14 Mart"), zodiac ("Balık"), hobby, favorite_food, favorite_drink}
nlohmann::json GenerateDetails(const std::string& seed_key, const nlohmann::json& pool);

}  // namespace hikaye::profile_gen
