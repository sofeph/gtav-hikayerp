#include "clerks.h"
#include "interaction.h"

#include <windows.h>

#include <cmath>

namespace hikaye {

Shop* Interaction::Update(ShopManager& shops, const Vector3& player_pos) {
    Shop* nearest = nullptr;
    float best = 1e9f;
    for (auto& s : shops.shops()) {
        if (s.ped == 0) continue;
        const float dx = player_pos.x - s.pos.x, dy = player_pos.y - s.pos.y, dz = player_pos.z - s.pos.z;
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d <= s.interact_radius && d < best) {
            best = d;
            nearest = &s;
        }
    }
    if (nearest) {
        // Kullanıcıya görünen metin Türkçe (CLAUDE.md §14).
        ShowHelp(("~INPUT_PICKUP~ Konus: " + (nearest->npc_name.empty() ? nearest->name : nearest->npc_name)).c_str());
    }
    return nearest;
}

Ped Interaction::UpdateStreet(ShopManager& shops, const Vector3& player_pos) {
    constexpr float kStreetRadius = 2.5f;
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (scan_tick_++ % 6 == 0) {
        street_candidate_ = 0;
        float best = kStreetRadius;
        int peds[128];
        const int n = worldGetAllPeds(peds, 128);
        for (int i = 0; i < n; ++i) {
            const Ped p = peds[i];
            if (p == player || !ENTITY::DOES_ENTITY_EXIST(p) || ENTITY::IS_ENTITY_DEAD(p)) continue;
            if (PED::IS_PED_A_PLAYER(p) || !PED::IS_PED_HUMAN(p) || PED::IS_PED_IN_ANY_VEHICLE(p, false)) continue;
            const int type = PED::GET_PED_TYPE(p);
            if (type == 6 || type == 27 || type == 28 || type == 29) continue;  // polis, swat, hayvan, asker
            bool is_shop_npc = false;
            for (const auto& s : shops.shops()) is_shop_npc |= (s.ped == p);
            if (is_shop_npc) continue;
            if (IsNativeVendorModel(ENTITY::GET_ENTITY_MODEL(p))) continue;  // oyunun kendi satıcıları sohbete açık değil
            const Vector3 c = ENTITY::GET_ENTITY_COORDS(p, true);
            const float dx = player_pos.x - c.x, dy = player_pos.y - c.y, dz = player_pos.z - c.z;
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d < best) {
                best = d;
                street_candidate_ = p;
            }
        }
    }
    if (street_candidate_ != 0 &&
        (!ENTITY::DOES_ENTITY_EXIST(street_candidate_) || ENTITY::IS_ENTITY_DEAD(street_candidate_))) {
        street_candidate_ = 0;
    }
    if (street_candidate_ != 0) ShowHelp("~INPUT_PICKUP~ Konus");
    return street_candidate_;
}

bool Interaction::InteractPressed() {
    const bool down = (GetAsyncKeyState('E') & 0x8000) != 0;
    const bool pressed = down && !was_down_;
    was_down_ = down;
    return pressed;
}

}  // namespace hikaye
