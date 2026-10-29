#include "marketplace.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <random>

#include "common.h"
#include "economy.h"
#include "addons.h"
#include "json.hpp"

namespace hikaye {

using nlohmann::json;

namespace {
std::mt19937& Rng() {
    static std::mt19937 r(static_cast<unsigned>(GetTickCount64()));
    return r;
}

std::string Money(int v) {
    std::string d = std::to_string(v), g;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) g += '.';
        g += d[i];
    }
    return "$" + g;
}
}  // namespace

int Marketplace::Rand(int lo, int hi) { return std::uniform_int_distribution<int>(lo, std::max(lo, hi))(Rng()); }

void Marketplace::Load() {
    models_.clear();
    conds_.clear();
    sellers_.clear();
    try {
        std::ifstream in(std::string(kBaseDir) + "/data/marketplace.json");
        if (!in) {
            Log("marketplace.json yok");
            return;
        }
        const json j = json::parse(in);
        listing_count_ = j.value("listing_count", 8);
        refresh_s_ = j.value("refresh_s", 240);
        if (j.contains("offer_every_s")) {
            offer_lo_ = j["offer_every_s"][0].get<int>();
            offer_hi_ = j["offer_every_s"][1].get<int>();
        }
        max_offers_ = j.value("max_offers_per_ad", 3);
        if (j.contains("offer_range")) {
            offer_min_ = j["offer_range"][0].get<double>();
            offer_max_ = j["offer_range"][1].get<double>();
        }
        for (const auto& m : j.value("models", json::array())) models_.push_back({m.value("model", ""), m.value("name", ""), m.value("base", 10000)});
        for (const auto& dir : addons::Dirs()) {  // eklenti araçları (vehicles.json)
            std::ifstream av(dir + "/vehicles.json");
            if (!av) continue;
            for (const auto& m : json::parse(av).value("vehicles", json::array())) models_.push_back({m.value("model", ""), m.value("name", ""), m.value("base", 10000)});
        }
        for (const auto& c : j.value("conditions", json::array())) {
            Cond k;
            k.label = c.value("label", "");
            k.mult = c.value("mult", 1.0);
            k.km_lo = c["km"][0].get<int>();
            k.km_hi = c["km"][1].get<int>();
            conds_.push_back(std::move(k));
        }
        sellers_ = j.value("sellers", std::vector<std::string>{"Satıcı"});
    } catch (const std::exception& e) {
        Log(std::string("marketplace.Load HATA: ") + e.what());
    }
    loaded_ = !models_.empty() && !conds_.empty();
}

void Marketplace::Reset() {
    listings_.clear();
    ads_.clear();
    offers_.clear();
    next_refresh_ = 0;
}

Marketplace::Listing Marketplace::MakeListing() {
    Listing l;
    l.id = next_id_++;
    const Model& m = models_[static_cast<size_t>(Rand(0, static_cast<int>(models_.size()) - 1))];
    const Cond& c = conds_[static_cast<size_t>(Rand(0, static_cast<int>(conds_.size()) - 1))];
    l.model = m.id;
    l.name = m.name;
    l.cond = c.label;
    l.km = Rand(c.km_lo, c.km_hi) / 100 * 100;
    l.seller = sellers_[static_cast<size_t>(Rand(0, static_cast<int>(sellers_.size()) - 1))];
    const double jitter = 0.92 + 0.16 * (Rand(0, 100) / 100.0);  // ±8% pazarlık payı
    l.price = static_cast<int>(m.base * c.mult * jitter / 50.0) * 50;
    return l;
}

void Marketplace::Update() {
    if (!loaded_ || !garage_) return;
    const unsigned long long now = GetTickCount64();
    if (listings_.empty() || now >= next_refresh_) {
        if (listings_.empty()) {
            for (int i = 0; i < listing_count_; ++i) listings_.push_back(MakeListing());
        } else {  // her turda birkaç ilan düşer, yenisi gelir (dinamik pazar)
            for (int k = 0; k < 2 && !listings_.empty(); ++k) listings_.erase(listings_.begin() + Rand(0, static_cast<int>(listings_.size()) - 1));
            while (static_cast<int>(listings_.size()) < listing_count_) listings_.push_back(MakeListing());
        }
        next_refresh_ = now + static_cast<unsigned long long>(refresh_s_) * 1000ULL;
    }
    // Satışa çıkardığın araçlar: belli aralıkla alıcı teklifleri gelir
    const auto cars = garage_->List();
    for (size_t i = 0; i < ads_.size();) {
        Ad& ad = ads_[i];
        const bool owned = std::any_of(cars.begin(), cars.end(), [&](const auto& c) { return c.plate == ad.plate; });
        if (!owned) {  // araç artık yok (çalındı/satıldı): ilan ve teklifleri düşer
            offers_.erase(std::remove_if(offers_.begin(), offers_.end(), [&](const Offer& o) { return o.plate == ad.plate; }), offers_.end());
            ads_.erase(ads_.begin() + static_cast<long>(i));
            continue;
        }
        if (ad.offers_made < max_offers_ && now >= ad.next_offer) {
            Offer o;
            o.id = next_id_++;
            o.plate = ad.plate;
            o.model = ad.model;
            o.name = ad.name;
            o.buyer = sellers_[static_cast<size_t>(Rand(0, static_cast<int>(sellers_.size()) - 1))];
            const double r = offer_min_ + (offer_max_ - offer_min_) * (Rand(0, 100) / 100.0);
            o.price = static_cast<int>(ad.base * r / 50.0) * 50;
            offers_.push_back(std::move(o));
            ++ad.offers_made;
            ad.next_offer = now + static_cast<unsigned long long>(Rand(offer_lo_, offer_hi_)) * 1000ULL;
        }
        ++i;
    }
}

std::vector<hml::Item> Marketplace::Source(const std::string& name) {
    std::vector<hml::Item> out;
    auto card = [&](std::string title, std::string sub, std::string price, std::string href) {
        hml::Item it;
        it.kind = hml::Kind::Card;
        it.text = std::move(title);
        it.sub = std::move(sub);
        it.meta = std::move(price);
        it.href = std::move(href);
        it.selectable = !it.href.empty();
        out.push_back(std::move(it));
    };
    auto note = [&](const std::string& t) {
        hml::Item it;
        it.text = t;
        out.push_back(std::move(it));
    };
    if (!loaded_ || !garage_) {
        note("Pazar şu an kapalı.");
        return out;
    }
    if (name == "gs_buy") {
        if (listings_.empty()) Update();
        for (const auto& l : listings_) {
            card(l.name + " · " + l.cond, "Satıcı " + l.seller + " · " + std::to_string(l.km) + " km · alınca garajına girer",
                 Money(l.price), "act:buy:" + std::to_string(l.id));
        }
    } else if (name == "gs_sell") {
        const auto cars = garage_->List();
        if (cars.empty()) note("Satacak aracın yok. Önce bir araç satın al.");
        for (const auto& c : cars) {
            const auto m = std::find_if(models_.begin(), models_.end(), [&](const Model& x) { return x.id == c.model; });
            const bool listed = std::any_of(ads_.begin(), ads_.end(), [&](const Ad& a) { return a.plate == c.plate; });
            const int base = m == models_.end() ? 8000 : m->base;
            card((m == models_.end() ? c.model : m->name) + " (" + c.plate + ")",
                 std::string(listed ? "İlanda: alıcı teklifleri geliyor. Seç: ilanı kaldır" : "Seç: ilan ver, alıcı teklifleri zamanla gelir") +
                     " · yakıt %" + std::to_string(c.fuel),
                 "~" + Money(base), (listed ? "act:unlist:" : "act:sell:") + c.plate);
        }
    } else if (name == "gs_offers") {
        if (offers_.empty()) note(ads_.empty() ? "İlanda araç yok. 'Araç sat' sayfasından bir araç ilana koy." : "Henüz teklif yok. Alıcılar zamanla yazar, sayfa kendiliğinden güncellenir.");
        for (const auto& o : offers_) {
            card(o.buyer + " teklif etti: " + o.name + " (" + o.plate + ")", "Seç: teklifi kabul et ve aracı sat", Money(o.price), "act:accept:" + std::to_string(o.id));
        }
    }
    return out;
}

std::string Marketplace::Action(const std::string& cmd) {
    if (!loaded_ || !garage_) return "Pazar kapalı";
    const size_t c = cmd.find(':');
    const std::string verb = c == std::string::npos ? cmd : cmd.substr(0, c);
    const std::string arg = c == std::string::npos ? "" : cmd.substr(c + 1);
    if (verb == "buy") {
        const auto it = std::find_if(listings_.begin(), listings_.end(), [&](const Listing& l) { return std::to_string(l.id) == arg; });
        if (it == listings_.end()) return "Bu ilan artık yok";
        if (!economy::Spend(it->price)) return "Yetersiz bakiye: " + Money(it->price) + " gerekiyor";
        garage_->AddStored(it->model);
        const std::string what = it->name + " garajına eklendi (" + Money(it->price) + ")";
        listings_.erase(it);
        return what;
    }
    if (verb == "sell" || verb == "unlist") {
        const auto cars = garage_->List();
        const auto car = std::find_if(cars.begin(), cars.end(), [&](const auto& x) { return x.plate == arg; });
        if (car == cars.end()) return "Araç bulunamadı";
        if (verb == "unlist") {
            offers_.erase(std::remove_if(offers_.begin(), offers_.end(), [&](const Offer& o) { return o.plate == arg; }), offers_.end());
            ads_.erase(std::remove_if(ads_.begin(), ads_.end(), [&](const Ad& a) { return a.plate == arg; }), ads_.end());
            return "İlan kaldırıldı";
        }
        const auto m = std::find_if(models_.begin(), models_.end(), [&](const Model& x) { return x.id == car->model; });
        Ad ad;
        ad.plate = arg;
        ad.model = car->model;
        ad.name = m == models_.end() ? car->model : m->name;
        ad.base = m == models_.end() ? 8000 : m->base;
        ad.next_offer = GetTickCount64() + static_cast<unsigned long long>(Rand(offer_lo_, offer_hi_)) * 1000ULL;
        ads_.push_back(std::move(ad));
        return "İlan verildi: alıcı teklifleri 'Teklifler' sayfasına gelecek";
    }
    if (verb == "accept") {
        const auto it = std::find_if(offers_.begin(), offers_.end(), [&](const Offer& o) { return std::to_string(o.id) == arg; });
        if (it == offers_.end()) return "Teklif artık geçerli değil";
        const Offer o = *it;
        if (!garage_->SellByPlate(o.plate)) return "Araç bulunamadı";
        economy::Earn(o.price);
        offers_.erase(std::remove_if(offers_.begin(), offers_.end(), [&](const Offer& x) { return x.plate == o.plate; }), offers_.end());
        ads_.erase(std::remove_if(ads_.begin(), ads_.end(), [&](const Ad& a) { return a.plate == o.plate; }), ads_.end());
        return o.name + " satıldı: +" + Money(o.price);
    }
    return "Bilinmeyen işlem";
}

}  // namespace hikaye
