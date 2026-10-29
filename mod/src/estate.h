#pragma once
#include <string>
#include <vector>

#include "common.h"
#include "hml.h"
#include "input.h"
#include "json.hpp"

namespace hikaye {

// Emlak: GitHub'dan çekilen ev listesi (data/places/houses.json, tools/fetch_houses.py). GeneralSale > Emlak sayfasında ilanlar
// görünür; evin kapısına gidip E ile satın alınır veya kiralanır. Sahip olunan ev haritada "Evim" ikonuyla görünür; kapıda E:
// uyu (sabaha kadar, can dolar), evi sat (%80) / kirayı bırak. Evin garajında araç koy/al (Garage ek garaj noktası).
// OTORİTE KODDA: fiyat, kira, nakit hareketi ve mülk durumu burada; sayfa (HML) sadece gösterir.
class Estate {
public:
    void Load();                  // ev listesi
    void LoadSave(int slot);      // karaktere özel mülkler (saves/estate_<slot>.json)
    void Shutdown();              // kaydet, ikonları kaldır
    void Update(const Vector3& player_pos, bool e_pressed);  // her frame (mod aktifken)
    bool OwnsBlip(Blip b) const;
    bool MenuOpen() const { return menu_open_ || inside_menu_; }
    std::vector<hml::Item> Source(const std::string& name);  // estate_list | estate_mine
    std::string Action(const std::string& cmd);               // look:<id>
    std::vector<Vector3> GarageSpots() const;                 // sahip olunan evlerin garaj noktaları
    std::vector<Vector3> OwnedPositions() const;
    std::vector<std::pair<std::string, Vector3>> AllHouses() const;  // yönetici ışınlanması: tüm evler (ad, kapı)              // sahip olunan/kiralanan evlerin kapı noktaları (telefon şarjı)

private:
    struct House {
        std::string id, label;
        Vector3 pos{}, garage{};
        bool has_garage = false;
        int tier = 3, price = 0, rent = 0;
        float heading = 0.f;
    };
    struct Owned {
        std::string id;
        bool rented = false;
        int days_left = 0;  // kira: kalan oyun günü
        Blip blip = 0;
    };
    const House* Find(const std::string& id) const;
    Owned* FindOwned(const std::string& id);
    void Save() const;
    void ClearBlips();
    void Sleep(int heal_pct = 100, int armor = 0);
    // Ev içi: fiyat + konum + yükseklik puanına göre iç mekan (data/home_interiors.json), içeride uyuma ve çıkış
    void LoadInteriors();
    int InteriorIndexFor(const House& h) const;
    bool EnterHouse(const House& h);
    void ExitHouse();
    void UpdateInside(const Vector3& pos, bool e_pressed);
    int slot_ = -1;
    std::vector<House> houses_;
    std::vector<Owned> owned_;
    // E menüsü
    bool menu_open_ = false;
    std::string menu_house_;
    int menu_sel_ = 0;
    Keys keys_;
    int last_day_ = -1;
    nlohmann::json home_cfg_ = nlohmann::json::object();
    std::vector<std::string> ipls_requested_;
    bool inside_ = false, inside_menu_ = false;
    std::string inside_house_;
    int inside_interior_ = -1;
    Vector3 inside_spawn_{};
public:
    bool Inside() const { return inside_; }
};

}  // namespace hikaye
