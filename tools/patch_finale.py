"""Bir kerelik yama: 'finale' (kapanış sahnesi) desteği — interact aşamasında E'ye basınca ekran kararır, yazılar akar, sonra görev biter."""
import pathlib

R = pathlib.Path(__file__).resolve().parents[1]


def edit(rel, pairs):
    p = R / rel
    c = p.read_text(encoding="utf-8")
    for old, new in pairs:
        assert old in c, (rel, old[:60])
        c = c.replace(old, new, 1)
    p.write_text(c, encoding="utf-8")


edit("mod/src/missions.h", [
    ("        bool in_person = false;          // telefonla yapılamaz (teslimat gibi)",
     "        nlohmann::json finale;           // interact: E'ye basınca kapanış sahnesi {lines:[...], seconds:N}\n"
     "        bool in_person = false;          // telefonla yapılamaz (teslimat gibi)"),
    ("    bool install_request_ = false;",
     "    bool install_request_ = false;\n"
     "    bool finale_active_ = false;           // kapanış sahnesi sürüyor (ekran siyah, yazılar akıyor)\n"
     "    unsigned long long finale_start_ = 0, finale_until_ = 0;\n"
     "    std::vector<std::string> finale_lines_;"),
])
edit("mod/src/missions.cpp", [
    ('                    st.guide = s.value("guide", "");',
     '                    st.guide = s.value("guide", "");\n                    if (s.contains("finale") && s["finale"].is_object()) st.finale = s["finale"];'),
    ("            if (e_pressed) {\n                if (s.type == \"interact\") Advance(m);",
     "            if (e_pressed && s.type == \"interact\" && s.finale.is_object()) {\n"
     "                // Kapanış sahnesi: ekran kararır, yazılar akar, süre dolunca görev biter.\n"
     "                finale_lines_ = s.finale.value(\"lines\", std::vector<std::string>());\n"
     "                finale_start_ = GetTickCount64();\n"
     "                finale_until_ = finale_start_ + static_cast<unsigned long long>(s.finale.value(\"seconds\", 16)) * 1000ULL;\n"
     "                finale_active_ = true;\n"
     "                CAM::DO_SCREEN_FADE_OUT(2000);\n"
     "                PLAYER::SET_PLAYER_CONTROL(PLAYER::PLAYER_ID(), false, 0);\n"
     "                Log(\"final sahnesi basladi\");\n"
     "            } else if (e_pressed) {\n                if (s.type == \"interact\") Advance(m);"),
    ("void Missions::Update(const Vector3& player_pos, bool e_pressed) {\n    Mission* m = Current();",
     "void Missions::Update(const Vector3& player_pos, bool e_pressed) {\n"
     "    if (finale_active_) {  // kapanış sahnesi: her şey durur\n"
     "        CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);\n"
     "        if (GetTickCount64() > finale_until_) {\n"
     "            finale_active_ = false;\n"
     "            PLAYER::SET_PLAYER_CONTROL(PLAYER::PLAYER_ID(), true, 0);\n"
     "            flags_[\"oyun_bitti\"] = \"1\";\n"
     "            if (Mission* fm = Current()) Advance(*fm);  // ödül ve görev sonu\n"
     "            CAM::DO_SCREEN_FADE_IN(2500);\n"
     "            Log(\"final sahnesi bitti\");\n"
     "        }\n"
     "        return;\n"
     "    }\n"
     "    Mission* m = Current();"),
    ("void Missions::DrawHud() {\n    const unsigned long long now = GetTickCount64();",
     "void Missions::DrawHud() {\n    const unsigned long long now = GetTickCount64();\n"
     "    if (finale_active_) {  // satırlar sırayla belirir, her biri ~3,5 sn sonra bir sonraki gelir\n"
     "        const size_t shown = std::min(finale_lines_.size(), static_cast<size_t>((now - finale_start_) / 3500ULL) + 1);\n"
     "        for (size_t i = 0; i < shown; ++i) {\n"
     "            const bool last = i + 1 == finale_lines_.size();\n"
     "            DrawLine(finale_lines_[i], 0.40f + 0.07f * static_cast<float>(i), last ? 255 : 230, last ? 215 : 230, last ? 110 : 230);\n"
     "        }\n"
     "        return;\n"
     "    }"),
])
print("ok")
