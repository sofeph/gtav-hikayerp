"""Bir kerelik yama: araç kontrolleri (kilit, sinyal, dörtlü, kontak) için tuş ayarları ve bağlantılar."""
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


edit("mod/HikayeTurkRP.vcxproj", [('    <ClCompile Include="src\\blips.cpp" />', '    <ClCompile Include="src\\blips.cpp" />\n    <ClCompile Include="src\\vehctl.cpp" />')])

# controls
edit("mod/src/controls.h", [("int SwitchKey();", "int SwitchKey();\nint LockKey();         // araç kilidi (varsayılan K)\nint SignalLeftKey();   // sol sinyal (varsayılan NUM4)\nint SignalRightKey();  // sağ sinyal (varsayılan NUM6)\nint HazardKey();       // dörtlü (varsayılan NUM5)\nint EngineKey();       // kontak (varsayılan NUM8)")])
edit("mod/src/controls.cpp", [
    ("int g_switch = VK_F10;", "int g_switch = VK_F10;\nint g_lock = 'K', g_sig_l = VK_NUMPAD4, g_sig_r = VK_NUMPAD6, g_hazard = VK_NUMPAD5, g_engine = VK_NUMPAD8;"),
    ("    if (s.size() >= 2 && s[0] == 'F') {", "    if (s.size() == 4 && s.rfind(\"NUM\", 0) == 0 && s[3] >= '0' && s[3] <= '9') return VK_NUMPAD0 + (s[3] - '0');  // NUM0-NUM9\n    if (s.size() >= 2 && s[0] == 'F') {"),
    ('        if (const int k = Parse(j.value("switch", "F10"))) g_switch = k;',
     '        if (const int k = Parse(j.value("switch", "F10"))) g_switch = k;\n'
     '        if (const int k = Parse(j.value("lock", "K"))) g_lock = k;\n'
     '        if (const int k = Parse(j.value("signal_left", "NUM4"))) g_sig_l = k;\n'
     '        if (const int k = Parse(j.value("signal_right", "NUM6"))) g_sig_r = k;\n'
     '        if (const int k = Parse(j.value("hazard", "NUM5"))) g_hazard = k;\n'
     '        if (const int k = Parse(j.value("engine", "NUM8"))) g_engine = k;'),
    ("int SwitchKey() { return g_switch; }", "int SwitchKey() { return g_switch; }\nint LockKey() { return g_lock; }\nint SignalLeftKey() { return g_sig_l; }\nint SignalRightKey() { return g_sig_r; }\nint HazardKey() { return g_hazard; }\nint EngineKey() { return g_engine; }"),
])
cp = R / "data" / "controls.json"
cj = json.loads(cp.read_text(encoding="utf-8"))
cj.update({"switch": "F10", "lock": "K", "signal_left": "NUM4", "signal_right": "NUM6", "hazard": "NUM5", "engine": "NUM8"})
cj["_not"] += " Araç: lock (kapı kilidi), signal_left/signal_right (sinyal), hazard (dörtlü), engine (kontak) — NUM0-NUM9 numpad tuşlarıdır. switch: serbest dolaşımda karakter değiştirme."
cp.write_text(json.dumps(cj, ensure_ascii=False, indent=2), encoding="utf-8")

# garage: sahip olunan araç sorguları
edit("mod/src/garage.h", [("    int Count() const { return static_cast<int>(cars_.size()); }",
                          "    int Count() const { return static_cast<int>(cars_.size()); }\n    bool IsOwned(Vehicle v) const;                // bu araç senin mi?\n    std::vector<Vehicle> Handles() const;         // dünyadaki sahip olunan araçlar")])
edit("mod/src/garage.cpp", [("void Garage::Shutdown() {", "bool Garage::IsOwned(Vehicle v) const {\n    for (const auto& c : cars_) {\n        if (c.handle != 0 && c.handle == v) return true;\n    }\n    return false;\n}\n\nstd::vector<Vehicle> Garage::Handles() const {\n    std::vector<Vehicle> out;\n    for (const auto& c : cars_) {\n        if (c.handle != 0 && ENTITY::DOES_ENTITY_EXIST(c.handle)) out.push_back(c.handle);\n    }\n    return out;\n}\n\nvoid Garage::Shutdown() {")])

# main
edit("mod/src/main.cpp", [
    ('#include "fuel.h"', '#include "fuel.h"\n#include "vehctl.h"'),
    ("            fuel::Update(pos, e_pressed && !conversation.active(), fuel_e_used);\n",
     "            fuel::Update(pos, e_pressed && !conversation.active(), fuel_e_used);\n"
     "            vehctl::Update(conversation.typing() || conversation.active() || installer.active(),\n"
     "                           [&](Vehicle v) { return garage.IsOwned(v); }, [&]() { return garage.Handles(); });\n"),
    ("            fuel::Reset();\n            ", "            fuel::Reset();\n            vehctl::Reset();\n            "),
])
print("ok")
