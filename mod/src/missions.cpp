#include "missions.h"
#include "addons.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>

#include "economy.h"
#include "passenger.h"
#include "profile_gen.h"

using nlohmann::json;
namespace fs = std::filesystem;

namespace hikaye {

namespace {
constexpr float kSpawnDist = 100.f, kDespawnDist = 150.f, kTalkRadius = 2.5f, kVehicleSpawnDist = 160.f;
constexpr int kBlipColourYellow = 5;
std::string SavePath() { return std::string(kBaseDir) + "/saves/missions.json"; }
std::string DetailsPath() { return std::string(kBaseDir) + "/saves/npc_details.json"; }

json ReadJsonFile(const std::string& path) {
    try {
        std::ifstream in(path);
        if (in) return json::parse(in);
    } catch (...) {
    }
    return json::object();
}
float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
Vector3 V3(const json& p) { return {p[0].get<float>(), 0, p[1].get<float>(), 0, p[2].get<float>(), 0}; }
}  // namespace

void Missions::Load() {
    missions_.clear();
    // Arkadaşların kişisel ayrıntıları (yaş, burç...) ilk oluşturulunca kaydedilir ve sabit kalır.
    const json pool = ReadJsonFile(std::string(kBaseDir) + "/data/profile_pool.json");
    json details = ReadJsonFile(DetailsPath());
    bool details_changed = false;
    // İsim değişikliği göçü: Ayşe Demir -> Ezgi Güler (sabit kişisel detaylar ve ilişki durumu korunur)
    if (details.contains("Ayşe Demir") && !details.contains("Ezgi Güler")) {
        details["Ezgi Güler"] = details["Ayşe Demir"];
        details.erase("Ayşe Demir");
        details_changed = true;
    }
    const fs::path dir = fs::path(kBaseDir) / "data" / "missions";
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (e.path().extension() == ".json") files.push_back(e.path());
        }
        std::sort(files.begin(), files.end());
        for (const auto& f : addons::Files("missions", ".json")) files.push_back(f);  // eklenti görevleri sona eklenir
        for (const auto& f : files) {
            try {
                std::ifstream in(f);
                const json j = json::parse(in);
                Mission m;
                m.id = j.at("id").get<std::string>();
                m.title = j.value("title", m.id);
                m.giver = j.at("giver").get<std::string>();
                m.reward = j.value("reward", 0);
                m.order = j.value("order", 0);
                m.requires_ = j.value("requires", std::vector<std::string>());
                m.start_by = j.value("start_by", std::string("contact"));
                m.call_delay_s = j.value("call_delay_s", 60);
                m.giver_always = j.value("giver_always", true);
                m.unlocks_contacts = j.value("unlocks_contacts", std::vector<std::string>());
                if (j.contains("secret") && j["secret"].is_object()) m.secret = j["secret"];
                if (j.contains("on_complete_flags")) m.on_complete_flags = j["on_complete_flags"].get<std::map<std::string, std::string>>();
                m.clear_contacts = j.value("clear_contacts", false);
                m.removes_contacts = j.value("removes_contacts", std::vector<std::string>());
                if (j.contains("on_start_flags")) m.on_start_flags = j["on_start_flags"].get<std::map<std::string, std::string>>();
                if (j.contains("sets_relationship")) {
                    m.rel_npc = j["sets_relationship"].value("npc", "");
                    m.rel_status = j["sets_relationship"].value("status", "");
                }
                for (const auto& [id, nj] : j.at("npcs").items()) {
                    Npc n;
                    n.id = id;
                    n.model = nj.at("model").get<std::string>();
                    n.fallback_models = nj.value("fallback_models", std::vector<std::string>());
                    n.gender = nj.value("gender", "male");
                    n.exact_z = nj.value("exact_z", false);
                    n.follows = nj.value("follows_into_interiors", false);
                    n.pos = V3(nj.at("pos"));
                    n.heading = nj.value("heading", 0.f);
                    n.npc_json = {{"personality", nj.value("personality", "")},
                                  {"profile", nj.value("profile", json::object())},
                                  {"voice", nj.value("voice", json::object())},
                                  {"gender", n.gender}};
                    {   // Kişisel ayrıntılar: JSON'da elle yazılmış alan kalır, eksikler kişinin ADI anahtarıyla üretilip saklanır.
                        json prof = n.npc_json["profile"];
                        const std::string pname = prof.value("name", id);
                        if (!details.contains(pname)) {
                            details[pname] = profile_gen::GenerateDetails(pname, pool);
                            details_changed = true;
                        }
                        for (const auto& [k, v] : details[pname].items()) {
                            if (!prof.contains(k)) prof[k] = v;
                        }
                        n.npc_json["profile"] = prof;
                    }
                    m.npcs.push_back(std::move(n));
                }
                for (const auto& s : j.at("stages")) {
                    Stage st;
                    st.id = s.value("id", "");
                    st.type = s.value("type", "talk");
                    st.npc = s.value("npc", "");
                    st.objective = s.value("objective", "");
                    st.brief = s.value("brief", "");
                    st.advance_when = s.value("advance_when", "");
                    st.vehicle = s.value("vehicle", "");
                    st.prompt = s.value("prompt", "");
                    st.guide = s.value("guide", "");
                    st.distance = s.value("distance", 200.f);
                    st.place = s.value("place", "");
                    st.kill_npc = s.value("kill_npc", "");
                    if (s.contains("finale") && s["finale"].is_object()) st.finale = s["finale"];
                    st.help = s.value("help", "");
                    st.item_options = s.value("item_options", std::vector<std::string>());
                    if (s.contains("installer")) st.installer = s["installer"];
                    if (s.contains("enemies") && s["enemies"].is_array()) st.enemies = s["enemies"];
                    st.kill_target = s.value("kill_target", 0);
                    st.in_person = s.value("in_person", false);
                    st.auto_advance = s.value("auto_advance", false);
                    st.min_turns = std::max(1, s.value("min_turns", 1));
                    if (s.contains("pos")) {
                        st.pos = V3(s["pos"]);
                        st.has_pos = true;
                    }
                    st.radius = s.value("radius", st.type == "interact" || st.type == "install" ? 3.0f : 12.0f);
                    st.heading = s.value("heading", 0.f);
                    if ((st.type == "talk" || st.type == "install" || st.type == "escort" || st.type == "walk") && st.npc.empty()) throw std::runtime_error("npc yok: " + st.id);
                    if ((st.type == "interact" || st.type == "install" || st.type == "escort" || st.type == "scene") && !st.has_pos) throw std::runtime_error("pos yok: " + st.id);
                    if (st.type == "combat" && st.enemies.empty()) throw std::runtime_error("combat asamasi icin enemies gerekli: " + st.id);
                    m.stages.push_back(std::move(st));
                }
                if (m.stages.empty()) throw std::runtime_error("asama yok");
                Log("gorev yuklendi: " + m.id + " (" + std::to_string(m.stages.size()) + " asama)");
                missions_.push_back(std::move(m));
            } catch (const std::exception& e) {
                Log("gorev okunamadi (" + f.string() + "): " + e.what());
            }
        }
    }
    std::stable_sort(missions_.begin(), missions_.end(), [](const Mission& a, const Mission& b) { return a.order < b.order; });
    if (details_changed) {
        try {
            fs::create_directories(std::string(kBaseDir) + "/saves");
            std::ofstream out(DetailsPath());
            out << details.dump(2);
        } catch (...) {
        }
    }
    try {  // ilerleme, rehber, ilişkiler ve bayraklar
        std::ifstream in(SavePath());
        if (in) {
            const json j = json::parse(in);
            completed_ = j.value("completed", std::vector<std::string>());
            contacts_ = j.value("contacts", std::vector<std::string>());
            stage_ = static_cast<size_t>(std::max(0, j.value("stage", 0)));  // eski kayıt: tek görev aşaması
            if (j.contains("stages")) stages_ = j["stages"].get<std::map<std::string, size_t>>();
            focus_ = j.value("focus", std::string());
            if (!focus_.empty()) {  // stage_ odak görevin aşamasıdır
                const auto it = stages_.find(focus_);
                stage_ = it == stages_.end() ? 0 : it->second;
            }
            if (j.contains("relationship")) rel_ = j["relationship"].get<std::map<std::string, std::string>>();
            if (j.contains("flags")) flags_ = j["flags"].get<std::map<std::string, std::string>>();
        }
    } catch (...) {
        completed_.clear();
        contacts_.clear();
        rel_.clear();
        flags_.clear();
        stage_ = 0;
    }
    if (rel_.count("Ayşe Demir") && !rel_.count("Ezgi Güler")) {
        rel_["Ezgi Güler"] = rel_["Ayşe Demir"];
        rel_.erase("Ayşe Demir");
    }
    if (const Mission* m = Current(); m && stage_ >= m->stages.size()) stage_ = 0;
}

const Missions::Mission* Missions::OpenMissionOf(const std::string& giver_id) const {
    const Mission* best = nullptr;
    size_t best_stage = 0;
    for (const auto& m : missions_) {
        if (m.giver != giver_id || !IsOpen(m)) continue;
        const size_t st = m.id == focus_ ? stage_ : (stages_.count(m.id) ? stages_.at(m.id) : 0);
        if (!best || (st > 0 && best_stage == 0)) {  // devam eden görev öncelikli, yoksa en düşük sıralı
            best = &m;
            best_stage = st;
        }
    }
    return best;
}

void Missions::Save() const {
    try {
        fs::create_directories(std::string(kBaseDir) + "/saves");
        std::ofstream out(SavePath());
        std::map<std::string, size_t> all = stages_;
        if (!focus_.empty()) all[focus_] = stage_;
        out << json{{"completed", completed_}, {"contacts", contacts_}, {"stages", all}, {"focus", focus_},
                    {"relationship", rel_},   {"flags", flags_}}.dump(2);
    } catch (...) {
    }
}

std::string Missions::Flag(const std::string& key) const {
    const auto it = flags_.find(key);
    return it == flags_.end() ? std::string() : it->second;
}

std::string Missions::MissionState(const std::string& id) const {
    if (std::find(completed_.begin(), completed_.end(), id) != completed_.end()) return "done";
    for (const auto& m : missions_) {
        if (m.id != id) continue;
        if (!IsOpen(m)) return "locked";
        const size_t st = id == focus_ ? stage_ : (stages_.count(id) ? stages_.at(id) : 0);
        return st > 0 ? "active" : "available";
    }
    return "unknown";
}

bool Missions::IsOpen(const Mission& m) const {
    if (std::find(completed_.begin(), completed_.end(), m.id) != completed_.end()) return false;
    for (const auto& r : m.requires_) {  // gerekli görevler bitti mi
        if (std::find(completed_.begin(), completed_.end(), r) == completed_.end()) return false;
    }
    return true;
}

void Missions::SwitchFocus(const std::string& id) const {
    if (id == focus_) return;
    Missions* self = const_cast<Missions*>(this);
    if (!focus_.empty()) {
        stages_[focus_] = stage_;
        for (auto& m : self->missions_) {  // eski odağın NPC'leri, aracı ve blipi temizlenir (ilerleme saklı kalır)
            if (m.id != focus_) continue;
            for (auto& n : m.npcs) self->Despawn(n);
            self->ReleaseVehicle(m);
        }
        self->RemoveBlip();
    }
    focus_ = id;
    const auto it = stages_.find(id);
    stage_ = it == stages_.end() ? 0 : it->second;
    for (const auto& m : missions_) {
        if (m.id == id && stage_ >= m.stages.size()) stage_ = 0;
    }
}

// Açık görevlerden biri odaktadır. Odak görev bitince en düşük sıralı açık görev seçilir. Paralel görevler arasında
// geçiş CallTarget ile yapılır (rehberden aranan kişinin görevi öne geçer).
const Missions::Mission* Missions::Current() const {
    if (!focus_.empty()) {
        for (const auto& m : missions_) {
            if (m.id == focus_ && IsOpen(m)) return &m;
        }
    }
    for (const auto& m : missions_) {
        if (IsOpen(m)) {
            SwitchFocus(m.id);
            return &m;
        }
    }
    return nullptr;  // hepsi bitti
}
Missions::Mission* Missions::Current() { return const_cast<Mission*>(static_cast<const Missions*>(this)->Current()); }

const Missions::Npc* Missions::FindNpc(const Mission& m, const std::string& id) const {
    for (const auto& n : m.npcs) {
        if (n.id == id) return &n;
    }
    return nullptr;
}
const Missions::Npc* Missions::FindNpcAnywhere(const std::string& id) const {
    for (const auto& m : missions_) {
        if (const Npc* n = FindNpc(m, id)) return n;
    }
    return nullptr;
}

// ---------------- NPC'ler ----------------

bool Missions::Spawn(Npc& n) {
    if (n.model == "__alter__") {  // oyuncunun diğer (oluşturulmuş) karakteri
        if (!alter_spawn_) return false;
        std::string who;
        float gx = n.pos.x, gy = n.pos.y, gz2 = n.pos.z, gz3 = 0.f;
        STREAMING::REQUEST_COLLISION_AT_COORD(gx, gy, gz2);
        if (GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(gx, gy, gz2 + 20.f, &gz3, false)) gz2 = gz3;
        else if (++n.ground_tries < 120) return false;
        const Ped p = alter_spawn_(gx, gy, gz2, n.heading, who);
        if (!p) return false;
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, true, true);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(p, true);
        ENTITY::SET_ENTITY_INVINCIBLE(p, true);
        ENTITY::FREEZE_ENTITY_POSITION(p, true);
        n.ped = p;
        n.pos.z = gz2;
        if (!who.empty()) {  // karakterin kendi adı konuşmada kullanılsın
            n.npc_json["profile"]["name"] = who;
        }
        Log("gorev NPC dogdu (diger karakter): " + n.id);
        return true;
    }
    // Model oyunda yoksa yedek zincirini dene (seçilen model kalıcı olur, her seferinde tekrar aranmaz).
    std::vector<std::string> candidates{n.model};
    candidates.insert(candidates.end(), n.fallback_models.begin(), n.fallback_models.end());
    std::string chosen;
    for (const auto& c : candidates) {
        const Hash h = HashKey(c.c_str());
        if (STREAMING::IS_MODEL_IN_CDIMAGE(h) && STREAMING::IS_MODEL_VALID(h)) {
            chosen = c;
            break;
        }
    }
    if (chosen.empty()) {
        Log("gorev NPC modeli bulunamadi: " + n.model + " (yedekler de yok)");
        n.ground_tries = 9999;  // tekrar deneme
        return false;
    }
    if (chosen != n.model) {
        Log("gorev NPC modeli yok, yedek kullaniliyor: " + n.model + " -> " + chosen);
        n.model = chosen;
        n.fallback_models.clear();
    }
    const Hash model = HashKey(n.model.c_str());
    STREAMING::REQUEST_MODEL(model);
    if (!STREAMING::HAS_MODEL_LOADED(model)) return false;  // sonraki karede tekrar

    // Zemin çarpışması yüklenene kadar bekle (bina içi/üstü yerine zemine oturt).
    float x = n.pos.x, y = n.pos.y, z = n.pos.z;
    STREAMING::REQUEST_COLLISION_AT_COORD(x, y, z);
    float gz = 0.f;
    if (n.exact_z) {
        // iç mekan: bulunduğu katın zeminine oturt (sadece 1 m yukarıdan aşağı bak; çatıyı bulmasın). Bulunamazsa verilen z.
        if (GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(x, y, z + 1.0f, &gz, false) && std::fabs(gz - z) < 2.5f) z = gz + 1.0f;
    } else if (GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(x, y, z + 20.f, &gz, false)) {
        z = gz;
    } else if (++n.ground_tries < 120) {
        return false;  // çarpışma henüz yok
    }
    const int ped_type = n.gender == "female" ? 5 : 4;
    Ped ped = PED::CREATE_PED(ped_type, model, x, y, z, n.heading, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
    if (!ENTITY::DOES_ENTITY_EXIST(ped)) return false;
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped, true, true);  // oyun bu NPC'yi kendiliğinden silmesin (özellikle iç mekanda)
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, true);
    PED::SET_PED_CAN_RAGDOLL(ped, false);
    ENTITY::SET_ENTITY_INVINCIBLE(ped, true);
    ENTITY::FREEZE_ENTITY_POSITION(ped, true);
    n.ped = ped;
    n.pos.z = z;
    {
        const Vector3 at = ENTITY::GET_ENTITY_COORDS(ped, true);
        Log("gorev NPC dogdu: " + n.id + " @ " + std::to_string(at.x) + ", " + std::to_string(at.y) + ", " + std::to_string(at.z));
    }
    return true;
}

void Missions::Despawn(Npc& n) {
    if (n.ped == 0) return;
    Ped p = n.ped;
    if (ENTITY::DOES_ENTITY_EXIST(p)) {
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, true, true);
        PED::DELETE_PED(&p);
    }
    n.ped = 0;
    n.ground_tries = 0;
}

void Missions::SpawnOrDespawn(Mission& m, const Vector3& player_pos) {
    // Görev verici (giver_always ise) her zaman durur; yüz yüze veya kurulum aşamasındaki NPC de belirir. Diğerleri silinir.
    std::string current;
    if (stage_ < m.stages.size() && (m.stages[stage_].type == "talk" || m.stages[stage_].type == "install" || m.stages[stage_].type == "escort" || m.stages[stage_].type == "scene" || m.stages[stage_].type == "walk")) {
        current = m.stages[stage_].npc;
    }
    for (auto& n : m.npcs) {
        const bool wanted = (m.giver_always && n.id == m.giver) || n.id == current;
        const float d = D2(player_pos, n.pos);
        if (n.ped != 0 && !ENTITY::DOES_ENTITY_EXIST(n.ped)) {
            Log("gorev NPC kayboldu: " + n.id);
            n.ped = 0;
        }
        if (n.ped != 0 && n.exact_z) {  // teşhis: iç mekan NPC'sinin yeri (5 sn'de bir)
            static unsigned long long diag_at = 0;
            const unsigned long long t = GetTickCount64();
            if (t > diag_at) {
                diag_at = t + 5000;
                const Vector3 at = ENTITY::GET_ENTITY_COORDS(n.ped, true);
                Log("gorev NPC " + n.id + " @ " + std::to_string(at.x) + ", " + std::to_string(at.y) + ", " + std::to_string(at.z) +
                    " oyuncuya " + std::to_string(D2(player_pos, at)) + " m, gorunur=" +
                    (ENTITY::IS_ENTITY_VISIBLE(n.ped) ? "evet" : "hayir") + " olu=" + (ENTITY::IS_ENTITY_DEAD(n.ped) ? "evet" : "hayir"));
            }
        }

        if (n.exact_z && inside_interior_ && d > 80.f) inside_interior_ = false;  // başka yolla uzaklaşıldı (menyoo, ölüm...)
        if (n.exact_z && n.ped != 0 && !inside_interior_) {
            Despawn(n);  // iç mekandan çıkıldı
        } else if (wanted && n.ped == 0 && (n.exact_z ? inside_interior_ : d < kSpawnDist)) {
            Spawn(n);
        } else if (n.ped != 0 && wanted) {
            n.despawn_at = 0;
            if (d > kDespawnDist && !((escort_boarded_ || walk_started_) && n.id == current)) Despawn(n);
        } else if (n.ped != 0) {
            // Artık gerekli değil: uzaktaysa hemen, yakındaysa 15 sn sonra sil (konuşma yarıda kesilmesin)
            const unsigned long long t = GetTickCount64();
            if (d > kDespawnDist) Despawn(n);
            else if (n.despawn_at == 0) n.despawn_at = t + 15000ULL;
            else if (t > n.despawn_at) Despawn(n);
        }
    }
}

// ---------------- Silahlı çatışma (combat) ----------------

void Missions::CleanupCombat() {
    for (auto& e : combat_enemies_) {
        if (e.ped != 0 && ENTITY::DOES_ENTITY_EXIST(e.ped)) {
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(e.ped, false, true);
            Ped p = e.ped;
            ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&p);
        }
    }
    combat_enemies_.clear();
    combat_spawned_ = false;
    combat_killed_ = 0;
    combat_done_ = false;
}

void Missions::SpawnCombatWave(const Stage& s) {
    CleanupCombat();
    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vector3 pp = ENTITY::GET_ENTITY_COORDS(player, true);
    static std::mt19937 rng(static_cast<unsigned>(GetTickCount64()));
    int total = 0;
    for (const auto& grp : s.enemies) {
        const std::string model_name = grp.value("model", "a_m_y_genstreet_01");
        const Hash model = HashKey(model_name.c_str());
        const int count = std::clamp(grp.value("count", 1), 1, 10);
        const float radius = grp.value("radius", 12.f);
        const int health = std::clamp(grp.value("health", 100), 1, 200);
        const int accuracy = std::clamp(grp.value("accuracy", 50), 0, 100);
        const int armor = std::clamp(grp.value("armor", 0), 0, 100);
        const Hash weapon = HashKey(grp.value("weapon", "WEAPON_PISTOL").c_str());
        Vector3 base = pp;
        if (grp.contains("pos") && grp["pos"].is_array() && grp["pos"].size() >= 3)
            base = V3(grp["pos"]);
        STREAMING::REQUEST_MODEL(model);
        for (int w = 0; w < 60 && !STREAMING::HAS_MODEL_LOADED(model); ++w) WAIT(0);
        if (!STREAMING::HAS_MODEL_LOADED(model)) {
            Log("combat: model yuklenemedi: " + model_name);
            STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
            continue;
        }
        for (int i = 0; i < count; ++i) {
            const float ang = std::uniform_real_distribution<float>(0.f, 6.2832f)(rng);
            const float r   = std::uniform_real_distribution<float>(4.f, std::max(4.1f, radius))(rng);
            Ped p = PED::CREATE_PED(4, model, base.x + r * std::cos(ang), base.y + r * std::sin(ang), base.z, 0.f, false, false);
            if (!ENTITY::DOES_ENTITY_EXIST(p)) continue;
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, true, true);
            ENTITY::SET_ENTITY_HEALTH(p, 100 + health);   // GTA: 100=ölü, 200=tam can
            PED::SET_PED_ARMOUR(p, armor);
            PED::SET_PED_ACCURACY(p, accuracy);
            WEAPON::GIVE_WEAPON_TO_PED(p, weapon, 200, false, true);
            PED::SET_PED_COMBAT_ABILITY(p, 2);             // profesyonel
            PED::SET_PED_COMBAT_MOVEMENT(p, 2);            // saldırgan
            PED::SET_PED_FLEE_ATTRIBUTES(p, 0, false);     // kaçmaz
            PED::SET_PED_COMBAT_ATTRIBUTES(p, 46, true);
            AI::TASK_COMBAT_HATED_TARGETS_AROUND_PED(p, 200.f, 0);
            combat_enemies_.push_back({p, false});
            ++total;
        }
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
    }
    combat_spawned_ = true;
    Log("combat: " + std::to_string(total) + " dusman dogdu");
}

// ---------------- Görev aracı ----------------

bool Missions::SpawnVehicle(Mission& m, const Stage& s) {
    const Hash model = HashKey(s.vehicle.c_str());
    if (!STREAMING::IS_MODEL_IN_CDIMAGE(model) || !STREAMING::IS_MODEL_A_VEHICLE(model)) {
        Log("gorev araci gecersiz: " + s.vehicle);
        return false;
    }
    STREAMING::REQUEST_MODEL(model);
    for (int i = 0; i < 200 && !STREAMING::HAS_MODEL_LOADED(model); ++i) WAIT(0);  // script thread'inde WAIT güvenli
    if (!STREAMING::HAS_MODEL_LOADED(model)) return false;
    Vector3 at = s.pos;
    Vector3 road{};
    if (PATHFIND::GET_CLOSEST_VEHICLE_NODE(s.pos.x, s.pos.y, s.pos.z, &road, 1, 3.0f, 0)) at = road;  // en yakın yola otur
    Vehicle v = VEHICLE::CREATE_VEHICLE(model, at.x, at.y, at.z, s.heading, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
    if (!ENTITY::DOES_ENTITY_EXIST(v)) return false;
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(v, true, true);
    VEHICLE::SET_VEHICLE_DOORS_LOCKED(v, 1);
    VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(v);
    m.veh = v;
    Log("gorev araci dogdu: " + s.vehicle);
    return true;
}

void Missions::ReleaseVehicle(Mission& m) {
    if (m.veh != 0 && ENTITY::DOES_ENTITY_EXIST(m.veh)) {
        Vehicle v = m.veh;
        ENTITY::SET_VEHICLE_AS_NO_LONGER_NEEDED(&v);  // oyun yönetsin; oyuncu isterse kullanır
    }
    m.veh = 0;
}

// ---------------- Aşamalar (araç, etkileşim, kurulum) ----------------

void Missions::UpdateStages(Mission& m, const Vector3& player_pos, bool e_pressed) {
    if (stage_ >= m.stages.size()) return;
    const Stage& s = m.stages[stage_];
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (s.type == "vehicle") {
        if (m.veh == 0 || !ENTITY::DOES_ENTITY_EXIST(m.veh)) {
            m.veh = 0;
            if (D2(player_pos, s.pos) < kVehicleSpawnDist) SpawnVehicle(m, s);
        } else if (PED::IS_PED_IN_VEHICLE(player, m.veh, false)) {
            Advance(m);  // araca bindi: kod algılar, LLM'e gerek yok
        }
    } else if (s.type == "drive_to") {
        const bool lost = m.veh == 0 || !ENTITY::DOES_ENTITY_EXIST(m.veh) || ENTITY::IS_ENTITY_DEAD(m.veh) ||
                          !VEHICLE::IS_VEHICLE_DRIVEABLE(m.veh, false);
        if (lost) {  // araç yok oldu/bozuldu: bir önceki 'vehicle' aşamasına dön, yeni araç doğar
            ReleaseVehicle(m);
            for (size_t i = stage_; i-- > 0;) {
                if (m.stages[i].type == "vehicle") {
                    stage_ = i;
                    break;
                }
            }
            toast_ = "Görev aracı kullanılamaz oldu, yeni araç bekleniyor";
            toast_until_ = GetTickCount64() + 5000;
            RemoveBlip();
            Save();
            return;
        }
        const Vector3 vp = ENTITY::GET_ENTITY_COORDS(m.veh, true);
        if (PED::IS_PED_IN_VEHICLE(player, m.veh, false) && D2(vp, s.pos) < s.radius) Advance(m);
    } else if (s.type == "combat") {
        // Silahlı çatışma: enemies listesinden NPC'ler doğurulur; kill_target kadar (veya hepsi) öldürülünce ilerler.
        if (!combat_spawned_) SpawnCombatWave(s);
        if (!combat_done_) {
            const int kill_need = s.kill_target > 0 ? s.kill_target : static_cast<int>(combat_enemies_.size());
            for (auto& e : combat_enemies_) {
                if (!e.confirmed_dead && e.ped != 0 && ENTITY::DOES_ENTITY_EXIST(e.ped) && ENTITY::IS_ENTITY_DEAD(e.ped)) {
                    e.confirmed_dead = true;
                    ++combat_killed_;
                }
            }
            if (kill_need > 0 && combat_killed_ >= kill_need) combat_done_ = true;
        }
        if (combat_done_) {
            CleanupCombat();
            Advance(m);
        } else if (!s.objective.empty()) {
            const int kill_need = s.kill_target > 0 ? s.kill_target : static_cast<int>(combat_enemies_.size());
            const std::string prog = std::to_string(combat_killed_) + "/" + std::to_string(kill_need);
            toast_ = s.objective + "  [" + prog + "]";
            toast_until_ = GetTickCount64() + 500;
        }
    } else if (s.type == "escort") {
        // NPC'yi arabayla taşı: yanına yaklaş, bindir, hedefe götür. Hepsini kod algılar.
        Npc* n = nullptr;
        for (auto& x : m.npcs) {
            if (x.id == s.npc) n = &x;
        }
        if (!n || n->ped == 0 || !ENTITY::DOES_ENTITY_EXIST(n->ped)) return;
        const Vehicle pv = PED::GET_VEHICLE_PED_IS_IN(player, false);
        if (!escort_boarded_) {
            if (pv != 0 && VEHICLE::IS_THIS_MODEL_A_CAR(ENTITY::GET_ENTITY_MODEL(pv)) && D2(player_pos, n->pos) < 14.f &&
                VEHICLE::IS_VEHICLE_SEAT_FREE(pv, 0)) {
                if (escort_veh_ != pv) {
                    escort_veh_ = pv;
                    passenger::Board(n->ped, pv);
                }
            }
            if (escort_veh_ != 0 && PED::IS_PED_IN_VEHICLE(n->ped, escort_veh_, false)) {
                escort_boarded_ = true;
                toast_ = n->npc_json.value("profile", json::object()).value("name", n->id) + " arabaya bindi";
                toast_until_ = GetTickCount64() + 5000;
                RemoveBlip();
            }
        } else {
            if (escort_veh_ == 0 || !PED::IS_PED_IN_VEHICLE(n->ped, escort_veh_, false)) {  // indi/düştü: yeniden almak gerek
                escort_boarded_ = false;
                escort_veh_ = 0;
                RemoveBlip();
            } else if (PED::IS_PED_IN_VEHICLE(player, escort_veh_, false) &&
                       D2(ENTITY::GET_ENTITY_COORDS(escort_veh_, true), s.pos) < s.radius) {
                passenger::Release(n->ped, escort_veh_);  // NPC iner, uzaklaşır; sonra silinir
                n->ped = 0;
                escort_boarded_ = false;
                escort_veh_ = 0;
                Advance(m);
            }
        }
    } else if (s.type == "walk") {
        // Birlikte yürüyüş: NPC oyuncuyu takip eder; yakınken kat edilen mesafeyi KOD sayar, bitince aşama ilerler.
        Npc* n = nullptr;
        for (auto& x : m.npcs) {
            if (x.id == s.npc) n = &x;
        }
        if (!n || n->ped == 0 || !ENTITY::DOES_ENTITY_EXIST(n->ped)) return;
        const unsigned long long now = GetTickCount64();
        const Vector3 np = ENTITY::GET_ENTITY_COORDS(n->ped, true);
        if (!walk_started_) {
            if (D2(player_pos, np) < 6.f && !PED::IS_PED_IN_ANY_VEHICLE(player, false)) {
                walk_started_ = true;
                walk_progress_ = 0.f;
                walk_toasted_ = 0;
                walk_last_ = player_pos;
                ENTITY::FREEZE_ENTITY_POSITION(n->ped, false);
                AI::TASK_FOLLOW_TO_OFFSET_OF_ENTITY(n->ped, player, 1.1f, -0.4f, 0.f, 1.0f, -1, 1.5f, true);
                RemoveBlip();
                toast_ = "Yürüyüşe başladınız";
                toast_until_ = now + 4000;
            }
            return;
        }
        if (PED::IS_PED_IN_ANY_VEHICLE(player, false)) {  // araçtayken sayılmaz
            walk_last_ = player_pos;
            if (now - walk_toast_at_ > 8000) {
                toast_ = "Yürüyüş için araçtan in";
                toast_until_ = now + 3000;
                walk_toast_at_ = now;
            }
            return;
        }
        const float together = D2(player_pos, np);
        if (together > 25.f) {  // geride kaldı: bekle
            walk_last_ = player_pos;
            if (now - walk_toast_at_ > 8000) {
                toast_ = "Ezgi'yi bekle, yanında yürü";
                toast_until_ = now + 3000;
                walk_toast_at_ = now;
            }
            return;
        }
        const float step = D2(player_pos, walk_last_);
        walk_last_ = player_pos;
        if (step < 4.f && together < 12.f) walk_progress_ += step;  // ışınlanma/uzak adımlar sayılmaz
        const int pct = static_cast<int>(std::min(100.f, walk_progress_ / std::max(1.f, s.distance) * 100.f));
        if (pct / 25 > walk_toasted_ && pct < 100) {
            walk_toasted_ = pct / 25;
            toast_ = "Yürüyüş: %" + std::to_string(pct);
            toast_until_ = now + 3000;
        }
        if (walk_progress_ >= s.distance) {
            AI::CLEAR_PED_TASKS(n->ped);
            ENTITY::FREEZE_ENTITY_POSITION(n->ped, true);
            Advance(m);
        }
    } else if (s.type == "scene") {
        // Kurgu sahnesi: oyuncu yaklaşınca NPC'yi KOD öldürür (örn. Ayşe'nin ölümü); 5 sn sonra aşama ilerler.
        Npc* n = nullptr;
        for (auto& x : m.npcs) {
            if (x.id == s.kill_npc) n = &x;
        }
        if (!n || n->ped == 0 || !ENTITY::DOES_ENTITY_EXIST(n->ped)) return;
        const unsigned long long now = GetTickCount64();
        if (scene_at_ == 0) {
            if (D2(player_pos, ENTITY::GET_ENTITY_COORDS(n->ped, true)) < s.radius) {
                scene_at_ = now;
                ENTITY::SET_ENTITY_INVINCIBLE(n->ped, false);
                ENTITY::FREEZE_ENTITY_POSITION(n->ped, false);
                ENTITY::SET_ENTITY_HEALTH(n->ped, 0);  // sahne: ölüm
                toast_ = s.objective;
                toast_until_ = now + 6000;
                Log("sahne: " + n->id + " oldu");
            }
        } else if (now - scene_at_ > 5000) {
            scene_at_ = 0;
            Advance(m);
        }
    } else if (s.type == "interact" || s.type == "install") {
        // Belirli bir noktada E: sensör kur, bilgisayarı kullan...
        if (!PED::IS_PED_IN_ANY_VEHICLE(player, false) && D2(player_pos, s.pos) < s.radius) {
            ShowHelp(("~INPUT_PICKUP~ " + s.prompt).c_str());
            if (e_pressed && s.type == "interact" && s.finale.is_object()) {
                // Kapanış sahnesi: ekran kararır, yazılar akar, süre dolunca görev biter.
                finale_lines_ = s.finale.value("lines", std::vector<std::string>());
                finale_start_ = GetTickCount64();
                finale_until_ = finale_start_ + static_cast<unsigned long long>(s.finale.value("seconds", 16)) * 1000ULL;
                finale_active_ = true;
                CAM::DO_SCREEN_FADE_OUT(2000);
                PLAYER::SET_PLAYER_CONTROL(PLAYER::PLAYER_ID(), false, 0);
                Log("final sahnesi basladi");
            } else if (e_pressed) {
                if (s.type == "interact") Advance(m);
                else install_request_ = true;
            }
        }
    }
}

bool Missions::ConsumeInstallRequest(InstallSpec& out) {
    if (!install_request_) return false;
    install_request_ = false;
    const Mission* m = Current();
    if (!m || stage_ >= m->stages.size() || m->stages[stage_].type != "install") return false;
    const json& in = m->stages[stage_].installer;
    out = InstallSpec{};
    out.title = in.value("title", "Kurulum");
    for (const auto& c : in.value("components", json::array())) {
        out.components.push_back({c.value("id", ""), c.value("label", "")});
    }
    out.required = in.value("required", std::vector<std::string>());
    out.avoid = in.value("avoid", std::vector<std::string>());
    out.key = in.value("key", "");
    out.hints = Flag("yoklama") != "knows";  // IT yoklamasında bilen oyuncuya ipucu yok, diğerine var
    return true;
}

void Missions::InstallFinished(bool success, int mistakes) {
    Mission* m = Current();
    if (!m || stage_ >= m->stages.size() || m->stages[stage_].type != "install") return;
    if (!success) {
        toast_ = "Kurulum iptal edildi. Bilgisayara tekrar E ile dönebilirsin.";
        toast_until_ = GetTickCount64() + 5000;
        return;
    }
    flags_["kurulum_hata"] = std::to_string(mistakes);  // NPC'ler sonraki konuşmalarda bundan bahseder
    Log("kurulum tamamlandi, hata: " + std::to_string(mistakes));
    Advance(*m);
}

// ---------------- Blip ve ilerleme ----------------

bool Missions::StageTarget(const Mission& m, Vector3& out) const {
    if (stage_ >= m.stages.size()) return false;
    const Stage& s = m.stages[stage_];
    if (s.type == "talk") {
        if (!s.in_person && !m.giver_always) return false;  // telefonla yapılan aşamada gidilecek yer yok (blip/rota yok)
        const Npc* n = FindNpc(m, s.npc);
        if (!n) return false;
        out = n->pos;
        return true;
    }
    if (s.type == "walk") {
        if (walk_started_) return false;  // yürüyüş başladı: blip yok
        const Npc* wn = FindNpc(m, s.npc);
        if (!wn) return false;
        out = wn->pos;  // önce Ezgi'nin yanına git
        return true;
    }
    if (s.type == "escort" && !escort_boarded_) {
        const Npc* n = FindNpc(m, s.npc);
        if (!n) return false;
        out = n->pos;  // önce NPC'nin yanına git
        return true;
    }
    if (s.type == "vehicle" && m.veh != 0 && ENTITY::DOES_ENTITY_EXIST(m.veh)) {
        out = ENTITY::GET_ENTITY_COORDS(m.veh, true);  // araç doğduktan sonra aracın üstü
        return true;
    }
    out = s.pos;
    return true;
}

void Missions::RemoveBlip() {
    if (blip_ != 0 && UI::DOES_BLIP_EXIST(blip_)) UI::REMOVE_BLIP(&blip_);
    blip_ = 0;
    blip_for_.clear();
}

void Missions::RefreshBlip(const Mission& m) {
    // Anahtar: görev + aşama + (araç doğdu mu). Araç doğunca blip aracın üstüne taşınır.
    const std::string key = m.id + ":" + std::to_string(stage_) + ":" + (m.veh != 0 ? "v" : "-") + (escort_boarded_ ? "e" : "-") + (walk_started_ ? "w" : "-");
    if (blip_ != 0 && UI::DOES_BLIP_EXIST(blip_) && key == blip_for_) return;
    RemoveBlip();
    Vector3 t{};
    if (!StageTarget(m, t)) return;
    blip_ = UI::ADD_BLIP_FOR_COORD(t.x, t.y, t.z);
    UI::SET_BLIP_SPRITE(blip_, 1);
    UI::SET_BLIP_COLOUR(blip_, kBlipColourYellow);
    UI::SET_BLIP_SCALE(blip_, 1.1f);
    UI::SET_BLIP_ROUTE(blip_, true);
    UI::SET_BLIP_ROUTE_COLOUR(blip_, kBlipColourYellow);
    blip_for_ = key;
}

void Missions::UpdateGuidance(const Mission& m, const Vector3& pos) {
    if (stage_ >= m.stages.size()) return;
    const std::string key = m.id + ":" + std::to_string(stage_);
    const unsigned long long now = GetTickCount64();
    if (key != hint_key_) {  // aşama değişti: sayaç sıfırlanır
        hint_key_ = key;
        hint_since_ = now;
        return;
    }
    if (now - hint_since_ < 45000ULL || toast_until_ > now) return;  // 45 sn ilerleme yoksa ipucu ver (başka mesaj varken bekle)
    hint_since_ = now;
    const Stage& s = m.stages[stage_];
    const Npc* n = s.npc.empty() ? nullptr : FindNpc(m, s.npc);
    const std::string who = n ? n->npc_json.value("profile", json::object()).value("name", n->id) : std::string();
    std::string text = s.guide;
    if (text.empty()) {
        if (s.type == "talk") {
            if (!s.in_person && !m.giver_always) text = "Telefonu aç, rehberden " + who + "'ı ara ve konuş.";
            else text = who + "'ın yanına git ve yakınken E ile konuş.";
        } else if (s.type == "vehicle") {
            text = "Sarı işaretli araca bin.";
        } else if (s.type == "drive_to") {
            text = "Aracı sarı işaretli noktaya götür; araçtan inme.";
        } else if (s.type == "interact") {
            text = "Sarı işaretli noktaya git ve E'ye bas.";
        } else if (s.type == "install") {
            text = "Bilgisayarın başına git ve E'ye bas. Ürün anahtarını " + who + "'tan iste.";
        } else if (s.type == "walk") {
            text = who + "'ın yanına git ve yan yana yürü; sana eşlik eder. Sohbet için E ile konuşabilirsin.";
        } else if (s.type == "escort") {
            text = escort_boarded_ ? "Yolcuyu sarı işaretli noktaya götür; araçtan inme." : who + "'ın yanına arabayla yaklaş, binmesini bekle.";
        }
    }
    // Kişi bekleniyor ama ortada yok ve oyuncu yakında: büyük ihtimalle bina içi.
    if (s.guide.empty() && n && (s.type == "talk" || s.type == "install") && (s.in_person || m.giver_always) &&
        (n->ped == 0 || !ENTITY::DOES_ENTITY_EXIST(n->ped)) && D2(pos, n->pos) < 60.f) {
        text = who + " görünmüyorsa bina içindedir: girişteki E istemiyle içeri gir.";
    }
    if (text.empty()) return;
    toast_ = "İpucu: " + text;
    toast_until_ = now + 9000;
}

void Missions::UpdateGiverBlip(const Mission& m) {
    // Görevin şu anki hedefi zaten görev vericiyse sarı blip + rota onu gösterir; ayrıca mavi blipe gerek yok.
    const Npc* g = m.giver_always ? FindNpc(m, m.giver) : nullptr;  // sürekli duran görev vericisi yoksa mavi blip yok
    const bool target_is_giver = stage_ < m.stages.size() && m.stages[stage_].type == "talk" && m.stages[stage_].npc == m.giver;
    if (!g || target_is_giver) {
        if (giver_blip_ != 0 && UI::DOES_BLIP_EXIST(giver_blip_)) UI::REMOVE_BLIP(&giver_blip_);
        giver_blip_ = 0;
        giver_blip_for_.clear();
        return;
    }
    const std::string key = m.id + ":" + g->id;
    if (giver_blip_ != 0 && UI::DOES_BLIP_EXIST(giver_blip_) && key == giver_blip_for_) return;
    if (giver_blip_ != 0 && UI::DOES_BLIP_EXIST(giver_blip_)) UI::REMOVE_BLIP(&giver_blip_);
    giver_blip_ = UI::ADD_BLIP_FOR_COORD(g->pos.x, g->pos.y, g->pos.z);
    UI::SET_BLIP_SPRITE(giver_blip_, 1);
    UI::SET_BLIP_COLOUR(giver_blip_, 3);  // mavi
    UI::SET_BLIP_SCALE(giver_blip_, 0.85f);
    UI::SET_BLIP_AS_SHORT_RANGE(giver_blip_, false);
    giver_blip_for_ = key;
}

void Missions::AddContact(const std::string& id) {
    if (std::find(contacts_.begin(), contacts_.end(), id) != contacts_.end()) return;
    contacts_.push_back(id);
    const Npc* n = FindNpcAnywhere(id);
    const std::string name = n ? n->npc_json.value("profile", json::object()).value("name", id) : id;
    toast_ = "Rehbere eklendi: " + name;
    Log("rehbere eklendi: " + id);
}

json Missions::Advance(Mission& m) {
    talk_turns_ = 0;
    escort_boarded_ = false;
    walk_started_ = false;
    walk_progress_ = 0.f;
    scene_at_ = 0;
    if (stage_ == 0) {
        for (const auto& [k, v] : m.on_start_flags) flags_[k] = v;
    }
    ++stage_;
    json r = {{"advanced", true}, {"completed", false}};
    if (stage_ >= m.stages.size()) {  // görev bitti
        economy::Earn(m.reward);
        completed_.push_back(m.id);
        AddContact(m.giver);  // görev verici telefon rehberine girer; sonraki görev oradan aranır
        if (m.clear_contacts) contacts_.clear();  // bölüm geçişi: yeni karakterin rehberi sıfırdan başlar
        for (const auto& c : m.unlocks_contacts) AddContact(c);
        for (const auto& c : m.removes_contacts) contacts_.erase(std::remove(contacts_.begin(), contacts_.end(), c), contacts_.end());
        for (const auto& [k, v] : m.on_complete_flags) flags_[k] = v;
        if (!m.rel_npc.empty() && !m.rel_status.empty()) {  // ilişki aşaması ilerler (örn. tanışma -> arkadaş -> sevgili)
            const Npc* rn = FindNpc(m, m.rel_npc);
            if (rn) rel_[rn->npc_json.value("profile", json::object()).value("name", rn->id)] = m.rel_status;
        }
        const std::string reward_line =
            "Görev tamamlandı: " + m.title + (m.reward > 0 ? "  +$" + std::to_string(m.reward) : std::string());
        toast_ = reward_line + "  |  " + toast_;  // AddContact "Rehbere eklendi" yazdı
        ReleaseVehicle(m);
        stage_ = 0;
        r["completed"] = true;
        r["reward"] = m.reward;
        r["title"] = m.title;
        Log("gorev tamamlandi: " + m.id);
    } else {
        r["objective"] = m.stages[stage_].objective;
        toast_ = "Yeni hedef: " + m.stages[stage_].objective;
    }
    toast_until_ = GetTickCount64() + 7000;
    RemoveBlip();  // bir sonraki Update yeni hedefe blip koyar
    Save();
    return r;
}

void Missions::Update(const Vector3& player_pos, bool e_pressed) {
    if (finale_active_) {  // kapanış sahnesi: her şey durur
        CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
        if (GetTickCount64() > finale_until_) {
            finale_active_ = false;
            PLAYER::SET_PLAYER_CONTROL(PLAYER::PLAYER_ID(), true, 0);
            flags_["oyun_bitti"] = "1";
            if (Mission* fm = Current()) Advance(*fm);  // ödül ve görev sonu
            CAM::DO_SCREEN_FADE_IN(2500);
            Log("final sahnesi bitti");
        }
        return;
    }
    Mission* m = Current();
    if (!m) {
        RemoveBlip();
        if (!missions_.empty()) UpdateGiverBlip(missions_.back());  // bütün görevler bitti: görev vericinin blipi kalsın
        return;
    }
    SpawnOrDespawn(*m, player_pos);
    UpdateStages(*m, player_pos, e_pressed);
    if ((m = Current())) {  // görev az önce bitmiş olabilir
        RefreshBlip(*m);
        UpdateGiverBlip(*m);
        UpdateGuidance(*m, player_pos);
    } else {
        RemoveBlip();
    }
}

void Missions::Shutdown() {
    CleanupCombat();  // combat NPC'leri temizle
    if (finale_active_) {  // mod kapanırken final sahnesi yarım kaldıysa ekranı ve kontrolü geri ver
        finale_active_ = false;
        PLAYER::SET_PLAYER_CONTROL(PLAYER::PLAYER_ID(), true, 0);
        CAM::DO_SCREEN_FADE_IN(500);
    }
    inside_interior_ = false;
    RemoveBlip();
    if (giver_blip_ != 0 && UI::DOES_BLIP_EXIST(giver_blip_)) UI::REMOVE_BLIP(&giver_blip_);
    giver_blip_ = 0;
    giver_blip_for_.clear();
    install_request_ = false;
    for (auto& m : missions_) {
        for (auto& n : m.npcs) Despawn(n);
        if (m.veh != 0 && ENTITY::DOES_ENTITY_EXIST(m.veh)) {
            Vehicle v = m.veh;
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(v, true, true);
            VEHICLE::DELETE_VEHICLE(&v);  // mod kapanınca görev aracı kalmasın
        }
        m.veh = 0;
    }
}

void Missions::DrawHud() {
    const unsigned long long now = GetTickCount64();
    if (finale_active_) {  // satırlar sırayla belirir, her biri ~3,5 sn sonra bir sonraki gelir
        const size_t shown = std::min(finale_lines_.size(), static_cast<size_t>((now - finale_start_) / 3500ULL) + 1);
        for (size_t i = 0; i < shown; ++i) {
            const bool last = i + 1 == finale_lines_.size();
            DrawLine(finale_lines_[i], 0.40f + 0.07f * static_cast<float>(i), last ? 255 : 230, last ? 215 : 230, last ? 110 : 230);
        }
        return;
    }
    if (toast_until_ > now) DrawLine(toast_, 0.12f, 120, 230, 150);
    const Mission* m = Current();
    if (!m || stage_ >= m->stages.size()) return;
    DrawLine("GÖREV: " + m->title + "  -  " + m->stages[stage_].objective, 0.05f, 255, 215, 110);
}

// ---------------- Konuşma, rehber, gelen arama, ilişki ----------------

bool Missions::NearestTalkable(const Vector3& pos, Talk& out) const {
    const Mission* m = Current();
    // Sadece bilgisayarın hemen başındayken (kurulum istemi çıkarken); Burak'ın yanında konuşma (ürün anahtarı...) açık.
    if (m && stage_ < m->stages.size() && m->stages[stage_].type == "install" &&
        D2(pos, m->stages[stage_].pos) < m->stages[stage_].radius + 0.3f) {
        return false;
    }
    if (!m) {
        // Bütün görevler bitti: görev vericisi (son görevin) hâlâ konuşabilir (sohbet).
        if (missions_.empty()) return false;
        m = &missions_.back();
    }
    const Npc* best = nullptr;
    float best_d = kTalkRadius;
    for (const auto& n : m->npcs) {
        if (n.ped == 0 || !ENTITY::DOES_ENTITY_EXIST(n.ped)) continue;
        const float d = D2(pos, ENTITY::GET_ENTITY_COORDS(n.ped, true));
        if (d < best_d) {
            best_d = d;
            best = &n;
        }
    }
    if (!best) return false;
    out.npc_id = best->id;
    out.name = best->npc_json.value("profile", json::object()).value("name", best->id);
    out.ped = best->ped;
    out.npc_json = best->npc_json;
    out.pos = ENTITY::GET_ENTITY_COORDS(best->ped, true);
    return true;
}

bool Missions::CurrentTargetPos(Vector3& out, std::string& label) const {
    const Mission* m = Current();
    if (!m || stage_ >= m->stages.size() || !StageTarget(*m, out)) return false;
    label = m->title;
    return true;
}

std::vector<Missions::Contact> Missions::KnownCharacters() const {
    std::vector<std::string> ids = contacts_;
    const auto add = [&](const std::string& id) {
        if (!id.empty() && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    };
    const auto done = [&](const Mission& m) { return std::find(completed_.begin(), completed_.end(), m.id) != completed_.end(); };
    int floor = -1;  // bölüm geçişi (rehber sıfırlanır): eski bölümün karakterleri tanıdık sayılmaz
    for (const auto& m : missions_) {
        if (done(m) && m.clear_contacts) floor = std::max(floor, m.order);
    }
    std::vector<std::string> removed;
    for (const auto& m : missions_) {
        if (!done(m) || m.order < floor) continue;
        add(m.giver);
        for (const auto& u : m.unlocks_contacts) add(u);
        for (const auto& r : m.removes_contacts) removed.push_back(r);
    }
    std::vector<Contact> out;
    for (const auto& id : ids) {
        if (std::find(removed.begin(), removed.end(), id) != removed.end()) continue;  // ölen/gidenler
        const Npc* n = FindNpcAnywhere(id);
        if (!n) continue;
        Contact c;
        c.id = id;
        c.name = n->npc_json.value("profile", json::object()).value("name", id);
        out.push_back(std::move(c));
    }
    return out;
}

bool Missions::SecretMentioned(const std::string& npc_id, const std::string& text, std::string& label, std::string& value) const {
    const Mission* m = Current();
    if (!m || !m->secret.is_object() || npc_id != m->secret.value("npc", std::string())) return false;
    if (static_cast<int>(stage_) < m->secret.value("from_stage", 0) || static_cast<int>(stage_) > m->secret.value("to_stage", 0)) return false;
    const auto norm = [](const std::string& s) {
        std::string o;
        for (unsigned char c : s) {
            if (std::isalnum(c)) o += static_cast<char>(std::toupper(c));
        }
        return o;
    };
    const std::string v = m->secret.value("value", std::string());
    const std::string key = norm(v);
    if (key.size() < 4 || norm(text).find(key) == std::string::npos) return false;
    label = m->secret.value("label", std::string("Bilgi"));
    value = v;
    return true;
}

bool Missions::IsKnownCharacter(const std::string& id) const {
    for (const auto& c : KnownCharacters()) {
        if (c.id == id) return true;
    }
    return false;
}

bool Missions::RemoveContact(const std::string& id) {
    const auto it = std::find(contacts_.begin(), contacts_.end(), id);
    if (it == contacts_.end()) return false;
    contacts_.erase(it);
    Save();
    return true;
}

int Missions::RestoreContacts() {
    int n = 0;
    for (const auto& k : KnownCharacters()) {
        if (std::find(contacts_.begin(), contacts_.end(), k.id) == contacts_.end()) {
            contacts_.push_back(k.id);
            ++n;
        }
    }
    if (n) Save();
    return n;
}

std::vector<Missions::Contact> Missions::Contacts() const {
    std::vector<Contact> out;
    for (const auto& id : contacts_) {
        const Npc* n = FindNpcAnywhere(id);
        Contact c;
        c.id = id;
        c.name = n ? n->npc_json.value("profile", json::object()).value("name", id) : id;
        // Görev rehberden alınabilir: sıradaki görevin vericisi bu kişi ve henüz başlamadı (ilk aşama telefonla yapılabilir).
        const Mission* om = OpenMissionOf(id);
        const size_t st = !om ? 0 : (om->id == focus_ ? stage_ : (stages_.count(om->id) ? stages_.at(om->id) : 0));
        c.mission_ready = om && st == 0 && !om->stages[0].in_person;
        out.push_back(std::move(c));
    }
    return out;
}

void Missions::FollowThroughPortal(const Vector3& from, bool entering, const float dest[4]) {
    inside_interior_ = entering;  // içeri girince iç mekan NPC'leri doğar, çıkınca silinir
    Mission* m = Current();
    if (!m || stage_ >= m->stages.size()) return;
    const Stage& s = m->stages[stage_];
    if (s.type != "talk" && s.type != "install" && s.type != "escort") return;
    for (auto& n : m->npcs) {
        if (n.id != s.npc || !n.follows || n.ped == 0 || !ENTITY::DOES_ENTITY_EXIST(n.ped)) continue;
        if (D2(from, ENTITY::GET_ENTITY_COORDS(n.ped, true)) > 15.f) continue;
        // İçeride bilgisayarın yakınına (portal noktasının yanı), dışarıda girişin önüne.
        const float ox = entering ? 2.5f : 1.5f, oy = entering ? -2.0f : 1.0f;
        ENTITY::SET_ENTITY_COORDS(n.ped, dest[0] + ox, dest[1] + oy, dest[2], false, false, false, true);
        ENTITY::SET_ENTITY_HEADING(n.ped, dest[3] + 180.f);
        Log("npc portaldan gecti: " + n.id);
    }
}

bool Missions::PersonTalk(const std::string& id, Talk& out) const {
    if (std::find(contacts_.begin(), contacts_.end(), id) == contacts_.end()) return false;
    const Npc* n = FindNpcAnywhere(id);
    if (!n) return false;
    out = Talk{};
    out.npc_id = n->id;
    out.name = n->npc_json.value("profile", json::object()).value("name", n->id);
    out.ped = 0;
    out.npc_json = n->npc_json;
    out.model = n->model;
    out.fallback_models = n->fallback_models;
    return true;
}

bool Missions::CallTarget(const std::string& contact_id, Talk& out) const {
    if (!IsKnownCharacter(contact_id)) return false;  // rehberdeki ya da numarası bilinen (tanışılmış) kişi
    // Paralel görevler: aranan kişinin açık görevi öne geçer (diğerinin ilerlemesi saklı kalır).
    if (const Mission* om = OpenMissionOf(contact_id); om && om->id != focus_) SwitchFocus(om->id);
    const Npc* n = FindNpcAnywhere(contact_id);
    if (!n) return false;
    out = Talk{};
    out.npc_id = n->id;
    out.name = n->npc_json.value("profile", json::object()).value("name", n->id);
    out.ped = 0;  // telefonda: fiziksel NPC yok
    out.npc_json = n->npc_json;
    return true;
}

bool Missions::TakeFlag(const std::string& key) {
    if (Flag(key) != "1") return false;
    flags_[key] = "0";
    Save();
    return true;
}

std::string Missions::Relationship(const std::string& npc_id) const {
    if (Flag("kardes_" + npc_id) == "1") return "sibling";  // Bölüm 2: kadın karakterin kız kardeşi
    const Npc* n = FindNpcAnywhere(npc_id);
    if (!n) return "stranger";
    const auto it = rel_.find(n->npc_json.value("profile", json::object()).value("name", n->id));
    return it == rel_.end() ? "stranger" : it->second;
}

bool Missions::IncomingCall(Talk& out) {
    const Mission* m = Current();
    if (!m || stage_ != 0 || m->start_by != "incoming_call") return false;
    const unsigned long long now = GetTickCount64();
    if (armed_id_ != m->id) {  // bu görev için gecikmeyi başlat
        armed_id_ = m->id;
        ready_at_ = now + static_cast<unsigned long long>(m->call_delay_s) * 1000ULL;
        misses_ = 0;
        Log("gelen arama bekleniyor: " + m->id + " (" + std::to_string(m->call_delay_s) + " sn)");
    }
    if (now < ready_at_) return false;
    const Npc* n = FindNpc(*m, m->giver);
    if (!n) return false;
    out = Talk{};
    out.npc_id = n->id;
    out.name = n->npc_json.value("profile", json::object()).value("name", n->id);
    out.ped = 0;
    out.npc_json = n->npc_json;
    return true;
}

void Missions::IncomingHandled(bool answered) {
    const Mission* m = Current();
    if (!m) return;
    if (answered) {
        AddContact(m->giver);  // cevapladın: numarası rehberde; konuşma yarım kalsa da onu arayabilirsin
        ready_at_ = ~0ULL;     // tekrar çalmasın
        toast_until_ = GetTickCount64() + 6000;
        Save();
        return;
    }
    ++misses_;
    ready_at_ = GetTickCount64() + 60000ULL;  // 60 sn sonra tekrar dener
    if (misses_ >= 3) {  // 3 kaçırışta numara rehbere girer, sen ararsın
        AddContact(m->giver);
        ready_at_ = ~0ULL;
        toast_until_ = GetTickCount64() + 6000;
        Save();
    }
}

json Missions::QuestState(const std::string& npc_id, bool call) const {
    const Mission* m = Current();
    const bool can_invite = call && Relationship(npc_id) != "stranger";
    const bool in_car = !in_car_with_.empty() && in_car_with_ == npc_id;
    if (!m) {
        json q = {{"status", "none"}, {"channel", call ? "phone" : "face_to_face"}, {"relationship", Relationship(npc_id)},
                  {"note", "Şu an verecek başka görevin yok; sadece sohbet et."}};
        if (can_invite) q["can_invite_to_car"] = true;
        if (in_car) q["setting"] = "Oyuncuyla aynı arabadasınız; birlikte geziyor, rahat sohbet ediyorsunuz.";
        return {{"mission", q}};
    }
    const Stage& s = m->stages[std::min(stage_, m->stages.size() - 1)];
    json q = {{"title", m->title}, {"reward_usd", m->reward}, {"status", stage_ == 0 ? "available" : "in_progress"},
              {"channel", call ? "phone" : "face_to_face"}, {"relationship", Relationship(npc_id)}};
    // Önceki aşamalarda toplanan bilgiler: IT yoklaması sonucu, kurulumdaki hata sayısı.
    if (!s.place.empty()) q["meeting_place"] = s.place;
    if (const Npc* self = FindNpc(*m, npc_id)) {  // NPC kendi adını görev metinlerinde görünce üçüncü kişi sanmasın
        const std::string nm = self->npc_json.value("profile", json::object()).value("name", std::string());
        if (!nm.empty()) {
            q["you_are"] = nm;
            q["self_rule"] = "Hedef/görev metinlerinde geçen '" + nm + "' SENSİN. Kendinden üçüncü kişi gibi bahsetme: 'beni ara', 'bana gel', 'benimle konuş' de.";
        }
    }
    // Yer adı yoksa NPC'nin/hedefin bulunduğu sokak (gerçek koordinattan): "nerede buluşuruz / nereye gideyim" sorusu boş kalmasın.
    auto street_of = [](const Vector3& p) {
        Hash s1 = 0, s2 = 0;
        PATHFIND::GET_STREET_NAME_AT_COORD(p.x, p.y, p.z, &s1, &s2);
        return s1 != 0 ? std::string(UI::GET_STREET_NAME_FROM_HASH_KEY(s1)) : std::string();
    };
    if (s.place.empty()) {
        std::string st;
        if (s.type == "talk") {
            if (const Npc* tn = FindNpc(*m, s.npc); tn && (tn->pos.x != 0.f || tn->pos.y != 0.f)) st = street_of(tn->pos);
        } else if (s.has_pos) {
            st = street_of(s.pos);
        }
        if (!st.empty()) q["meeting_street"] = st;
    }
    // İşin dökümü: oyuncu "iş ne / ne yapacağım / sırada ne var" diye sorarsa NPC buna dayanarak anlatır (uydurma yok).
    {
        json outline = json::array();
        for (size_t i = 0; i < m->stages.size() && i < 8; ++i) {
            outline.push_back({{"step", i + 1},
                               {"what", m->stages[i].objective},
                               {"state", i < stage_ ? "done" : i == stage_ ? "current" : "next"}});
        }
        q["job_outline"] = outline;
        q["job_rule"] = "Oyuncu işin detayını, ne yapacağını veya nereye gideceğini sorarsa job_outline, situation ve ücretten anlat; "
                        "listede olmayan ayrıntı uydurma. Konuşma durmasın: cevabın sonunda oyuncuya bir sonraki adımı hatırlat.";
    }
    if (can_invite) q["can_invite_to_car"] = true;
    if (in_car) q["setting"] = "Oyuncuyla aynı arabadasınız; birlikte geziyor, rahat sohbet ediyorsunuz.";
    if (!Flag("yoklama").empty()) q["it_level"] = Flag("yoklama");
    if (!Flag("kurulum_hata").empty()) q["install_mistakes"] = std::atoi(Flag("kurulum_hata").c_str());

    if (s.type == "talk" && s.npc == npc_id && !(call && s.in_person)) {
        q["your_turn"] = true;
        q["situation"] = s.brief;
        if (s.auto_advance) {
            q["note"] = "Görevi sen bitirmezsin; kod ilerletir. 'advance' kullanma, görevi anlat ve oyuncuyu yönlendir.";
            if (s.min_turns > 1) {
                q["note"] = q["note"].get<std::string>() + " Görev oyuncuyla en az " + std::to_string(s.min_turns) +
                            " karşılıklı sohbet gerektirir (şu an " + std::to_string(talk_turns_) +
                            "): sohbeti erken bitirme, kısa cevap verip ghosting yapma; soru sor, konuyu sürdür.";
            }
        }
        else q["advance_when"] = s.advance_when;
        if (!s.item_options.empty()) q["advance_item_options"] = s.item_options;  // 'advance' ile birlikte item'a bunlardan biri yazılır
    } else if (s.type == "talk" && s.npc == npc_id) {
        q["your_turn"] = false;
        q["note"] = "Bu işi telefonla halledemezsiniz; yüz yüze buluşmanız gerek. Oyuncuya yanına gelmesini söyle.";
    } else {
        q["your_turn"] = false;
        q["note"] = "Oyuncunun şu anki hedefi: " + s.objective + ". Oyuncu detay isterse job_outline ve help_for_player'dan anlat; ayrıca sohbet et, destek ol.";
        if (!s.guide.empty()) q["current_task_hint"] = s.guide;
        if (!s.help.empty()) q["help_for_player"] = s.help;  // oyuncu yardım için aradıysa kullan
    }
    // Sır: NPC'nin oyuncuya söyleyebileceği bilgi (örn. ürün anahtarı); yalnızca ilgili aşamalarda ve ilgili NPC'ye verilir.
    if (m->secret.is_object() && npc_id == m->secret.value("npc", std::string()) &&
        static_cast<int>(stage_) >= m->secret.value("from_stage", 0) && static_cast<int>(stage_) <= m->secret.value("to_stage", 0)) {
        q["secret_for_player"] = {{"label", m->secret.value("label", "")}, {"value", m->secret.value("value", "")}};
        q["secret_rule"] = "Oyuncu açıkça isterse söyle, sorulmadan söyleme; harf harf, yavaş ve net oku.";
    }
    return {{"mission", q}};
}

json Missions::TryAutoAdvance(const std::string& npc_id, bool call) {
    Mission* m = Current();
    if (!m || stage_ >= m->stages.size()) return {{"advanced", false}};
    const Stage& s = m->stages[stage_];
    if (s.type != "talk" || !s.auto_advance || s.npc != npc_id || (call && s.in_person)) return {{"advanced", false}};
    if (s.min_turns > 1) {  // en az N diyalog: oyuncu yeterince sohbet edene kadar görev ilerlemez
        ++talk_turns_;
        if (talk_turns_ < s.min_turns) return {{"advanced", false}, {"turns", talk_turns_}, {"need", s.min_turns}};
    }
    return Advance(*m);
}

bool Missions::ConsumeFreeRelease() {
    if (Flag("sule_ucretsiz_hak") != "1") return false;
    flags_["sule_ucretsiz_hak"] = "0";
    Save();
    return true;
}

json Missions::TryAdvance(const std::string& npc_id, bool call, const std::string& item) {
    Mission* m = Current();
    if (!m || stage_ >= m->stages.size()) return {{"advanced", false}};
    const Stage& s = m->stages[stage_];
    if (s.type != "talk" || s.npc != npc_id || (call && s.in_person)) return {{"advanced", false}};
    if (!s.item_options.empty()) {  // LLM'in seçtiği sonucu (örn. knows/partial/clueless) doğrula ve sakla
        const bool valid = std::find(s.item_options.begin(), s.item_options.end(), item) != s.item_options.end();
        flags_[s.id] = valid ? item : s.item_options[s.item_options.size() / 2];  // geçersizse ortadaki seçenek
    }
    return Advance(*m);
}

}  // namespace hikaye
