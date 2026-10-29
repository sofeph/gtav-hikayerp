#pragma once
#include "common.h"

namespace hikaye {

// Oyunun kendi satıcı/tezgâhtar modelleri (market, Ammu-Nation, kıyafet, berber, dövme, bayi). Mod aktifken bunlar silinir ve
// sokak sohbetine hedef olmaz (LLM "ben satıcı değilim" demesin).
inline bool IsNativeVendorModel(Hash model) {
    static const char* const kNames[] = {"mp_m_shopkeep_01", "s_m_y_ammucity_01", "s_m_m_ammucountry", "s_f_y_shop_low",    "s_f_y_shop_mid",
                                         "s_f_m_shop_high",  "s_m_y_shop_mask",   "s_m_m_hairdress_01", "u_m_y_tattoo_01"};
    static Hash hashes[9] = {};
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < 9; ++i) hashes[i] = HashKey(kNames[i]);
        ready = true;
    }
    for (Hash h : hashes) {
        if (h == model) return true;
    }
    return false;
}

}  // namespace hikaye
