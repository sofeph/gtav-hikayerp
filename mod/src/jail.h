#pragma once
#include <functional>
#include <string>
#include <vector>

#include "common.h"
#include "json.hpp"

namespace hikaye {

// Davanın LLM'e özetlenen gerçekleri. Tüm karar (hapis süresi, indirim, ücret) kodda hesaplanır.
struct CaseFacts {
    bool player_armed = false;  // olay anında oyuncunun elinde silah var mıydı
    bool npc_injured = false;   // karşı taraf yaralandı mı
    int stars = 0;              // >0: suçun ağırlığına göre verilecek arama seviyesi (0: data/jail.json varsayılanı)
};

// Polis çağrısı sonrası akış: 3 yıldız -> polis yaklaşınca tazer -> tutuklama -> hücre -> ceza/avukat -> tahliye.
// Cinayet izleme: izlenen NPC'yi oyuncu öldürürse 5 yıldız (bu durumda tazer/hapis akışı atlanır, normal kovalamaca).
class JailSystem {
public:
    void Load();  // data/jail.json + HikayeTurkRP/data/record.json (tutuklanma sayısı)

    // Moddan çıkarken: yıldız/polis ayarlarını geri alır, akışı sıfırlar.
    void Reset();
    bool active() const { return state_ != State::Idle; }
    bool CanUsePhone() const { return state_ == State::Idle || state_ == State::Pursuit; }
    bool HasWantedCase() const { return own_wanted_ && PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) > 0; }
    int BarAssociationChance() const { return 40 - bar_calls_ * 10 > 0 ? 40 - bar_calls_ * 10 : 0; }
    nlohmann::json AttemptWantedClear(bool bar_association);
    // Yıldızı bizim sistem mi verdi (tanık ihbarı, cinayet izleme)? Oyunun kendi ürettiği yıldızlar main'de silinir.
    bool OwnWanted() const { return own_wanted_; }
    bool in_cell() const { return state_ == State::InCell; }
    // Ekran klavyesi açıkken L/Enter yok sayılır (yazılan harf avukat/ceza tuşunu tetiklemesin).
    void SetInputBlocked(bool blocked) { input_blocked_ = blocked; }
    double PoliceCallChance() const;
    double ArmedAttackChance() const;

    void OnPoliceCalled(const CaseFacts& facts);
    void WatchForMurder(Ped ped);
    void Update();  // her frame
    void Draw();    // hapis ipuçları ve bildirimler

    // Hücrede 'L' tuşuna basıldıysa true (avukat görüşmesi başlatılır), bir kez döner.
    bool ConsumeLawyerRequest();
    int arrests() const { return arrests_; }

    // Avukat görüşmesi. Para/koordinat içermez; sadece davanın özeti LLM'e gider.
    nlohmann::json CaseState() const;
    int LawyerFee() const;  // ücretsiz hak varsa 0
    // Şule'nin tanıdığı ücretsiz tahliye hakkı: provider(consume=false) varlığını söyler, (true) tüketir.
    void SetFreeReleaseProvider(std::function<bool(bool)> f) { free_release_ = std::move(f); }
    // "lawyer_hired" | "insufficient_funds" | "already_hired" | "not_in_cell"
    std::string HireLawyer();
    void ReleaseWithLawyer();  // avukat tutulduysa indirimli cezayla erken tahliye

private:
    enum class State { Idle, Pursuit, Tase, Down, FadeOut, InCell, ServeFadeOut, ServeFadeIn };
    struct Assessment {
        int expected_days = 0;
        std::vector<std::string> reasons;  // jail.json "reasons" anahtarları
        int fee = 0;
    };

    Assessment Assess() const;
    const nlohmann::json* NearestStation(const Vector3& pos) const;
    void Notice(const std::string& text, unsigned ms = 6000);
    void StartServing(int days, bool via_lawyer);
    void SaveRecord() const;
    void UpdateMurderWatch();
    void PacifyPrison();  // hapisteyken gardiyanlar ateş etmez, alarm susar
    Ped FindTaseCop(const Vector3& player_pos);
    void Teleport(const nlohmann::json& p4);  // [x, y, z, heading]
    // Gözaltı: tazerlendiğin andan tahliyeye kadar yıldız olmaz (azami 0, polis yok sayar); tahliyede eski hâl.
    void EnterCustody();
    void LeaveCustody();
    bool in_custody_ = false;
    unsigned long long last_pacify_at_ = 0;
    int bar_calls_ = 0;
    int BaseDays() const;

    nlohmann::json cfg_ = nlohmann::json::object();
    State state_ = State::Idle;
    bool own_wanted_ = false;
    CaseFacts facts_;
    int arrests_ = 0;
    bool hired_ = false;
    std::function<bool(bool)> free_release_;
    bool lawyer_requested_ = false;
    bool serve_via_lawyer_ = false;
    int serve_days_ = 0;
    int total_sentence_days_ = 0;
    unsigned long long sentence_end_time_ = 0;
    std::string serve_station_;
    nlohmann::json hold_ = nlohmann::json::array();  // hücre/bekleme noktası
    nlohmann::json exit_ = nlohmann::json::array();  // tahliye noktası
    Ped tase_cop_ = 0;
    int scan_tick_ = 0;
    unsigned long long t_ = 0;
    bool l_was_down_ = false, enter_was_down_ = false;
    bool input_blocked_ = false;
    std::vector<Ped> watched_;
    std::string notice_;
    unsigned long long notice_until_ = 0;
};

}  // namespace hikaye
