#pragma once
#include <string>
#include <vector>

#include "creator.h"
#include "input.h"
#include "json.hpp"
#include "profile.h"

namespace hikaye {

// "Hikaye RP" modu: hikaye modundan AYRI, kendi karakteri (Online tarzı) ve cüzdanı olan mod.
// - Hikayede mod tamamen uyur (dükkan NPC'si, telefon, hizmet, görev yok); sadece etkinleştirme tuşu dinlenir.
// - Tuşa basınca menü açılır. Girerken hikaye karakteri (model, kıyafet, silahlar, konum, can) saklanır; çıkınca
//   aynen geri yüklenir. Hikaye parasına (SPx_TOTAL_CASH) dokunulmaz; mod kendi cüzdanını kullanır.
// - Mod içindeyken oyun kaydı (save menüsü) kapatılır; özel karakterle alınan kayıt hikaye kaydını bozmasın.
// "Sıfırdan başla" seçilince işaret dosyası yazılır; oyun bir sonraki açılışta (hiçbir modül kayıt okumadan ÖNCE) tüm kayıtlar
// saves_yedek_<zaman> klasörüne taşınır ve hikaye sıfırdan başlar. Karakter bir kez oluşturulunca değiştirilemez: ya devam edilir ya sıfırdan başlanır.
void ApplyPendingReset();

class Mode {
public:
    void Load();
    // Her frame. `pool`: rastgele NPC/geçmiş havuzu (oluşturucudaki memleket, takım listeleri için).
    void Update(const nlohmann::json& pool);

    bool engaged() const { return state_ != State::Dormant; }  // menü/oluşturucu/aktif: yükleme kapağı kalkabilir
    bool active() const { return state_ == State::Active; }  // mod aktif ve oynanıyor: diğer sistemler çalışabilir
    bool busy() const { return state_ == State::Menu || state_ == State::Creating || state_ == State::ActiveMenu; }
    // Dışarıdan (örn. pause menüsündeki Online sekmesi) mod menüsünü aç; açılamazsa nedenini ekrana yazar.
    void RequestMenu();
    bool ConsumeEntered();  // moda yeni girildi (sistemler başlasın)
    bool ConsumeExited();   // moddan çıkıldı (sistemler temizlensin)
    const CharacterProfile& profile() const { return profile_; }
    // Bölüm 1 bitti: erkek karakter saklanır, Bölüm 2 için kadın karakter oluşturucuyla yaratılır ve oyuna girer.
    void BeginChapter2(const nlohmann::json& pool);
    void SetSwitchAllowed(bool v) { switch_allowed_ = v; }  // serbest dolaşım: iki karakter arasında geçiş
    bool HasOther() const { return has_other_; }
    int slot() const { return slot_; }  // aktif karakter slotu (garaj/kayıtlar karaktere özel)
    // Diğer karakterin görünümüyle bir NPC yaratır (Bölüm 2 finalinde buluşma); name_out: karakterin adı.
    Ped SpawnAlter(float x, float y, float z, float heading, std::string& name_out);

private:
    enum class State { Dormant, Menu, Creating, Active, ActiveMenu };
    struct Story {  // hikaye karakterinin saklanan hali
        bool valid = false;
        Hash model = 0;
        Vector3 pos{};
        float heading = 0.f;
        int health = 200, armor = 0;
        int drawable[12] = {}, texture[12] = {}, palette[12] = {};
        int prop[10] = {}, prop_tex[10] = {};
        std::vector<std::pair<Hash, int>> weapons;  // silah, mermi
    };

    // Oyun yüklenince menüyü kendiliğinden bir kez aç (data/mode.json "open_menu_on_start"). Yükleme/görev/ekran geçişi
    // bitene kadar sessizce bekler, 3 dakika içinde açılamazsa vazgeçer.
    void TryAutoOpen(const nlohmann::json& pool);
    bool launcher_direct_ = false;  // başlatıcıdan açıldı (HIKAYE_LAUNCHER=1): menü yerine doğrudan Hikaye RP'ye geç
    bool CanEnter(std::string& why) const;
    void GiveRpWeapons();     // RP envanterini ver (hikaye silahlarını kaldırır)
public:
    void RestoreRpLoadout() { if (active()) GiveRpWeapons(); }  // ölümden sonra RP silahları geri (death modülü)
private:
    void CaptureRpWeapons();  // elindeki silahları RP profiline yaz
    void SaveStory();
    void RestoreStory();
    Ped SwapModel(bool female);
    void PlaceAt(float x, float y, float z, float heading);
    void Spawn();
    void EnterMode(bool create, const nlohmann::json& pool);
    void FinishCreation();
    void CancelCreation();
    void ExitMode();
    void OpenMenu();
    void CloseMenu();
    void UpdateMenu(const nlohmann::json& pool);
    void Notice(const std::string& text);
    void PersistIfChanged();
    void SwitchCharacter();
    bool PrepareBus();  // yeni karakter: başlangıç noktasında yolcu otobüsü, oyuncu içinde
    void BusExit();     // otobüs durur, karakter iner, otobüs gider
    Vehicle bus_ = 0;
    Ped bus_driver_ = 0;
    bool arrival_pending_ = false;
    void SaveSlotState() const;

    State state_ = State::Dormant;
    nlohmann::json cfg_ = nlohmann::json::object();
    CharacterProfile profile_, backup_, other_;  // other_: aktif olmayan karakter
    int slot_ = 0;                               // aktif karakter: 0 erkek (B1), 1 kadın (B2)
    bool has_other_ = false, switch_allowed_ = false, creating_ch2_ = false;
    bool has_saved_ = false;
    bool editing_ = false;  // oluşturucu mevcut karakteri düzenliyor (iptalde eski haline dönülür)
    Story story_;
    Creator creator_;
    Keys keys_;
    bool entered_ = false, exited_ = false;
    int selected_ = 0;
    std::vector<std::string> menu_rows_;
    std::string notice_;
    unsigned long long notice_until_ = 0, last_save_ms_ = 0, model_bad_since_ = 0;
    int last_saved_cash_ = 0;
    bool auto_open_ = true;
    unsigned long long start_ms_ = 0, stable_since_ = 0;
};

}  // namespace hikaye
