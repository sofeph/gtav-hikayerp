#include "jail.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "economy.h"

using nlohmann::json;

namespace hikaye {

namespace {
constexpr unsigned FIRING_PATTERN_SINGLE_SHOT = 0x5D60E4E0;
constexpr int kCopPedType = 6;

float Dist3(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Vector3 V3(const json& p) { return {p[0].get<float>(), 0, p[1].get<float>(), 0, p[2].get<float>(), 0}; }

std::string RecordPath() { return std::string(kBaseDir) + "/data/record.json"; }
}  // namespace

void JailSystem::Load() {
    try {
        std::ifstream in(std::string(kBaseDir) + "/data/jail.json");
        if (in) cfg_ = json::parse(in);
        else Log("jail.json yok: polis/hapis sistemi varsayilanlarla calisir");
    } catch (const std::exception& e) {
        Log(std::string("jail.json okunamadi: ") + e.what());
    }
    try {
        std::ifstream in(RecordPath());
        if (in) arrests_ = json::parse(in).value("arrests", 0);
    } catch (...) {
        arrests_ = 0;
    }
}

void JailSystem::SaveRecord() const {
    std::ofstream out(RecordPath());
    out << json{{"arrests", arrests_}}.dump();
}

double JailSystem::PoliceCallChance() const { return cfg_.value("confront", json::object()).value("police_call_chance", 0.4); }
double JailSystem::ArmedAttackChance() const { return cfg_.value("confront", json::object()).value("armed_attack_chance", 0.5); }

int JailSystem::BaseDays() const { return cfg_.value("sentence", json::object()).value("base_days", 8); }

void JailSystem::Notice(const std::string& text, unsigned ms) {
    notice_ = text;
    notice_until_ = GetTickCount64() + ms;
}

void JailSystem::WatchForMurder(Ped ped) {
    if (std::find(watched_.begin(), watched_.end(), ped) != watched_.end()) return;
    if (watched_.size() >= 32) watched_.erase(watched_.begin());
    watched_.push_back(ped);
}

void JailSystem::UpdateMurderWatch() {
    const Player pid = PLAYER::PLAYER_ID();
    if (own_wanted_ && PLAYER::GET_PLAYER_WANTED_LEVEL(pid) == 0) own_wanted_ = false;  // kaçtın ya da bitti
    const Ped player = PLAYER::PLAYER_PED_ID();
    const int stars = cfg_.value("wanted", json::object()).value("murder_stars", 5);
    for (auto it = watched_.begin(); it != watched_.end();) {
        if (!ENTITY::DOES_ENTITY_EXIST(*it)) {
            it = watched_.erase(it);
        } else if (ENTITY::IS_ENTITY_DEAD(*it)) {
            if (!in_custody_ && ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(*it, player, true)) {  // gözaltında yıldız yok
                PLAYER::SET_PLAYER_WANTED_LEVEL(pid, stars, false);
                PLAYER::SET_PLAYER_WANTED_LEVEL_NOW(pid, false);
                own_wanted_ = true;
                Notice("Cinayet! Polis peşinde: " + std::to_string(stars) + " yıldız", 7000);
                Log("cinayet: " + std::to_string(stars) + " yildiz");
            }
            it = watched_.erase(it);
        } else {
            ++it;
        }
    }
}

void JailSystem::OnPoliceCalled(const CaseFacts& facts) {
    if (active()) return;
    facts_ = facts;
    hired_ = false;
    lawyer_requested_ = false;
    const int stars = facts.stars > 0 ? std::min(5, facts.stars) : cfg_.value("wanted", json::object()).value("police_call_stars", 3);
    const Player pid = PLAYER::PLAYER_ID();
    PLAYER::SET_PLAYER_WANTED_LEVEL(pid, stars, false);
    PLAYER::SET_PLAYER_WANTED_LEVEL_NOW(pid, false);
    own_wanted_ = true;
    state_ = State::Pursuit;
    Notice("Biri polisi aradı! " + std::to_string(stars) + " yıldız", 7000);
    Log("polis cagrildi: " + std::to_string(stars) + " yildiz");
}

Ped JailSystem::FindTaseCop(const Vector3& player_pos) {
    const Ped player = PLAYER::PLAYER_PED_ID();
    const float range = cfg_.value("tase", json::object()).value("range_m", 14.0f);
    Ped best = 0;
    float best_d = range;
    int peds[128];
    const int n = worldGetAllPeds(peds, 128);
    for (int i = 0; i < n; ++i) {
        const Ped p = peds[i];
        if (!ENTITY::DOES_ENTITY_EXIST(p) || ENTITY::IS_ENTITY_DEAD(p)) continue;
        if (PED::GET_PED_TYPE(p) != kCopPedType || PED::IS_PED_IN_ANY_VEHICLE(p, false)) continue;
        const float d = Dist3(player_pos, ENTITY::GET_ENTITY_COORDS(p, true));
        if (d < best_d && ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY(p, player, 17)) {
            best_d = d;
            best = p;
        }
    }
    return best;
}

const json* JailSystem::NearestStation(const Vector3& pos) const {
    // NOT: cfg_.value(...) bir KOPYA döndürür; ondan alınan işaretçi fonksiyon bitince boşa düşer (çökme sebebi).
    // Bu yüzden cfg_ içindeki gerçek diziye referansla bakılır.
    if (!cfg_.contains("stations") || !cfg_["stations"].is_array()) return nullptr;
    const json* best = nullptr;
    float best_d = 1e12f;
    for (const auto& s : cfg_["stations"]) {
        const float d = Dist3(pos, V3(s.at("pos")));
        if (d < best_d) {
            best_d = d;
            best = &s;
        }
    }
    return best;
}

void JailSystem::Teleport(const json& p4) {
    const Ped player = PLAYER::PLAYER_PED_ID();
    // Kaynak koordinatlar zemin seviyesidir; ped'in yere gömülmemesi için biraz yükseğe bırakılır.
    constexpr float kTeleportZLift = 0.8f;
    ENTITY::SET_ENTITY_COORDS(player, p4[0].get<float>(), p4[1].get<float>(), p4[2].get<float>() + kTeleportZLift,
                              false, false, false, true);
    ENTITY::SET_ENTITY_HEADING(player, p4[3].get<float>());
    AI::CLEAR_PED_TASKS_IMMEDIATELY(player);
}

JailSystem::Assessment JailSystem::Assess() const {
    // Kural tabanlı: LLM sadece bu gerekçeleri anlatır, hesaplamayı KOD yapar.
    Assessment a;
    double days = BaseDays();
    if (arrests_ <= 1) {  // bu tutuklamayla birlikte ilk kez (kayıt tutuklamada artırılmıştır)
        days *= 0.5;
        a.reasons.push_back("ilk_suc");
    } else {
        a.reasons.push_back("sabikali");
    }
    if (!facts_.player_armed) {
        days *= 0.5;
        a.reasons.push_back("silah_yok");
    }
    if (!facts_.npc_injured) {
        days *= 0.5;
        a.reasons.push_back("yaralanma_yok");
    }
    // Eger ilk suc, silahsiz ve yaralanmasiz ise: gun icinde derhal tahliye (0 gun)!
    if (arrests_ <= 1 && !facts_.player_armed && !facts_.npc_injured) {
        a.expected_days = 0;
    } else {
        a.expected_days = std::max(1, static_cast<int>(std::round(days)));
    }
    const json lawyer = cfg_.value("lawyer", json::object());
    a.fee = facts_.player_armed ? lawyer.value("fee_if_armed", 3000) : lawyer.value("fee", 1500);
    return a;
}

int JailSystem::LawyerFee() const { return free_release_ && free_release_(false) ? 0 : Assess().fee; }

json JailSystem::CaseState() const {
    const Assessment a = Assess();
    json reasons = json::array();
    const json texts = cfg_.value("reasons", json::object());
    for (const auto& key : a.reasons) reasons.push_back(texts.value(key, key));
    return {
        {"offense", cfg_.value("sentence", json::object()).value("offense", "Kamu düzenini bozma")},
        {"sentence_days", BaseDays()},
        {"expected_days_with_lawyer", a.expected_days},
        {"reasons", reasons},
        {"lawyer_fee", LawyerFee()},
        {"free_by_sule", free_release_ && free_release_(false)},
    };
}

std::string JailSystem::HireLawyer() {
    if (state_ != State::InCell) return "not_in_cell";
    if (hired_) return "already_hired";
    const bool free_right = free_release_ && free_release_(true);  // Şule'nin bir kerelik hakkı
    if (!free_right && !economy::Spend(Assess().fee)) return "insufficient_funds";
    hired_ = true;
    Log("avukat tutuldu");
    return "lawyer_hired";
}

void JailSystem::ReleaseWithLawyer() {
    if (state_ != State::InCell || !hired_) return;
    const Assessment a = Assess();
    if (a.expected_days == 0) {
        // Avukat gun icinde hemen cikartiyor: derhal tahliye!
        StartServing(0, true);
    } else {
        // Avukat cezada indirim yapiyor (ornegin 2 gun surer dedi):
        total_sentence_days_ = a.expected_days;
        sentence_end_time_ = GetTickCount64() + (a.expected_days * 60ULL * 1000ULL);
        Notice("Avukatin sayesinde cezan " + std::to_string(a.expected_days) + " gune (" + std::to_string(a.expected_days) + " dakikaya) indirildi!", 9000);
    }
}

void JailSystem::Reset() {
    LeaveCustody();
    PLAYER::SET_PLAYER_CONTROL(PLAYER::PLAYER_ID(), true, 0);
    state_ = State::Idle;
    hired_ = false;
    lawyer_requested_ = false;
    tase_cop_ = 0;
    watched_.clear();
}

// Güvenlik gruplarının oyuncuya karşı tutumu (1 saygı, 3 tarafsız): hapishane gardiyanları ayrı grupta olabilir
static void SetGuardRelations(int rel) {
    const Hash pg = PED::GET_PED_RELATIONSHIP_GROUP_HASH(PLAYER::PLAYER_PED_ID());
    for (const char* g : {"COP", "ARMY", "SECURITY_GUARD", "PRISONER"}) {
        const Hash h = HashKey(g);
        PED::SET_RELATIONSHIP_BETWEEN_GROUPS(rel, h, pg);
        PED::SET_RELATIONSHIP_BETWEEN_GROUPS(rel, pg, h);
    }
}

void JailSystem::EnterCustody() {
    if (in_custody_) return;
    const Player pid = PLAYER::PLAYER_ID();
    PLAYER::CLEAR_PLAYER_WANTED_LEVEL(pid);
    PLAYER::SET_MAX_WANTED_LEVEL(0);  // hapisteyken oyunun kendi yıldızı da çıkmasın
    PLAYER::SET_POLICE_IGNORE_PLAYER(pid, true);
    PLAYER::SET_EVERYONE_IGNORE_PLAYER(pid, true);  // gardiyan/polis/asker: mahkuma ateş etmez
    SetGuardRelations(1);                           // saygı: düşmanlık yok
    // Gözaltında ölümsüz ve NPC'lere görünmez: hedef alınamaz, hasar almaz
    PLAYER::SET_PLAYER_INVINCIBLE(pid, true);
    ENTITY::SET_ENTITY_INVINCIBLE(PLAYER::PLAYER_PED_ID(), true);
    PED::SET_PED_CAN_BE_TARGETTED(PLAYER::PLAYER_PED_ID(), false);
    PLAYER::SET_PLAYER_CAN_BE_HASSLED_BY_GANGS(pid, false);
    in_custody_ = true;
}

void JailSystem::LeaveCustody() {
    if (!in_custody_) return;
    const Player pid = PLAYER::PLAYER_ID();
    PLAYER::SET_MAX_WANTED_LEVEL(5);  // oyunun varsayılanı
    PLAYER::SET_POLICE_IGNORE_PLAYER(pid, false);
    PLAYER::SET_EVERYONE_IGNORE_PLAYER(pid, false);
    SetGuardRelations(3);  // varsayılan: tarafsız
    PLAYER::SET_PLAYER_INVINCIBLE(pid, false);
    ENTITY::SET_ENTITY_INVINCIBLE(PLAYER::PLAYER_PED_ID(), false);
    PED::SET_PED_CAN_BE_TARGETTED(PLAYER::PLAYER_PED_ID(), true);
    PLAYER::SET_PLAYER_CAN_BE_HASSLED_BY_GANGS(pid, true);
    PLAYER::CLEAR_PLAYER_WANTED_LEVEL(pid);
    in_custody_ = false;
}

void JailSystem::StartServing(int days, bool via_lawyer) {
    serve_days_ = days;
    serve_via_lawyer_ = via_lawyer;
    CAM::DO_SCREEN_FADE_OUT(1500);
    state_ = State::ServeFadeOut;
    t_ = GetTickCount64();
}

bool JailSystem::ConsumeLawyerRequest() {
    const bool r = lawyer_requested_;
    lawyer_requested_ = false;
    return r;
}

// Hapse girince: gardiyanlar/polisler ateş etmez (silahları alınır, tepki vermezler) ve hapishane alarmı susar.
void JailSystem::PacifyPrison() {
    static unsigned long long last = 0;
    const unsigned long long now = GetTickCount64();
    if (now - last < 300ULL) return;
    last = now;
    AUDIO::STOP_ALARM(const_cast<char*>("PRISON_ALARMS"), true);
    AUDIO::STOP_ALL_ALARMS(true);
    const Vector3 pp = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true);
    int peds[128];
    const int n = worldGetAllPeds(peds, 128);
    for (int i = 0; i < n; ++i) {
        const Ped p = peds[i];
        if (!ENTITY::DOES_ENTITY_EXIST(p) || ENTITY::IS_ENTITY_DEAD(p)) continue;
        const int t = PED::GET_PED_TYPE(p);
        const bool guard_model = ENTITY::GET_ENTITY_MODEL(p) == HashKey("s_m_m_prisguard_01");
        if (t != kCopPedType && t != 27 && t != 29 && !guard_model) continue;  // polis, SWAT, ordu, hapishane gardiyanı
        if (Dist3(pp, ENTITY::GET_ENTITY_COORDS(p, true)) > 200.f) continue;
        WEAPON::REMOVE_ALL_PED_WEAPONS(p, true);
        PED::SET_PED_CAN_BE_TARGETTED_BY_PLAYER(p, PLAYER::PLAYER_ID(), false);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(p, true);
        PED::SET_PED_COMBAT_ATTRIBUTES(p, 46, false);
        PED::SET_PED_FLEE_ATTRIBUTES(p, 0, false);
        if (PED::IS_PED_IN_COMBAT(p, PLAYER::PLAYER_PED_ID()) || (now / 1000ULL) % 3 == 0) AI::CLEAR_PED_TASKS(p);  // savaşa girenleri hemen sakinleştir
    }
}

void JailSystem::Update() {
    UpdateMurderWatch();
    if (in_custody_ && state_ != State::Idle && state_ != State::Pursuit) PacifyPrison();  // tazerdan tahliyeye kadar
    if (state_ == State::Idle) {
        if (in_custody_) LeaveCustody();  // güvenlik: akış beklenmedik biçimde bittiyse ayarları geri al
        return;
    }
    // Gözaltındayken (tazerlendikten tahliyeye kadar) herhangi bir yıldız belirirse hemen sil.
    if (in_custody_ && PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) > 0) {
        PLAYER::CLEAR_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID());
    }

    const unsigned long long now = GetTickCount64();
    const Player pid = PLAYER::PLAYER_ID();
    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vector3 pos = ENTITY::GET_ENTITY_COORDS(player, true);
    const json tase = cfg_.value("tase", json::object());

    switch (state_) {
        case State::Pursuit: {
            const int wl = PLAYER::GET_PLAYER_WANTED_LEVEL(pid);
            if (wl == 0) {
                state_ = State::Idle;
                Notice("Polisten kurtuldun");
                break;
            }
            // Ağır suçta (ör. cinayet, 5 yıldız) tazer yok: normal kovalamaca. Araçtaysan da yok.
            if (wl > cfg_.value("wanted", json::object()).value("tase_max_stars", 3)) break;
            if (PED::IS_PED_IN_ANY_VEHICLE(player, false) || ENTITY::IS_ENTITY_DEAD(player)) break;
            if (scan_tick_++ % 6 != 0) break;
            const Ped cop = FindTaseCop(pos);
            if (cop != 0) {
                tase_cop_ = cop;
                WEAPON::GIVE_WEAPON_TO_PED(cop, HashKey("WEAPON_STUNGUN"), 5, false, true);
                AI::TASK_SHOOT_AT_ENTITY(cop, player, tase.value("aim_ms", 1500) + 500, FIRING_PATTERN_SINGLE_SHOT);
                state_ = State::Tase;
                t_ = now;
            }
            break;
        }
        case State::Tase:
            if (!ENTITY::DOES_ENTITY_EXIST(tase_cop_) || ENTITY::IS_ENTITY_DEAD(tase_cop_) ||
                PLAYER::GET_PLAYER_WANTED_LEVEL(pid) == 0) {
                state_ = State::Pursuit;  // polis düştü veya kurtuldun
                break;
            }
            if (now - t_ >= static_cast<unsigned long long>(tase.value("aim_ms", 1500))) {
                PED::SET_PED_TO_RAGDOLL(player, 4000, 4000, 0, false, false, false);
                CAM::SHAKE_GAMEPLAY_CAM(const_cast<char*>("HAND_SHAKE"), 1.0f);
                PLAYER::SET_PLAYER_CONTROL(pid, false, 0);
                Notice("Tazerlendin!");
                EnterCustody();  // yakalandın: artık yıldız yok
                state_ = State::Down;
                t_ = now;
            }
            break;
        case State::Down:
            if (now - t_ > 2500) {
                CAM::DO_SCREEN_FADE_OUT(1500);
                state_ = State::FadeOut;
                t_ = now;
            }
            break;
        case State::FadeOut:
            if (CAM::IS_SCREEN_FADED_OUT() || now - t_ > 3000) {
                const json* st = NearestStation(pos);
                if (st) {
                    serve_station_ = st->value("id", "");
                    exit_ = st->at("exit");
                    if (st->contains("cells") && st->at("cells").is_array() && !st->at("cells").empty()) {
                        const auto& cells = st->at("cells");  // hücrelerden rastgele biri
                        hold_ = cells[static_cast<size_t>(GetTickCount64()) % cells.size()];
                    } else if (st->contains("cell") && !st->at("cell").is_null()) {
                        hold_ = st->at("cell");
                    } else {
                        hold_ = st->at("exit");
                    }
                    Teleport(hold_);
                }
                PLAYER::CLEAR_PLAYER_WANTED_LEVEL(pid);
                ++arrests_;
                SaveRecord();
                PLAYER::SET_PLAYER_CONTROL(pid, true, 0);
                CAM::DO_SCREEN_FADE_IN(1500);
                total_sentence_days_ = BaseDays();
                sentence_end_time_ = now + (total_sentence_days_ * 60ULL * 1000ULL);  // her gun 1 gercek dakika
                Notice(std::string("Tutuklandin: ") + (st ? st->value("name", "Karakol") : "Karakol") + ". " +
                           std::to_string(total_sentence_days_) + " gun (" + std::to_string(total_sentence_days_) + " dakika) hapis cezasi.",
                       9000);
                Log("tutuklandi: " + serve_station_ + " (" + std::to_string(total_sentence_days_) + " gun)");
                state_ = State::InCell;
                t_ = now;
            }
            break;
        case State::InCell: {
            // Cezasi bitmeden karakolun/hucrenin cok uzagina gitmesin
            if (!hold_.empty() && Dist3(pos, V3(hold_)) > 25.f) Teleport(hold_);

            // Gercek dakika cinsinden sure kontrolu (sure dolmadan gecilemez)
            const int remaining_sec = sentence_end_time_ > now ? static_cast<int>((sentence_end_time_ - now) / 1000ULL) : 0;
            if (remaining_sec <= 0) {
                // Ceza suresi bitti: tahliye baslat
                StartServing(total_sentence_days_, hired_);
                break;
            }

            const bool l = (GetAsyncKeyState('L') & 0x8000) != 0;
            if (!input_blocked_ && !hired_) {
                if (l && !l_was_down_) lawyer_requested_ = true;
            }
            l_was_down_ = l;
            break;
        }
        case State::ServeFadeOut:
            if (CAM::IS_SCREEN_FADED_OUT() || now - t_ > 3000) {
                for (int d = 0; d < serve_days_; ++d) {  // oyun saati ilerler
                    TIME::ADD_TO_CLOCK_TIME(12, 0, 0);
                    TIME::ADD_TO_CLOCK_TIME(12, 0, 0);
                }
                if (!exit_.empty()) Teleport(exit_);
                PLAYER::CLEAR_PLAYER_WANTED_LEVEL(pid);
                CAM::DO_SCREEN_FADE_IN(1500);
                state_ = State::ServeFadeIn;
                t_ = now;
            }
            break;
        case State::ServeFadeIn:
            if (CAM::IS_SCREEN_FADED_IN() || now - t_ > 3000) {
                if (serve_via_lawyer_) {
                    const Assessment a = Assess();
                    const json texts = cfg_.value("reasons", json::object());
                    std::string why;
                    for (const auto& key : a.reasons) why += texts.value(key, key) + " ";
                    Notice("Avukatın sayesinde " + std::to_string(BaseDays()) + " günlük ceza " + std::to_string(serve_days_) +
                               " güne indi. " + why,
                           12000);
                } else {
                    Notice(std::to_string(serve_days_) + " gün hapis yattın. Serbestsin.", 8000);
                }
                LeaveCustody();  // tahliye: yıldız sistemi ve polis normale döner
                state_ = State::Idle;
                hired_ = false;
                lawyer_requested_ = false;
            }
            break;
        default:
            break;
    }
}

void JailSystem::Draw() {
    const unsigned long long now = GetTickCount64();
    if (notice_until_ > now) DrawLine(notice_, 0.66f, 255, 200, 120);
    if (state_ == State::InCell) {
        const int remaining_sec = sentence_end_time_ > now ? static_cast<int>((sentence_end_time_ - now) / 1000ULL) : 0;
        const int dk = remaining_sec / 60;
        const int sn = remaining_sec % 60;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%02d:%02d", dk, sn);
        std::string line = "HAPIS: " + std::string(buf) + " kaldi (" + std::to_string(total_sentence_days_) + " gunluk ceza)";
        if (!hired_) {
            line += "    L: Avukatla gorus ($" + std::to_string(Assess().fee) + ")";
        } else {
            line += "    (Avukat indirimi devrede)";
        }
        DrawLine(line, 0.72f, 255, 255, 255);
    }
}

}  // namespace hikaye
