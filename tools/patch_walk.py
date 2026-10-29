"""Bir kerelik yama: 'walk' aşaması (NPC oyuncuyla birlikte yürür; kat edilen mesafeyi kod sayar) ve Ezgi sahil yürüyüşü görevi."""
import pathlib

R = pathlib.Path(__file__).resolve().parents[1]


def edit(rel, pairs):
    p = R / rel
    c = p.read_text(encoding="utf-8-sig")
    for old, new in pairs:
        assert old in c, (rel, old[:80])
        c = c.replace(old, new, 1)
    p.write_text(c, encoding="utf-8")


edit("mod/src/missions.h", [
    ("        std::string place; ", "        std::string place; ") if False else ("        std::string prompt;              //",
     "        float distance = 200.f;          // walk: birlikte yürünecek mesafe (m)\n        std::string prompt;              //"),
    ("    bool install_request_ = false;",
     "    bool install_request_ = false;\n"
     "    bool walk_started_ = false;         // walk aşaması: NPC oyuncuyu takip ediyor\n"
     "    float walk_progress_ = 0.f;         // birlikte yürünen mesafe (m)\n"
     "    int walk_toasted_ = 0;\n"
     "    unsigned long long walk_toast_at_ = 0;\n"
     "    Vector3 walk_last_{};"),
])
edit("mod/src/missions.cpp", [
    ('                    st.guide = s.value("guide", "");', '                    st.guide = s.value("guide", "");\n                    st.distance = s.value("distance", 200.f);'),
    ('(st.type == "talk" || st.type == "install" || st.type == "escort") && st.npc.empty()', '(st.type == "talk" || st.type == "install" || st.type == "escort" || st.type == "walk") && st.npc.empty()'),
    ('m.stages[stage_].type == "escort" || m.stages[stage_].type == "scene")) {', 'm.stages[stage_].type == "escort" || m.stages[stage_].type == "scene" || m.stages[stage_].type == "walk")) {'),
    ("d > kDespawnDist && !(escort_boarded_ && n.id == current)", "d > kDespawnDist && !((escort_boarded_ || walk_started_) && n.id == current)"),
    ('(m.veh != 0 ? "v" : "-") + (escort_boarded_ ? "e" : "-");', '(m.veh != 0 ? "v" : "-") + (escort_boarded_ ? "e" : "-") + (walk_started_ ? "w" : "-");'),
    ("json Missions::Advance(Mission& m) {\n    escort_boarded_ = false;", "json Missions::Advance(Mission& m) {\n    escort_boarded_ = false;\n    walk_started_ = false;\n    walk_progress_ = 0.f;"),
    ('    if (s.type == "escort" && !escort_boarded_) {',
     '    if (s.type == "walk") {\n'
     '        if (walk_started_) return false;  // yürüyüş başladı: blip yok\n'
     '        const Npc* wn = FindNpc(m, s.npc);\n'
     '        if (!wn) return false;\n'
     '        out = wn->pos;  // önce Ezgi\'nin yanına git\n'
     '        return true;\n'
     '    }\n'
     '    if (s.type == "escort" && !escort_boarded_) {'),
    ('        } else if (s.type == "escort") {\n            text =',
     '        } else if (s.type == "walk") {\n'
     '            text = who + "\'ın yanına git ve yan yana yürü; sana eşlik eder. Sohbet için E ile konuşabilirsin.";\n'
     '        } else if (s.type == "escort") {\n            text ='),
    ('    } else if (s.type == "scene") {',
     '    } else if (s.type == "walk") {\n'
     '        // Birlikte yürüyüş: NPC oyuncuyu takip eder; yakınken kat edilen mesafeyi KOD sayar, bitince aşama ilerler.\n'
     '        Npc* n = nullptr;\n'
     '        for (auto& x : m.npcs) {\n'
     '            if (x.id == s.npc) n = &x;\n'
     '        }\n'
     '        if (!n || n->ped == 0 || !ENTITY::DOES_ENTITY_EXIST(n->ped)) return;\n'
     '        const unsigned long long now = GetTickCount64();\n'
     '        const Vector3 np = ENTITY::GET_ENTITY_COORDS(n->ped, true);\n'
     '        if (!walk_started_) {\n'
     '            if (D2(player_pos, np) < 6.f && !PED::IS_PED_IN_ANY_VEHICLE(player, false)) {\n'
     '                walk_started_ = true;\n'
     '                walk_progress_ = 0.f;\n'
     '                walk_toasted_ = 0;\n'
     '                walk_last_ = player_pos;\n'
     '                ENTITY::FREEZE_ENTITY_POSITION(n->ped, false);\n'
     '                AI::TASK_FOLLOW_TO_OFFSET_OF_ENTITY(n->ped, player, 1.1f, -0.4f, 0.f, 1.0f, -1, 1.5f, true);\n'
     '                RemoveBlip();\n'
     '                toast_ = "Yürüyüşe başladınız";\n'
     '                toast_until_ = now + 4000;\n'
     '            }\n'
     '            return;\n'
     '        }\n'
     '        if (PED::IS_PED_IN_ANY_VEHICLE(player, false)) {  // araçtayken sayılmaz\n'
     '            walk_last_ = player_pos;\n'
     '            if (now - walk_toast_at_ > 8000) {\n'
     '                toast_ = "Yürüyüş için araçtan in";\n'
     '                toast_until_ = now + 3000;\n'
     '                walk_toast_at_ = now;\n'
     '            }\n'
     '            return;\n'
     '        }\n'
     '        const float together = D2(player_pos, np);\n'
     '        if (together > 25.f) {  // geride kaldı: bekle\n'
     '            walk_last_ = player_pos;\n'
     '            if (now - walk_toast_at_ > 8000) {\n'
     '                toast_ = "Ezgi\'yi bekle, yanında yürü";\n'
     '                toast_until_ = now + 3000;\n'
     '                walk_toast_at_ = now;\n'
     '            }\n'
     '            return;\n'
     '        }\n'
     '        const float step = D2(player_pos, walk_last_);\n'
     '        walk_last_ = player_pos;\n'
     '        if (step < 4.f && together < 12.f) walk_progress_ += step;  // ışınlanma/uzak adımlar sayılmaz\n'
     '        const int pct = static_cast<int>(std::min(100.f, walk_progress_ / std::max(1.f, s.distance) * 100.f));\n'
     '        if (pct / 25 > walk_toasted_ && pct < 100) {\n'
     '            walk_toasted_ = pct / 25;\n'
     '            toast_ = "Yürüyüş: %" + std::to_string(pct);\n'
     '            toast_until_ = now + 3000;\n'
     '        }\n'
     '        if (walk_progress_ >= s.distance) {\n'
     '            AI::CLEAR_PED_TASKS(n->ped);\n'
     '            ENTITY::FREEZE_ENTITY_POSITION(n->ped, true);\n'
     '            Advance(m);\n'
     '        }\n'
     '    } else if (s.type == "scene") {'),
])
edit("tools/story_block.py", [
    ('         face_talk("yuruyus", "ayse", "Vespucci Plajı\'nda Ezgi\'yle yürü",\n'
     '                   "Sahilde sohbet; ikisi de rahat. Ezgi artık oyuncuyu arkadaş sayar.")],',
     '         face_talk("karsilas", "ayse", "Vespucci Plajı\'nda Ezgi\'yle buluş",\n'
     '                   "Ezgi plajda bekliyor, hava güzel. Yürüyüşe çıkmayı önerir ve yanında yürüyeceğini söyler."),\n'
     '         {"id": "yuruyus", "type": "walk", "npc": "ayse", "distance": 180.0,\n'
     '          "objective": "Vespucci Plajı\'nda Ezgi\'yle yan yana yürü (sohbet edebilirsin)",\n'
     '          "guide": "Ezgi\'nin yanına git ve yürümeye başla; o sana eşlik eder. Sohbet için E ile konuş."}],'),
])
print("ok")
