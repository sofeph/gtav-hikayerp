"""Bir kerelik yama: Bölüm 2 (kadın karakter), Ayşe'nin ölümü, serbest dolaşımda karakter değiştirme için kod altyapısı."""
import pathlib

R = pathlib.Path(__file__).resolve().parents[1]


def edit(rel, pairs):
    p = R / rel
    c = p.read_text(encoding="utf-8")
    for old, new in pairs:
        assert old in c, (rel, old[:70])
        c = c.replace(old, new, 1)
    p.write_text(c, encoding="utf-8")


# ---------- profile: slotlu kayıt ----------
edit("mod/src/profile.h", [
    ("bool LoadProfile(CharacterProfile& out);", "bool LoadProfile(CharacterProfile& out, int slot = 0);  // slot 0: character.json (Bölüm 1, erkek), 1: character_2.json (Bölüm 2, kadın)"),
    ("bool SaveProfile(const CharacterProfile& p);", "bool SaveProfile(const CharacterProfile& p, int slot = 0);"),
])
edit("mod/src/profile.cpp", [
    ('std::string SavePath() { return std::string(kBaseDir) + "/saves/character.json"; }',
     'std::string SavePath(int slot = 0) {\n    return std::string(kBaseDir) + (slot == 0 ? "/saves/character.json" : "/saves/character_2.json");\n}'),
    ("bool LoadProfile(CharacterProfile& out) {\n    try {\n        std::ifstream in(SavePath());",
     "bool LoadProfile(CharacterProfile& out, int slot) {\n    try {\n        std::ifstream in(SavePath(slot));"),
    ("bool SaveProfile(const CharacterProfile& p) {\n    try {\n        std::filesystem::create_directories(std::string(kBaseDir) + \"/saves\");\n        std::ofstream out(SavePath());",
     "bool SaveProfile(const CharacterProfile& p, int slot) {\n    try {\n        std::filesystem::create_directories(std::string(kBaseDir) + \"/saves\");\n        std::ofstream out(SavePath(slot));"),
])

# ---------- creator: cinsiyet kilidi ----------
edit("mod/src/creator.h", [
    ("    Ped ped() const { return ped_; }\n", "    Ped ped() const { return ped_; }\n    void LockGender(bool v) { lock_gender_ = v; }  // Bölüm 2: kadın karakter, cinsiyet değiştirilemez\n\nprivate:\n    bool lock_gender_ = false;\n"),
])
edit("mod/src/creator.cpp", [
    ("        sex.change = [this](int) {\n            p_->female = !p_->female;", "        sex.change = [this](int) {\n            if (lock_gender_) return;\n            p_->female = !p_->female;"),
])

# ---------- controls: karakter değiştirme tuşu ----------
edit("mod/src/controls.h", [("int ModeKey();", "int SwitchKey();  // serbest dolaşımda karakter değiştirme (varsayılan F10)\nint ModeKey();")])
edit("mod/src/controls.cpp", [
    ("int g_mode = VK_F9;", "int g_mode = VK_F9;\nint g_switch = VK_F10;"),
    ('        if (const int k = Parse(j.value("mode", "F9"))) g_mode = k;', '        if (const int k = Parse(j.value("switch", "F10"))) g_switch = k;\n        if (const int k = Parse(j.value("mode", "F9"))) g_mode = k;'),
    ("int ModeKey() { return g_mode; }", "int ModeKey() { return g_mode; }\nint SwitchKey() { return g_switch; }"),
])

# ---------- mode: iki karakter, Bölüm 2, değiştirme ----------
edit("mod/src/mode.h", [
    ("    const CharacterProfile& profile() const { return profile_; }",
     "    const CharacterProfile& profile() const { return profile_; }\n"
     "    // Bölüm 1 bitti: erkek karakter saklanır, Bölüm 2 için kadın karakter oluşturucuyla yaratılır ve oyuna girer.\n"
     "    void BeginChapter2(const nlohmann::json& pool);\n"
     "    void SetSwitchAllowed(bool v) { switch_allowed_ = v; }  // serbest dolaşım: iki karakter arasında geçiş\n"
     "    bool HasOther() const { return has_other_; }\n"
     "    // Diğer karakterin görünümüyle bir NPC yaratır (Bölüm 2 finalinde buluşma); name_out: karakterin adı.\n"
     "    Ped SpawnAlter(float x, float y, float z, float heading, std::string& name_out);"),
    ("    void PersistIfChanged();", "    void PersistIfChanged();\n    void SwitchCharacter();\n    void SaveSlotState() const;"),
    ("    CharacterProfile profile_, backup_;", "    CharacterProfile profile_, backup_, other_;  // other_: aktif olmayan karakter\n    int slot_ = 0;                               // aktif karakter: 0 erkek (B1), 1 kadın (B2)\n    bool has_other_ = false, switch_allowed_ = false, creating_ch2_ = false;"),
])
edit("mod/src/mode.cpp", [
    ("    has_saved_ = LoadProfile(profile_);\n    if (has_saved_) last_saved_cash_ = profile_.cash;",
     "    {   // aktif karakter slotu ve diğer karakter\n"
     "        std::ifstream sin(std::string(kBaseDir) + \"/saves/mode_state.json\");\n"
     "        if (sin) {\n"
     "            try {\n"
     "                slot_ = json::parse(sin).value(\"slot\", 0) == 1 ? 1 : 0;\n"
     "            } catch (...) {\n"
     "                slot_ = 0;\n"
     "            }\n"
     "        }\n"
     "    }\n"
     "    has_saved_ = LoadProfile(profile_, slot_);\n"
     "    if (!has_saved_ && slot_ == 1) {  // bozuk durum: ilk karaktere dön\n"
     "        slot_ = 0;\n"
     "        has_saved_ = LoadProfile(profile_, 0);\n"
     "    }\n"
     "    has_other_ = LoadProfile(other_, slot_ ^ 1);\n"
     "    if (has_saved_) last_saved_cash_ = profile_.cash;"),
    ("void Mode::FinishCreation() {\n    creator_.End();\n    FadeOut(500);\n    SaveProfile(profile_);",
     "void Mode::SaveSlotState() const {\n"
     "    try {\n"
     "        std::ofstream out(std::string(kBaseDir) + \"/saves/mode_state.json\");\n"
     "        out << json{{\"slot\", slot_}}.dump();\n"
     "    } catch (...) {\n"
     "    }\n"
     "}\n\n"
     "void Mode::FinishCreation() {\n    creator_.End();\n    creator_.LockGender(false);\n    creating_ch2_ = false;\n    FadeOut(500);\n    SaveProfile(profile_, slot_);\n    SaveSlotState();"),
    ("void Mode::CancelCreation() {\n    creator_.End();", "void Mode::CancelCreation() {\n    if (creating_ch2_) {  // Bölüm 2 karakteri iptal edilemez: mevcut hâliyle onaylanır\n        FinishCreation();\n        return;\n    }\n    creator_.End();"),
    ("        CaptureRpWeapons();\n        SaveProfile(profile_);\n        exited_ = true;", "        CaptureRpWeapons();\n        SaveProfile(profile_, slot_);\n        exited_ = true;"),
    ("        SaveProfile(profile_);\n        last_saved_cash_ = profile_.cash;\n        last_save_ms_ = now;", "        SaveProfile(profile_, slot_);\n        last_saved_cash_ = profile_.cash;\n        last_save_ms_ = now;"),
    ("void Mode::Update(const json& pool) {",
     "void Mode::BeginChapter2(const json& pool) {\n"
     "    if (state_ != State::Active) return;\n"
     "    FadeOut(900);\n"
     "    {   // Bölüm 1 karakteri (erkek) olduğu yerde saklanır\n"
     "        const Ped ped = PLAYER::PLAYER_PED_ID();\n"
     "        const Vector3 p = ENTITY::GET_ENTITY_COORDS(ped, true);\n"
     "        profile_.has_pos = true;\n"
     "        profile_.x = p.x;\n"
     "        profile_.y = p.y;\n"
     "        profile_.z = p.z;\n"
     "        profile_.heading = ENTITY::GET_ENTITY_HEADING(ped);\n"
     "        CaptureRpWeapons();\n"
     "        SaveProfile(profile_, slot_);\n"
     "    }\n"
     "    other_ = profile_;\n"
     "    has_other_ = true;\n"
     "    // Bölüm 2 karakteri: kadın, yeni, oluşturucuyla yaratılır (cinsiyet kilitli)\n"
     "    profile_ = CharacterProfile{};\n"
     "    profile_.female = true;\n"
     "    profile_.ResetOutfit();\n"
     "    const json d = cfg_.value(\"default_character\", json::object());\n"
     "    profile_.hometown = d.value(\"hometown\", \"\");\n"
     "    profile_.district = d.value(\"district\", \"\");\n"
     "    profile_.past_job = d.value(\"past_job\", \"\");\n"
     "    profile_.team = d.value(\"team\", \"\");\n"
     "    profile_.name = \"\";\n"
     "    profile_.cash = cfg_.value(\"start_cash\", 5000);\n"
     "    slot_ = 1;\n"
     "    SaveSlotState();\n"
     "    economy::SetWallet(&profile_.cash);\n"
     "    const Ped ped = SwapModel(true);\n"
     "    ApplyToPed(ped, profile_);\n"
     "    WEAPON::REMOVE_ALL_PED_WEAPONS(ped, true);\n"
     "    editing_ = false;\n"
     "    creating_ch2_ = true;\n"
     "    creator_.LockGender(true);\n"
     "    creator_.Begin(&profile_, ped, &pool, [this](bool female) { return SwapModel(female); });\n"
     "    state_ = State::Creating;\n"
     "    FadeIn(700);\n"
     "    Log(\"Bolum 2: kadin karakter olusturucu acildi\");\n"
     "}\n\n"
     "void Mode::SwitchCharacter() {\n"
     "    if (!has_other_ || state_ != State::Active) return;\n"
     "    FadeOut(700);\n"
     "    {   // aktif karakter bulunduğu yerde kalır\n"
     "        const Ped ped = PLAYER::PLAYER_PED_ID();\n"
     "        const Vector3 p = ENTITY::GET_ENTITY_COORDS(ped, true);\n"
     "        profile_.has_pos = true;\n"
     "        profile_.x = p.x;\n"
     "        profile_.y = p.y;\n"
     "        profile_.z = p.z;\n"
     "        profile_.heading = ENTITY::GET_ENTITY_HEADING(ped);\n"
     "        CaptureRpWeapons();\n"
     "        SaveProfile(profile_, slot_);\n"
     "    }\n"
     "    std::swap(profile_, other_);\n"
     "    slot_ ^= 1;\n"
     "    SaveSlotState();\n"
     "    const Ped ped = SwapModel(profile_.female);\n"
     "    ApplyToPed(ped, profile_);\n"
     "    Spawn();\n"
     "    GiveRpWeapons();\n"
     "    economy::SetWallet(&profile_.cash);\n"
     "    last_saved_cash_ = profile_.cash;\n"
     "    Notice(\"Karakter: \" + (profile_.name.empty() ? std::string(profile_.female ? \"Kadın\" : \"Erkek\") : profile_.name));\n"
     "    FadeIn(700);\n"
     "    Log(std::string(\"karakter degisti: slot \") + std::to_string(slot_));\n"
     "}\n\n"
     "Ped Mode::SpawnAlter(float x, float y, float z, float heading, std::string& name_out) {\n"
     "    if (!has_other_) return 0;\n"
     "    const Hash m = HashKey(other_.female ? \"mp_f_freemode_01\" : \"mp_m_freemode_01\");\n"
     "    if (!RequestModel(m)) return 0;\n"
     "    const Ped ped = PED::CREATE_PED(other_.female ? 5 : 4, m, x, y, z, heading, false, false);\n"
     "    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(m);\n"
     "    if (!ENTITY::DOES_ENTITY_EXIST(ped)) return 0;\n"
     "    PED::SET_PED_DEFAULT_COMPONENT_VARIATION(ped);\n"
     "    ApplyToPed(ped, other_);\n"
     "    name_out = other_.name;\n"
     "    return ped;\n"
     "}\n\n"
     "void Mode::Update(const json& pool) {"),
])
# karakter değiştirme tuşu: aktif durumda (Update'deki Active dalı)
mode_cpp = (R / "mod/src/mode.cpp").read_text(encoding="utf-8")
anchor = "        case State::Active: {\n            if (key) {"
assert anchor in mode_cpp
mode_cpp = mode_cpp.replace(anchor, "        case State::Active: {\n            if (switch_allowed_ && has_other_ && keys_.Pressed(controls::SwitchKey())) {\n                SwitchCharacter();\n                break;\n            }\n            if (key) {", 1)
(R / "mod/src/mode.cpp").write_text(mode_cpp, encoding="utf-8")

# ---------- missions ----------
edit("mod/src/missions.h", [
    ("    bool PersonTalk(", "    // Bölüm 2 kancaları: iç karakterin NPC olarak yaratılması (model \"__alter__\").\n"
     "    void SetAlterSpawner(std::function<Ped(float, float, float, float, std::string&)> f) { alter_spawn_ = std::move(f); }\n"
     "    // Bayraklar (görev boyunca toplanan bilgiler; kayıtlıdır).\n"
     "    bool FlagIs(const std::string& key, const std::string& value) const { return Flag(key) == value; }\n"
     "    bool TakeFlag(const std::string& key);  // \"1\" ise \"0\" yapıp true döner (tek seferlik olay)\n"
     "    bool PersonTalk("),
    ("        std::map<std::string, std::string> on_start_flags;", "        std::map<std::string, std::string> on_complete_flags;  // görev bitince konan bayraklar\n        bool clear_contacts = false;                           // görev bitince rehber temizlenir (bölüm geçişi)\n        std::vector<std::string> removes_contacts;             // görev bitince bu kişiler rehberden silinir (ölüm vb.)\n        std::map<std::string, std::string> on_start_flags;"),
    ("        bool in_person = false;          // telefonla yapılamaz (teslimat gibi)", "        std::string kill_npc;            // scene: oyuncu yaklaşınca bu NPC'yi kod öldürür (kurgu sahnesi)\n        bool in_person = false;          // telefonla yapılamaz (teslimat gibi)"),
    ("    bool install_request_ = false;", "    bool install_request_ = false;\n    unsigned long long scene_at_ = 0;  // scene aşaması: ölüm sahnesinin başladığı an\n    std::function<Ped(float, float, float, float, std::string&)> alter_spawn_;"),
])
mh = (R / "mod/src/missions.h").read_text(encoding="utf-8")
if "#include <functional>" not in mh:
    mh = mh.replace("#include <map>", "#include <functional>\n#include <map>", 1)
    (R / "mod/src/missions.h").write_text(mh, encoding="utf-8")
edit("mod/src/missions.cpp", [
    ('                if (j.contains("on_start_flags"))', '                if (j.contains("on_complete_flags")) m.on_complete_flags = j["on_complete_flags"].get<std::map<std::string, std::string>>();\n                m.clear_contacts = j.value("clear_contacts", false);\n                m.removes_contacts = j.value("removes_contacts", std::vector<std::string>());\n                if (j.contains("on_start_flags"))'),
    ('                    st.guide = s.value("guide", "");', '                    st.guide = s.value("guide", "");\n                    st.kill_npc = s.value("kill_npc", "");'),
    ('if ((st.type == "interact" || st.type == "install" || st.type == "escort") && !st.has_pos)', 'if ((st.type == "interact" || st.type == "install" || st.type == "escort" || st.type == "scene") && !st.has_pos)'),
    ('(m.stages[stage_].type == "talk" || m.stages[stage_].type == "install" || m.stages[stage_].type == "escort")) {',
     '(m.stages[stage_].type == "talk" || m.stages[stage_].type == "install" || m.stages[stage_].type == "escort" || m.stages[stage_].type == "scene")) {'),
    # iç karakter NPC'si
    ("    // Model oyunda yoksa yedek zincirini dene", "    if (n.model == \"__alter__\") {  // oyuncunun diğer (oluşturulmuş) karakteri\n        if (!alter_spawn_) return false;\n        std::string who;\n        float gx = n.pos.x, gy = n.pos.y, gz2 = n.pos.z, gz3 = 0.f;\n        STREAMING::REQUEST_COLLISION_AT_COORD(gx, gy, gz2);\n        if (GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(gx, gy, gz2 + 20.f, &gz3, false)) gz2 = gz3;\n        else if (++n.ground_tries < 120) return false;\n        const Ped p = alter_spawn_(gx, gy, gz2, n.heading, who);\n        if (!p) return false;\n        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, true, true);\n        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(p, true);\n        ENTITY::SET_ENTITY_INVINCIBLE(p, true);\n        ENTITY::FREEZE_ENTITY_POSITION(p, true);\n        n.ped = p;\n        n.pos.z = gz2;\n        if (!who.empty()) {  // karakterin kendi adı konuşmada kullanılsın\n            n.npc_json[\"profile\"][\"name\"] = who;\n        }\n        Log(\"gorev NPC dogdu (diger karakter): \" + n.id);\n        return true;\n    }\n    // Model oyunda yoksa yedek zincirini dene"),
    # tamamlanma
    ("        for (const auto& c : m.unlocks_contacts) AddContact(c);",
     "        if (m.clear_contacts) contacts_.clear();  // bölüm geçişi: yeni karakterin rehberi sıfırdan başlar\n"
     "        for (const auto& c : m.unlocks_contacts) AddContact(c);\n"
     "        for (const auto& c : m.removes_contacts) contacts_.erase(std::remove(contacts_.begin(), contacts_.end(), c), contacts_.end());\n"
     "        for (const auto& [k, v] : m.on_complete_flags) flags_[k] = v;"),
    ("std::string Missions::Relationship(const std::string& npc_id) const {\n    const Npc* n = FindNpcAnywhere(npc_id);\n    if (!n) return \"stranger\";",
     "bool Missions::TakeFlag(const std::string& key) {\n    if (Flag(key) != \"1\") return false;\n    flags_[key] = \"0\";\n    Save();\n    return true;\n}\n\n"
     "std::string Missions::Relationship(const std::string& npc_id) const {\n    if (Flag(\"kardes_\" + npc_id) == \"1\") return \"sibling\";  // Bölüm 2: kadın karakterin kız kardeşi\n    const Npc* n = FindNpcAnywhere(npc_id);\n    if (!n) return \"stranger\";"),
    # scene aşaması
    ('    } else if (s.type == "interact" || s.type == "install") {',
     '    } else if (s.type == "scene") {\n'
     '        // Kurgu sahnesi: oyuncu yaklaşınca NPC\'yi KOD öldürür (örn. Ayşe\'nin ölümü); 5 sn sonra aşama ilerler.\n'
     '        Npc* n = nullptr;\n'
     '        for (auto& x : m.npcs) {\n'
     '            if (x.id == s.kill_npc) n = &x;\n'
     '        }\n'
     '        if (!n || n->ped == 0 || !ENTITY::DOES_ENTITY_EXIST(n->ped)) return;\n'
     '        const unsigned long long now = GetTickCount64();\n'
     '        if (scene_at_ == 0) {\n'
     '            if (D2(player_pos, ENTITY::GET_ENTITY_COORDS(n->ped, true)) < s.radius) {\n'
     '                scene_at_ = now;\n'
     '                ENTITY::SET_ENTITY_INVINCIBLE(n->ped, false);\n'
     '                ENTITY::FREEZE_ENTITY_POSITION(n->ped, false);\n'
     '                ENTITY::SET_ENTITY_HEALTH(n->ped, 0);  // sahne: ölüm\n'
     '                toast_ = s.objective;\n'
     '                toast_until_ = now + 6000;\n'
     '                Log("sahne: " + n->id + " oldu");\n'
     '            }\n'
     '        } else if (now - scene_at_ > 5000) {\n'
     '            scene_at_ = 0;\n'
     '            Advance(m);\n'
     '        }\n'
     '    } else if (s.type == "interact" || s.type == "install") {'),
])
# finale aşamasına da scene_at_ sıfırlaması gerekmez; Advance sıfırlar
c = (R / "mod/src/missions.cpp").read_text(encoding="utf-8")
c = c.replace("json Missions::Advance(Mission& m) {\n    escort_boarded_ = false;", "json Missions::Advance(Mission& m) {\n    escort_boarded_ = false;\n    scene_at_ = 0;", 1)
(R / "mod/src/missions.cpp").write_text(c, encoding="utf-8")

print("ok")
