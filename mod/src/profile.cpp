#include "profile.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

using nlohmann::json;

namespace hikaye {

namespace {
// GTA Online'daki ebeveyn yüz kimlikleri: anneler 21-41 ve 45, babalar 0-20 ve 42-44.
constexpr int kMothers[kMotherCount] = {21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 45};
constexpr int kFathers[kFatherCount] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 42, 43, 44};

constexpr int kCompIds[] = {1, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr int kPropIds[] = {0, 1, 2, 6, 7};

std::string SavePath(int slot = 0) {
    return std::string(kBaseDir) + (slot == 0 ? "/saves/character.json" : "/saves/character_2.json");
}

int Clamp(int v, int lo, int hi) { return std::max(lo, std::min(v, hi)); }
}  // namespace

CharacterProfile::CharacterProfile() { ResetOutfit(); }

void CharacterProfile::ResetOutfit() {
    // Online karakter oluşturucunun varsayılanına yakın: sade beyaz tişört, kot, spor ayakkabı (sonra seçilir).
    for (auto& c : comps) c = {0, 0};
    comps[3] = {15, 0};
    comps[8] = {15, 0};
    comps[11] = {15, 0};
    comps[4] = {female ? 15 : 21, 0};
    comps[6] = {female ? 35 : 34, 0};
}

json CharacterProfile::ToJson() const {
    json j = {{"name", name},     {"female", female},     {"mother", mother},           {"father", father},
              {"shape_mix", shape_mix}, {"skin_mix", skin_mix}, {"hair", hair},         {"hair_color", hair_color},
              {"hair_highlight", hair_highlight}, {"eye_color", eye_color},
              {"hometown", hometown}, {"district", district}, {"military_unit", military_unit},
              {"past_job", past_job}, {"team", team}, {"cash", cash}};
    j["face"] = json::array();
    for (float f : face) j["face"].push_back(f);
    j["overlays"] = json::array();
    for (const auto& o : overlays) j["overlays"].push_back({{"i", o.index}, {"o", o.opacity}, {"c", o.color}});
    j["comps"] = json::array();
    for (const auto& c : comps) j["comps"].push_back({c.drawable, c.texture});
    j["props"] = json::array();
    for (const auto& p : props) j["props"].push_back({p.drawable, p.texture});
    j["weapons"] = json::array();
    for (const auto& w : weapons) j["weapons"].push_back({{"w", w.first}, {"a", w.second}});
    if (has_pos) j["pos"] = {x, y, z, heading};
    return j;
}

CharacterProfile CharacterProfile::FromJson(const json& j) {
    CharacterProfile p;
    p.name = j.value("name", std::string());
    p.female = j.value("female", false);
    p.mother = Clamp(j.value("mother", 0), 0, kMotherCount - 1);
    p.father = Clamp(j.value("father", 0), 0, kFatherCount - 1);
    p.shape_mix = j.value("shape_mix", 0.5f);
    p.skin_mix = j.value("skin_mix", 0.5f);
    p.hair = j.value("hair", 0);
    p.hair_color = j.value("hair_color", 0);
    p.hair_highlight = j.value("hair_highlight", 0);
    p.eye_color = j.value("eye_color", 0);
    p.hometown = j.value("hometown", "");
    p.district = j.value("district", "");
    p.military_unit = j.value("military_unit", "");
    p.past_job = j.value("past_job", "");
    p.team = j.value("team", "");
    p.cash = j.value("cash", 5000);
    if (j.contains("weapons") && j["weapons"].is_array()) {
        for (const auto& w : j["weapons"]) p.weapons.push_back({w.value("w", std::string()), w.value("a", 0)});
    }
    if (j.contains("face")) {
        for (size_t i = 0; i < 20 && i < j["face"].size(); ++i) p.face[i] = j["face"][i].get<float>();
    }
    if (j.contains("overlays")) {
        for (size_t i = 0; i < 13 && i < j["overlays"].size(); ++i) {
            const auto& o = j["overlays"][i];
            p.overlays[i] = {o.value("i", 255), o.value("o", 1.f), o.value("c", 0)};
        }
    }
    if (j.contains("comps")) {
        for (size_t i = 0; i < 12 && i < j["comps"].size(); ++i) p.comps[i] = {j["comps"][i][0].get<int>(), j["comps"][i][1].get<int>()};
    }
    if (j.contains("props")) {
        for (size_t i = 0; i < 10 && i < j["props"].size(); ++i) p.props[i] = {j["props"][i][0].get<int>(), j["props"][i][1].get<int>()};
    }
    if (j.contains("pos") && j["pos"].size() == 4) {
        p.has_pos = true;
        p.x = j["pos"][0].get<float>();
        p.y = j["pos"][1].get<float>();
        p.z = j["pos"][2].get<float>();
        p.heading = j["pos"][3].get<float>();
    }
    return p;
}

bool LoadProfile(CharacterProfile& out, int slot) {
    try {
        std::ifstream in(SavePath(slot));
        if (!in) return false;
        out = CharacterProfile::FromJson(json::parse(in));
        return true;
    } catch (const std::exception& e) {
        Log(std::string("karakter kaydi okunamadi: ") + e.what());
        return false;
    }
}

bool SaveProfile(const CharacterProfile& p, int slot) {
    try {
        std::filesystem::create_directories(std::string(kBaseDir) + "/saves");
        std::ofstream out(SavePath(slot));
        out << p.ToJson().dump(2);
        return static_cast<bool>(out);
    } catch (const std::exception& e) {
        Log(std::string("karakter kaydedilemedi: ") + e.what());
        return false;
    }
}

void ApplyToPed(Ped ped, const CharacterProfile& p) {
    if (!ENTITY::DOES_ENTITY_EXIST(ped)) return;
    const int mom = kMothers[Clamp(p.mother, 0, kMotherCount - 1)];
    const int dad = kFathers[Clamp(p.father, 0, kFatherCount - 1)];
    PED::SET_PED_HEAD_BLEND_DATA(ped, mom, dad, 0, mom, dad, 0, p.shape_mix, p.skin_mix, 0.f, false);
    for (int i = 0; i < 20; ++i) PED::_SET_PED_FACE_FEATURE(ped, i, p.face[i]);

    PED::SET_PED_COMPONENT_VARIATION(ped, 2, p.hair, 0, 0);
    PED::_SET_PED_HAIR_COLOR(ped, p.hair_color, p.hair_highlight);
    PED::_SET_PED_EYE_COLOR(ped, p.eye_color);

    for (int id = 0; id < 13; ++id) {
        const auto& o = p.overlays[id];
        PED::SET_PED_HEAD_OVERLAY(ped, id, o.index, o.opacity);
        if (o.index == 255) continue;
        if (id == 1 || id == 2 || id == 10) PED::_SET_PED_HEAD_OVERLAY_COLOR(ped, id, 1, p.hair_color, p.hair_highlight);  // sakal, kaş, göğüs kılı saçla uyumlu
        else if (id == 4 || id == 5 || id == 8) PED::_SET_PED_HEAD_OVERLAY_COLOR(ped, id, 2, o.color, o.color);  // makyaj, allık, ruj
    }
    for (int id : kCompIds) PED::SET_PED_COMPONENT_VARIATION(ped, id, p.comps[id].drawable, p.comps[id].texture, 0);
    for (int id : kPropIds) {
        if (p.props[id].drawable < 0) PED::CLEAR_PED_PROP(ped, id);
        else PED::SET_PED_PROP_INDEX(ped, id, p.props[id].drawable, p.props[id].texture, true);
    }
}

}  // namespace hikaye
