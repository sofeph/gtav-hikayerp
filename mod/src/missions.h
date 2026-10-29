#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "common.h"
#include "json.hpp"

namespace hikaye {

// Hikaye RP görev sistemi (data/missions/*.json). OTORİTE KODDA: aşama ilerlemesi, ödül ve hedefleri kod belirler;
// LLM sadece NPC rolünü oynar ve "advance" niyetini önerir (sırası gelen NPC'de ve koşul sağlanınca kod ilerletir).
// Aşama türleri:
//   talk     NPC ile konuş (yüz yüze veya telefonla)
//   vehicle  belirtilen araca bin            (kod algılar)
//   drive_to aracı hedefe götür              (kod algılar)
//   interact belirli bir noktada E'ye bas    (kod algılar; örn. sensör kur)
//   install  bilgisayar başında kurulum sihirbazı (Installer; sonucu kod doğrular)
// Görev bitince görev verici telefon rehberine eklenir; sonraki görev rehberden aranarak alınır.
// Hikaye görev bliplerinin yerine bizim görev bliplerimizi çizer (haritada sarı işaret + rota).
class Missions {
public:
    struct Talk {  // konuşulabilir görev NPC'si (yüz yüze) veya telefondaki kişi (ped = 0)
        std::string npc_id;
        std::string name;
        Ped ped = 0;
        nlohmann::json npc_json;
        Vector3 pos{};
        std::string model;                         // hangout için: fiziksel NPC yaratılırken
        std::vector<std::string> fallback_models;
    };
    struct Contact {  // telefon rehberi kaydı
        std::string id, name;
        bool mission_ready = false;  // bu kişiden alınabilecek bir görev var
    };
    struct InstallSpec {  // kurulum sihirbazının görev tarafından verilen ayarları
        std::string title;
        std::vector<std::pair<std::string, std::string>> components;  // kimlik, etiket
        std::vector<std::string> required, avoid;
        std::string key;       // ürün anahtarı (Burak'tan istenir)
        bool hints = false;    // IT yoklamasında "knows" çıkmadıysa sihirbaz ipucu verir
    };

    void Load();      // data/missions/*.json + saves/missions.json (ilerleme, rehber, ilişki, bayraklar)
    void Shutdown();  // moddan çıkarken: NPC'leri, görev aracını ve blipleri temizle
    // Her frame (mod aktifken). e_pressed: E'ye yeni basıldı ve başka bir etkileşim onu kullanmıyor.
    void Update(const Vector3& player_pos, bool e_pressed);
    void DrawHud();

    bool NearestTalkable(const Vector3& pos, Talk& out) const;
    std::vector<Contact> Contacts() const;
    // Telefon: tanışılan (görevi bitmiş verici/açılan) karakterler; rehberden silinenler buradan geri kaydedilir.
    std::vector<Contact> KnownCharacters() const;
    bool CurrentTargetPos(Vector3& out, std::string& label) const;  // yönetici ışınlanması: odaktaki görevin hedefi
    bool IsKnownCharacter(const std::string& id) const;
    // NPC'nin söylediği cümlede görevin SIRRI (örn. ürün anahtarı) geçti mi (harf/rakam dışı karakterler ve büyük-küçük harf yok sayılır). Geçtiyse etiket ve değeri verir.
    bool SecretMentioned(const std::string& npc_id, const std::string& text, std::string& label, std::string& value) const;
    bool RemoveContact(const std::string& id);
    int RestoreContacts();  // eksik tanıdıkları rehbere ekler, eklenen sayısı
    bool CallTarget(const std::string& contact_id, Talk& out) const;  // rehberdeki kişiyi aramak için
    // Rehberdeki bir arkadaşın bilgisi (hangout daveti için; görev odağını değiştirmez).
    // Oyuncu iç mekan portalından geçti: aşamadaki NPC yakındaysa onunla birlikte geçer.
    void FollowThroughPortal(const Vector3& from, bool entering, const float dest[4]);
    // Bölüm 2 kancaları: iç karakterin NPC olarak yaratılması (model "__alter__").
    void SetAlterSpawner(std::function<Ped(float, float, float, float, std::string&)> f) { alter_spawn_ = std::move(f); }
    // Bayraklar (görev boyunca toplanan bilgiler; kayıtlıdır).
    bool FlagIs(const std::string& key, const std::string& value) const { return Flag(key) == value; }
    std::string FlagValue(const std::string& key) const { return Flag(key); }  // telefon tarayıcısı sayfaları için
    // Görev durumu: "done" | "active" (başladı) | "available" (açık, başlamadı) | "locked" (gereksinim eksik) | "unknown".
    std::string MissionState(const std::string& id) const;
    bool TakeFlag(const std::string& key);  // "1" ise "0" yapıp true döner (tek seferlik olay)
    bool PersonTalk(const std::string& id, Talk& out) const;
    void SetInCarWith(const std::string& npc_id) { in_car_with_ = npc_id; }  // hangout sırasında LLM'e "aynı arabadasınız" bilgisi
    // LLM'e giden görev durumu (para yok). call: telefonla mı konuşuluyor.
    nlohmann::json QuestState(const std::string& npc_id, bool call) const;
    // LLM 'advance' önerdi. item: LLM'in yazdığı seçenek (örn. IT yoklaması: knows/partial/clueless).
    nlohmann::json TryAdvance(const std::string& npc_id, bool call, const std::string& item = "");
    int talk_turns_ = 0;  // geçerli talk aşamasında yaşanan diyalog sayısı (aşama değişince sıfırlanır)
    // Kod kararlı ilerleme: 'auto_advance' talk aşaması, oyuncu NPC ile gerçekten konuşunca (LLM'e sorulmadan) ilerler.
    nlohmann::json TryAutoAdvance(const std::string& npc_id, bool call);
    // Avukat Şule'nin tanıdığı ücretsiz tahliye hakkı: varsa tüketir (bir kez).
    bool ConsumeFreeRelease();
    bool HasFreeRelease() const { return Flag("sule_ucretsiz_hak") == "1"; }
    bool OwnsBlip(Blip b) const { return b != 0 && (b == blip_ || b == giver_blip_); }

    // Gelen arama: sıradaki görev "start_by: incoming_call" ise ve gecikme dolduysa true (out = arayan kişi).
    bool IncomingCall(Talk& out);
    // Arama sonucu: cevaplandı mı. Cevaplanınca kişi rehbere girer; kaçırılırsa 60 sn sonra tekrar çalar (3 kaçırışta rehbere girer).
    void IncomingHandled(bool answered);
    // NPC ile ilişkimiz: stranger, acquaintance, friend, close_friend, partner (görevlerin sets_relationship alanı belirler).
    std::string Relationship(const std::string& npc_id) const;

    // Bilgisayar başında E'ye basıldıysa true (bir kez); out = sihirbaz ayarları.
    bool ConsumeInstallRequest(InstallSpec& out);
    // Sihirbaz bitti: success=true ise aşama ilerler. mistakes: yapılan hata sayısı (sonraki konuşmalarda NPC'ler söyler).
    void InstallFinished(bool success, int mistakes);

private:
    struct Npc {
        std::string id, model, gender;
        std::vector<std::string> fallback_models;  // model oyunda yoksa sırayla denenir (ek DLC modelleri eski sürümlerde olmayabilir)
        nlohmann::json npc_json;  // personality, profile, voice (companion'a gider)
        Vector3 pos{};
        float heading = 0.f;
        Ped ped = 0;
        int ground_tries = 0;
        bool follows = false;  // oyuncu portaldan geçince (iç mekan) yanında gelir
        unsigned long long despawn_at = 0;  // aşama bitince NPC hemen silinmez (veda/ödeme cümlesi bitsin)
        bool exact_z = false;  // iç mekan: zemine oturtma, verilen yüksekliği kullan; sadece oyuncu yakınken doğar
    };
    struct Stage {
        std::string id, type = "talk", npc, objective, brief, advance_when, vehicle;
        std::string guide;               // oyuncu takılırsa gösterilen ipucu (boşsa aşama türüne göre otomatik)
        std::string place;               // buluşma yeri (LLM'e meeting_place olarak gider; profil bilgisiyle karışmasın)
        float distance = 200.f;          // walk: birlikte yürünecek mesafe (m)
        std::string prompt;              // interact/install: yakınken çıkan ipucu (ASCII; oyun fontu Türkçe çizemeyebilir)
        std::string help;                // oyuncu bu aşamadayken görev vericiyi ararsa ona verilen yardım bilgisi
        std::vector<std::string> item_options;  // 'advance' ile birlikte LLM'in yazacağı seçenekler (örn. IT yoklaması)
        nlohmann::json installer;        // install: sihirbaz ayarları
        nlohmann::json finale;           // interact: E'ye basınca kapanış sahnesi {lines:[...], seconds:N}
        // combat aşaması
        nlohmann::json enemies;      // [{model,weapon,count,pos:[x,y,z],radius,health,accuracy,armor}]
        int kill_target = 0;         // 0=hepsi öldür; >0=bu kadar öldür yeter
        std::string kill_npc;            // scene: oyuncu yaklaşınca bu NPC'yi kod öldürür (kurgu sahnesi)
        bool in_person = false;          // telefonla yapılamaz (teslimat gibi)
        bool auto_advance = false;       // talk: ilk konuşmadan sonra kod ilerletir (AI bitirmez)
        int min_turns = 1;               // talk(auto_advance): ilerlemek için gereken en az karşılıklı diyalog sayısı
        bool has_pos = false;
        Vector3 pos{};                   // vehicle/drive_to/interact/install hedefi
        float radius = 12.f, heading = 0.f;
    };
    struct Mission {
        std::string id, title, giver;
        int reward = 0;
        int order = 0;                         // görev sırası (küçük olan önce)
        std::vector<std::string> requires_;    // bunlar bitmeden açılmaz
        std::string start_by = "contact";      // "contact" (rehberden arayarak/yüz yüze) | "incoming_call" (karakter arar)
        int call_delay_s = 60;                 // incoming_call: önceki görev bitince kaç saniye sonra arasın
        bool giver_always = true;              // false: görev verici sadece sırası geldiğinde (yüz yüze aşamada) belirir
        std::string rel_npc, rel_status;       // bitince ilişki durumu bu olur (örn. ayse -> friend)
        std::vector<std::string> unlocks_contacts;  // bitince bu kişiler de rehbere girer
        std::map<std::string, std::string> on_complete_flags;  // görev bitince konan bayraklar
        bool clear_contacts = false;                           // görev bitince rehber temizlenir (bölüm geçişi)
        std::vector<std::string> removes_contacts;             // görev bitince bu kişiler rehberden silinir (ölüm vb.)
        std::map<std::string, std::string> on_start_flags;  // görev kabul edilince (1. aşama bitince) konan bayraklar
        nlohmann::json secret;                 // {npc,label,value,from_stage,to_stage}: NPC'nin oyuncuya söyleyebileceği sır
        std::vector<Npc> npcs;
        std::vector<Stage> stages;
        Vehicle veh = 0;  // görev aracı (vehicle/drive_to aşamaları)
    };

    const Mission* Current() const;
    Mission* Current();
    const Npc* FindNpc(const Mission& m, const std::string& id) const;
    const Npc* FindNpcAnywhere(const std::string& id) const;
    void SpawnOrDespawn(Mission& m, const Vector3& player_pos);
    bool Spawn(Npc& n);
    void Despawn(Npc& n);
    bool SpawnVehicle(Mission& m, const Stage& s);
    void ReleaseVehicle(Mission& m);
    void UpdateStages(Mission& m, const Vector3& player_pos, bool e_pressed);
    nlohmann::json Advance(Mission& m);
    bool StageTarget(const Mission& m, Vector3& out) const;
    void RefreshBlip(const Mission& m);
    void UpdateGuidance(const Mission& m, const Vector3& player_pos);  // takılan oyuncuyu yönlendirir
    void RemoveBlip();
    void UpdateGiverBlip(const Mission& m);  // görev vericinin sürekli duran küçük mavi blipi
    void AddContact(const std::string& id);
    std::string Flag(const std::string& key) const;
    void Save() const;

    std::vector<Mission> missions_;
    std::vector<std::string> completed_, contacts_;
    std::map<std::string, std::string> rel_;    // npc adı -> ilişki durumu
    std::map<std::string, std::string> flags_;  // görev boyunca toplanan bilgiler (örn. yoklama sonucu, kurulum hataları)
    unsigned long long ready_at_ = 0;           // gelen aramanın çalacağı an
    std::string armed_id_;                      // gecikmesi başlatılan görev
    int misses_ = 0;
    bool install_request_ = false;
    bool walk_started_ = false;         // walk aşaması: NPC oyuncuyu takip ediyor
    float walk_progress_ = 0.f;         // birlikte yürünen mesafe (m)
    int walk_toasted_ = 0;
    unsigned long long walk_toast_at_ = 0;
    Vector3 walk_last_{};
    unsigned long long scene_at_ = 0;  // scene aşaması: ölüm sahnesinin başladığı an
    std::function<Ped(float, float, float, float, std::string&)> alter_spawn_;
    bool finale_active_ = false;           // kapanış sahnesi sürüyor (ekran siyah, yazılar akıyor)
    unsigned long long finale_start_ = 0, finale_until_ = 0;
    std::vector<std::string> finale_lines_;
    // combat aşaması runtime state
    struct CombatEnemy { Ped ped = 0; bool confirmed_dead = false; };
    std::vector<CombatEnemy> combat_enemies_;
    bool combat_spawned_ = false;
    int combat_killed_ = 0;
    bool combat_done_ = false;
    void CleanupCombat();
    void SpawnCombatWave(const Stage& s);
    bool escort_boarded_ = false;  // escort aşaması: NPC arabaya bindi mi (şu an hiçbir görev kullanmıyor)
    Vehicle escort_veh_ = 0;
    std::string hint_key_;               // yönlendirme: şu anki aşama anahtarı
    unsigned long long hint_since_ = 0;  // bu aşamada kalmaya başladığı an
    bool inside_interior_ = false;  // oyuncu iç mekan portalından içeri girdi (exact_z NPC'ler sadece o zaman doğar)
    std::string in_car_with_;      // hangout: şu an arabada olan arkadaş
    // Paralel görevler: her görevin kendi aşaması var. stage_ = odaktaki (aktif) görevin aşaması; odak değişince saklanır.
    // Current() const olduğu için bu alanlar mutable (odak seçimi onun içinde yapılır).
    mutable size_t stage_ = 0;
    mutable std::string focus_;
    mutable std::map<std::string, size_t> stages_;
    bool IsOpen(const Mission& m) const;
    const Mission* OpenMissionOf(const std::string& giver_id) const;  // bu kişinin verdiği açık görev (devam eden öncelikli)
    void SwitchFocus(const std::string& id) const;
    Blip blip_ = 0;
    Blip giver_blip_ = 0;
    std::string giver_blip_for_;
    std::string blip_for_;  // blipin gösterdiği "mission:aşama" anahtarı
    std::string toast_;
    unsigned long long toast_until_ = 0;
};

}  // namespace hikaye
