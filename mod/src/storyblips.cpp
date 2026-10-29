#include "storyblips.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <fstream>

#include "json.hpp"

using nlohmann::json;

namespace hikaye {

void StoryBlips::Load() {
    try {
        std::ifstream in(std::string(kBaseDir) + "/data/mode.json");
        if (!in) return;
        const json cfg = json::parse(in);
        const json h = cfg.value("hide_story_blips", json::object());
        enabled_ = h.value("enabled", true);
        debug_ = h.value("debug", false);
        hide_all_ = h.value("hide_all", false);
        if (h.contains("keep_sprites")) keep_ = h["keep_sprites"].get<std::vector<int>>();
        if (h.contains("sprites")) sprites_ = h["sprites"].get<std::vector<int>>();
        if (h.contains("types")) types_ = h["types"].get<std::vector<int>>();
    } catch (const std::exception& e) {
        Log(std::string("hide_story_blips okunamadi: ") + e.what());
    }
}

void StoryBlips::Dump() {
    // sprite başına (renk, tür) gruplarını say: hangi blipler hikaye görev blipi?
    std::map<std::tuple<int, int, int>, int> count;
    for (int sprite = 1; sprite < 900; ++sprite) {
        Blip b = UI::GET_FIRST_BLIP_INFO_ID(sprite);
        int guard = 0;
        while (UI::DOES_BLIP_EXIST(b) && guard++ < 200) {
            ++count[{sprite, UI::GET_BLIP_COLOUR(b), static_cast<int>(UI::GET_BLIP_INFO_ID_TYPE(b))}];
            b = UI::GET_NEXT_BLIP_INFO_ID(sprite);
        }
    }
    Log("blip dokumu #" + std::to_string(dumps_ + 1) + " (" + std::to_string(count.size()) + " grup):");
    for (const auto& [key, n] : count) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "blip: sprite=%d colour=%d type=%d adet=%d", std::get<0>(key), std::get<1>(key),
                      std::get<2>(key), n);
        Log(buf);
    }
}

void StoryBlips::Update(bool mode_active, const std::function<bool(Blip)>& is_own) {
    if (!mode_active) {
        if (was_active_) RestoreAll();
        was_active_ = false;
        return;
    }
    const unsigned long long now = GetTickCount64();
    if (!was_active_) {
        was_active_ = true;
        entered_ms_ = now;
        dumps_ = 0;
    }
    if (debug_ && dumps_ < 2 && now - entered_ms_ > (dumps_ == 0 ? 3000ULL : 12000ULL)) {  // girişten 3 ve 12 sn sonra
        Dump();
        ++dumps_;
    }
    if (!enabled_ || now - last_scan_ms_ < 1000) return;  // saniyede bir; hikaye script'leri blipleri yeniden oluşturabilir
    last_scan_ms_ = now;

    // Hangi sprite'lar taranacak: hide_all ise hepsi (keep_ hariç), değilse listedekiler.
    std::vector<int> scan;
    if (hide_all_) {
        for (int s = 1; s < 900; ++s) {
            if (std::find(keep_.begin(), keep_.end(), s) == keep_.end()) scan.push_back(s);
        }
    } else {
        scan = sprites_;
    }
    for (int sprite : scan) {
        Blip b = UI::GET_FIRST_BLIP_INFO_ID(sprite);
        int guard = 0;
        while (UI::DOES_BLIP_EXIST(b) && guard++ < 300) {
            const int type = static_cast<int>(UI::GET_BLIP_INFO_ID_TYPE(b));
            const bool type_ok = types_.empty() || std::find(types_.begin(), types_.end(), type) != types_.end();
            if (type_ok && !is_own(b) && hidden_.find(b) == hidden_.end()) {
                hidden_[b] = static_cast<int>(UI::GET_BLIP_INFO_ID_DISPLAY(b));
                UI::SET_BLIP_DISPLAY(b, 0);
            } else if (hidden_.find(b) != hidden_.end()) {
                UI::SET_BLIP_DISPLAY(b, 0);  // script geri açmış olabilir
            }
            b = UI::GET_NEXT_BLIP_INFO_ID(sprite);
        }
    }
}

void StoryBlips::RestoreAll() {
    for (const auto& [blip, display] : hidden_) {
        if (UI::DOES_BLIP_EXIST(blip)) UI::SET_BLIP_DISPLAY(blip, display);
    }
    hidden_.clear();
}

}  // namespace hikaye
