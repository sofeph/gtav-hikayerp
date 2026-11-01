#pragma once

#include <string>
#include <vector>
#include "common.h"

namespace hikaye {

struct TvChannel {
    int channel_no = 0;
    std::string name;
    std::string url;
    std::string streamtype;  // "HLS Stream" or "Youtube Playlist"
    int start_minute = -1;   // -1 = live stream
    int program_minutes = -1; // -1 = live stream
};

class TvOverlay {
public:
    bool Initialize(std::string& error);
    bool Update(const Vector3& player_pos, bool allow_interaction, bool e_pressed, bool p_pressed,
                int game_hour, int game_minute);
    bool IsOpen() const { return open_; }
    bool CanUse() const { return nearest_tv_ != 0; }
    void CloseTv();
    
    // Get current channel info for rendering
    const TvChannel* CurrentChannel() const;
    bool IsLiveStream() const;
    
    // HLS player integration
    void* GetHLSPlayer() const { return hls_player_; }
    void SetHLSPlayer(void* player) { hls_player_ = player; }

private:
    Object FindNearest(const Vector3& player_pos) const;
    void NextChannel();
    void PrevChannel();
    void ShowChannelInfo();
    void UpdateHLSPlayer();

    Object nearest_tv_ = 0;
    bool open_ = false;
    bool initialized_ = false;
    unsigned long long next_scan_at_ = 0;
    int current_channel_idx_ = 0;
    std::vector<TvChannel> channels_;
    void* hls_player_ = nullptr;  // HLSPlayer instance
    int game_seconds_per_minute_ = 120;
};

}  // namespace hikaye
