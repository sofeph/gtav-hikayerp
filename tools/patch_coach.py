"""Bir kerelik yama: bölgesel yolcu otobüsü (coach) hatları — şehirler arası, mesafeye göre ücretli, saati ilerleten sembolik yolculuk."""
import pathlib

R = pathlib.Path(__file__).resolve().parents[1]


def edit(rel, pairs):
    p = R / rel
    c = p.read_text(encoding="utf-8-sig")
    for old, new in pairs:
        assert old in c, (rel, old[:80])
        c = c.replace(old, new, 1)
    p.write_text(c, encoding="utf-8")


edit("mod/src/transit.cpp", [
    ("    int leg_s = 20;          // metro: iki durak arası (tarife)",
     "    int leg_s = 20;          // metro: iki durak arası (tarife)\n    float fare_per_km = 0.f; // coach (bölgesel): kilometre başına ücret\n    int fare_min = 10;       // coach: en düşük ücret\n    int phase_s = 0;         // coach/metro: tarife kayması (ters yön hattı yarım sefer aralığı kaydırılır)"),
    ("            l.leg_s = std::max(5, lj.value(\"leg_s\", 20));",
     "            l.leg_s = std::max(5, lj.value(\"leg_s\", 20));\n            l.fare_per_km = lj.value(\"fare_per_km\", 0.f);\n            l.fare_min = lj.value(\"fare_min\", 10);\n            l.phase_s = lj.value(\"phase_s\", 0);"),
    # tarife kayması
    ("    const long long phase = (now_s - static_cast<long long>(i) * l.leg_s) % l.headway_s;",
     "    const long long phase = (now_s - static_cast<long long>(i) * l.leg_s - l.phase_s) % l.headway_s;"),
    # leg ücreti yardımcıları (TramAtStop'tan önce)
    ("// --- Metro ---\n",
     "// --- Metro / bölgesel otobüs ---\n"
     "bool IsCoach(const Line& l) { return l.type == \"coach\"; }\n"
     "// Bölgesel otobüs: i. duraktan bir sonrakine mesafeye göre ücret (metro ve şehir otobüsü: sabit ücret)\n"
     "int LegFare(const Line& l, int i) {\n"
     "    if (!IsCoach(l)) return l.fare;\n"
     "    const int n = static_cast<int>(l.stops.size());\n"
     "    const float d = D2(l.stops[static_cast<size_t>(i)].pos, l.stops[static_cast<size_t>((i + 1) % n)].pos);\n"
     "    return std::max(l.fare_min, static_cast<int>(d / 1000.f * l.fare_per_km));\n"
     "}\n"
     "// Yolculuk süresi (saniye, ekran kararık kalma): bölgesel hat uzun, metro kısa\n"
     "int LegBlackSeconds(const Line& l, int i) {\n"
     "    if (!IsCoach(l)) return std::max(4, l.leg_s / 4);\n"
     "    const int n = static_cast<int>(l.stops.size());\n"
     "    const float d = D2(l.stops[static_cast<size_t>(i)].pos, l.stops[static_cast<size_t>((i + 1) % n)].pos);\n"
     "    return std::max(6, std::min(16, static_cast<int>(d / 600.f)));\n"
     "}\n\n"),
    # yolculuk ekranı: süre, metin, saat ilerletme
    ("        DrawLine(\"LST Metro  -  \" + l.stops[next].name + \" durağına gidiliyor...\", 0.46f, 230, 230, 230);\n"
     "        if (now - g_tram.t > static_cast<unsigned long long>(std::max(4, l.leg_s / 4)) * 1000ULL) {",
     "        const float leg_d = D2(l.stops[static_cast<size_t>(g_tram.stop)].pos, l.stops[static_cast<size_t>(next)].pos);\n"
     "        if (IsCoach(l)) {\n"
     "            const int mins = std::max(1, static_cast<int>(leg_d / 22.f / 60.f));\n"
     "            DrawLine(\"Bölgesel Otobüs \" + l.id + \"  -  \" + l.stops[next].name + \" otogarına gidiliyor (~\" + std::to_string(mins) + \" dk)\", 0.46f, 230, 230, 230);\n"
     "        } else {\n"
     "            DrawLine(\"LST Metro  -  \" + l.stops[next].name + \" durağına gidiliyor...\", 0.46f, 230, 230, 230);\n"
     "        }\n"
     "        if (now - g_tram.t > static_cast<unsigned long long>(LegBlackSeconds(l, g_tram.stop)) * 1000ULL) {\n"
     "            if (IsCoach(l)) {  // yol süresi kadar oyun saati ilerler (ortalama 22 m/s)\n"
     "                const int mins = std::max(1, static_cast<int>(leg_d / 22.f / 60.f));\n"
     "                TIME::ADD_TO_CLOCK_TIME(mins / 60, mins % 60, 0);\n"
     "            }"),
    ("            Toast(\"Istasyon: \" + l.stops[next].name + \" (E: devam et, uzaklaşırsan inersin)\", 6000);",
     "            Toast((IsCoach(l) ? \"Otogar: \" : \"Istasyon: \") + l.stops[next].name + \" (E: devam et, uzaklaşırsan inersin)\", 6000);"),
    # devam: ücret
    ("        ShowHelp((\"~INPUT_PICKUP~ Metro ile devam et (Hat \" + l.id + \")\").c_str());\n        if (e_pressed) {\n",
     "        const int cont_fare = IsCoach(l) ? LegFare(l, g_tram.stop) : 0;\n"
     "        ShowHelp((std::string(\"~INPUT_PICKUP~ \") + (IsCoach(l) ? \"Yolculuga devam et\" : \"Metro ile devam et\") + \" (Hat \" + l.id + \")\" +\n"
     "                  (cont_fare > 0 ? \" $\" + std::to_string(cont_fare) : std::string())).c_str());\n"
     "        if (e_pressed) {\n"
     "            if (cont_fare > 0 && !economy::Spend(cont_fare)) {\n"
     "                Toast(\"Yetersiz bakiye\");\n"
     "                return;\n"
     "            }\n"),
    # biniş
    ("                ShowHelp((\"~INPUT_PICKUP~ Metro \" + l.id + \"'e bin ($\" + std::to_string(l.fare) + \")\").c_str());\n                if (e_pressed) {\n                    if (!economy::Spend(l.fare)) {",
     "                const int board_fare = LegFare(l, static_cast<int>(i));\n"
     "                ShowHelp((std::string(\"~INPUT_PICKUP~ \") + (IsCoach(l) ? \"Bolgesel otobuse bin (\" + l.stops[(i + 1) % l.stops.size()].name + \")\" : \"Metro \" + l.id + \"'e bin\") +\n"
     "                          \" ($\" + std::to_string(board_fare) + \")\").c_str());\n"
     "                if (e_pressed) {\n                    if (!economy::Spend(board_fare)) {"),
    ('                        Toast("Metro " + l.id + " kalkıyor");', '                        Toast((IsCoach(l) ? "Bölgesel otobüs " : "Metro ") + l.id + " kalkıyor");'),
    # tabela
    ('                DrawLine("LST Metro " + l.id + " — " + l.stops[i].name + ": sonraki sefer " + std::to_string(wait) + " sn", 0.84f, 230, 230, 230);',
     '                DrawLine((IsCoach(l) ? "Bölgesel Otobüs " : "LST Metro ") + l.id + " — " + l.stops[i].name + ": sonraki sefer " + std::to_string(wait) + " sn", 0.84f, 230, 230, 230);'),
    # ikonlar
    ("            const bool bus = l.type == \"bus\";\n            for (auto& s : l.stops) {\n                s.blip = blips::Add(s.pos, 1, bus ? 3 : 29, std::string(bus ? \"Otobus Duragi \" : \"Metro Istasyonu \") + s.name, 0.55f, true);",
     "            const bool bus = l.type == \"bus\";\n            for (auto& s : l.stops) {\n                if (IsCoach(l)) s.blip = blips::Add(s.pos, 1, 47, \"Otogar \" + s.name, 0.8f, true);\n                else s.blip = blips::Add(s.pos, 1, bus ? 3 : 29, std::string(bus ? \"Otobus Duragi \" : \"Metro Istasyonu \") + s.name, 0.55f, true);"),
    # rota planı
    ("    out[\"line_type\"] = bl->type == \"bus\" ? \"otobus\" : \"metro\";", "    out[\"line_type\"] = bl->type == \"bus\" ? \"otobus\" : (bl->type == \"coach\" ? \"bolgesel_otobus\" : \"metro\");"),
])
# fare alanı: coach ücreti rota sonucunda mesafeye göre (yaklaşık)
edit("mod/src/transit.cpp", [
    ("    out[\"fare\"] = bl->fare;", "    out[\"fare\"] = bl->type == \"coach\" ? std::max(bl->fare_min, static_cast<int>(D2(board.pos, alight.pos) / 1000.f * bl->fare_per_km)) : bl->fare;"),
    ("                const double est = w1 / 1.4 + (bus ? 90.0 : 60.0) + ride / (bus ? 8.0 : 12.0) + hops * l.dwell_s + w2 / 1.4;",
     "                const bool coach = l.type == \"coach\";\n                const double est = w1 / 1.4 + (bus ? 90.0 : (coach ? 150.0 : 60.0)) + ride / (bus ? 8.0 : (coach ? 22.0 : 12.0)) + hops * l.dwell_s + w2 / 1.4;"),
])
edit("companion/server.py", [("'line_type'), 'board_walk_m'", "'line_type'), 'board_walk_m'") if False else ("(otobus/metro)", "(otobus/metro/bolgesel_otobus)")])

# --- gen_transit: bölgesel hatlar ---
p = R / "tools/gen_transit.py"
c = p.read_text(encoding="utf-8")
marker = '(R / "data/transit.json")'
assert marker in c
coach = '''# Bölgesel yolcu otobüsü (şehirler arası): otogarlar, bölge merkezine en yakın GERÇEK otobüs durağına oturtulur (400 m içinde yoksa merkez).
# R1 saat yönünde, R2 ters yönde; R2 yarım sefer aralığı kaydırılır (aynı otogarda iki yön aynı anda kalkmaz). Ücret mesafeye göre.
REGIONS = [("Los Santos", (195.0, -934.0)), ("Sandy Shores", (1960.0, 3740.0)), ("Grapeseed", (1700.0, 4920.0)),
           ("Paleto Bay", (-100.0, 6290.0)), ("Chumash", (-3150.0, 1090.0))]
terms = []
for nm, ctr in REGIONS:
    best = min(stops_all, key=lambda p: dist(p, ctr))
    pos = best if dist(best, ctr) < 400 else [ctr[0], ctr[1], 30.0]
    terms.append({"name": nm, "pos": [round(pos[0], 1), round(pos[1], 1), round(pos[2], 1)]})
lines.append({"id": "R1", "name": "Bolgesel Otobus (ileri)", "type": "coach", "fare_per_km": 3.0, "fare_min": 12, "dwell_s": 14,
              "headway_s": 360, "leg_s": 40, "phase_s": 0, "stops": terms})
lines.append({"id": "R2", "name": "Bolgesel Otobus (ters)", "type": "coach", "fare_per_km": 3.0, "fare_min": 12, "dwell_s": 14,
              "headway_s": 360, "leg_s": 40, "phase_s": 180, "stops": list(reversed(terms))})

'''
c = c.replace(marker, coach + marker, 1)
p.write_text(c, encoding="utf-8")
print("ok")
