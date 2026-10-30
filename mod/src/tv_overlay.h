#pragma once

#include <string>

#include "common.h"

namespace hikaye {

class TvOverlay {
public:
    bool Initialize(std::string& error);
    bool Update(const Vector3& player_pos, bool allow_interaction, bool e_pressed, bool p_pressed,
                int game_hour, int game_minute);
    void SetVolume(float volume);
    bool IsOpen() const { return open_; }
    bool CanUse() const { return nearest_tv_ != 0; }
    void ToggleOpen();

private:
    Object FindNearest(const Vector3& player_pos) const;
    bool PublishScreenQuad();

    Object nearest_tv_ = 0;
    bool open_ = false;
    bool initialized_ = false;
    unsigned long long next_scan_at_ = 0;
    int last_game_hour_ = -1;
    int last_game_minute_ = -1;
    bool left_mouse_was_down_ = false;
};

}  // namespace hikaye
