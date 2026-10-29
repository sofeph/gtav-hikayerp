#include "mode.h"
#include "weaponwatch.h"

#include <windows.h>

#include <ctime>
#include <filesystem>
#include <fstream>

#include "controls.h"
#include "economy.h"
#include "overlay.h"

using nlohmann::json;

namespace hikaye {

namespace {
const char* const kStoryModels[] = {"player_zero", "player_one", "player_two"};

const char* const kWeapons[] = {
    "PISTOL", "COMBATPISTOL", "APPISTOL", "PISTOL50", "SNSPISTOL", "HEAVYPISTOL", "VINTAGEPISTOL", "STUNGUN", "FLAREGUN",
    "MICROSMG", "SMG", "ASSAULTSMG", "COMBATPDW", "MACHINEPISTOL", "ASSAULTRIFLE", "CARBINERIFLE", "ADVANCEDRIFLE",
    "SPECIALCARBINE", "BULLPUPRIFLE", "COMPACTRIFLE", "MG", "COMBATMG", "GUSENBERG", "PUMPSHOTGUN", "SAWNOFFSHOTGUN",
    "ASSAULTSHOTGUN", "BULLPUPSHOTGUN", "MUSKET", "HEAVYSHOTGUN", "DBSHOTGUN", "SNIPERRIFLE", "HEAVYSNIPER",
    "MARKSMANRIFLE", "RPG", "GRENADELAUNCHER", "MINIGUN", "FIREWORK", "RAILGUN", "HOMINGLAUNCHER", "GRENADE", "STICKYBOMB",
    "SMOKEGRENADE", "BZGAS", "MOLOTOV", "PROXMINE", "SNOWBALL", "FLARE", "PETROLCAN", "KNIFE", "NIGHTSTICK", "HAMMER", "BAT",
    "GOLFCLUB", "CROWBAR", "BOTTLE", "DAGGER", "HATCHET", "KNUCKLE", "MACHETE", "FLASHLIGHT", "SWITCHBLADE", "POOLCUE",
    "WRENCH", "BATTLEAXE", "FIREEXTINGUISHER"};

void FadeOut(int ms) {
    CAM::DO_SCREEN_FADE_OUT(ms);
    for (int i = 0; i < 240 && !CAM::IS_SCREEN_FADED_OUT(); ++i) WAIT(0);
}
void FadeIn(int ms) {
    CAM::DO_SCREEN_FADE_IN(ms);
    for (int i = 0; i < 240 && !CAM::IS_SCREEN_FADED_IN(); ++i) WAIT(0);
}
bool RequestModel(Hash m) {
    if (!STREAMING::IS_MODEL_IN_CDIMAGE(m) || !STREAMING::IS_MODEL_VALID(m)) return false;
    STREAMING::REQUEST_MODEL(m);
    for (int i = 0; i < 300 && !STREAMING::HAS_MODEL_LOADED(m); ++i) WAIT(0);
    return STREAMING::HAS_MODEL_LOADED(m) != 0;
}
}  // namespace

namespace {
std::string ResetMarker() { return std::string(kBaseDir) + "/RESET_PENDING"; }
bool ResetPending() { return std::filesystem::exists(ResetMarker()); }
}  // namespace

void ApplyPendingReset() {
    try {
        if (!ResetPending()) return;
        const std::string saves = std::string(kBaseDir) + "/saves";
        if (std::filesystem::exists(saves)) {
            char stamp[32];
            const std::time_t t = std::time(nullptr);
            std::tm tmv{};
            localtime_s(&tmv, &t);
            std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmv);
            std::filesystem::rename(saves, std::string(kBaseDir) + "/saves_yedek_" + stamp);
        }
        std::filesystem::create_directories(saves);
        std::filesystem::remove(ResetMarker());
        Log("sifirdan hikaye: kayitlar yedeklendi, yeni hikaye basliyor");
    } catch (const std::exception& e) {
        Log(std::string("sifirlama basarisiz: ") + e.what());
    }
}

void Mode::Load() {
    try {
        std::ifstream in(std::string(kBaseDir) + "/data/mode.json");
        if (in) cfg_ = json::parse(in);
        else Log("mode.json yok: varsayilanlar kullanilir");
    } catch (const std::exception& e) {
        Log(std::string("mode.json okunamadi: ") + e.what());
    }
    auto_open_ = cfg_.value("open_menu_on_start", true);
    {
        char buf[8] = {0};
        if (GetEnvironmentVariableA("HIKAYE_LAUNCHER", buf, sizeof(buf)) > 0 && buf[0] == '1' && cfg_.value("launcher_direct_start", true)) {
            launcher_direct_ = true;
            auto_open_ = true;
            Log("baslatici ile acildi: dogrudan Hikaye RP'ye gecilecek");
        }
    }
    start_ms_ = GetTickCount64();
    {
        std::ifstream sin(std::string(kBaseDir) + "/saves/mode_state.json");
        if (sin) {
            try {
                slot_ = json::parse(sin).value("slot", 0) == 1 ? 1 : 0;
            } catch (...) {
                slot_ = 0;
            }
        }
    }
    has_saved_ = LoadProfile(profile_, slot_);
    if (!has_saved_ && slot_ == 1) {
        slot_ = 0;
        has_saved_ = LoadProfile(profile_, 0);
    }
    has_other_ = LoadProfile(other_, slot_ ^ 1);
    if (has_saved_) last_saved_cash_ = profile_.cash;
    Log(std::string("Hikaye RP modu hazir (kayitli karakter: ") + (has_saved_ ? profile_.name : "yok") + ")");
}

void Mode::TryAutoOpen(const json& pool) {
    if (!auto_open_) return;
    const unsigned long long now = GetTickCount64();
    if (now - start_ms_ > 180000) {
        auto_open_ = false;
        return;
    }
    std::string why;
    if (!CanEnter(why)) {
        stable_since_ = 0;
        return;
    }
    if (stable_since_ == 0) stable_since_ = now;
    if (now - stable_since_ < 4000) return;
    auto_open_ = false;
    if (launcher_direct_) {
        Log(std::string("oyun yuklendi: dogrudan Hikaye RP (") + (has_saved_ ? "devam" : "yeni karakter") + ")");
        EnterMode(!has_saved_, pool);
        return;
    }
    OpenMenu();
    Log("oyun yuklendi: Hikaye RP menusu otomatik acildi");
}

void Mode::RequestMenu() {
    if (state_ == State::Dormant) {
        std::string why;
        if (CanEnter(why)) OpenMenu();
        else Notice(why);
    } else if (state_ == State::Active) {
        OpenMenu();
    }
}

bool Mode::ConsumeEntered() {
    const bool r = entered_;
    entered_ = false;
    return r;
}
bool Mode::ConsumeExited() {
    const bool r = exited_;
    exited_ = false;
    return r;
}

void Mode::Notice(const std::string& text) {
    notice_ = text;
    notice_until_ = GetTickCount64() + 3500;
}

bool Mode::CanEnter(std::string& why) const {
    const Ped ped = PLAYER::PLAYER_PED_ID();
    if (GAMEPLAY::GET_MISSION_FLAG()) why = "Hikaye gorevi sirasinda mod acilamaz";
    else if (STREAMING::IS_PLAYER_SWITCH_IN_PROGRESS()) why = "Karakter gecisi suruyor";
    else if (PLAYER::IS_PLAYER_DEAD(PLAYER::PLAYER_ID())) why = "Oluyken mod acilamaz";
    else if (PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) > 0) why = "Aranirken mod acilamaz";
    else if (PED::IS_PED_IN_ANY_VEHICLE(ped, false)) why = "Once aractan in";
    else if (!CAM::IS_SCREEN_FADED_IN()) why = "Ekran gecisi suruyor";
    else {
        bool story_char = false;
        for (const char* m : kStoryModels) story_char |= (ENTITY::GET_ENTITY_MODEL(ped) == HashKey(m));
        if (!story_char) why = "Sadece hikaye karakterinden girilebilir";
        else return true;
    }
    return false;
}

void Mode::SaveStory() {
    const Ped ped = PLAYER::PLAYER_PED_ID();
    story_ = Story{};
    story_.valid = true;
    story_.model = ENTITY::GET_ENTITY_MODEL(ped);
    story_.pos = ENTITY::GET_ENTITY_COORDS(ped, true);
    story_.heading = ENTITY::GET_ENTITY_HEADING(ped);
    story_.health = ENTITY::GET_ENTITY_HEALTH(ped);
    story_.armor = PED::GET_PED_ARMOUR(ped);
    for (int i = 0; i < 12; ++i) {
        story_.drawable[i] = PED::GET_PED_DRAWABLE_VARIATION(ped, i);
        story_.texture[i] = PED::GET_PED_TEXTURE_VARIATION(ped, i);
        story_.palette[i] = PED::GET_PED_PALETTE_VARIATION(ped, i);
    }
    for (int i = 0; i < 10; ++i) {
        story_.prop[i] = PED::GET_PED_PROP_INDEX(ped, i);
        story_.prop_tex[i] = PED::GET_PED_PROP_TEXTURE_INDEX(ped, i);
    }
    for (const char* name : kWeapons) {
        const Hash h = HashKey((std::string("WEAPON_") + name).c_str());
        if (WEAPON::HAS_PED_GOT_WEAPON(ped, h, false)) story_.weapons.push_back({h, WEAPON::GET_AMMO_IN_PED_WEAPON(ped, h)});
    }
    Log("hikaye karakteri saklandi (" + std::to_string(story_.weapons.size()) + " silah)");
}

Ped Mode::SwapModel(bool female) {
    const Hash m = HashKey(female ? "mp_f_freemode_01" : "mp_m_freemode_01");
    if (RequestModel(m)) {
        PLAYER::SET_PLAYER_MODEL(PLAYER::PLAYER_ID(), m);
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(m);
        WAIT(0);
    }
    const Ped ped = PLAYER::PLAYER_PED_ID();
    PED::SET_PED_DEFAULT_COMPONENT_VARIATION(ped);
    return ped;
}

void Mode::PlaceAt(float x, float y, float z, float heading) {
    const Ped ped = PLAYER::PLAYER_PED_ID();
    ENTITY::SET_ENTITY_COORDS(ped, x, y, z + 1.f, false, false, false, true);
    ENTITY::SET_ENTITY_HEADING(ped, heading);
    ENTITY::FREEZE_ENTITY_POSITION(ped, true);
    for (int i = 0; i < 90; ++i) {
        STREAMING::REQUEST_COLLISION_AT_COORD(x, y, z);
        if (ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(ped)) break;
        WAIT(0);
    }
    float gz = 0.f;
    if (GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(x, y, z + 100.f, &gz, false)) {
        ENTITY::SET_ENTITY_COORDS(ped, x, y, gz + 1.f, false, false, false, true);
    }
    ENTITY::FREEZE_ENTITY_POSITION(ped, false);
}

void Mode::GiveRpWeapons() {
    const Ped ped = PLAYER::PLAYER_PED_ID();
    WEAPON::REMOVE_ALL_PED_WEAPONS(ped, true);
    for (const auto& w : profile_.weapons) {
        const Hash h = HashKey(("WEAPON_" + w.first).c_str());
        WEAPON::GIVE_WEAPON_TO_PED(ped, h, w.second, false, false);
        WEAPON::SET_PED_AMMO(ped, h, w.second);
    }
    weaponwatch::SnapshotLegal();
}

void Mode::CaptureRpWeapons() {
    const Ped ped = PLAYER::PLAYER_PED_ID();
    profile_.weapons.clear();
    for (const char* name : kWeapons) {
        const Hash h = HashKey((std::string("WEAPON_") + name).c_str());
        if (WEAPON::HAS_PED_GOT_WEAPON(ped, h, false)) profile_.weapons.push_back({name, WEAPON::GET_AMMO_IN_PED_WEAPON(ped, h)});
    }
}

void Mode::Spawn() {
    if (profile_.has_pos) {
        PlaceAt(profile_.x, profile_.y, profile_.z, profile_.heading);
    } else {
        const json s = cfg_.value("spawn", json::array({195.0, -934.0, 30.7, 160.0}));
        PlaceAt(s[0].get<float>(), s[1].get<float>(), s[2].get<float>(), s[3].get<float>());
    }
}

void Mode::RestoreStory() {
    if (!story_.valid) return;
    if (RequestModel(story_.model)) {
        PLAYER::SET_PLAYER_MODEL(PLAYER::PLAYER_ID(), story_.model);
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(story_.model);
        WAIT(0);
    }
    const Ped ped = PLAYER::PLAYER_PED_ID();
    PED::SET_PED_DEFAULT_COMPONENT_VARIATION(ped);
    WEAPON::REMOVE_ALL_PED_WEAPONS(ped, true);
    for (int i = 0; i < 12; ++i) PED::SET_PED_COMPONENT_VARIATION(ped, i, story_.drawable[i], story_.texture[i], story_.palette[i]);
    for (int i = 0; i < 10; ++i) {
        if (story_.prop[i] < 0) PED::CLEAR_PED_PROP(ped, i);
        else PED::SET_PED_PROP_INDEX(ped, i, story_.prop[i], story_.prop_tex[i], true);
    }
    for (const auto& w : story_.weapons) {
        WEAPON::GIVE_WEAPON_TO_PED(ped, w.first, w.second, false, false);
        WEAPON::SET_PED_AMMO(ped, w.first, w.second);
    }
    ENTITY::SET_ENTITY_COORDS(ped, story_.pos.x, story_.pos.y, story_.pos.z, false, false, false, true);
    ENTITY::SET_ENTITY_HEADING(ped, story_.heading);
    ENTITY::SET_ENTITY_HEALTH(ped, story_.health);
    PED::SET_PED_ARMOUR(ped, story_.armor);
    story_.valid = false;
    Log("hikaye karakteri geri yuklendi");
}

void Mode::EnterMode(bool create, const json& pool) {
    FadeOut(700);
    SaveStory();
    
    menu_rows_.clear();
    overlay::SetMenu({});

    editing_ = false;
    if (create) {
        profile_ = CharacterProfile{};
        const json d = cfg_.value("default_character", json::object());
        profile_.name = d.value("name", profile_.name);
        profile_.hometown = d.value("hometown", "");
        profile_.district = d.value("district", "");
        profile_.military_unit = d.value("military_unit", "");
        profile_.past_job = d.value("past_job", "");
        profile_.team = d.value("team", "");
        profile_.cash = cfg_.value("start_cash", 5000);
    }
    const Ped ped = SwapModel(profile_.female);
    ApplyToPed(ped, profile_);
    if (create) {
        creator_.Begin(&profile_, ped, &pool, [this](bool female) { return SwapModel(female); });
        state_ = State::Creating;
    } else {
        Spawn();
        GiveRpWeapons();
        economy::SetWallet(&profile_.cash);
        state_ = State::Active;
        entered_ = true;
    }
    FadeIn(700);
}

bool Mode::PrepareBus() {
    const json s = cfg_.value("bus_arrival", cfg_.value("spawn", json::array({115.5, -781.3, 30.4, 160.0})));
    const float x = s[0].get<float>(), y = s[1].get<float>(), z = s[2].get<float>();
    float h = s.size() > 3 ? s[3].get<float>() : 0.f;
    Vector3 node{};
    float node_h = 0.f;
    if (PATHFIND::GET_CLOSEST_VEHICLE_NODE_WITH_HEADING(x, y, z, &node, &node_h, 1, 3.0f, 0)) h = node_h;
    else if (!PATHFIND::GET_CLOSEST_VEHICLE_NODE(x, y, z, &node, 1, 3.0f, 0)) return false;
    Hash bm = HashKey("coach");
    if (!RequestModel(bm)) {
        bm = HashKey("bus");
        if (!RequestModel(bm)) return false;
    }
    Hash dm = HashKey("s_m_m_trucker_01");
    if (!RequestModel(dm)) {
        dm = HashKey("a_m_m_business_01");
        if (!RequestModel(dm)) return false;
    }
    STREAMING::REQUEST_COLLISION_AT_COORD(node.x, node.y, node.z);
    bus_ = VEHICLE::CREATE_VEHICLE(bm, node.x, node.y, node.z, h, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(bm);
    if (!ENTITY::DOES_ENTITY_EXIST(bus_)) return false;
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(bus_, true, true);
    VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(bus_);
    VEHICLE::SET_VEHICLE_HANDBRAKE(bus_, true);
    bus_driver_ = PED::CREATE_PED_INSIDE_VEHICLE(bus_, 26, dm, -1, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(dm);
    ENTITY::FREEZE_ENTITY_POSITION(PLAYER::PLAYER_PED_ID(), false);
    ENTITY::SET_ENTITY_INVINCIBLE(PLAYER::PLAYER_PED_ID(), false);
    PED::SET_PED_INTO_VEHICLE(PLAYER::PLAYER_PED_ID(), bus_, 1);
    Log("yolcu otobusu hazir");
    return PED::IS_PED_IN_VEHICLE(PLAYER::PLAYER_PED_ID(), bus_, false) != 0;
}

void Mode::BusExit() {
    const Ped player = PLAYER::PLAYER_PED_ID();
    for (int i = 0; i < 150; ++i) WAIT(0);
    AI::TASK_LEAVE_VEHICLE(player, bus_, 0);
    for (int i = 0; i < 480 && PED::IS_PED_IN_VEHICLE(player, bus_, true); ++i) WAIT(0);
    if (PED::IS_PED_IN_VEHICLE(player, bus_, false)) {
        const Vector3 p = ENTITY::GET_ENTITY_COORDS(bus_, true);
        ENTITY::SET_ENTITY_COORDS(player, p.x + 4.f, p.y, p.z, false, false, false, true);
    }
    for (int i = 0; i < 60; ++i) WAIT(0);
    VEHICLE::SET_VEHICLE_HANDBRAKE(bus_, false);
    if (ENTITY::DOES_ENTITY_EXIST(bus_driver_)) AI::TASK_VEHICLE_DRIVE_WANDER(bus_driver_, bus_, 12.0f, 786603);
    Vehicle b = bus_;
    Ped d = bus_driver_;
    if (ENTITY::DOES_ENTITY_EXIST(d)) ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&d);
    if (ENTITY::DOES_ENTITY_EXIST(b)) ENTITY::SET_VEHICLE_AS_NO_LONGER_NEEDED(&b);
    bus_ = 0;
    bus_driver_ = 0;
    arrival_pending_ = false;
}

void Mode::SaveSlotState() const {
    try {
        std::ofstream out(std::string(kBaseDir) + "/saves/mode_state.json");
        out << json{{"slot", slot_}}.dump();
    } catch (...) {
    }
}

void Mode::FinishCreation() {
    creator_.End();
    creating_ch2_ = false;
    FadeOut(500);
    SaveProfile(profile_, slot_);
    SaveSlotState();
    has_saved_ = true;
    last_saved_cash_ = profile_.cash;
    if (!editing_) {
        arrival_pending_ = PrepareBus();
        if (!arrival_pending_) Spawn();
        GiveRpWeapons();
    }
    economy::SetWallet(&profile_.cash);
    const bool first = !editing_;
    editing_ = false;
    state_ = State::Active;
    if (first) entered_ = true;
    FadeIn(700);
    if (arrival_pending_) BusExit();
}

void Mode::CancelCreation() {
    if (creating_ch2_) {
        FinishCreation();
        return;
    }
    creator_.End();
    if (editing_) {
        FadeOut(400);
        profile_ = backup_;
        const Ped ped = SwapModel(profile_.female);
        ApplyToPed(ped, profile_);
        editing_ = false;
        state_ = State::Active;
        FadeIn(500);
    } else {
        ExitMode();
    }
}

void Mode::ExitMode() {
    const bool was_playing = state_ == State::Active || state_ == State::ActiveMenu;
    FadeOut(700);
    if (was_playing) {
        const Ped ped = PLAYER::PLAYER_PED_ID();
        const Vector3 p = ENTITY::GET_ENTITY_COORDS(ped, true);
        profile_.has_pos = true;
        profile_.x = p.x;
        profile_.y = p.y;
        profile_.z = p.z;
        profile_.heading = ENTITY::GET_ENTITY_HEADING(ped);
        CaptureRpWeapons();
        SaveProfile(profile_, slot_);
        exited_ = true;
    }
    economy::SetWallet(nullptr);
    overlay::SetMenu({});
    RestoreStory();
    
    // Native save menüsü mod dışındayken de kapalı tutulur
    GAMEPLAY::SET_SAVE_MENU_ACTIVE(false);

    state_ = State::Dormant;
    FadeIn(700);
}

void Mode::OpenMenu() {
    menu_rows_.clear();
    if (state_ == State::Dormant) {
        if (has_saved_) menu_rows_.push_back("Devam et: " + (profile_.name.empty() ? std::string("Karakterim") : profile_.name));
        if (!has_saved_) {
            menu_rows_.push_back("Yeni karakter olustur");
        } else if (ResetPending()) {
            menu_rows_.push_back("Sifirdan baslama bekliyor: oyunu yeniden baslat (iptal etmek icin Enter)");
        } else {
            menu_rows_.push_back("Sifirdan basla (karakter ve TUM ilerleme yedeklenip silinir; karakter sonradan degistirilemez)");
        }
        menu_rows_.push_back("Kapat");
        state_ = State::Menu;
    } else {
        menu_rows_ = {"Hikaye moduna don", "Kapat"};
        state_ = State::ActiveMenu;
    }
    selected_ = 0;
}

void Mode::CloseMenu() {
    auto_open_ = false;
    state_ = state_ == State::Menu ? State::Dormant : State::Active;
    menu_rows_.clear();
    overlay::SetMenu({});
}

void Mode::UpdateMenu(const json& pool) {
    CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
    const int n = static_cast<int>(menu_rows_.size());
    if (keys_.Repeat(VK_UP)) selected_ = (selected_ + n - 1) % n;
    if (keys_.Repeat(VK_DOWN)) selected_ = (selected_ + 1) % n;
    const bool enter = keys_.Pressed(VK_RETURN);
    const bool back = keys_.Pressed(VK_BACK);

    overlay::MenuView v;
    v.open = true;
    v.title = cfg_.value("title", "Hikaye RP");
    v.subtitle = cfg_.value("subtitle", "");
    v.selected = selected_;
    v.hint = "↑↓ sec   Enter onay   Backspace kapat";
    for (const auto& r : menu_rows_) v.rows.push_back({r, "", false});
    overlay::SetMenu(v);

    if (back) return CloseMenu();
    if (!enter) return;

    const std::string choice = menu_rows_[static_cast<size_t>(selected_)];
    if (state_ == State::Menu) {
        if (choice.rfind("Devam et", 0) == 0) EnterMode(false, pool);
        else if (choice.rfind("Yeni karakter", 0) == 0) EnterMode(true, pool);
        else if (choice.rfind("Sifirdan baslama bekliyor", 0) == 0) {
            std::filesystem::remove(ResetMarker());
            Notice("Sifirdan baslama iptal edildi");
            CloseMenu();
        } else if (choice.rfind("Sifirdan basla", 0) == 0) {
            std::ofstream(ResetMarker()) << "1";
            Notice("Oyunu yeniden baslattiginda hikaye sifirdan baslayacak (eski kayitlar saves_yedek klasörüne tasinir)");
            CloseMenu();
        } else CloseMenu();
    } else {
        if (choice == "Hikaye moduna don") {
            ExitMode();
        } else {
            CloseMenu();
        }
    }
}

void Mode::PersistIfChanged() {
    const unsigned long long now = GetTickCount64();
    if (profile_.cash != last_saved_cash_ && now - last_save_ms_ > 2000) {
        SaveProfile(profile_, slot_);
        last_saved_cash_ = profile_.cash;
        last_save_ms_ = now;
    }
}

void Mode::BeginChapter2(const json& pool) {
    if (state_ != State::Active) return;
    FadeOut(900);
    
    other_ = profile_;
    has_other_ = true;
    profile_ = CharacterProfile{};
    profile_.female = true;
    profile_.ResetOutfit();
    const json d = cfg_.value("default_character", json::object());
    profile_.hometown = d.value("hometown", "");
    profile_.district = d.value("district", "");
    profile_.past_job = d.value("past_job", "");
    profile_.team = d.value("team", "");
    profile_.name = "";
    profile_.cash = cfg_.value("start_cash", 5000);
    slot_ = 1;
    SaveSlotState();
    economy::SetWallet(&profile_.cash);
    const Ped ped = SwapModel(true);
    ApplyToPed(ped, profile_);
    WEAPON::REMOVE_ALL_PED_WEAPONS(ped, true);
    editing_ = false;
    creating_ch2_ = true;
    creator_.LockGender(true);
    creator_.Begin(&profile_, ped, &pool, [this](bool female) { return SwapModel(female); });
    state_ = State::Creating;
    FadeIn(700);
    Log("Bolum 2: kadin karakter olusturucu acildi");
}

void Mode::SwitchCharacter() {
    if (!has_other_ || state_ != State::Active) return;
    FadeOut(700);
    
    std::swap(profile_, other_);
    slot_ ^= 1;
    SaveSlotState();
    const Ped ped = SwapModel(profile_.female);
    ApplyToPed(ped, profile_);
    Spawn();
    GiveRpWeapons();
    economy::SetWallet(&profile_.cash);
    last_saved_cash_ = profile_.cash;
    Notice("Karakter: " + (profile_.name.empty() ? std::string(profile_.female ? "Kadin" : "Erkek") : profile_.name));
    FadeIn(700);
    Log(std::string("karakter degisti: slot ") + std::to_string(slot_));
}

Ped Mode::SpawnAlter(float x, float y, float z, float heading, std::string& name_out) {
    if (!has_other_) return 0;
    const Hash m = HashKey(other_.female ? "mp_f_freemode_01" : "mp_m_freemode_01");
    if (!RequestModel(m)) return 0;
    const Ped ped = PED::CREATE_PED(other_.female ? 5 : 4, m, x, y, z, heading, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(m);
    if (!ENTITY::DOES_ENTITY_EXIST(ped)) return 0;
    PED::SET_PED_DEFAULT_COMPONENT_VARIATION(ped);
    ApplyToPed(ped, other_);
    name_out = other_.name;
    return ped;
}

void Mode::Update(const json& pool) {
    if (GetTickCount64() < notice_until_) DrawLine(notice_, 0.8f, 255, 200, 120);

    const bool key = keys_.Pressed(controls::ModeKey());
    switch (state_) {
        case State::Dormant:
            if (key) {
                auto_open_ = false;
                std::string why;
                if (CanEnter(why)) OpenMenu();
                else Notice(why);
            } else {
                TryAutoOpen(pool);
            }
            break;
        case State::Menu:
        case State::ActiveMenu:
            if (key) CloseMenu();
            else UpdateMenu(pool);
            break;
        case State::Creating: {
            const Creator::Result r = creator_.Update();
            if (r == Creator::Result::Done) FinishCreation();
            else if (r == Creator::Result::Cancelled) CancelCreation();
            break;
        }
        case State::Active: {
            if (switch_allowed_ && has_other_ && keys_.Pressed(controls::SwitchKey())) {
                SwitchCharacter();
                break;
            }
            if (key) {
                OpenMenu();
                break;
            }
            PersistIfChanged();
            const Ped ped = PLAYER::PLAYER_PED_ID();
            const Hash want = HashKey(profile_.female ? "mp_f_freemode_01" : "mp_m_freemode_01");
            if (ENTITY::GET_ENTITY_MODEL(ped) != want && !PLAYER::IS_PLAYER_DEAD(PLAYER::PLAYER_ID()) && CAM::IS_SCREEN_FADED_IN()) {
                const unsigned long long now = GetTickCount64();
                if (model_bad_since_ == 0) model_bad_since_ = now;
                if (now - model_bad_since_ > 1500) {
                    ApplyToPed(SwapModel(profile_.female), profile_);
                    model_bad_since_ = 0;
                }
            } else {
                model_bad_since_ = 0;
            }
            break;
        }
    }
}

}  // namespace hikaye