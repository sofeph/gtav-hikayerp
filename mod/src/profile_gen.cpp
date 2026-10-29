#include "profile_gen.h"

#include <cstdint>
#include <random>

using nlohmann::json;

namespace hikaye::profile_gen {

namespace {
uint32_t Fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

// std::uniform_int_distribution platforma göre farklı sonuç verebilir; bu yüzden doğrudan mod alıyoruz.
const json& Pick(const json& arr, std::mt19937& rng) { return arr[rng() % arr.size()]; }
}  // namespace

json Resolve(const json& npc, const std::string& seed_key, const json& pool) {
    json out = npc;
    if (pool.empty() || !pool.contains("male") || !pool.contains("female")) return out;

    std::mt19937 rng(Fnv1a(seed_key));
    // Çağrı sırası sonucu belirler: yeni rastgele alan eklerken sona ekle (mevcut karakterler değişmesin).
    std::string gender = out.value("gender", "random");
    const bool coin = (rng() % 2) == 0;
    if (gender != "male" && gender != "female") gender = coin ? "male" : "female";
    out["gender"] = gender;
    const json& g = pool.at(gender);

    const bool gen_profile = !out.contains("profile") || out["profile"].is_string();
    json p = json::object();
    if (gen_profile) {
        p["name"] = Pick(g.at("names"), rng);
        const json& city = Pick(pool.at("hometowns"), rng);
        p["hometown"] = city.at("city");
        p["hometown_district"] = Pick(city.at("districts"), rng);
        if (gender == "male") p["military_unit"] = Pick(pool.at("military_units"), rng);  // kadınlarda askerlik yok
        json jobs = json::array();
        jobs.push_back(Pick(pool.at("past_jobs"), rng));
        const json& second = Pick(pool.at("past_jobs"), rng);
        if (second != jobs[0] && rng() % 2 == 0) jobs.push_back(second);
        p["past_jobs"] = jobs;
        p["team"] = Pick(pool.at("teams"), rng);
    } else {
        p = out["profile"];
    }
    p["gender"] = gender;
    out["profile"] = p;

    if (!out.contains("model")) out["model"] = Pick(g.at("models"), rng);
    if (!out.contains("voice")) out["voice"] = Pick(g.at("voices"), rng);
    if (!out.contains("personality")) {
        const std::string role = out.value("role", "çalışan");
        out["personality"] = Pick(pool.at("archetypes"), rng).get<std::string>() + " bir " + role + ".";
    }
    // Flörtöz kadınlar (havuzdaki flirty_rate, varsayılan %5). Zar en sonda atılır ki mevcut karakterler değişmesin.
    if (gender == "female" && !out.contains("flirty") && pool.contains("flirty_rate")) {
        const uint32_t threshold = static_cast<uint32_t>(pool.at("flirty_rate").get<double>() * 10000.0);
        const bool flirty = (rng() % 10000) < threshold;
        out["flirty"] = flirty;
        if (flirty && !npc.contains("personality")) {
            out["personality"] = out["personality"].get<std::string>() +
                                 " Flörtöz ve cilveli bir tavrın var, ama müstehcen konuşmazsın.";
        }
    }
    return out;
}

namespace {
const char* const kMonths[12] = {"Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran",
                                 "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık"};
const int kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

std::string Zodiac(int day, int month) {
    // Her burcun başladığı gün (ay, gün); sırayla Oğlak'tan başlar.
    struct Cut { int month, day; const char* name; };
    static const Cut cuts[] = {{1, 20, "Kova"},   {2, 19, "Balık"},   {3, 21, "Koç"},    {4, 20, "Boğa"},
                               {5, 21, "İkizler"}, {6, 21, "Yengeç"}, {7, 23, "Aslan"},   {8, 23, "Başak"},
                               {9, 23, "Terazi"}, {10, 23, "Akrep"},  {11, 22, "Yay"},    {12, 22, "Oğlak"}};
    std::string sign = "Oğlak";  // 1 Ocak - 19 Ocak
    for (const auto& c : cuts) {
        if (month > c.month || (month == c.month && day >= c.day)) sign = c.name;
    }
    return sign;
}
}  // namespace

json GenerateDetails(const std::string& seed_key, const json& pool) {
    std::mt19937 rng(Fnv1a("detay:" + seed_key));
    json d;
    d["age"] = 24 + static_cast<int>(rng() % 21);  // 24-44
    const int month = 1 + static_cast<int>(rng() % 12);
    const int day = 1 + static_cast<int>(rng() % static_cast<uint32_t>(kMonthDays[month - 1]));
    d["birthday"] = std::to_string(day) + " " + kMonths[month - 1];
    d["zodiac"] = Zodiac(day, month);
    auto pick = [&](const char* key, const char* out) {
        if (pool.contains(key) && pool[key].is_array() && !pool[key].empty()) d[out] = Pick(pool[key], rng);
    };
    pick("hobbies", "hobby");
    pick("foods", "favorite_food");
    pick("drinks", "favorite_drink");
    return d;
}

}  // namespace hikaye::profile_gen
