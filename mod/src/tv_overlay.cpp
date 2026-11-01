#include "tv_overlay.h"

#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <cmath>

#include "common.h"
#include "json.hpp"
#include "hls_player.h"

using json = nlohmann::json;

namespace hikaye {

namespace {

constexpr float kTvSearchRadius = 4.5f;
constexpr unsigned long long kScanIntervalMs = 500;
const char* const kTvModels[] = {
    "prop_tv_flat_01", "prop_tv_flat_02", "prop_tv_flat_03", "prop_tv_flat_03b", "prop_tv_flat_michael",
    "prop_tv_01", "prop_tv_02", "prop_tv_03", "prop_tv_04", "prop_tv_06",
    "prop_tv_07", "prop_tv_08", "prop_tv_09", "prop_tv_10", "prop_tv_11"
};

float DistanceSquared(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

}  // namespace

bool TvOverlay::Initialize(std::string& error) {
    if (initialized_) return true;

    // Load channels configuration
    const std::string path = std::string(kBaseDir) + "/data/tv/channels.json";
    std::ifstream file(path);
    if (file.is_open()) {
        try {
            json j = json::parse(file);
            channels_.clear();
            if (j.contains("game_seconds_per_minute")) {
                game_seconds_per_minute_ = j.value("game_seconds_per_minute", 120);
            }
            if (j.contains("channels") && j["channels"].is_array()) {
                for (const auto& ch : j["channels"]) {
                    TvChannel cfg;
                    cfg.channel_no = ch.value("channel_no", 0);
                    cfg.name = ch.value("name", "");
                    cfg.url = ch.value("url", "");
                    cfg.streamtype = ch.value("streamtype", "HLS Stream");
                    cfg.start_minute = ch.value("start_minute", -1);
                    cfg.program_minutes = ch.value("program_minutes", -1);
                    channels_.push_back(cfg);
                }
            }
            Log("TV: " + std::to_string(channels_.size()) + " kanal yuklendi.");
        } catch (const std::exception& e) {
            Log("TV: channels.json ayrıştırma hatası: " + std::string(e.what()));
        }
    } else {
        Log("TV: channels.json bulunamadi (" + path + "), varsayilan kanallar kullanilacak.");
    }

    if (channels_.empty()) {
        channels_.push_back({1, "Weazel News", "", "HLS Stream", -1, -1});
        channels_.push_back({2, "CNT Network", "", "HLS Stream", -1, -1});
        channels_.push_back({3, "Lifeinvader Live", "", "HLS Stream", -1, -1});
    }

    // Create HLS player instance
    hls_player_ = new hikaye::hls::HLSPlayer();

    initialized_ = true;
    return true;
}

Object TvOverlay::FindNearest(const Vector3& player_pos) const {
    Object nearest = 0;
    float best_dist = kTvSearchRadius * kTvSearchRadius;

    for (const char* model_name : kTvModels) {
        const Object candidate = OBJECT::GET_CLOSEST_OBJECT_OF_TYPE(
            player_pos.x, player_pos.y, player_pos.z, kTvSearchRadius, HashKey(model_name), false, false, false);
        if (!candidate || !ENTITY::DOES_ENTITY_EXIST(candidate)) continue;

        const Vector3 pos = ENTITY::GET_ENTITY_COORDS(candidate, false);
        const float dist = DistanceSquared(pos, player_pos);
        if (dist < best_dist) {
            nearest = candidate;
            best_dist = dist;
        }
    }
    return nearest;
}

const TvChannel* TvOverlay::CurrentChannel() const {
    if (channels_.empty()) return nullptr;
    if (current_channel_idx_ < 0 || current_channel_idx_ >= static_cast<int>(channels_.size())) {
        return nullptr;
    }
    return &channels_[static_cast<size_t>(current_channel_idx_)];
}

bool TvOverlay::IsLiveStream() const {
    const TvChannel* ch = CurrentChannel();
    if (!ch) return false;
    return ch->start_minute == -1 && ch->program_minutes == -1;
}

void TvOverlay::ShowChannelInfo() {
    if (channels_.empty()) return;
    if (current_channel_idx_ < 0 || current_channel_idx_ >= static_cast<int>(channels_.size())) {
        current_channel_idx_ = 0;
    }
    const auto& ch = channels_[static_cast<size_t>(current_channel_idx_)];
    std::string text = "~b~[TV " + std::to_string(ch.channel_no) + "]~s~ " + ch.name +
                       "  ~y~[Kanal: Sol/Sag | Kapat: E/P]~s~";
    ShowHelp(text.c_str());
}

void TvOverlay::NextChannel() {
    if (channels_.empty()) return;
    current_channel_idx_ = (current_channel_idx_ + 1) % static_cast<int>(channels_.size());
    UpdateHLSPlayer();
    ShowChannelInfo();
}

void TvOverlay::PrevChannel() {
    if (channels_.empty()) return;
    current_channel_idx_ = (current_channel_idx_ - 1 + static_cast<int>(channels_.size())) % static_cast<int>(channels_.size());
    UpdateHLSPlayer();
    ShowChannelInfo();
}

void TvOverlay::UpdateHLSPlayer() {
    if (!hls_player_) return;
    
    auto* player = static_cast<hikaye::hls::HLSPlayer*>(hls_player_);
    const TvChannel* ch = CurrentChannel();
    if (!ch) {
        player->Stop();
        return;
    }
    
    // Only play HLS streams directly
    if (ch->streamtype == "HLS Stream" && !ch->url.empty()) {
        std::string error;
        if (!player->IsPlaying() || player->GetSRV() == nullptr) {
            player->Open(ch->url, error);
            if (!error.empty()) {
                Log("TV HLS Open Error: " + error);
            }
        }
    } else {
        // YouTube playlist or other - handled by Companion
        player->Stop();
    }
}

void TvOverlay::CloseTv() {
    if (open_) {
        open_ = false;
        ShowHelp("~b~[TV]~s~ Kapatildi.");
    }
    if (hls_player_) {
        static_cast<hikaye::hls::HLSPlayer*>(hls_player_)->Stop();
    }
    nearest_tv_ = 0;
}

bool TvOverlay::Update(const Vector3& player_pos, bool allow_interaction, bool e_pressed, bool p_pressed,
                       int game_hour, int game_minute) {
    (void)game_hour;
    (void)game_minute;

    const unsigned long long now = GetTickCount64();

    // TV tara (periyodik olarak)
    if (!open_ && allow_interaction && now >= next_scan_at_) {
        nearest_tv_ = FindNearest(player_pos);
        next_scan_at_ = now + kScanIntervalMs;
    }

    // TV açıkken uzaklaşma kontrolü
    if (open_) {
        if (nearest_tv_ && ENTITY::DOES_ENTITY_EXIST(nearest_tv_)) {
            const Vector3 tv_pos = ENTITY::GET_ENTITY_COORDS(nearest_tv_, false);
            if (DistanceSquared(tv_pos, player_pos) > (kTvSearchRadius + 2.0f) * (kTvSearchRadius + 2.0f)) {
                CloseTv();
                return false;
            }
        }

        // Kapatma (E veya P tuşu)
        if (p_pressed || e_pressed) {
            CloseTv();
            return true;
        }

        // Kanal değiştirme tuşları (Sol / Sağ yön tuşları)
        if (GetAsyncKeyState(VK_RIGHT) & 1) {
            NextChannel();
            return true;
        }
        if (GetAsyncKeyState(VK_LEFT) & 1) {
            PrevChannel();
            return true;
        }

        // Update HLS player if playing
        if (hls_player_) {
            static_cast<hikaye::hls::HLSPlayer*>(hls_player_)->Update();
        }

        return true;
    }

    // Açma (E basıldığında TV varsa)
    if (allow_interaction && e_pressed && nearest_tv_ != 0) {
        open_ = true;
        UpdateHLSPlayer();
        ShowChannelInfo();
        return true;
    }

    return false;
}

}  // namespace hikaye
