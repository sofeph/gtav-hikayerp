#include "interiors.h"

#include <windows.h>

#include <cmath>
#include <fstream>

#include "json.hpp"

namespace hikaye {

using nlohmann::json;

void Interiors::Load() {
    ipls_.clear();
    portals_.clear();
    try {
        std::ifstream in(std::string(kBaseDir) + "/data/interiors.json");
        if (!in) return;
        const json j = json::parse(in);
        ipls_ = j.value("ipls", std::vector<std::string>());
        for (const auto& p : j.value("portals", json::array())) {
            Portal q;
            q.name = p.value("name", "");
            q.enter_prompt = p.value("enter_prompt", "Iceri gir");
            q.exit_prompt = p.value("exit_prompt", "Disari cik");
            q.radius = p.value("radius", 4.5f);
            for (int i = 0; i < 4; ++i) {
                q.out[i] = p["outside"].at(i).get<float>();
                q.in[i] = p["inside"].at(i).get<float>();
            }
            portals_.push_back(std::move(q));
        }
    } catch (const std::exception& e) {
        Log(std::string("interiors.Load HATA: ") + e.what());
    }
}

void Interiors::Enable() {
    if (enabled_) return;
    for (auto& s : ipls_) STREAMING::REQUEST_IPL(const_cast<char*>(s.c_str()));
    enabled_ = true;
    Log("iç mekan IPL'leri istendi: " + std::to_string(ipls_.size()));
}

void Interiors::Disable() {
    inside_ = false;
    if (!enabled_) return;
    for (auto& s : ipls_) STREAMING::REMOVE_IPL(const_cast<char*>(s.c_str()));
    enabled_ = false;
}

bool Interiors::Teleport(const float p[4], bool into_interior) {
    const Ped ped = PLAYER::PLAYER_PED_ID();
    int interior = 0;
    if (into_interior) {
        // Önce iç mekanı belleğe sabitle ve hazır olmasını bekle; yoksa dışarıdaki tünele/boşluğa düşülür.
        interior = INTERIOR::GET_INTERIOR_AT_COORDS(p[0], p[1], p[2]);
        if (interior == 0 || !INTERIOR::IS_VALID_INTERIOR(interior)) {
            Log("interiors: ic mekan bulunamadi (IPL yuklenmedi), isinlanmadi");
            return false;
        }
        INTERIOR::_0x2CA429C029CCF247(interior);  // PIN_INTERIOR_IN_MEMORY
        STREAMING::SET_INTERIOR_ACTIVE(interior, true);
        INTERIOR::REFRESH_INTERIOR(interior);
        for (int i = 0; i < 240 && !INTERIOR::IS_INTERIOR_READY(interior); ++i) {
            STREAMING::REQUEST_COLLISION_AT_COORD(p[0], p[1], p[2]);
            WAIT(0);
        }
        if (!INTERIOR::IS_INTERIOR_READY(interior)) {
            // Bazı IPL'lerde "hazır" bayrağı hiç dönmez ama içeri girilebilir; iç mekan bulunduysa yine de ışınla.
            Log("interiors: ic mekan 'hazir' degil, yine de isinlaniyor");
        }
    }
    CAM::DO_SCREEN_FADE_OUT(400);
    for (int i = 0; i < 30 && !CAM::IS_SCREEN_FADED_OUT(); ++i) WAIT(0);
    ENTITY::FREEZE_ENTITY_POSITION(ped, true);  // çarpışma yüklenene kadar düşmesin
    ENTITY::SET_ENTITY_COORDS(ped, p[0], p[1], p[2], false, false, false, true);
    ENTITY::SET_ENTITY_HEADING(ped, p[3]);
    for (int i = 0; i < 120 && !ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(ped); ++i) {
        STREAMING::REQUEST_COLLISION_AT_COORD(p[0], p[1], p[2]);
        WAIT(0);
    }
    ENTITY::FREEZE_ENTITY_POSITION(ped, false);
    CAM::DO_SCREEN_FADE_IN(400);
    return true;
}

bool Interiors::ConsumeTransition(bool& entering, float dest[4], Vector3& from) {
    if (!trans_) return false;
    trans_ = false;
    entering = trans_enter_;
    for (int i = 0; i < 4; ++i) dest[i] = trans_dest_[i];
    from = trans_from_;
    return true;
}

bool Interiors::Update(const Vector3& pos, bool e_pressed) {
    if (!enabled_) return false;
    if (GetTickCount64() < notice_until_) ShowHelp(notice_.c_str());
    if (PED::IS_PED_IN_ANY_VEHICLE(PLAYER::PLAYER_PED_ID(), false)) return false;
    for (const auto& p : portals_) {
        const float d_out = std::hypot(pos.x - p.out[0], pos.y - p.out[1]);
        const float d_in = std::hypot(pos.x - p.in[0], pos.y - p.in[1]);
        if (d_out > 80.f && d_in > 80.f) inside_ = false;  // başka yolla uzaklaşıldı (menyoo, ölüm...): sıfırla
        const bool near_out = !inside_ && d_out < p.radius && std::fabs(pos.z - p.out[2]) < 4.f;
        const bool near_in = inside_ && d_in < 2.0f && std::fabs(pos.z - p.in[2]) < 3.f;  // içeride: kapıya yakın dururken çıkış
        if (!near_out && !near_in) continue;
        ShowHelp(("~INPUT_PICKUP~ " + (near_out ? p.enter_prompt : p.exit_prompt)).c_str());
        if (e_pressed) {
            const Vector3 before = pos;
            if (Teleport(near_out ? p.in : p.out, near_out)) {
                trans_ = true;
                inside_ = near_out;  // girdiysek içerideyiz, çıktıysak dışarıdayız
                trans_enter_ = near_out;
                for (int i = 0; i < 4; ++i) trans_dest_[i] = near_out ? p.in[i] : p.out[i];
                trans_from_ = before;
            } else {
                notice_ = "Ic mekan yuklenemedi";
                notice_until_ = GetTickCount64() + 3000;
            }
            return true;
        }
    }
    return false;
}

}  // namespace hikaye
