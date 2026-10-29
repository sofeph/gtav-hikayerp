#pragma once
#include <string>
#include <vector>

#include "garage.h"
#include "hml.h"

namespace hikaye {

// GeneralSale: dinamik ikinci el araç pazarı (telefon tarayıcısındaki ilanlar). İlanlar zamanla yenilenir, fiyat/durum/satıcı
// rastgeledir. Alınan araç doğrudan garaja girer; kendi aracını satmak için ilan verirsin, zamanla alıcı teklifleri gelir.
// OTORİTE KODDA: fiyatlar, nakit düşümü/ekleme ve garajdan çıkarma burada; sayfa (HML) sadece gösterir.
class Marketplace {
public:
    void Load();
    void SetGarage(Garage* g) { garage_ = g; }
    void Update();  // her frame: ilan yenileme, alıcı teklifleri
    // HML <source name="gs_buy|gs_sell|gs_offers"/> verisi
    std::vector<hml::Item> Source(const std::string& name);
    // "buy:<id>", "sell:<plaka>", "accept:<id>", "reject:<id>", "unlist:<plaka>" -> oyuncuya gösterilen sonuç metni
    std::string Action(const std::string& cmd);
    void Reset();  // karakter/mod değişince

private:
    struct Model { std::string id, name; int base = 0; };
    struct Cond { std::string label; double mult = 1.0; int km_lo = 0, km_hi = 0; };
    struct Listing { int id = 0; std::string model, name, seller, cond; int price = 0, km = 0; };
    struct Ad { std::string plate, model, name; int base = 0; unsigned long long next_offer = 0; int offers_made = 0; };
    struct Offer { int id = 0; std::string plate, model, name, buyer; int price = 0; };

    Listing MakeListing();
    int Rand(int lo, int hi);

    Garage* garage_ = nullptr;
    std::vector<Model> models_;
    std::vector<Cond> conds_;
    std::vector<std::string> sellers_;
    int listing_count_ = 8, refresh_s_ = 240, offer_lo_ = 40, offer_hi_ = 110, max_offers_ = 3;
    double offer_min_ = 0.68, offer_max_ = 1.0;
    std::vector<Listing> listings_;
    std::vector<Ad> ads_;
    std::vector<Offer> offers_;
    unsigned long long next_refresh_ = 0;
    int next_id_ = 1;
    bool loaded_ = false;
};

}  // namespace hikaye
