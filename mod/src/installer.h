#pragma once
#include <vector>
#include <set>
#include <string>

#include "input.h"
#include "missions.h"

namespace hikaye {

// Bilgisayar başında "Facade Office" kurulum sihirbazı (görev 'install' aşaması). Menü overlay'de çizilir.
// Doğrulama kodda: yanlış seçimler hata sayılır ve sonraki konuşmalarda NPC'ler bundan bahseder.
// Adımlar: karşılama, lisans, kurulum türü (hızlı = tuzak), bileşenler, disk, ürün anahtarı, ilerleme, bitiş.
class Installer {
public:
    void Begin(const Missions::InstallSpec& spec);
    // Her frame. Bittiğinde true döner; success ve mistakes doldurulur.
    bool Update(bool& success, int& mistakes);
    bool active() const { return active_; }

private:
    enum class Step { Welcome, License, Type, Components, Disk, Key, Progress, Done };
    void Draw() const;
    void Fail(const std::string& generic, const std::string& hint);

    Missions::InstallSpec spec_;
    bool active_ = false;
    Step step_ = Step::Welcome;
    int sel_ = 0;
    int mistakes_ = 0;
    bool custom_ = false;
    std::set<std::string> chosen_;
    std::string notice_, typed_key_;
    unsigned long long progress_start_ = 0;
    Keys keys_;
    // Pencere içi sihirbaz: Facade masaüstünde "Kurulum" penceresi (fare ile tıklanır; yön tuşu yok)
    void ReadCursor();
    float cur_x_ = 0.5f, cur_y_ = 0.5f;
    int click_seq_ = 0;
    bool lmb_was_ = false;
    mutable std::vector<int> row_items_;  // seçilebilir satır sırası -> pencere öğesi sırası (fare isabeti için)
};

}  // namespace hikaye
