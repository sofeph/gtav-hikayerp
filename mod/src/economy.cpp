#include "economy.h"
#include "inventory.h"

#include <string>

#include "fuel.h"
#include "weaponwatch.h"

namespace hikaye::economy {

namespace {
// Hikaye RP modunun kendi cüzdanı (profile.cash). Hikaye karakterlerinin parasına (SPx_TOTAL_CASH) HİÇ dokunulmaz.
int* g_wallet = nullptr;
void (*g_bought_hook)(Vehicle, const std::string&) = nullptr;
}  // namespace

std::vector<Tx> g_history;
void Record(int amount) {
    g_history.push_back({amount, TIME::GET_CLOCK_HOURS(), TIME::GET_CLOCK_MINUTES()});
    if (g_history.size() > 12) g_history.erase(g_history.begin());
}

void SetWallet(int* cash) { g_wallet = cash; }
std::vector<Tx> History() { return g_history; }
void SetVehicleBoughtHook(void (*hook)(Vehicle, const std::string&)) { g_bought_hook = hook; }

int GetCash() { return g_wallet ? *g_wallet : -1; }

bool Spend(int amount) {
    if (!g_wallet || amount < 0 || *g_wallet < amount) return false;
    *g_wallet -= amount;
    if (amount > 0) Record(-amount);
    return true;
}

bool Earn(int amount) {
    if (!g_wallet || amount < 0) return false;
    *g_wallet += amount;
    if (amount > 0) Record(amount);
    return true;
}

bool ApplyEffect(const nlohmann::json& effect, const Vector3& spawn_at, float spawn_heading) {
    const std::string type = effect.value("type", "none");
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (type == "none") return true;
    if (type == "give_item") {  // dükkan eşyası doğrudan envantere girer (kapasite yetmezse teslimat başarısız -> para iade)
        return inventory::Add(effect.value("item", ""), effect.value("qty", 1));
    }
    if (type == "heal") {
        ENTITY::SET_ENTITY_HEALTH(player, ENTITY::GET_ENTITY_MAX_HEALTH(player));
        return true;
    }
    if (type == "armor") {
        PED::SET_PED_ARMOUR(player, 100);
        return true;
    }
    if (type == "refuel") {  // bidon: son kullanılan araç yakındaysa deposuna yakıt ekler
        const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(player, true);
        if (v == 0 || !ENTITY::DOES_ENTITY_EXIST(v)) return false;
        const Vector3 a = ENTITY::GET_ENTITY_COORDS(player, true), b = ENTITY::GET_ENTITY_COORDS(v, true);
        if ((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) > 40.f * 40.f) return false;
        const float cur = fuel::Get(v) < 0.f ? 30.f : fuel::Get(v);
        fuel::Set(v, cur + effect.value("pct", 50.f));
        VEHICLE::SET_VEHICLE_ENGINE_ON(v, true, true, false);
        return true;
    }
    if (type == "spawn_vehicle") {
        const Hash model = HashKey(effect.value("model", "").c_str());
        if (!STREAMING::IS_MODEL_IN_CDIMAGE(model) || !STREAMING::IS_MODEL_A_VEHICLE(model)) {
            Log("gecersiz arac modeli: " + effect.value("model", ""));
            return false;
        }
        STREAMING::REQUEST_MODEL(model);
        // Script thread'inde birkaç frame beklemek güvenli (WAIT oyunu dondurmaz).
        for (int i = 0; i < 120 && !STREAMING::HAS_MODEL_LOADED(model); ++i) WAIT(0);
        if (!STREAMING::HAS_MODEL_LOADED(model)) return false;
        Vehicle veh = VEHICLE::CREATE_VEHICLE(model, spawn_at.x, spawn_at.y, spawn_at.z, spawn_heading, false, false);
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
        if (!ENTITY::DOES_ENTITY_EXIST(veh)) return false;
        VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(veh);
        if (g_bought_hook) {
            g_bought_hook(veh, effect.value("model", ""));  // garaj: sahiplik, plaka, dolu depo (araç silinmez)
        } else {
            ENTITY::SET_VEHICLE_AS_NO_LONGER_NEEDED(&veh);  // oyun zamanla temizleyebilir
        }
        return true;
    }
    if (type == "give_weapon") {
        const Hash wh = HashKey(effect.value("weapon", "").c_str());
        const int ammo = effect.value("ammo", 30);
        if (wh == 0) { Log("give_weapon: silah hash 0"); return false; }
        WEAPON::GIVE_WEAPON_TO_PED(player, wh, ammo, false, true);
        weaponwatch::SnapshotLegal();  // satın alınan silah yasal sayılsın
        return true;
    }
    if (type == "give_ammo") {
        const Hash wh = HashKey(effect.value("weapon", "").c_str());
        const int ammo = effect.value("ammo", 30);
        if (wh == 0) return false;
        WEAPON::ADD_AMMO_TO_PED(player, wh, ammo);
        weaponwatch::SnapshotLegal();
        return true;
    }
    Log("desteklenmeyen etki: " + type);
    return false;
}

}  // namespace hikaye::economy
