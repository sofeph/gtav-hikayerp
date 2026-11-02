#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common.h"

namespace hikaye::vlc {
class Player;
}

namespace hikaye {

class Radio {
public:
    Radio();
    ~Radio();
    Radio(const Radio&) = delete;
    Radio& operator=(const Radio&) = delete;

    bool Initialize(std::string& error);
    void Update(bool enabled);
    void Shutdown();

private:
    struct Station {
        std::string name;
        std::string genre;
        std::string url;
    };

    void StartStation(unsigned long long now);
    void StartStream(unsigned long long now);
    void ChangeStation(unsigned long long now, int direction);
    void StopPlayers();
    void RestoreNativeRadio();
    void Publish(bool active);

    std::vector<Station> stations_;
    std::unique_ptr<vlc::Player> noise_player_;
    std::unique_ptr<vlc::Player> stream_player_;
    unsigned long long noise_until_ = 0;
    unsigned long long stream_started_at_ = 0;
    int wheel_remainder_ = 0;
    Vehicle muted_vehicle_ = 0;
    size_t station_index_ = 0;
    bool initialized_ = false;
    bool active_ = false;
    bool radio_on_ = false;
        bool muted_ = false;
                int volume_ = 30;  // 0-100 (default 30%)
        bool q_was_down_ = false;
        bool m_was_down_ = false;
        bool station_error_logged_ = false;
        std::string status_;
};

}  // namespace hikaye
