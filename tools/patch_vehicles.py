"""Bir kerelik yama: araç satın alma (garaj), benzin, tamirci çağırma, vale ("aracımı getir") entegrasyonu."""
import pathlib

R = pathlib.Path(__file__).resolve().parents[1]


def edit(rel, pairs):
    p = R / rel
    c = p.read_text(encoding="utf-8")
    for old, new in pairs:
        assert old in c, (rel, old[:80])
        c = c.replace(old, new, 1)
    p.write_text(c, encoding="utf-8")


# ---------- vcxproj ----------
edit("mod/HikayeTurkRP.vcxproj", [
    ('    <ClCompile Include="src\\passenger.cpp" />',
     '    <ClCompile Include="src\\passenger.cpp" />\n    <ClCompile Include="src\\fuel.cpp" />\n    <ClCompile Include="src\\garage.cpp" />'),
])

# ---------- economy: satın alınan araç kancası ----------
edit("mod/src/economy.h", [
    ("bool ApplyEffect(", "// Satın alınan araç doğunca çağrılır (garaj sahipliği için): kanca(araç, model adı).\nvoid SetVehicleBoughtHook(void (*hook)(Vehicle, const std::string&));\nbool ApplyEffect("),
])
edit("mod/src/economy.cpp", [
    ("int* g_wallet = nullptr;", "int* g_wallet = nullptr;\nvoid (*g_bought_hook)(Vehicle, const std::string&) = nullptr;"),
    ("void SetWallet(int* cash) { g_wallet = cash; }", "void SetWallet(int* cash) { g_wallet = cash; }\nvoid SetVehicleBoughtHook(void (*hook)(Vehicle, const std::string&)) { g_bought_hook = hook; }"),
    ("        VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(veh);\n        ENTITY::SET_VEHICLE_AS_NO_LONGER_NEEDED(&veh);  // oyun zamanla temizleyebilir\n        return true;",
     "        VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(veh);\n        if (g_bought_hook) {\n            g_bought_hook(veh, effect.value(\"model\", \"\"));  // garaj: sahiplik, plaka, dolu depo (araç silinmez)\n        } else {\n            ENTITY::SET_VEHICLE_AS_NO_LONGER_NEEDED(&veh);  // oyun zamanla temizleyebilir\n        }\n        return true;"),
])

# ---------- services: tamirci ----------
edit("mod/src/services.h", [
    ("enum class ServiceKind { Taxi, HeliTaxi, Police, Ambulance, Fire };", "enum class ServiceKind { Taxi, HeliTaxi, Police, Ambulance, Fire, Mechanic };"),
    ("        bool dead = false;  // temizlendi, listeden silinecek", "        bool dead = false;  // temizlendi, listeden silinecek\n        Vehicle target_veh = 0;  // tamirci: onarılacak araç\n        int work_phase = 0;      // tamirci: 0 yürüyor, 1 çalışıyor\n        int fee = 0;             // tamirci: tahmini ücret\n        unsigned long long work_since = 0;"),
    ("    unsigned long long last_dispatch_[5] = {0, 0, 0, 0, 0};", "    unsigned long long last_dispatch_[6] = {0, 0, 0, 0, 0, 0};"),
])
edit("mod/src/services.cpp", [
    ('        case ServiceKind::Fire: return "fire";', '        case ServiceKind::Fire: return "fire";\n        case ServiceKind::Mechanic: return "mechanic";'),
    ('        case ServiceKind::Fire: return "İtfaiye";', '        case ServiceKind::Fire: return "İtfaiye";\n        case ServiceKind::Mechanic: return "Tamirci";'),
    ("for (ServiceKind k : {ServiceKind::Taxi, ServiceKind::HeliTaxi, ServiceKind::Police, ServiceKind::Ambulance, ServiceKind::Fire}) {",
     "for (ServiceKind k : {ServiceKind::Taxi, ServiceKind::HeliTaxi, ServiceKind::Police, ServiceKind::Ambulance, ServiceKind::Fire, ServiceKind::Mechanic}) {"),
    ('#include "economy.h"', '#include "economy.h"\n#include "fuel.h"'),
    # dispatch: tamirci araç kontrolü
    ("    // Doğma noktası: helikopter havada, diğerleri yolda.",
     "    // Tamirci: oyuncunun SON aracı yakında olmalı; ücret önceden tahmin edilir, para en az taban ücrete yetmeli.\n"
     "    Vehicle fix_veh = 0;\n"
     "    int fix_fee = 0;\n"
     "    if (kind == ServiceKind::Mechanic) {\n"
     "        fix_veh = PED::GET_VEHICLE_PED_IS_IN(player, true);\n"
     "        if (fix_veh == 0 || !ENTITY::DOES_ENTITY_EXIST(fix_veh)) return refuse(\"no_vehicle\");\n"
     "        if (D2(ENTITY::GET_ENTITY_COORDS(fix_veh, true), pos) > 250.f) return refuse(\"vehicle_too_far\");\n"
     "        const int base = kcfg.value(\"base_fee\", 120);\n"
     "        const float hp = std::max(0.f, std::min(1000.f, VEHICLE::GET_VEHICLE_ENGINE_HEALTH(fix_veh)));\n"
     "        const float f = fuel::Get(fix_veh);\n"
     "        fix_fee = base + static_cast<int>((1000.f - hp) * kcfg.value(\"repair_per_hp\", 0.15f)) +\n"
     "                  static_cast<int>(std::ceil((100.f - (f < 0.f ? 50.f : f)) * kcfg.value(\"fuel_per_pct\", 1.2f)));\n"
     "        if (economy::GetCash() < base) return refuse(\"insufficient_funds\");\n"
     "    }\n\n"
     "    // Doğma noktası: helikopter havada, diğerleri yolda."),
    ("    d.target = pos;\n    Vector3 road{};", "    d.target = kind == ServiceKind::Mechanic ? ENTITY::GET_ENTITY_COORDS(fix_veh, true) : pos;\n    d.target_veh = fix_veh;\n    d.fee = fix_fee;\n    Vector3 road{};"),
    ("    if (!heli && PATHFIND::GET_CLOSEST_VEHICLE_NODE(pos.x, pos.y, pos.z, &road, 1, 3.0f, 0)) d.target = road;",
     "    if (!heli && PATHFIND::GET_CLOSEST_VEHICLE_NODE(d.target.x, d.target.y, d.target.z, &road, 1, 3.0f, 0)) d.target = road;"),
    ("    if (is_ride_service) {\n        facts[\"fare_info\"]", "    if (kind == ServiceKind::Mechanic) facts[\"estimated_fee\"] = fix_fee;\n    if (is_ride_service) {\n        facts[\"fare_info\"]"),
    ("            } else if (d.kind == ServiceKind::Fire) {\n                FIRE::STOP_FIRE_IN_RANGE",
     "            } else if (d.kind == ServiceKind::Mechanic) {\n"
     "                const Ped mech = d.crew[0];\n"
     "                if (!ENTITY::DOES_ENTITY_EXIST(d.target_veh)) {\n"
     "                    SendAway(d);\n"
     "                } else if (!d.crew_out) {  // tamirci araçtan iner, bozuk araca yürür\n"
     "                    AI::TASK_LEAVE_VEHICLE(mech, d.veh, 0);\n"
     "                    d.crew_out = true;\n"
     "                    d.t = now;\n"
     "                } else if (!d.done_effect) {\n"
     "                    const Vector3 tv = ENTITY::GET_ENTITY_COORDS(d.target_veh, true);\n"
     "                    if (d.work_phase == 0) {\n"
     "                        if (now - d.t > 1500 && now - d.t < 1600) AI::TASK_GO_TO_ENTITY(mech, d.target_veh, -1, 2.5f, 2.0f, 0, 0);\n"
     "                        if (now - d.t > 1500 && D2(ENTITY::GET_ENTITY_COORDS(mech, true), tv) < 5.f) {\n"
     "                            AI::TASK_START_SCENARIO_IN_PLACE(mech, const_cast<char*>(\"WORLD_HUMAN_VEHICLE_MECHANIC\"), 0, true);\n"
     "                            d.work_phase = 1;\n"
     "                            d.work_since = now;\n"
     "                        } else if (now - d.t > 45000) {\n"
     "                            SendAway(d);  // tamirci araca ulaşamadı\n"
     "                        }\n"
     "                    } else if (now - d.work_since > 7000) {  // onarım bitti\n"
     "                        VEHICLE::SET_VEHICLE_FIXED(d.target_veh);\n"
     "                        VEHICLE::SET_VEHICLE_DEFORMATION_FIXED(d.target_veh);\n"
     "                        VEHICLE::SET_VEHICLE_ENGINE_HEALTH(d.target_veh, 1000.f);\n"
     "                        VEHICLE::SET_VEHICLE_PETROL_TANK_HEALTH(d.target_veh, 1000.f);\n"
     "                        VEHICLE::SET_VEHICLE_DIRT_LEVEL(d.target_veh, 0.f);\n"
     "                        fuel::Set(d.target_veh, 100.f);  // depo de dolar\n"
     "                        VEHICLE::SET_VEHICLE_ENGINE_ON(d.target_veh, true, true, false);\n"
     "                        economy::Spend(d.fee);  // yetmiyorsa ücretsiz (acil yardım, ambulansla aynı mantık)\n"
     "                        AI::TASK_ENTER_VEHICLE(mech, d.veh, 8000, -1, 2.0f, 1, 0);\n"
     "                        d.done_effect = true;\n"
     "                        d.t = now;\n"
     "                        Notify_Fixed:\n"
     "                        Log(\"tamirci: arac onarildi, ucret \" + std::to_string(d.fee));\n"
     "                    }\n"
     "                } else if (now - d.t > 9000) {\n"
     "                    SendAway(d);\n"
     "                }\n"
     "            } else if (d.kind == ServiceKind::Fire) {\n                FIRE::STOP_FIRE_IN_RANGE"),
])
# 'Notify_Fixed:' etiketini kaldır (yalnızca okunabilirlik için yazılmıştı)
c = (R / "mod/src/services.cpp").read_text(encoding="utf-8")
c = c.replace("                        Notify_Fixed:\n", "", 1)
(R / "mod/src/services.cpp").write_text(c, encoding="utf-8")

# ---------- phone ----------
edit("mod/src/phone.h", [("static constexpr int kServiceCount = 5;", "static constexpr int kServiceCount = 6;")])
edit("mod/src/phone.cpp", [("const ServiceKind kKinds[] = {ServiceKind::Taxi, ServiceKind::HeliTaxi, ServiceKind::Police, ServiceKind::Ambulance, ServiceKind::Fire};",
                           "const ServiceKind kKinds[] = {ServiceKind::Taxi, ServiceKind::HeliTaxi, ServiceKind::Police, ServiceKind::Ambulance, ServiceKind::Fire, ServiceKind::Mechanic};")])

# ---------- mode: slot erişimi ----------
edit("mod/src/mode.h", [("    bool HasOther() const { return has_other_; }", "    bool HasOther() const { return has_other_; }\n    int slot() const { return slot_; }  // aktif karakter slotu (garaj/kayıtlar karaktere özel)")])

# ---------- conversation ----------
edit("mod/src/conversation.h", [
    ("    void SetHangout(Hangout* hangout) { hangout_ = hangout; }", "    void SetHangout(Hangout* hangout) { hangout_ = hangout; }\n    void SetGarage(Garage* garage) { garage_ = garage; }"),
    ("    Hangout* hangout_ = nullptr;", "    Hangout* hangout_ = nullptr;\n    Garage* garage_ = nullptr;"),
    ('#include "hangout.h"', '#include "garage.h"\n#include "hangout.h"'),
])
edit("mod/src/conversation.cpp", [
    ("                                           {\"call_fire\", ServiceKind::Fire}};",
     "                                           {\"call_fire\", ServiceKind::Fire},\n                                           {\"call_mechanic\", ServiceKind::Mechanic}};"),
    ("                if (intent == \"hang_up\" && pending_ == Pending::None) {\n                    pending_ = Pending::HangUp;  // veda cümlesi bitince kapanır",
     "                if (intent == \"call_my_vehicle\" && garage_) {  // sahip olunan aracı çağır (vale)\n"
     "                    fresh_transcript_ = false;\n"
     "                    const json facts = garage_->Summon();\n"
     "                    if (facts.value(\"accepted\", false)) Notify(\"Aracin yolda: \" + std::to_string(facts.value(\"eta_seconds\", 0)) + \" sn\");\n"
     "                    SendFollowup(\"phone\", facts);\n"
     "                }\n"
     "                if (intent == \"hang_up\" && pending_ == Pending::None) {\n                    pending_ = Pending::HangUp;  // veda cümlesi bitince kapanır"),
])

# ---------- main ----------
edit("mod/src/main.cpp", [
    ('#include "hangout.h"', '#include "fuel.h"\n#include "garage.h"\n#include "hangout.h"'),
    ("    Hangout hangout;", "    Garage garage;  // satın alınan araçlar (karaktere özel)\n    int garage_slot = -1;\n    static Garage* s_garage = nullptr;\n    s_garage = &garage;\n    economy::SetVehicleBoughtHook([](Vehicle v, const std::string& model) { if (s_garage) s_garage->Register(v, model); });\n    fuel::Load();\n    conversation.SetGarage(&garage);\n    Hangout hangout;"),
    ("            hangout.Shutdown();\n", "            hangout.Shutdown();\n            garage.Shutdown();\n            garage_slot = -1;\n            fuel::Reset();\n"),
    ("            hangout.Update();\n", "            if (garage_slot != mode.slot()) {  // karakter değişince garaj da değişir\n                if (garage_slot >= 0) garage.Save();\n                garage_slot = mode.slot();\n                garage.Load(garage_slot);\n                fuel::Reset();\n            }\n            garage.Update();\n            bool fuel_e_used = false;\n            fuel::Update(pos, e_pressed && !conversation.active(), fuel_e_used);\n            hangout.Update();\n"),
    ("            const bool free_e = e_pressed && !portal_used && !installer.active() && !conversation.active();", "            const bool free_e = e_pressed && !portal_used && !installer.active() && !conversation.active();  // (benzin E'si aşağıda ayrıca işlenir)"),
])

# ---------- python: niyetler, kurallar ----------
edit("companion/dialogue/prompt_builder.py", [
    ('"phone": ["chat", "call_taxi", "call_heli_taxi", "call_police", "call_ambulance", "call_fire", "hang_up"],',
     '"phone": ["chat", "call_taxi", "call_heli_taxi", "call_police", "call_ambulance", "call_fire", "call_mechanic", "call_my_vehicle", "hang_up"],'),
    ("\"'call_fire' (itfaiye). Hangi hizmet",
     "\"'call_fire' (itfaiye), 'call_mechanic' (tamirci: son kullanılan aracı onarır ve deposunu doldurur), 'call_my_vehicle' \"\n    \"(satın aldığı aracı yanına getirtir). Hangi hizmet"),
])
edit("companion/server.py", [
    ("zaten yolda; cooldown: kısa süre önce çağrıldı, biraz bekle; wanted_level: arandığın için hizmet \"\n            \"verilemiyor; no_road_nearby: yakında uygun yol yok; busy_in_ride: şu an yolculuktasın).",
     "zaten yolda; cooldown: kısa süre önce çağrıldı, biraz bekle; wanted_level: arandığın için hizmet \"\n            \"verilemiyor; no_road_nearby: yakında uygun yol yok; busy_in_ride: şu an yolculuktasın; no_vehicle: aracın yok/son aracı \"\n            \"bulunamadı; vehicle_too_far: araç çok uzakta; insufficient_funds: parası yetmiyor; already_in_it: zaten o aracın içinde). \"\n            \"Tamirci/vale kabulünde 'estimated_fee' / 'fee' ücretini ve 'eta_seconds' süresini söyle."),
])
print("ok")
