#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace hikaye::vlc {

class Player {
public:
    enum class PlaybackState { Unknown, Opening, Buffering, Playing, Paused, Stopped, Ended, Error };

    Player();
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    bool Open(const std::string& url, std::string& error);
    bool OpenAudio(const std::string& url, std::string& error);
    bool OpenPlaylist(const std::vector<std::string>& urls, std::string& error);
    void Stop();
    PlaybackState State() const;
    bool CopyLatestFrame(std::vector<std::uint8_t>& pixels, unsigned& width, unsigned& height,
                         std::uint64_t& version);
        bool SetVolume(int volume_percent);  // 0-100

private:
    struct Api;

    static unsigned __cdecl SetupVideo(void** opaque, char* chroma, unsigned* width, unsigned* height,
                                       unsigned* pitches, unsigned* lines);
    static void __cdecl CleanupVideo(void* opaque);
    static void* __cdecl LockVideo(void* opaque, void** planes);
    static void __cdecl UnlockVideo(void* opaque, void* picture, void* const* planes);
    static void __cdecl DisplayVideo(void* opaque, void* picture);
    bool OpenInternal(const std::string& url, std::string& error, bool video_output);

    unsigned Setup(unsigned* width, unsigned* height, unsigned* pitches, unsigned* lines);
    void* Lock(void** planes);
    void Unlock();
    void Display();

    std::unique_ptr<Api> api_;
    void* instance_ = nullptr;
    void* media_player_ = nullptr;
    void* media_list_ = nullptr;
    void* media_list_player_ = nullptr;
    void* library_ = nullptr;

    std::mutex frame_mutex_;
    std::vector<std::uint8_t> front_frame_;
    std::vector<std::uint8_t> write_frame_;
    unsigned width_ = 0;
    unsigned height_ = 0;
    std::uint64_t frame_version_ = 0;
};

}  // namespace hikaye::vlc
