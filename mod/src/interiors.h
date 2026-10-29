#pragma once
#include <string>
#include <vector>

#include "common.h"

namespace hikaye {

// Kapalı gelen iç mekanlar: data/interiors.json'daki IPL'ler mod aktifken yüklenir, çıkınca kaldırılır (hikaye bozulmaz).
// Portal: dış nokta <-> iç nokta arasında E ile geçiş (kapı kilitliyse de girilir).
class Interiors {
public:
    void Load();
    void Enable();   // moda girince: IPL'leri iste
    void Disable();  // moddan çıkınca: IPL'leri kaldır
    // Portaldan geçildi mi (bir kez): entering=true içeri girdi; dest = varış noktası (x,y,z,h), from = geçiş öncesi konum.
    bool ConsumeTransition(bool& entering, float dest[4], Vector3& from);
    // Her frame (mod aktifken). e_pressed: E'ye yeni basıldı. true dönerse E bu sistem tarafından kullanıldı.
    bool Update(const Vector3& player_pos, bool e_pressed);

private:
    struct Portal {
        std::string name, enter_prompt, exit_prompt;
        float out[4]{}, in[4]{}, radius = 4.5f;
    };
    bool Teleport(const float p[4], bool into_interior);
    std::string notice_;
    unsigned long long notice_until_ = 0;
    std::vector<std::string> ipls_;
    std::vector<Portal> portals_;
    bool enabled_ = false;
    bool inside_ = false;  // anahtar: içerideyken sadece 'çık', dışarıdayken sadece 'gir' çıkar
    bool trans_ = false, trans_enter_ = false;
    float trans_dest_[4]{};
    Vector3 trans_from_{};
};

}  // namespace hikaye
