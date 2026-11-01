#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "common.h"

namespace hikaye::vlc {
class Player;
}

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
    TvOverlay();
    ~TvOverlay();
    TvOverlay(const TvOverlay&) = delete;
    TvOverlay& operator=(const TvOverlay&) = delete;
    bool Initialize(std::string& error);
    bool Update(const Vector3& player_pos, bool allow_interaction, bool e_pressed, bool p_pressed,
                bool escape_pressed, int game_hour, int game_minute);
    bool IsOpen() const { return open_; }
    bool CanUse() const { return nearest_tv_ != 0; }
    const std::vector<TvChannel>& Channels() const { return channels_; }
    bool IsPhoneWatching() const { return phone_playback_; }
    const std::string& PlaybackStatus() const { return player_status_; }
    bool WatchPhoneChannel(int channel_no);
    void StopPhonePlayback();
    void CloseTv();
    
    // Get current channel info for rendering
    const TvChannel* CurrentChannel() const;
    bool IsLiveStream() const;
    
private:
    Object FindNearest(const Vector3& player_pos) const;
    void NextChannel();
    void PrevChannel();
    void ShowChannelInfo();
    void UpdateVlcPlayer();
    void StartVlcPlayer(const std::string& url, bool preserve_youtube_playlist = false);
    void StartVlcPlayer(const std::vector<std::string>& urls);
    void StartYoutubePlaylistItem(size_t index);
    bool AdvanceYoutubePlaylist();
    bool StartYoutubePotProvider(const std::wstring& provider_root, std::string& error);
    void StopYoutubePotProvider();
    bool StartYoutubeResolver(const std::string& url, std::string& error);
    void UpdateYoutubeResolver();
    void CancelYoutubeResolver();
    void CloseVlcPlayer();
    void UpdateTvScreen();
    void UseFirstPersonCamera();
    void RestoreCameraMode();

    Object nearest_tv_ = 0;
    bool open_ = false;
    bool phone_playback_ = false;
    bool initialized_ = false;
    unsigned long long next_scan_at_ = 0;
    int current_channel_idx_ = 0;
    std::vector<TvChannel> channels_;
    std::unique_ptr<vlc::Player> vlc_player_;
    std::vector<std::string> youtube_playlist_urls_;
    size_t youtube_playlist_index_ = 0;
    void* youtube_pot_provider_process_ = nullptr;
    void* youtube_process_ = nullptr;
    void* youtube_output_ = nullptr;
    unsigned long long youtube_started_at_ = 0;
    std::string youtube_output_buffer_;
    std::uint64_t last_video_version_ = 0;
    unsigned long long last_video_frame_at_ = 0;
    int previous_camera_mode_ = 0;
    bool camera_mode_saved_ = false;
    bool camera_was_in_vehicle_ = false;
    bool playback_started_ = false;
    bool signal_lost_ = false;
    std::array<float, 8> screen_corners_{};
    std::string player_status_;
    int game_seconds_per_minute_ = 120;
};

}  // namespace hikaye
