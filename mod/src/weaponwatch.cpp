#include "weaponwatch.h"

#include <windows.h>

#include <cmath>
#include <functional>
#include <random>
#include <set>
#include <vector>

#include "crimes.h"
#include "jail.h"
#include "inventory.h"
#include "karma.h"

namespace hikaye::weaponwatch {

namespace {
enum class State { Idle, Reported, Approaching, Dialogue, Searching };

JailSystem* g_jail = nullptr;
std::vector<Hash> g_firearms;
std::set<Hash> g_legal;
bool g_legal_set = false;
State g_state = State::Idle;
unsigned long long g_scan_at = 0, g_seen_since = 0, g_deadline = 0, g_state_at = 0, g_cooldown_until = 0, g_legal_roll_at = 0;
bool g_report_is_illegal = false;
Vector3 g_search_at{};
Ped g_cops[2] = {0, 0};
std::string g_reason = "weapon_report";
std::function<bool(Ped, const std::string&)> g_starter;
unsigned long long g_next_random = 0;
int g_insults = 0;

float Rand01() {
    static std::mt19937 r(static_cast<unsigned>(GetTickCount64()));
    return std::uniform_real_distribution<float>(0.f, 1.f)(r);
}

float D2(const Vector3& a, const Vector3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

void InitFirearms() {
    if (!g_firearms.empty()) return;
    static const char* const names[] = {
        "WEAPON_PISTOL", "WEAPON_COMBATPISTOL", "WEAPON_APPISTOL", "WEAPON_PISTOL50", "WEAPON_SNSPISTOL", "WEAPON_HEAVYPISTOL",
        "WEAPON_VINTAGEPISTOL", "WEAPON_REVOLVER", "WEAPON_MICROSMG", "WEAPON_SMG", "WEAPON_ASSAULTSMG", "WEAPON_MG", "WEAPON_COMBATMG",
        "WEAPON_GUSENBERG", "WEAPON_ASSAULTRIFLE", "WEAPON_CARBINERIFLE", "WEAPON_ADVANCEDRIFLE", "WEAPON_SPECIALCARBINE", "WEAPON_BULLPUPRIFLE",
        "WEAPON_PUMPSHOTGUN", "WEAPON_SAWNOFFSHOTGUN", "WEAPON_ASSAULTSHOTGUN", "WEAPON_BULLPUPSHOTGUN", "WEAPON_HEAVYSHOTGUN",
        "WEAPON_SNIPERRIFLE", "WEAPON_HEAVYSNIPER", "WEAPON_MARKSMANRIFLE", "WEAPON_GRENADELAUNCHER", "WEAPON_RPG", "WEAPON_MINIGUN"};
    for (const char* n : names) g_firearms.push_back(HashKey(n));
}

bool IsFirearm(Hash w) {
    for (Hash h : g_firearms) {
        if (h == w) return true;
    }
    return false;
}

std::vector<Hash> HeldIllegal() {
    std::vector<Hash> out;
    const Ped player = PLAYER::PLAYER_PED_ID();
    for (Hash h : g_firearms) {
        if (WEAPON::HAS_PED_GOT_WEAPON(player, h, false) && !g_legal.count(h)) out.push_back(h);
    }
    return out;
}

// Çevrede polis olmayan, oyuncuyu gören biri var mı?
bool Witnessed() {
    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vector3 pos = ENTITY::GET_ENTITY_COORDS(player, true);
    int arr[2 + 2 * 16];
    arr[0] = 16;
    const int n = PED::GET_PED_NEARBY_PEDS(player, arr, -1);
    for (int i = 0; i < n && i < 16; ++i) {
        const Ped p = arr[2 + i * 2];
        if (p == 0 || p == player || !ENTITY::DOES_ENTITY_EXIST(p) || !PED::IS_PED_HUMAN(p) || ENTITY::IS_ENTITY_DEAD(p)) continue;
        const int t = PED::GET_PED_TYPE(p);
        if (t == 6 || t == 27 || t == 29) continue;  // polis/SWAT/ordu ihbar etmez, zaten kendisi müdahale eder
        if (D2(ENTITY::GET_ENTITY_COORDS(p, true), pos) > 20.f) continue;
        if (ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY(p, player, 17)) return true;
    }
    return false;
}

void ReleaseCops(bool walk_away) {
    for (Ped& c : g_cops) {
        if (c != 0 && ENTITY::DOES_ENTITY_EXIST(c)) {
            if (walk_away) AI::TASK_WANDER_STANDARD(c, 10.0f, 10);
            PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(c, false);
            Ped p = c;
            ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&p);
        }
        c = 0;
    }
}

void Finish(unsigned long long now, unsigned long long cooldown_ms) {
    ReleaseCops(true);
    g_state = State::Idle;
    g_seen_since = 0;
    g_cooldown_until = now + cooldown_ms;
}

void Warrant(const char* why, unsigned long long now) {
    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vector3 pos = ENTITY::GET_ENTITY_COORDS(player, true);
    karma::Toast(std::string("Yakalama kararı: ") + why, 6);
    crimes::Record("warrant", pos, 1);
    ReleaseCops(false);  // gerçek polis akışı devralır
    g_state = State::Idle;
    g_seen_since = 0;
    g_cooldown_until = now + 180000ULL;
    if (g_jail) {
        CaseFacts f;
        f.player_armed = true;
        g_jail->OnPoliceCalled(f);  // yıldız verir; polis yaklaşınca tazer -> tutuklama -> hücre akışı
    }
}

bool SpawnCops(const Vector3& pos) {
    const Hash model = HashKey("s_m_y_cop_01");
    STREAMING::REQUEST_MODEL(model);
    for (int i = 0; i < 100 && !STREAMING::HAS_MODEL_LOADED(model); ++i) WAIT(0);
    if (!STREAMING::HAS_MODEL_LOADED(model)) return false;
    bool any = false;
    for (int k = 0; k < 2; ++k) {
        Vector3 out{};
        const float a = static_cast<float>((GetTickCount64() / 53 + k * 211) % 628) / 100.f;
        if (!PATHFIND::GET_SAFE_COORD_FOR_PED(pos.x + 34.f * std::cos(a), pos.y + 34.f * std::sin(a), pos.z, true, &out, 16)) continue;
        const Ped cop = PED::CREATE_PED(6, model, out.x, out.y, out.z, 0.f, false, false);
        if (!ENTITY::DOES_ENTITY_EXIST(cop)) continue;
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(cop, true, true);
        WEAPON::GIVE_WEAPON_TO_PED(cop, HashKey("WEAPON_STUNGUN"), 5, false, true);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(cop, true);
        AI::TASK_GO_TO_ENTITY(cop, PLAYER::PLAYER_PED_ID(), 40000, 3.0f, 2.0f, 1073741824.f, 0);
        g_cops[k] = cop;
        any = true;
    }
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
    return any;
}
}  // namespace

namespace {
std::function<bool(const Vector3&)> g_safe_zone;
}  // namespace
void SetSafeZone(std::function<bool(const Vector3&)> safe) { g_safe_zone = std::move(safe); }

void SetJail(JailSystem* jail) { g_jail = jail; }

void SetDialogueStarter(std::function<bool(Ped, const std::string&)> starter) { g_starter = std::move(starter); }

bool InDialogue() { return g_state == State::Dialogue; }

void OnPoliceIntent(const std::string& intent) {
    if (g_state != State::Dialogue) return;
    const unsigned long long now = GetTickCount64();
    if (intent == "comply") {  // iş birliği: arama başlar
        g_state = State::Searching;
        g_state_at = now;
        g_search_at = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true);
        karma::Toast("Polis üstünü arıyor, yerinden ayrılma", 6);
    } else if (intent == "flee") {
        Warrant("kaçmaya kalktın", now);
    } else if (intent == "insult") {
        karma::Add(-3, "Polise hakaret ettin", "hakaret");
        if (++g_insults >= 2) Warrant("polise hakaret ve direnç", now);
    }
    // plead / chat: etkisi yok (yalvarmak yasadışı silahı yok saydırmaz)
}

void SnapshotLegal() {
    InitFirearms();
    g_legal.clear();
    const Ped player = PLAYER::PLAYER_PED_ID();
    for (Hash h : g_firearms) {
        if (WEAPON::HAS_PED_GOT_WEAPON(player, h, false)) g_legal.insert(h);
    }
    g_legal_set = true;
}

void Reset() {
    ReleaseCops(false);
    g_next_random = 0;
    g_insults = 0;
    g_state = State::Idle;
    g_seen_since = 0;
    g_legal_set = false;
    g_legal.clear();
}

void Update() {
    InitFirearms();
    const unsigned long long now = GetTickCount64();
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (!g_legal_set) {
        // RP silahları verilmeden çağrılırsa, o an elde olanlar yasal sayılır (yanlış alarm olmasın)
        SnapshotLegal();
    }
    if (g_jail && g_jail->active()) {
        if (g_state != State::Idle) Finish(now, 120000ULL);
        return;
    }
    if (now - g_scan_at < 300) return;
    g_scan_at = now;
    const Vector3 pos = ENTITY::GET_ENTITY_COORDS(player, true);

    if (g_state == State::Idle) {
        if (now < g_cooldown_until || ENTITY::IS_ENTITY_DEAD(player) || PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) > 0) return;
        if (INTERIOR::GET_INTERIOR_FROM_ENTITY(player) != 0 || (g_safe_zone && g_safe_zone(pos))) {  // dükkân/iç mekan: ihbar ve çevirme yok
            g_seen_since = 0;
            if (g_next_random != 0 && g_next_random < now + 60000ULL) g_next_random = now + 60000ULL;  // çevirme dükkânda tetiklenmesin
            return;
        }
        // Rastgele çevirme: rutin kontrol (yaya veya durmuş araçta; silah ihbarından bağımsız)
        if (g_next_random == 0) g_next_random = now + (420000ULL + static_cast<unsigned long long>(Rand01() * 360000.f));
        if (now >= g_next_random) {
            const Vehicle pv = PED::GET_VEHICLE_PED_IS_IN(player, false);
            const bool ok = pv == 0 || ENTITY::GET_ENTITY_SPEED(pv) < 2.f;
            g_next_random = now + (600000ULL + static_cast<unsigned long long>(Rand01() * 480000.f));
            if (ok && SpawnCops(pos)) {
                g_reason = "random_check";
                g_state = State::Approaching;
                g_state_at = now;
                karma::Toast("Polis devriyesi seni durdurmak için yaklaşıyor", 6);
                return;
            }
        }
        Hash cur = 0;
        WEAPON::GET_CURRENT_PED_WEAPON(player, &cur, true);
        const bool drawn = IsFirearm(cur) && WEAPON::IS_PED_ARMED(player, 4) && !PED::IS_PED_IN_ANY_VEHICLE(player, false);
        if (!drawn || !Witnessed()) {
            g_seen_since = 0;
            return;
        }
        if (g_seen_since == 0) g_seen_since = now;
        if (now - g_seen_since < 2500ULL) return;
        const bool illegal = !g_legal.count(cur);
        // Yasal silahı da bazen şüpheli bulurlar; gasp edilmiş silah her zaman ihbar edilir
        if (!illegal) {
            if (now - g_legal_roll_at < 90000ULL) return;
            g_legal_roll_at = now;
            if (Rand01() > 0.35f) {
                g_seen_since = 0;
                return;
            }
        }
        g_report_is_illegal = illegal;
        g_reason = "weapon_report";
        g_state = State::Reported;
        g_deadline = now + 9000ULL + (now / 7) % 8000ULL;
        karma::Toast("Biri seni silahla görüp polise bildirdi!", 6);
        crimes::Record("weapon_report", pos, 1);
        return;
    }

    if (g_state == State::Reported) {
        if (now < g_deadline) return;
        if (!SpawnCops(pos)) {  // devriye çıkarılamadı: ihbar yatışır
            Finish(now, 60000ULL);
            return;
        }
        g_state = State::Approaching;
        g_state_at = now;
        karma::Toast("Polis devriyesi sana yaklaşıyor: kıpırdama, kaçma", 6);
        return;
    }

    if (g_state == State::Approaching) {
        if (PED::IS_PED_SHOOTING(player) || now - g_state_at > 70000ULL) {
            Warrant("polise direnç / zamanında durmadın", now);
            return;
        }
        for (Ped c : g_cops) {
            if (c == 0 || !ENTITY::DOES_ENTITY_EXIST(c) || ENTITY::IS_ENTITY_DEAD(c)) continue;
            if (D2(ENTITY::GET_ENTITY_COORDS(c, true), pos) < 5.5f) {
                g_search_at = pos;
                g_insults = 0;
                for (Ped s : g_cops) {
                    if (s != 0 && ENTITY::DOES_ENTITY_EXIST(s)) {
                        AI::CLEAR_PED_TASKS(s);
                        AI::TASK_TURN_PED_TO_FACE_ENTITY(s, player, 60000);
                    }
                }
                if (g_starter && g_starter(c, g_reason)) {  // polis seninle konuşur (LLM)
                    g_state = State::Dialogue;
                    g_state_at = now;
                    return;
                }
                g_state = State::Searching;
                g_state_at = now;
                g_search_at = pos;
                for (Ped s : g_cops) {
                    if (s != 0 && ENTITY::DOES_ENTITY_EXIST(s)) {
                        AI::CLEAR_PED_TASKS(s);
                        AI::TASK_TURN_PED_TO_FACE_ENTITY(s, player, 6000);
                    }
                }
                karma::Toast("Polis: Elini göster! Üstün aranıyor, yerinden ayrılma", 6);
                return;
            }
        }
        return;
    }

    if (g_state == State::Dialogue) {
        // Konuşma sürerken yerinden ayrılırsan / araca binip kaçarsan / ateş edersen / 90 sn susup kaçarsan -> yakalama kararı
        const bool fled = D2(pos, g_search_at) > 12.f || PED::IS_PED_IN_ANY_VEHICLE(player, false) || PED::IS_PED_SHOOTING(player);
        if (fled || now - g_state_at > 90000ULL) Warrant("polisle konuşmadan kaçtın", now);
        return;
    }

    if (g_state == State::Searching) {
        const bool fled = D2(pos, g_search_at) > 9.f || PED::IS_PED_IN_ANY_VEHICLE(player, false) || PED::IS_PED_SHOOTING(player);
        if (fled) {
            Warrant("aramadan kaçtın", now);
            return;
        }
        if (now - g_state_at < 6000ULL) return;
        const std::vector<Hash> illegal = HeldIllegal();
        const bool contraband = inventory::HasIllegal();
        if (illegal.empty() && !contraband) {
            karma::Toast("Üstün temiz çıktı. Rahatsızlık için özür dileriz, serbestsin.", 6);
            Finish(now, 150000ULL);
            return;
        }
        for (Hash h : illegal) WEAPON::REMOVE_WEAPON_FROM_PED(player, h);  // müsadere (elde taşınan silahlar)
        const std::vector<std::string> seized = inventory::ConfiscateIllegal();  // çakı, molotof, kaçak içki, esrar...
        if (!seized.empty()) {
            std::string list;
            for (const auto& s : seized) list += (list.empty() ? "" : ", ") + s;
            karma::Toast("Müsadere edildi: " + list, 6);
        }
        crimes::Record("weapon_seized", pos, 1);
        Warrant("yasadışı silah bulundu ve müsadere edildi", now);
        return;
    }
}

}  // namespace hikaye::weaponwatch
