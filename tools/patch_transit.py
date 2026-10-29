"""Bir kerelik yama: toplu taşıma (otobüs/tramvay) ve telefonda toplu taşıma navigasyonu."""
import pathlib

R = pathlib.Path(__file__).resolve().parents[1]


def edit(rel, pairs):
    p = R / rel
    c = p.read_text(encoding="utf-8-sig")
    for old, new in pairs:
        assert old in c, (rel, old[:80])
        c = c.replace(old, new, 1)
    p.write_text(c, encoding="utf-8")


BS = chr(92)
VC = '    <ClCompile Include="src' + BS + 'businesses.cpp" />'
edit("mod/HikayeTurkRP.vcxproj", [(VC, VC + '\n    <ClCompile Include="src' + BS + 'transit.cpp" />')])

# --- transit: rota planlayıcı (telefon navigasyonu) ---
edit("mod/src/transit.h", [
    ('#include "common.h"', '#include <string>\n\n#include "common.h"\n#include "json.hpp"'),
    ("void Load();", "// Telefon navigasyonu: 'from'dan 'dest'e toplu taşıma rotası. En kısa tahmini süreli (yürü + bekle + yolculuk + yürü) hat/durak çifti\n"
     "// seçilir; yürümek daha kısaysa bunu söyler. Biniş durağını haritaya işaretler. Dönüş LLM'e gerçek bilgi olarak gider.\n"
     "nlohmann::json PlanRoute(const Vector3& from, const Vector3& dest, const std::string& dest_name);\nvoid Load();"),
])
edit("mod/src/transit.cpp", [
    ("bool OwnsBlip(Blip b) {",
     "json PlanRoute(const Vector3& from, const Vector3& dest, const std::string& dest_name) {\n"
     "    json out = {{\"kind\", \"transit_route\"}, {\"destination\", dest_name}, {\"found\", false}};\n"
     "    if (g_lines.empty()) {\n"
     "        out[\"reason\"] = \"no_service\";\n"
     "        return out;\n"
     "    }\n"
     "    const float walk_direct_s = D2(from, dest) / 1.4f * 1.2f;\n"
     "    double best = 1e18;\n"
     "    const Line* bl = nullptr;\n"
     "    int bi = -1, bj = -1, bhops = 0;\n"
     "    for (const auto& l : g_lines) {\n"
     "        const int n = static_cast<int>(l.stops.size());\n"
     "        const bool bus = l.type == \"bus\";\n"
     "        for (int i = 0; i < n; ++i) {\n"
     "            const float w1 = D2(from, l.stops[i].pos);\n"
     "            if (w1 > 1500.f) continue;\n"
     "            float ride = 0.f;\n"
     "            for (int hops = 1; hops < n; ++hops) {\n"
     "                const int a = (i + hops - 1) % n, b = (i + hops) % n;\n"
     "                ride += D2(l.stops[a].pos, l.stops[b].pos) * 1.25f;\n"
     "                const float w2 = D2(l.stops[b].pos, dest);\n"
     "                const double est = w1 / 1.4 + (bus ? 90.0 : 60.0) + ride / (bus ? 8.0 : 12.0) + hops * l.dwell_s + w2 / 1.4;\n"
     "                if (est < best) {\n"
     "                    best = est;\n"
     "                    bl = &l;\n"
     "                    bi = i;\n"
     "                    bj = b;\n"
     "                    bhops = hops;\n"
     "                }\n"
     "            }\n"
     "        }\n"
     "    }\n"
     "    if (!bl) {\n"
     "        out[\"reason\"] = \"no_stop_nearby\";\n"
     "        return out;\n"
     "    }\n"
     "    if (walk_direct_s <= best * 1.1) {  // yürümek daha kısa\n"
     "        out[\"reason\"] = \"walk\";\n"
     "        out[\"walk_minutes\"] = std::max(1, static_cast<int>(walk_direct_s / 60.f));\n"
     "        return out;\n"
     "    }\n"
     "    const Stop& board = bl->stops[bi];\n"
     "    const Stop& alight = bl->stops[bj];\n"
     "    UI::SET_NEW_WAYPOINT(board.pos.x, board.pos.y);  // önce biniş durağı\n"
     "    out[\"found\"] = true;\n"
     "    out[\"line\"] = bl->id;\n"
     "    out[\"line_type\"] = bl->type == \"bus\" ? \"otobus\" : \"tramvay\";\n"
     "    out[\"board_stop\"] = board.name;\n"
     "    out[\"board_walk_m\"] = static_cast<int>(D2(from, board.pos));\n"
     "    out[\"alight_stop\"] = alight.name;\n"
     "    out[\"stops_to_ride\"] = bhops;\n"
     "    out[\"final_walk_m\"] = static_cast<int>(D2(alight.pos, dest));\n"
     "    out[\"est_minutes\"] = std::max(1, static_cast<int>(best / 60.0));\n"
     "    out[\"fare\"] = bl->fare;\n"
     "    out[\"waypoint_set\"] = true;\n"
     "    Log(\"toplu tasima rotasi: hat \" + bl->id + \" \" + board.name + \" -> \" + alight.name);\n"
     "    return out;\n"
     "}\n\n"
     "bool OwnsBlip(Blip b) {"),
])

# --- conversation: telefon niyeti ---
edit("mod/src/conversation.cpp", [
    ('#include "conversation.h"', '#include "conversation.h"\n#include "transit.h"'),
    ("            if (shop_ && context_ == \"phone\" && services_ && fresh_transcript_) {\n                struct Map { const char* intent; ServiceKind kind; };",
     "            if (shop_ && context_ == \"phone\" && places_ && fresh_transcript_ && intent == \"transit_route\" &&\n"
     "                m.contains(\"destination\") && m[\"destination\"].is_string()) {\n"
     "                // Toplu taşıma navigasyonu: LLM sadece 'nereye' metnini çıkarır; hat, durak, süre ve ücret kodda.\n"
     "                fresh_transcript_ = false;\n"
     "                const Vector3 from = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true);\n"
     "                Place pl;\n"
     "                json facts;\n"
     "                const std::string asked = m[\"destination\"].get<std::string>();\n"
     "                if (places_->Resolve(asked, from, pl)) facts = transit::PlanRoute(from, pl.pos, pl.name);\n"
     "                else facts = {{\"kind\", \"transit_route\"}, {\"found\", false}, {\"reason\", \"unknown_destination\"}, {\"asked\", asked}};\n"
     "                if (facts.value(\"found\", false)) Notify(\"Biniş durağı haritaya işaretlendi\");\n"
     "                SendFollowup(\"transit\", facts);\n"
     "            }\n"
     "            if (shop_ && context_ == \"phone\" && services_ && fresh_transcript_) {\n                struct Map { const char* intent; ServiceKind kind; };"),
])

# --- main ---
edit("mod/src/main.cpp", [
    ('#include "vehctl.h"', '#include "transit.h"\n#include "vehctl.h"'),
    ("    businesses::Load();\n", "    businesses::Load();\n    transit::Load();\n"),
    ("|| businesses::OwnsBlip(b);", "|| businesses::OwnsBlip(b) || transit::OwnsBlip(b);"),
    ("            businesses::Reset();\n", "            businesses::Reset();\n            transit::Reset();\n"),
    ("            businesses::Update(pos, e_pressed && !conversation.active());\n", "            businesses::Update(pos, e_pressed && !conversation.active());\n            transit::Update(pos, e_pressed && !conversation.active());\n"),
])
# Telefon rehberine "Toplu Taşıma Navigasyon" uygulaması (sahte kişi): seçilince navigasyon hattı (santral) açılır
edit("mod/src/main.cpp", [
    ("            const std::vector<Missions::Contact> contacts = missions.Contacts();  // görev bitince eklenen kişiler",
     "            std::vector<Missions::Contact> contacts = missions.Contacts();  // görev bitince eklenen kişiler\n"
     "            {\n"
     "                Missions::Contact nav;  // telefon uygulaması: toplu taşıma navigasyonu\n"
     "                nav.id = \"transit_nav\";\n"
     "                nav.name = \"Toplu Taşıma Navigasyon\";\n"
     "                contacts.insert(contacts.begin(), nav);\n"
     "            }"),
    ("                } else if (!call_contact.empty()) {",
     "                } else if (call_contact == \"transit_nav\") {\n"
     "                    conversation.StartPhone(shops.pool());  // navigasyon: santralle aynı hat, 'transit_route' niyeti ile rota verir\n"
     "                } else if (!call_contact.empty()) {"),
])

# --- python ---
edit("companion/dialogue/prompt_builder.py", [
    ('"call_mechanic", "call_my_vehicle", "hang_up"],', '"call_mechanic", "call_my_vehicle", "transit_route", "hang_up"],'),
    ("'call_my_vehicle' \"\n    \"(satın aldığı aracı yanına getirtir).",
     "'call_my_vehicle' \"\n    \"(satın aldığı aracı yanına getirtir), 'transit_route' (toplu taşıma navigasyonu: otobüs/tramvayla nasıl gidilir; 'destination' \"\n    \"alanına gidilecek yeri yaz, hat/durak/süreyi UYDURMA, sonuç sana gönderilecek)."),
])
edit("companion/server.py", [
    ('        "invite": (', '        "transit": (\n'
     '            "Toplu taşıma navigasyonu sonucu aşağıda (GERÇEK). \'found\' true ise 1-2 kısa cümleyle söyle: \'board_walk_m\' metre yürüyüp "\n'
     '            "\'board_stop\' durağından \'line\' hattına (\'line_type\') bin, \'stops_to_ride\' durak sonra \'alight_stop\' durağında in, \'final_walk_m\' "\n'
     '            "metre yürü; yaklaşık \'est_minutes\' dakika, ücret \'fare\' dolar; biniş durağı haritaya işaretlendi. \'found\' false ise \'reason\': "\n'
     '            "walk (yürümek daha kısa, \'walk_minutes\' dakika), no_stop_nearby (yakında durak yok), no_service (hat yok), unknown_destination "\n'
     '            "(orayı bilmiyorsun). Bilgi dışında hat, durak veya süre uydurma."\n'
     '        ),\n        "invite": ('),
])
print("ok")
