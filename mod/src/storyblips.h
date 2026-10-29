#pragma once
#include <functional>
#include <map>
#include <vector>

#include "common.h"

namespace hikaye {

// Mod aktifken hikaye görev bliplerini haritadan/mini haritadan gizler, çıkınca eski görünürlüğüne döndürür.
// Hikayenin kendi script'leri blipleri oluşturmaya devam eder; biz sadece görünürlüğünü (SET_BLIP_DISPLAY) değiştiririz.
// Hangi blipin gizleneceği data/mode.json -> hide_story_blips ile ayarlanır. Bizim görev bliplerimiz `is_own` ile hariç tutulur.
class StoryBlips {
public:
    void Load();
    void Update(bool mode_active, const std::function<bool(Blip)>& is_own);
    void RestoreAll();

private:
    void Dump();  // debug: hangi sprite/renk/tür kaç blip var (mod.log)

    bool enabled_ = true, debug_ = false;
    bool hide_all_ = false;            // true: tüm koordinat/kişi blipleri gizlenir (sadece bizimkiler ve keep_ kalır)
    std::vector<int> keep_{8};         // hide_all iken dokunulmayacak sprite'lar (8 = harita işaret noktası)
    std::vector<int> sprites_{1};
    std::vector<int> types_{4, 5};
    std::map<Blip, int> hidden_;  // blip -> eski görünürlük
    unsigned long long entered_ms_ = 0, last_scan_ms_ = 0;
    int dumps_ = 0;
    bool was_active_ = false;
};

}  // namespace hikaye
