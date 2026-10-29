#pragma once
#include <string>

#include "common.h"

namespace hikaye {

// DENEYSEL: Pause menüsündeki "Online" sekmesini Hikaye RP girişine çevirir.
// SDK'da seçili sekmeyi veren bir native yok; sadece pause menüsü durumu (GET_PAUSE_MENU_STATE) ve bir menü kimliği
// (_0x2309595AD6145265) okunabiliyor. Bu sınıf pause menüsü açıkken bu değerleri izler:
//  - debug açıksa değişimleri mod.log'a yazar (Online sekmesine gelince hangi değerin çıktığını görmek için),
//  - data/mode.json "pause_probe.online_tab" ({state, hash}) tanımlıysa ve değerler eşleşirse "Online sekmesi seçildi"
//    sinyali verir; main pause menüsünü kapatıp Hikaye RP menüsünü açar.
// Sadece okur; hikayeye hiçbir şey yazmaz. Online'a girmeyi ENGELLEMEZ (ScriptHookV zaten Online'a girince oyunu kapatır):
// sekmede Enter'a basma, ok tuşlarıyla gez.
class PauseMenu {
public:
    void Load();
    void Update();  // her frame, hikayede de çalışır (sadece okur)
    bool ConsumeOnlineTabSelected();  // eşleşme oldu (bir kez)

private:
    bool debug_ = false;
    bool has_signal_ = false;
    int want_state_ = -1;
    unsigned want_hash_ = 0;
    bool want_hash_set_ = false;
    bool matched_ = false;
    bool was_active_ = false;
    int last_state_ = -2;
    unsigned last_hash_ = 0;
};

}  // namespace hikaye
