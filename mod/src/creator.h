#pragma once
#include <functional>
#include <string>
#include <vector>

#include "input.h"
#include "json.hpp"
#include "profile.h"

namespace hikaye {

// GTA Online tarzı karakter oluşturucu. Klavyeyle gezilir: PageUp/PageDown sekme, ↑↓ satır, ←→ değer, Enter onay.
// Her değişiklik canlı olarak ped'e uygulanır; kamera yüze/boya göre ayarlanır. Çizim overlay menüsündedir.
class Creator {
public:
    enum class Result { Continue, Done, Cancelled };
    // Cinsiyet değişince model değişir; yeni ped döndürülür.
    using SwapModelFn = std::function<Ped(bool female)>;

    void Begin(CharacterProfile* profile, Ped ped, const nlohmann::json* pool, SwapModelFn swap);
    Result Update();  // her frame
    void End();       // kamerayı ve menüyü kapatır
    Ped ped() const { return ped_; }
    void LockGender(bool v) { lock_gender_ = v; }  // Bölüm 2: kadın karakter, cinsiyet değiştirilemez

private:
    bool lock_gender_ = true;  // Bölüm 1 erkek, Bölüm 2 kadın: cinsiyet oluşturucuda değiştirilemez

private:
    struct Row {
        int tab = 0;
        std::string label;
        std::function<std::string()> value;  // boşsa eylem satırı
        std::function<void(int)> change;     // -1 / +1
        std::function<void()> enter;
        bool header = false;
    };

    void BuildRows();
    void AddOverlay(int tab, int id, const std::string& name, bool color);
    void AddComp(int tab, const std::string& name, int comp);
    void AddProp(int tab, const std::string& name, int prop);
    void Reapply();
    void UpdateCamera();
    void Publish();
    int RowsInTab(int tab) const;
    void Move(int dir);

    CharacterProfile* p_ = nullptr;
    Ped ped_ = 0;
    const nlohmann::json* pool_ = nullptr;
    SwapModelFn swap_;
    std::vector<Row> rows_;
    std::vector<std::string> tabs_;
    int tab_ = 0;
    int selected_ = 0;  // rows_ içindeki indeks
    Keys keys_;
    Cam cam_ = 0;
    bool typing_ = false;
    Result result_ = Result::Continue;
};

}  // namespace hikaye
