"""Bir kerelik yama: işletme (dükkan, benzin istasyonu) harita ikonları."""
import pathlib

R = pathlib.Path(__file__).resolve().parents[1]


def edit(rel, pairs):
    p = R / rel
    c = p.read_text(encoding="utf-8-sig")
    for old, new in pairs:
        assert old in c, (rel, old[:80])
        c = c.replace(old, new, 1)
    p.write_text(c, encoding="utf-8")


edit("mod/HikayeTurkRP.vcxproj", [('    <ClCompile Include="src\\fuel.cpp" />', '    <ClCompile Include="src\\fuel.cpp" />\n    <ClCompile Include="src\\blips.cpp" />')])

# --- ShopManager ---
edit("mod/src/shop_manager.h", [
    ("    int ground_tries = 0;", "    int ground_tries = 0;\n    std::string type;  // car_dealer | market ...\n    Blip blip = 0;     // haritadaki işletme ikonu"),
    ("    void DespawnAll();", "    void DespawnAll();  // NPC'ler ve harita ikonları\n    bool OwnsBlip(Blip b) const;  // story blip gizleme listesinden muaf tutmak için"),
    ("    bool Spawn(Shop& shop);", "    void EnsureBlips();\n    void RemoveBlips();\n    bool Spawn(Shop& shop);"),
])
edit("mod/src/shop_manager.cpp", [
    ('#include "json.hpp"', '#include "blips.h"\n#include "json.hpp"'),
    ('            s.name = j.value("name", s.id);', '            s.name = j.value("name", s.id);\n            s.type = j.value("type", "");'),
    ("void ShopManager::Update(const Vector3& player_pos) {\n", "void ShopManager::EnsureBlips() {\n"
     "    for (auto& s : shops_) {\n"
     "        if (s.blip != 0 && UI::DOES_BLIP_EXIST(s.blip)) continue;\n"
     "        // Sprite: 225 araba bayisi, 226 motosiklet, 52 market. Harita kapağında her zaman görünür, mini haritada yakınken.\n"
     "        int sprite = 52, colour = 2;\n"
     "        if (s.type == \"car_dealer\") {\n"
     "            sprite = s.id.find(\"motor\") != std::string::npos ? 226 : 225;\n"
     "            colour = 3;\n"
     "        }\n"
     "        s.blip = blips::Add(s.pos, sprite, colour, s.name, 0.9f, true);\n"
     "    }\n"
     "}\n\n"
     "void ShopManager::RemoveBlips() {\n"
     "    for (auto& s : shops_) blips::Remove(s.blip);\n"
     "}\n\n"
     "bool ShopManager::OwnsBlip(Blip b) const {\n"
     "    if (b == 0) return false;\n"
     "    for (const auto& s : shops_) {\n"
     "        if (s.blip == b) return true;\n"
     "    }\n"
     "    return false;\n"
     "}\n\n"
     "void ShopManager::Update(const Vector3& player_pos) {\n    EnsureBlips();\n"),
    ("void ShopManager::DespawnAll() {\n    for (auto& s : shops_) Despawn(s);", "void ShopManager::DespawnAll() {\n    RemoveBlips();\n    for (auto& s : shops_) Despawn(s);"),
])

# --- fuel: istasyon ikonları ---
edit("mod/src/fuel.h", [("void Reset();", "bool OwnsBlip(Blip b);  // istasyon ikonları story blip gizlemesinden muaf\nvoid Reset();")])
edit("mod/src/fuel.cpp", [
    ('#include "economy.h"', '#include "blips.h"\n#include "economy.h"'),
    ("std::vector<Vector3> g_stations;", "std::vector<Vector3> g_stations;\nstd::vector<Blip> g_blips;"),
    ("void Reset() {\n", "bool OwnsBlip(Blip b) {\n    if (b == 0) return false;\n    for (Blip x : g_blips) {\n        if (x == b) return true;\n    }\n    return false;\n}\n\n"
     "void Reset() {\n    for (Blip& b : g_blips) blips::Remove(b);\n    g_blips.clear();\n"),
    ("    e_used = false;\n    const unsigned long long now = GetTickCount64();",
     "    e_used = false;\n"
     "    if (g_blips.empty()) {  // benzin istasyonu ikonları (sprite 361)\n"
     "        for (const auto& s : g_stations) g_blips.push_back(blips::Add(s, 361, 1, \"Benzin Istasyonu\", 0.7f, true));\n"
     "    }\n"
     "    const unsigned long long now = GetTickCount64();"),
])
# --- main ---
edit("mod/src/main.cpp", [
    ("story_blips.Update(mode.active(), [&](Blip b) { return missions.OwnsBlip(b); });",
     "story_blips.Update(mode.active(), [&](Blip b) { return missions.OwnsBlip(b) || shops.OwnsBlip(b) || fuel::OwnsBlip(b); });"),
])
print("ok")
