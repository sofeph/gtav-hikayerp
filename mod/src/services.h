#pragma once
#include <string>
#include <vector>

#include "common.h"
#include "json.hpp"
#include "places.h"

namespace hikaye {

enum class ServiceKind { Taxi, HeliTaxi, Police, Ambulance, Fire, Mechanic };
enum class RideKind { Taxi, Heli };

// Telefonla çağrılan hizmet araçları (taksi, helikopter taksi, polis, ambulans, itfaiye) ve taksi/helikopter
// yolculuğu. OTORİTE KODDA: LLM sadece "ne istendi / nereye" niyetini önerir; hizmetin gelip gelmeyeceğini, süresini,
// mesafeyi ve ücreti burası hesaplar ve sonucu "facts" olarak döndürür (NPC bunu sözlü anlatır).
class Services {
public:
    void Load();  // data/services.json
    void Update();  // her frame
    void Shutdown();  // moddan çıkarken: çağrılmış araçları, yolculuğu ve blip'leri temizler

    // Telefondan hizmet çağır. Dönüş: {accepted, reason?, eta_seconds?...}
    nlohmann::json Dispatch(ServiceKind kind);

    // Telefon ekranı için her hizmetin anlık durumu.
    struct Status {
        ServiceKind kind;
        std::string state;  // "" (yok) | "yolda" | "geldi" | "ayriliyor"
        int eta_s = 0;      // sadece "yolda" iken
    };
    std::vector<Status> Statuses() const;
    static const char* Label(ServiceKind k);
    // Çağırdığın tamirci/taksi/helikopter taksi mürettebatı ve araçları: bunlara (araçtan indirme, vurma, araca el koyma) karşı suç işlenmez
    // (polis/ambulans/itfaiye dahil DEĞİL). p ya da v 0 olabilir; verilen herhangi biri hizmetin parçasıysa true.
    bool IsHiredService(Ped p, Vehicle v) const;
    bool HiredServiceNear(const Vector3& pos, float radius) const;  // Türkçe ad: "Taksi", "Helikopter taksi"...

    // Çağırdığın taksi/helikopter yanındaysa ve seni bekliyorsa aracın tutamacı (yoksa 0). "E: Araca bin" ipucu için.
    Vehicle BoardableVehicle(const Vector3& player_pos) const;
    // Oyuncuyu arka koltuğa bindirir. (Script ile doğan şoför "taksi şoförü" gibi davranmadığı için F tuşu şoförü
    // çıkarmaya çalışırdı; binişi bu yüzden kod yapar.)
    void BoardPlayer(Vehicle veh);

    // --- Yolculuk (oyuncu taksiye / çağırdığı helikoptere bindiğinde) ---
    bool InRide() const { return ride_.active; }
    RideKind ride_kind() const { return ride_.kind; }
    Ped ride_driver() const { return ride_.driver; }
    // Yeni yolculuk başladıysa true (bir kez). Konuşma oturumu bunun üzerine açılır.
    bool ConsumeRideStart();
    // "Daha hızlı git / yavaşla" ricası (direction: +1 hızlı, -1 yavaş). Taksi: speed_request_accept_chance ile kabul,
    // helikopter: her zaman reddeder. Karar KODDA; dönüş LLM'e gerçek bilgi olarak gider: {kind, direction, granted, reason}.
    nlohmann::json RequestSpeed(int direction);
    // "Havaalanına", "Davis", "haritadaki işaret"... Dönüş: {accepted, destination, distance_m, fare, ...}
    nlohmann::json SetDestination(const std::string& text, const Places& places);
    // Varışta bir kez true: {arrived, fare, charged}
    bool ConsumeArrival(nlohmann::json& facts);

private:
    // Helikopter inişi: hedefe yüksekten yaklaş -> yakında uygun alan ara -> alanın üstünde asıl -> yavaşça dikey in.
    struct HeliLanding {
        enum class P { Idle, Cruise, Search, Align, Descend, Landed } p = P::Idle;
        Vector3 target{};  // istenen nokta
        Vector3 spot{};    // seçilen iniş alanı (zemin yüksekliğiyle)
        int cand = 0;      // sıradaki aday (merkez + 3 halka x 8 yön)
        int no_ground = 0;  // zemin çarpışması henüz yüklenmediği için bekleyen kare sayısı
        unsigned long long t = 0;
    };
    static constexpr int kLandCandidates = 25;
    void LandBegin(HeliLanding& l, Ped pilot, Vehicle veh, const Vector3& target);
    bool LandUpdate(HeliLanding& l, Ped pilot, Vehicle veh);  // true: indi
    // 0 = uygun, 1 = zemin verisi yok (bekle), 2 = uygun değil
    int CheckSpot(const Vector3& c, float ref_z, Vehicle veh, Vector3& spot) const;

    struct Dispatched {
        HeliLanding land;
        int id = 0;
        ServiceKind kind = ServiceKind::Taxi;
        Vehicle veh = 0;
        Ped crew[2] = {0, 0};
        int crew_n = 0;
        Blip blip = 0;
        enum class S { Arriving, Arrived, Working, Leaving } s = S::Arriving;
        Vector3 target{};  // çağrı anında oyuncunun konumu
        Vector3 origin{};  // doğduğu yer (dönüş yönü)
        unsigned long long created = 0, t = 0;
        bool done_effect = false;  // ambulans iyileştirdi, vb.
        bool crew_out = false;
        bool in_ride = false;
        int phase = 0;  // helikopter ayrılırken: 1 = yükseliyor, 2 = uzaklaşıyor
        bool dead = false;  // temizlendi, listeden silinecek
        Vehicle target_veh = 0;  // tamirci: onarılacak araç
        int work_phase = 0;      // tamirci: 0 yürüyor, 1 çalışıyor
        int fee = 0;             // tamirci: tahmini ücret
        unsigned long long work_since = 0;
    };
    struct Ride {
        HeliLanding land;
        bool active = false;
        RideKind kind = RideKind::Taxi;
        Vehicle veh = 0;
        Ped driver = 0;
        // Takeoff: helikopter önce dikey yükselir (yerden hedefe uçarsa çarpıyor), sonra hedefe gider.
        enum class S { WaitDest, Takeoff, Driving, Arrived } s = S::WaitDest;
        Vector3 dest{};
        int fare = 0;
        int speed_level = 0;  // -1 yavaş, 0 normal, +1 hızlı (taksi)
        unsigned long long last_speed_req = 0;
        int dispatch_id = 0;  // 0: sokaktan binilen (ambient) taksi
        unsigned long long t = 0;
    };

    bool RequestModel(Hash model);
    bool FindRoadSpawn(const Vector3& around, float min_dist, Vector3& out) const;
    Ped SpawnCrew(Vehicle veh, const std::string& model, int seat, int ped_type);
    void Cleanup(Dispatched& d);
    void UpdateDispatch(Dispatched& d, const Vector3& player_pos);
    void UpdateRide(const Vector3& player_pos);
    void EndRide();
    void SendAway(Dispatched& d);
    Dispatched* FindById(int id);
    static const char* Name(ServiceKind k);

    nlohmann::json cfg_ = nlohmann::json::object();
    std::vector<Dispatched> disp_;
    Ride ride_;
    int next_id_ = 1;
    bool ride_started_ = false;
    bool arrival_ready_ = false;
    nlohmann::json arrival_facts_;
    unsigned long long last_dispatch_[6] = {0, 0, 0, 0, 0, 0};
};

}  // namespace hikaye
