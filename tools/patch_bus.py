"""Bir kerelik yama: yeni karakter oluşturulunca yolcu otobüsünden iniş sahnesi (hem Bölüm 1 hem Bölüm 2 karakteri)."""
import pathlib

R = pathlib.Path(__file__).resolve().parents[1]


def edit(rel, pairs):
    p = R / rel
    c = p.read_text(encoding="utf-8")
    for old, new in pairs:
        assert old in c, (rel, old[:70])
        c = c.replace(old, new, 1)
    p.write_text(c, encoding="utf-8")


edit("mod/src/mode.h", [
    ("    void SwitchCharacter();", "    void SwitchCharacter();\n    bool PrepareBus();  // yeni karakter: başlangıç noktasında yolcu otobüsü, oyuncu içinde\n    void BusExit();     // otobüs durur, karakter iner, otobüs gider\n    Vehicle bus_ = 0;\n    Ped bus_driver_ = 0;\n    bool arrival_pending_ = false;"),
])
edit("mod/src/mode.cpp", [
    ("void Mode::SaveSlotState() const {",
     "bool Mode::PrepareBus() {\n"
     "    const json s = cfg_.value(\"spawn\", json::array({195.0, -934.0, 30.7, 160.0}));\n"
     "    const float x = s[0].get<float>(), y = s[1].get<float>(), z = s[2].get<float>(), h = s[3].get<float>();\n"
     "    Vector3 node{};\n"
     "    if (!PATHFIND::GET_CLOSEST_VEHICLE_NODE(x, y, z, &node, 1, 3.0f, 0)) return false;\n"
     "    Hash bm = HashKey(\"coach\");\n"
     "    if (!RequestModel(bm)) {\n"
     "        bm = HashKey(\"bus\");\n"
     "        if (!RequestModel(bm)) return false;\n"
     "    }\n"
     "    Hash dm = HashKey(\"s_m_m_trucker_01\");\n"
     "    if (!RequestModel(dm)) {\n"
     "        dm = HashKey(\"a_m_m_business_01\");\n"
     "        if (!RequestModel(dm)) return false;\n"
     "    }\n"
     "    STREAMING::REQUEST_COLLISION_AT_COORD(node.x, node.y, node.z);\n"
     "    bus_ = VEHICLE::CREATE_VEHICLE(bm, node.x, node.y, node.z, h, false, false);\n"
     "    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(bm);\n"
     "    if (!ENTITY::DOES_ENTITY_EXIST(bus_)) return false;\n"
     "    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(bus_, true, true);\n"
     "    VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(bus_);\n"
     "    VEHICLE::SET_VEHICLE_HANDBRAKE(bus_, true);\n"
     "    bus_driver_ = PED::CREATE_PED_INSIDE_VEHICLE(bus_, 26, dm, -1, false, false);\n"
     "    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(dm);\n"
     "    PED::SET_PED_INTO_VEHICLE(PLAYER::PLAYER_PED_ID(), bus_, 1);  // ilk yolcu koltuğu\n"
     "    Log(\"yolcu otobusu hazir\");\n"
     "    return PED::IS_PED_IN_VEHICLE(PLAYER::PLAYER_PED_ID(), bus_, false) != 0;\n"
     "}\n\n"
     "void Mode::BusExit() {\n"
     "    const Ped player = PLAYER::PLAYER_PED_ID();\n"
     "    for (int i = 0; i < 150; ++i) WAIT(0);  // ~2,5 sn: otobüs durdu, kapı açılıyor\n"
     "    AI::TASK_LEAVE_VEHICLE(player, bus_, 0);\n"
     "    for (int i = 0; i < 480 && PED::IS_PED_IN_VEHICLE(player, bus_, true); ++i) WAIT(0);\n"
     "    if (PED::IS_PED_IN_VEHICLE(player, bus_, false)) {  // inemedi: dışarı al\n"
     "        const Vector3 p = ENTITY::GET_ENTITY_COORDS(bus_, true);\n"
     "        ENTITY::SET_ENTITY_COORDS(player, p.x + 4.f, p.y, p.z, false, false, false, true);\n"
     "    }\n"
     "    for (int i = 0; i < 60; ++i) WAIT(0);\n"
     "    VEHICLE::SET_VEHICLE_HANDBRAKE(bus_, false);\n"
     "    if (ENTITY::DOES_ENTITY_EXIST(bus_driver_)) AI::TASK_VEHICLE_DRIVE_WANDER(bus_driver_, bus_, 12.0f, 786603);  // otobüs yoluna devam eder\n"
     "    Vehicle b = bus_;\n"
     "    Ped d = bus_driver_;\n"
     "    if (ENTITY::DOES_ENTITY_EXIST(d)) ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&d);\n"
     "    if (ENTITY::DOES_ENTITY_EXIST(b)) ENTITY::SET_VEHICLE_AS_NO_LONGER_NEEDED(&b);\n"
     "    bus_ = 0;\n"
     "    bus_driver_ = 0;\n"
     "    arrival_pending_ = false;\n"
     "}\n\n"
     "void Mode::SaveSlotState() const {"),
    # yeni karakter: otobüs sahnesi
    ("    if (!editing_) {\n        Spawn();  // yeni karakter: başlangıç noktasına git\n        GiveRpWeapons();  // yeni karakter silahsız başlar (hikaye silahları geçmez)\n    }",
     "    if (!editing_) {\n"
     "        // Yeni karakter: yolcu otobüsünün içinde başlar, fade-in sonrası iner. Otobüs kurulamazsa başlangıç noktasına ışınlanır.\n"
     "        arrival_pending_ = PrepareBus();\n"
     "        if (!arrival_pending_) Spawn();\n"
     "        GiveRpWeapons();  // yeni karakter silahsız başlar (hikaye silahları geçmez)\n"
     "    }"),
])
# fade-in sonrası otobüs inişi: FinishCreation'ın sonu (FadeIn(700) çağrısından sonra)
c = (R / "mod/src/mode.cpp").read_text(encoding="utf-8")
marker = "    state_ = State::Active;\n    if (first) entered_ = true;\n    FadeIn(700);\n}"
assert marker in c
c = c.replace(marker, "    state_ = State::Active;\n    if (first) entered_ = true;\n    FadeIn(700);\n    if (arrival_pending_) BusExit();\n}", 1)
(R / "mod/src/mode.cpp").write_text(c, encoding="utf-8")
print("ok")
