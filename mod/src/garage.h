#pragma once
#include <string>
#include <vector>

#include "common.h"
#include "input.h"
#include "json.hpp"

namespace hikaye {

// Satın alınan araçlar (karaktere özel: saves/garage_<slot>.json). Sahip olunan araç telefondan çağrılabilir (vale):
// araç yakındaki yola doğar/ışınlanır, sürücüsü sana sürer, sen binince biter. Yakıt seviyesi araçla birlikte saklanır.
// OTORİTE KODDA: LLM sadece "aracımı getir" niyetini önerir; gelip gelmeyeceğine, süreye ve ücrete kod karar verir.
class Garage {
public:
    void Load(int slot);
    void Save() const;
    // Dükkandan alınan araç: plaka verilir, yakıt dolu, sahiplik kaydedilir.
    void Register(Vehicle veh, const std::string& model);
    // Telefondan çağrı. Dönüş LLM'e gerçek bilgi olarak gider: {service, accepted, reason?, eta_seconds?, fee?, vehicle?}
    // 'which': araç adı (boşsa son kullanılan). Garajdaki araç da çağrılabilir (otopark görevlisi/vale).
    nlohmann::json Summon(const std::string& which = "");
    // Otopark görevlisi: yakındaki kayıtlı aracı vale taksisiyle fiziksel olarak garaja götürür ($50).
    // 'Aracım nerede': haritaya işaret koyar; {found, in_garage, distance_m, direction, street}
    nlohmann::json Locate(const std::string& which = "");
    nlohmann::json ParkVehicle(const std::string& which = "");
    void Update();    // her frame (mod aktifken): ikonlar, hırsızlık, vale
    // Garaj noktasında: araçtaysan 'garaja koy', yaya isen 'garajdan al'. true: E bu sistem tarafından kullanıldı.
    bool UpdateGarageSpots(const Vector3& player_pos, bool e_pressed);
    bool OwnsBlip(Blip b) const;
    bool MenuOpen() const { return menu_open_; }
    void Shutdown();  // moddan çıkarken: aktif vale yolculuğunu temizle, kaydet
    // İkinci el pazarı (GeneralSale) ve emlak için:
    struct CarInfo {
        std::string model, plate;
        int fuel = 100;
        float km = 0.f;  // kilometre sayacı
        bool in_world = false;
    };
    std::vector<CarInfo> List() const;
    std::string AddStored(const std::string& model, float km = 0.f);  // satın alınan araç doğrudan garaja girer; plakayı döner
    bool SellByPlate(const std::string& plate);       // satılan araç garajdan ve dünyadan silinir
    void SetExtraGarages(const std::vector<Vector3>& points) { extra_garages_ = points; }  // sahip olunan evlerin garajları

    int Count() const { return static_cast<int>(cars_.size()); }
    bool IsOwned(Vehicle v) const;                // bu araç senin mi?
    Ped ValetDriver() const { return valet_.active ? valet_.driver : 0; }
    Vehicle ValetVeh() const { return valet_.active ? valet_.veh : 0; }
    std::vector<Vehicle> Handles() const;         // dünyadaki sahip olunan araçlar

private:
    struct Owned {
        std::string model, plate;
        float fuel = 100.f;
        float km = 0.f;          // kilometre sayacı (odometer)
        nlohmann::json look = nlohmann::json::object();  // renk + modifiye (CaptureLook)
        unsigned long long look_at = 0;
        Vehicle handle = 0;      // 0: garajda (dünyada yok)
        Blip blip = 0;           // sahip olunan araç ikonu (araçta değilken görünür)
        unsigned long long unlocked_since = 0, next_roll = 0;  // hırsızlık zarı
    };
    struct Valet {
        bool active = false;
        Ped driver = 0;
        Vehicle veh = 0;
        Vector3 target{};
        unsigned long long since = 0, arrived_at = 0;
        bool arrived = false;
    };
    struct Leave {  // vale/tamirci araçtan sonra taksiyle gider
        bool active = false;
        Ped mech = 0;
        Vehicle taxi = 0;
        Ped taxi_driver = 0;
        unsigned long long since = 0;
        // Aşamalar: yürü (walked=false) -> telefon (phoned=false) -> taksi biniş (boarding) -> gidiş (driving)
        bool walked = false, phoned = false;
        unsigned long long phone_at = 0;
        bool boarding = false, driving = false, teleported = false;
        unsigned long long last_push = 0, boarded_at = 0;
        Vector3 walk_dest{};  // 100m uzaklaşma hedefi
    };
    struct ParkRide {
        enum class Stage { None, TaxiEnRoute, Pickup, Delivering, Finishing, Aborting } stage = Stage::None;
        Vehicle owned_vehicle = 0, taxi = 0;
        Ped taxi_driver = 0, valet = 0;
        std::string plate, model, abort_reason;
        Vector3 pickup{}, destination{};
        int original_lock = 0;
        unsigned long long started_at = 0, stage_at = 0, last_task_at = 0, last_occupant_exit_at = 0;
        bool player_exit_requested = false, valet_exit_requested = false;
        bool valet_enter_requested = false;
        bool fee_paid = false, vehicle_stored = false;
    };
    struct Theft {
        bool active = false;
        Vehicle veh = 0;
        Ped thief = 0;
        bool driving = false;
        unsigned long long since = 0;
    };
    struct Config {
        bool theft = true;
        int period_s = 3600;
        float chance = 0.20f, far_m = 120.f, garage_radius = 18.f;
    };
    void ManageOwned(unsigned long long now, const Vector3& pos, Vehicle player_veh);
    void UpdateTheft(unsigned long long now, const Vector3& pos);
    void StartTheft(size_t i, const Vector3& pos);
    void Remove(Vehicle veh);  // çalındı: garajdan silinir
    Vehicle SpawnOwned(Owned& car, const Vector3& at);
    void DropBlip(Owned& c);
    void StartLeave(Ped mech, const Vector3& player_pos);
    void UpdateLeave(unsigned long long now, const Vector3& player_pos);
    void CleanupLeave(bool force);
    void UpdateParkRide(unsigned long long now, const Vector3& player_pos);
    void AbortParkRide(const std::string& reason, unsigned long long now);
    void CleanupParkRide(bool force);
    void StoreParkRideVehicle();
    Leave leave_;
    ParkRide park_ride_;
    std::vector<Vector3> extra_garages_;
    Keys keys_;
    bool menu_open_ = false;
    int menu_sel_ = 0;
    int Pick(const std::string& which, bool in_world) const;  // adla/son kullanılana göre araç seç, -1: yok
    Config cfg_;
    Theft theft_;
    unsigned long long toast_until_ = 0;
    std::string toast_;
    std::vector<Owned> cars_;
    int slot_ = 0;
    size_t last_used_ = 0;
    Valet valet_;
    unsigned long long last_save_ = 0, last_summon_ = 0;
};

}  // namespace hikaye
