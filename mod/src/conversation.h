#pragma once
#include <functional>
#include <string>

#include "jail.h"
#include "garage.h"
#include "hangout.h"
#include "missions.h"
#include "overlay.h"
#include "phone.h"
#include "places.h"
#include "textinput.h"
#include "services.h"
#include "favors.h"
#include "shop_manager.h"
#include "street_events.h"
#include "ws_client.h"

namespace hikaye {

// Bir NPC ile açık konuşma oturumu: companion'a session_start gönderir, push-to-talk tuşunu izler,
// gelen transcript/npc_reply mesajlarını altyazı olarak çizer.
class Conversation {
public:
    explicit Conversation(WsClient& ws) : ws_(&ws) {}
    void SetWs(WsClient* ws) { ws_ = ws; }  // moda ilk girince gerçek bağlantı verilir

    bool active() const { return shop_ != nullptr; }
    // Telefon arayüzü açıkken sesli santral görüşmesi telefonun içinde gösterilir; altyazı tekrarlanmaz.
    void SetPhoneVisible(bool v) { phone_visible_ = v; }
    Phone::CallView GetPhoneView() const;
    // T ile açılan ekran klavyesi açık mı. Açıkken E/L/Enter gibi oyun tuşları yok sayılmalı.
    bool typing() const { return typing_; }
    void SendRaw(const std::string& text) { ws_->Send(text); }  // oturumsuz mesajlar (telefon içerik üretimi)
    void SetPostsHandler(std::function<void(const nlohmann::json&)> h) { on_posts_ = std::move(h); }
    void Start(Shop& shop, const Vector3& player_pos);
    // Sokaktaki rastgele yayayla konuşma: karakter (ad, memleket, ses...) ped'in handle'ından üretilir.
    void StartStreet(Ped ped, const Vector3& player_pos, const nlohmann::json& pool);
    // Yaya oyuncuya kendisi yaklaştı ve konuşmayı İLK o başlatıyor (iyilik isteği). facts: {kind: money|ride, ...}
    // Polis seni durdurdu/arama talebi: polis konuşmayı İLK başlatır ('police' bağlamı). facts: {reason}
    bool StartPoliceStop(Ped cop, const Vector3& player_pos, const nlohmann::json& pool, const nlohmann::json& facts);
    // 'ask_vendor' niyeti: seyyar satıcının yerini sormak; cevabı kod hesaplar (vendors::AskAround)
    void SetVendorAsk(std::function<nlohmann::json()> f) { vendor_ask_ = std::move(f); }
    bool StartProactive(Ped ped, const Vector3& player_pos, const nlohmann::json& pool, const nlohmann::json& facts);
    // Moddan çıkarken: açık oturumu, yazı kutusunu ve süren olayları (araba sahnesi, polis arama) kapatır.
    void Reset() {
        if (typing_) {
            textinput::Cancel();
            overlay::SetInput("", false);
            typing_ = false;
        }
        End("mod kapandi");
        pending_ = Pending::None;
        ride_.Abort();
        call_.Abort();
    }
    void SetJail(JailSystem* jail) { jail_ = jail; }
    void SetPlaces(Places* places) { places_ = places; }
    void SetMissions(Missions* missions) { missions_ = missions; }
    void SetHangout(Hangout* hangout) { hangout_ = hangout; }
    void SetGarage(Garage* garage) { garage_ = garage; }
    // Görev NPC'siyle konuşma (Yusuf Cansever, müşteri...). Aşama ilerlemesini kod belirler, LLM sadece önerir.
    void StartMission(const Missions::Talk& talk);
    // Rehberdeki görev kişisini telefonla arama (fiziksel NPC yok; yüz yüze gerektiren aşamalar ilerlemez).
    void StartMissionCall(const Missions::Talk& talk);
    void SetServices(Services* services) { services_ = services; }
    // Telefon (P): sanal santral görevlisiyle konuşma. Hizmet çağırma, sonuçlar kodda belirlenir.
    void StartPhone(const nlohmann::json& pool, const std::string& role = "santral görevlisi", const std::string& line_id = "santral");
    // Taksi/helikopter yolculuğu: şoför/pilotla diyalog (hedefi söylersin, ücreti kod hesaplar).
    void StartRide(const nlohmann::json& pool);
    void StartHitch(const nlohmann::json& pool);
    void StartPersonCall(const nlohmann::json& npc);  // telefon rehberindeki kayıtlı sokak tanıdığını ara  // otostop: sürücüyle sohbet (hitchhike modülü)
    // Hücredeki avukat görüşmesi (ped yok; avukatın karakteri havuzdan üretilir). Sadece hücredeyken başlar.
    void StartLawyer(const nlohmann::json& pool);
    // keep_ped: sokak NPC'si olay için (kavga/araba) bırakılmıyor, görevleri korunur.
    void End(const char* reason, bool keep_ped = false);
    // Oturumdan bağımsız sürenen olaylar (araba sahnesi); her frame çağrılır.
    void UpdateEvents() {
        ride_.Update();
        call_.Update();
    }
    bool IsEventPed(Ped p) const { return ride_.active() && ride_.ped() == p; }
    // Aktif oturumda her frame çağrılır. toggle_pressed: E'ye yeni basıldı (oturumu kapatır).
    void Update(const Vector3& player_pos, bool toggle_pressed);
    // Altyazılar oturum bitse de süreleri dolana kadar çizilir.
    void Draw();
    void Notify(const std::string& text);  // geçici uyarı (ör. "Servis kapalı")

private:
    void HandleMessage(const std::string& raw);
    nlohmann::json BuildState() const;
    void SendState();
    void RunPendingEvent();
    // Yön tarifi: hedefi, mesafeyi ve yönü KOD hesaplar; waypoint koyar; gerçek bilgiyi companion'a gönderir,
    // NPC bunu sözlü anlatır (LLM yer/mesafe uyduramaz).
    void Directions(const std::string& destination);
    // OTORİTE BURADA: LLM sadece niyet önerir; fiyat, stok, para ve teslimat modda doğrulanır.
    void TryPurchase(const std::string& item_id, int qty);

    WsClient* ws_;
    Shop* shop_ = nullptr;   // aktif hedef: dükkan NPC'si veya street_
    Shop street_;            // sokak yayası için geçici "dükkan" (envanteri boş)
    std::string context_ = "shop";  // "shop" | "street"
    bool ptt_down_ = false;
    bool speaking_ = false;
    bool phone_visible_ = false;
    bool native_kb_ = false;    // ImGui yoksa oyunun ekran klavyesi kullanılıyor (Türkçe harf desteklemez)
    bool typing_ = false;       // ekran klavyesi açık
    bool t_was_down_ = false;
    // NPC'nin cümlesi bitince tetiklenecek sokak olayı (LLM niyet önerir, koşulları ve zarı kod belirler).
    enum class Pending { None, Fight, Ride, LawyerRelease, HangUp, Leave, CallPolice };
    std::function<void(const nlohmann::json&)> on_posts_;
    std::function<nlohmann::json()> vendor_ask_;
    RideFavor ride_favor_;  // iyilik: yayayı arabayla bir yere götür
    bool gave_money_ = false;  // bu oturumda para verildi (aynı konuşmada tekrar puan yok)
    nlohmann::json BuildStateCore() const;
    Pending pending_ = Pending::None;
    JailSystem* jail_ = nullptr;
    Places* places_ = nullptr;
    Missions* missions_ = nullptr;
    Hangout* hangout_ = nullptr;
    Garage* garage_ = nullptr;
    std::string quest_npc_id_;
    bool quest_call_ = false;  // görev kişisiyle telefonla konuşuluyor
    bool person_call_ = false;  // rehberdeki kayıtlı sokak tanıdığıyla telefonla konuşuluyor
    Services* services_ = nullptr;
    street_events::PoliceCallEvent call_;
    // street_ hedefi sanal bir kişi için kullanılıyorsa bağlamı: "" (sokak yayası) | "lawyer" | "phone" | "taxi" | "heli"
    std::string virtual_context_;
    // Hizmet/yolculuk sonuçlarını companion'a gerçek bilgi olarak gönderir (NPC sözlü anlatır).
    void SendFollowup(const char* purpose, const nlohmann::json& facts);
    bool speaking_seen_ = false;
    unsigned long long pending_deadline_ = 0;
    street_events::RideEvent ride_;
    bool fresh_transcript_ = false;  // her satış yeni bir oyuncu cümlesi gerektirir (çift tahsilat engeli)
    std::string last_result_;
    std::string npc_name_, npc_line_, player_line_, notice_;
    std::string Thinking(unsigned long long now) const;
    unsigned long long thinking_since_ = 0;  // >0: NPC cevabı hazırlanıyor ('düşünüyor...' yazısı; ses/bildirim yok)
    unsigned long long npc_until_ = 0, player_until_ = 0, notice_until_ = 0;
};

}  // namespace hikaye
