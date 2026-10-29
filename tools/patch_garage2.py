"""Bir kerelik yama: garaj (koy/al), sahip olunan araç ikonu (GTA Online mantığı), kilitsiz araç hırsızlığı ve çalınma uyarısı."""
import json
import pathlib

R = pathlib.Path(__file__).resolve().parents[1]


def edit(rel, pairs):
    p = R / rel
    c = p.read_text(encoding="utf-8-sig")
    for old, new in pairs:
        assert old in c, (rel, old[:80])
        c = c.replace(old, new, 1)
    p.write_text(c, encoding="utf-8")


# --- ayarlar ---
(R / "data" / "vehicle.json").write_text(json.dumps({
    "_not": "Araç sahipliği. theft: kilitsiz, boşta duran SENİN araçlarından her period_s saniyede (gerçek zaman) chance olasılıkla biri çalınır. "
            "far_m'den uzaktaysan araç sessizce silinir ve uyarı çıkar; yakındaysan bir hırsız araca binip kaçar. Garaja koyulan araç çalınamaz.",
    "theft": {"enabled": True, "period_s": 3600, "chance": 0.20, "far_m": 120},
    "garage_radius_m": 18}, ensure_ascii=False, indent=2), encoding="utf-8")
bt = R / "data" / "business_types.json"
btj = json.loads(bt.read_text(encoding="utf-8"))
btj["garage"] = {"sprite": 357, "colour": 3, "name": "Garaj", "scale": 0.8}
bt.write_text(json.dumps(btj, ensure_ascii=False, indent=2), encoding="utf-8")

# --- businesses: tür yakınlığı ---
edit("mod/src/businesses.h", [("void Reset();", "// Oyuncu bu türden bir işletmeye (ör. \"garage\") radius içinde mi?\nbool NearType(const std::string& type, const Vector3& pos, float radius);\nvoid Reset();"),
                              ('#include "common.h"', '#include <string>\n\n#include "common.h"')])
edit("mod/src/businesses.cpp", [("void Reset() {", "bool NearType(const std::string& type, const Vector3& pos, float radius) {\n    for (const auto& p : g_places) {\n        if (p.type == type && D2(pos, p.pos) <= radius) return true;\n    }\n    return false;\n}\n\nvoid Reset() {")])

# --- garage.h ---
edit("mod/src/garage.h", [
    ("    void Update();    // her frame (mod aktifken)", "    void Update();    // her frame (mod aktifken): ikonlar, hırsızlık, vale\n    // Garaj noktasında: araçtaysan 'garaja koy', yaya isen 'garajdan al'. true: E bu sistem tarafından kullanıldı.\n    bool UpdateGarageSpots(const Vector3& player_pos, bool e_pressed);\n    bool OwnsBlip(Blip b) const;"),
    ("        float fuel = 100.f;\n        Vehicle handle = 0;", "        float fuel = 100.f;\n        Vehicle handle = 0;      // 0: garajda (dünyada yok)\n        Blip blip = 0;           // sahip olunan araç ikonu (araçta değilken görünür)\n        unsigned long long unlocked_since = 0, next_roll = 0;  // hırsızlık zarı"),
    ("    std::vector<Owned> cars_;", "    struct Theft {\n        bool active = false;\n        Vehicle veh = 0;\n        Ped thief = 0;\n        bool driving = false;\n        unsigned long long since = 0;\n    };\n    struct Config {\n        bool theft = true;\n        int period_s = 3600;\n        float chance = 0.20f, far_m = 120.f, garage_radius = 18.f;\n    };\n    void ManageOwned(unsigned long long now, const Vector3& pos, Vehicle player_veh);\n    void UpdateTheft(unsigned long long now, const Vector3& pos);\n    void StartTheft(size_t i, const Vector3& pos);\n    void Remove(Vehicle veh);  // çalındı: garajdan silinir\n    Vehicle SpawnOwned(Owned& car, const Vector3& at);\n    void DropBlip(Owned& c);\n    Config cfg_;\n    Theft theft_;\n    unsigned long long toast_until_ = 0;\n    std::string toast_;\n    std::vector<Owned> cars_;"),
])
# --- garage.cpp ---
edit("mod/src/garage.cpp", [
    ('#include "economy.h"', '#include "businesses.h"\n#include "economy.h"'),
    ('#include "fuel.h"', '#include "fuel.h"\n#include "street_events.h"'),
    ("    Log(\"garaj: \" + std::to_string(cars_.size()) + \" arac (slot \" + std::to_string(slot) + \")\");",
     "    try {  // data/vehicle.json: hırsızlık ayarları\n"
     "        std::ifstream cin2(std::string(kBaseDir) + \"/data/vehicle.json\");\n"
     "        if (cin2) {\n"
     "            const json c = json::parse(cin2);\n"
     "            const json t = c.value(\"theft\", json::object());\n"
     "            cfg_.theft = t.value(\"enabled\", true);\n"
     "            cfg_.period_s = std::max(60, t.value(\"period_s\", 3600));\n"
     "            cfg_.chance = t.value(\"chance\", 0.20f);\n"
     "            cfg_.far_m = t.value(\"far_m\", 120.f);\n"
     "            cfg_.garage_radius = c.value(\"garage_radius_m\", 18.f);\n"
     "        }\n"
     "    } catch (...) {\n"
     "    }\n"
     "    Log(\"garaj: \" + std::to_string(cars_.size()) + \" arac (slot \" + std::to_string(slot) + \")\");"),
    ("    if (now - last_save_ > 30000ULL) {\n        last_save_ = now;\n        Save();\n    }\n",
     "    if (now - last_save_ > 30000ULL) {\n        last_save_ = now;\n        Save();\n    }\n"
     "    ManageOwned(now, ENTITY::GET_ENTITY_COORDS(player, true), pv);\n"
     "    UpdateTheft(now, ENTITY::GET_ENTITY_COORDS(player, true));\n"
     "    if (now < toast_until_) DrawLine(toast_, 0.74f, 255, 120, 90);\n"),
    ("bool Garage::IsOwned(Vehicle v) const {",
     "bool Garage::OwnsBlip(Blip b) const {\n    if (b == 0) return false;\n    for (const auto& c : cars_) {\n        if (c.blip == b) return true;\n    }\n    return false;\n}\n\n"
     "void Garage::DropBlip(Owned& c) {\n    if (c.blip != 0 && UI::DOES_BLIP_EXIST(c.blip)) UI::REMOVE_BLIP(&c.blip);\n    c.blip = 0;\n}\n\n"
     "// GTA Online mantığı: sahip olunan araç haritada ikonla görünür (içindeyken görünmez). Kilitsiz ve boşta duran araç belli aralıkla çalınabilir.\n"
     "void Garage::ManageOwned(unsigned long long now, const Vector3& pos, Vehicle pv) {\n"
     "    for (size_t i = 0; i < cars_.size(); ++i) {\n"
     "        Owned& c = cars_[i];\n"
     "        if (c.handle == 0 || !ENTITY::DOES_ENTITY_EXIST(c.handle)) {\n"
     "            DropBlip(c);\n"
     "            continue;\n"
     "        }\n"
     "        const bool player_in = pv == c.handle;\n"
     "        if (player_in) {\n"
     "            DropBlip(c);\n"
     "        } else if (c.blip == 0 || !UI::DOES_BLIP_EXIST(c.blip)) {\n"
     "            c.blip = UI::ADD_BLIP_FOR_ENTITY(c.handle);\n"
     "            UI::SET_BLIP_SPRITE(c.blip, 225);\n"
     "            UI::SET_BLIP_COLOUR(c.blip, 3);\n"
     "            UI::SET_BLIP_SCALE(c.blip, 0.85f);\n"
     "            UI::SET_BLIP_AS_SHORT_RANGE(c.blip, false);\n"
     "            UI::_SET_TEXT_ENTRY(const_cast<char*>(\"STRING\"));\n"
     "            UI::_ADD_TEXT_COMPONENT_STRING(const_cast<char*>(\"Kendi Araciniz\"));\n"
     "            UI::END_TEXT_COMMAND_SET_BLIP_NAME(c.blip);\n"
     "        }\n"
     "        if (!cfg_.theft || theft_.active) continue;\n"
     "        const bool unlocked = VEHICLE::GET_VEHICLE_DOOR_LOCK_STATUS(c.handle) != 2;\n"
     "        const bool in_valet = valet_.active && valet_.veh == c.handle;\n"
     "        const bool idle = unlocked && !player_in && !in_valet && VEHICLE::GET_PED_IN_VEHICLE_SEAT(c.handle, -1) == 0;\n"
     "        if (!idle) {\n"
     "            c.unlocked_since = 0;\n"
     "            continue;\n"
     "        }\n"
     "        const unsigned long long period = static_cast<unsigned long long>(cfg_.period_s) * 1000ULL;\n"
     "        if (c.unlocked_since == 0) {\n"
     "            c.unlocked_since = now;\n"
     "            c.next_roll = now + period;\n"
     "        } else if (now >= c.next_roll) {\n"
     "            c.next_roll += period;\n"
     "            if (street_events::Chance(cfg_.chance)) StartTheft(i, pos);\n"
     "        }\n"
     "    }\n"
     "}\n\n"
     "void Garage::Remove(Vehicle veh) {\n"
     "    for (size_t i = 0; i < cars_.size(); ++i) {\n"
     "        if (cars_[i].handle != veh) continue;\n"
     "        DropBlip(cars_[i]);\n"
     "        cars_.erase(cars_.begin() + static_cast<long>(i));\n"
     "        break;\n"
     "    }\n"
     "    if (ENTITY::DOES_ENTITY_EXIST(veh)) {\n"
     "        Vehicle v = veh;\n"
     "        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(v, true, true);\n"
     "        VEHICLE::DELETE_VEHICLE(&v);\n"
     "    }\n"
     "    last_used_ = 0;\n"
     "    Save();\n"
     "}\n\n"
     "void Garage::StartTheft(size_t i, const Vector3& pos) {\n"
     "    Owned& c = cars_[i];\n"
     "    const Vector3 vp = ENTITY::GET_ENTITY_COORDS(c.handle, true);\n"
     "    const float d = D2(pos, vp);\n"
     "    if (d > cfg_.far_m) {  // uzaktayken çalındı: araç sessizce silinir, uyarı çıkar\n"
     "        Log(\"hirsizlik: \" + c.model + \" uzakta calindi\");\n"
     "        toast_ = \"Arabanız çalındı! (\" + c.model + \")\";\n"
     "        toast_until_ = GetTickCount64() + 8000;\n"
     "        Remove(c.handle);\n"
     "        return;\n"
     "    }\n"
     "    // yakındaysa hırsız görünür: araca binip kaçar\n"
     "    Hash tm = HashKey(\"g_m_y_mexgoon_01\");\n"
     "    if (!LoadModel(tm)) {\n"
     "        Remove(c.handle);\n"
     "        toast_ = \"Arabanız çalındı!\";\n"
     "        toast_until_ = GetTickCount64() + 8000;\n"
     "        return;\n"
     "    }\n"
     "    const Vector3 side = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(c.handle, -3.0f, 0.f, 0.f);\n"
     "    const Ped th = PED::CREATE_PED(4, tm, side.x, side.y, side.z, 0.f, false, false);\n"
     "    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(tm);\n"
     "    if (!ENTITY::DOES_ENTITY_EXIST(th)) {\n"
     "        Remove(c.handle);\n"
     "        toast_ = \"Arabanız çalındı!\";\n"
     "        toast_until_ = GetTickCount64() + 8000;\n"
     "        return;\n"
     "    }\n"
     "    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(th, true);\n"
     "    VEHICLE::SET_VEHICLE_DOORS_LOCKED(c.handle, 1);\n"
     "    AI::TASK_ENTER_VEHICLE(th, c.handle, 10000, -1, 2.0f, 1, 0);\n"
     "    theft_ = Theft{true, c.handle, th, false, GetTickCount64()};\n"
     "    toast_ = \"Biri arabanıza giriyor!\";\n"
     "    toast_until_ = GetTickCount64() + 6000;\n"
     "    Log(\"hirsizlik basladi: \" + c.model);\n"
     "}\n\n"
     "void Garage::UpdateTheft(unsigned long long now, const Vector3& pos) {\n"
     "    if (!theft_.active) return;\n"
     "    const Ped player = PLAYER::PLAYER_PED_ID();\n"
     "    if (!ENTITY::DOES_ENTITY_EXIST(theft_.thief) || !ENTITY::DOES_ENTITY_EXIST(theft_.veh) || ENTITY::IS_ENTITY_DEAD(theft_.thief)) {\n"
     "        theft_.active = false;\n"
     "        return;\n"
     "    }\n"
     "    if (PED::IS_PED_IN_VEHICLE(player, theft_.veh, false)) {  // sen bindin: hırsız kaçar\n"
     "        AI::TASK_SMART_FLEE_PED(theft_.thief, player, 120.f, -1, false, false);\n"
     "        Ped t = theft_.thief;\n"
     "        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&t);\n"
     "        theft_.active = false;\n"
     "        toast_ = \"Hırsızı püskürttün!\";\n"
     "        toast_until_ = now + 4000;\n"
     "        return;\n"
     "    }\n"
     "    const bool in = PED::IS_PED_IN_VEHICLE(theft_.thief, theft_.veh, false) != 0;\n"
     "    if (!in && now - theft_.since > 15000ULL) {  // binemedi: vazgeçer\n"
     "        Ped t = theft_.thief;\n"
     "        AI::TASK_WANDER_STANDARD(t, 10.0f, 10);\n"
     "        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&t);\n"
     "        theft_.active = false;\n"
     "        return;\n"
     "    }\n"
     "    if (in && !theft_.driving) {\n"
     "        AI::TASK_VEHICLE_DRIVE_WANDER(theft_.thief, theft_.veh, 26.0f, 786603);\n"
     "        theft_.driving = true;\n"
     "        toast_ = \"Arabanız çalınıyor!\";\n"
     "        toast_until_ = now + 6000;\n"
     "    }\n"
     "    if (in) {\n"
     "        const Vector3 vp = ENTITY::GET_ENTITY_COORDS(theft_.veh, true);\n"
     "        if (D2(pos, vp) > cfg_.far_m + 80.f || now - theft_.since > 60000ULL) {  // gözden kayboldu: araç silinir\n"
     "            Ped t = theft_.thief;\n"
     "            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(t, true, true);\n"
     "            PED::DELETE_PED(&t);\n"
     "            const Vehicle v = theft_.veh;\n"
     "            theft_.active = false;\n"
     "            Remove(v);\n"
     "            toast_ = \"Arabanız çalındı!\";\n"
     "            toast_until_ = now + 8000;\n"
     "            Log(\"hirsizlik: arac calindi\");\n"
     "        }\n"
     "    }\n"
     "}\n\n"
     "Vehicle Garage::SpawnOwned(Owned& car, const Vector3& at) {\n"
     "    const Hash model = HashKey(car.model.c_str());\n"
     "    if (!LoadModel(model)) return 0;\n"
     "    const Vehicle veh = VEHICLE::CREATE_VEHICLE(model, at.x, at.y, at.z, 0.f, false, false);\n"
     "    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);\n"
     "    if (!ENTITY::DOES_ENTITY_EXIST(veh)) return 0;\n"
     "    if (!car.plate.empty()) VEHICLE::SET_VEHICLE_NUMBER_PLATE_TEXT(veh, const_cast<char*>(car.plate.c_str()));\n"
     "    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(veh, true, true);\n"
     "    VEHICLE::SET_VEHICLE_HAS_BEEN_OWNED_BY_PLAYER(veh, true);\n"
     "    VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(veh);\n"
     "    fuel::Set(veh, car.fuel);\n"
     "    car.handle = veh;\n"
     "    return veh;\n"
     "}\n\n"
     "bool Garage::UpdateGarageSpots(const Vector3& pos, bool e_pressed) {\n"
     "    if (cars_.empty() || !businesses::NearType(\"garage\", pos, cfg_.garage_radius)) return false;\n"
     "    const Ped player = PLAYER::PLAYER_PED_ID();\n"
     "    const Vehicle pv = PED::GET_VEHICLE_PED_IS_IN(player, false);\n"
     "    if (pv != 0 && IsOwned(pv) && VEHICLE::GET_PED_IN_VEHICLE_SEAT(pv, -1) == player) {\n"
     "        ShowHelp(\"~INPUT_PICKUP~ Araci garaja koy\");\n"
     "        if (!e_pressed) return false;\n"
     "        for (auto& c : cars_) {\n"
     "            if (c.handle != pv) continue;\n"
     "            const float f = fuel::Get(pv);\n"
     "            if (f >= 0.f) c.fuel = f;\n"
     "            DropBlip(c);\n"
     "            Vehicle v = pv;\n"
     "            AI::TASK_LEAVE_VEHICLE(player, v, 16);  // çıkar (hemen)\n"
     "            for (int i = 0; i < 40 && PED::IS_PED_IN_VEHICLE(player, v, false); ++i) WAIT(0);\n"
     "            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(v, true, true);\n"
     "            VEHICLE::DELETE_VEHICLE(&v);\n"
     "            c.handle = 0;\n"
     "            c.unlocked_since = 0;\n"
     "            toast_ = \"Araç garaja konuldu (güvende)\";\n"
     "            toast_until_ = GetTickCount64() + 4000;\n"
     "            Save();\n"
     "            break;\n"
     "        }\n"
     "        return true;\n"
     "    }\n"
     "    if (pv != 0) return false;\n"
     "    // yaya: garajdaki (dünyada olmayan) aracı al; son kullanılan öncelikli\n"
     "    int pick = -1;\n"
     "    if (last_used_ < cars_.size() && cars_[last_used_].handle == 0) pick = static_cast<int>(last_used_);\n"
     "    for (size_t i = 0; pick < 0 && i < cars_.size(); ++i) {\n"
     "        if (cars_[i].handle == 0) pick = static_cast<int>(i);\n"
     "    }\n"
     "    if (pick < 0) return false;\n"
     "    ShowHelp((\"~INPUT_PICKUP~ Garajdan arac al: \" + cars_[static_cast<size_t>(pick)].model).c_str());\n"
     "    if (!e_pressed) return false;\n"
     "    Vector3 at = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(player, 0.f, 5.f, 0.f);\n"
     "    Vector3 n{};\n"
     "    if (PATHFIND::GET_CLOSEST_VEHICLE_NODE(pos.x, pos.y, pos.z, &n, 1, 3.0f, 0)) at = n;\n"
     "    const Vehicle v = SpawnOwned(cars_[static_cast<size_t>(pick)], at);\n"
     "    if (v == 0) {\n"
     "        toast_ = \"Araç garajdan çıkarılamadı\";\n"
     "    } else {\n"
     "        VEHICLE::SET_VEHICLE_DOORS_LOCKED(v, 1);\n"
     "        last_used_ = static_cast<size_t>(pick);\n"
     "        toast_ = \"Araç garajdan çıkarıldı\";\n"
     "    }\n"
     "    toast_until_ = GetTickCount64() + 4000;\n"
     "    return true;\n"
     "}\n\n"
     "bool Garage::IsOwned(Vehicle v) const {"),
    # çıkışta: sahip olunan araçlar dünyada bırakılmaz (garaja döner)
    ("    for (auto& c : cars_) c.handle = 0;  // araçlar dünyada kalabilir; bir sonraki girişte kayıttan yeniden doğar",
     "    for (auto& c : cars_) {  // moddan çıkarken araçlar garaja döner (yakıt kaydedilir), dünyada sızıntı kalmaz\n"
     "        DropBlip(c);\n"
     "        if (c.handle != 0 && ENTITY::DOES_ENTITY_EXIST(c.handle)) {\n"
     "            const float f = fuel::Get(c.handle);\n"
     "            if (f >= 0.f) c.fuel = f;\n"
     "            Vehicle v = c.handle;\n"
     "            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(v, true, true);\n"
     "            VEHICLE::DELETE_VEHICLE(&v);\n"
     "        }\n"
     "        c.handle = 0;\n"
     "    }\n"
     "    theft_ = Theft{};\n"
     "    Save();"),
])

# --- main ---
edit("mod/src/main.cpp", [
    ("|| businesses::OwnsBlip(b) || transit::OwnsBlip(b);", "|| businesses::OwnsBlip(b) || transit::OwnsBlip(b) || garage.OwnsBlip(b);"),
    ("            businesses::Update(pos, e_pressed && !conversation.active());\n",
     "            businesses::Update(pos, e_pressed && !conversation.active());\n            garage.UpdateGarageSpots(pos, e_pressed && !conversation.active());\n"),
])
print("ok")
