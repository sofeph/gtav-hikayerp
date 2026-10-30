#include "tv_overlay.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "cef_browser.h"
#include "overlay.h"

namespace hikaye {
namespace {

constexpr int kBrowserWidth = 1280;
constexpr int kBrowserHeight = 720;
constexpr float kTvSearchRadius = 5.0f;
constexpr unsigned long long kScanIntervalMs = 250;
constexpr std::array<const char*, 15> kTvModels = {
    "prop_tv_flat_01", "prop_tv_flat_02", "prop_tv_flat_03", "prop_tv_flat_03b", "prop_tv_flat_michael",
    "prop_tv_01", "prop_tv_02", "prop_tv_03", "prop_tv_04", "prop_tv_06",
    "prop_tv_07", "prop_tv_08", "prop_tv_09", "prop_tv_10", "prop_tv_11"};

float DistanceSquared(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

bool Project(const Vector3& world, float& x, float& y) {
    return invoke<BOOL>(0x34E82F05DF2974F5, world.x, world.y, world.z, &x, &y) != FALSE;
}

}  // namespace

bool TvOverlay::Initialize(std::string& error) {
    if (initialized_) return true;
    initialized_ = cef::Initialize(error);
    return initialized_;
}

Object TvOverlay::FindNearest(const Vector3& player_pos) const {
    Object nearest = 0;
    float best_distance = kTvSearchRadius * kTvSearchRadius;
    for (const char* model_name : kTvModels) {
        const Object candidate = OBJECT::GET_CLOSEST_OBJECT_OF_TYPE(
            player_pos.x, player_pos.y, player_pos.z, kTvSearchRadius, HashKey(model_name), false, false, false);
        if (!candidate || !ENTITY::DOES_ENTITY_EXIST(candidate)) continue;
        const Vector3 position = ENTITY::GET_ENTITY_COORDS(candidate, false);
        const float distance = DistanceSquared(position, player_pos);
        if (distance < best_distance) {
            nearest = candidate;
            best_distance = distance;
        }
    }
    return nearest;
}

bool TvOverlay::PublishScreenQuad() {
    if (!nearest_tv_ || !ENTITY::DOES_ENTITY_EXIST(nearest_tv_)) {
        overlay::SetTvQuad(nullptr);
        return false;
    }

    Vector3 minimum{}, maximum{};
    const Hash model = ENTITY::GET_ENTITY_MODEL(nearest_tv_);
    GAMEPLAY::GET_MODEL_DIMENSIONS(model, &minimum, &maximum);
    const float width = std::max(0.2f, (maximum.x - minimum.x) * 0.78f);
    const float height = std::min((maximum.z - minimum.z) * 0.82f, width * 9.0f / 16.0f);
    const float left = -width * 0.5f;
    const float right = width * 0.5f;
    const float center_z = minimum.z + (maximum.z - minimum.z) * 0.62f;
    const float top = center_z + height * 0.5f;
    const float bottom = center_z - height * 0.5f;
    const float front = maximum.y + 0.01f;

    const std::array<Vector3, 4> corners = {
        ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(nearest_tv_, left, front, top),
        ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(nearest_tv_, right, front, top),
        ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(nearest_tv_, right, front, bottom),
        ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(nearest_tv_, left, front, bottom)};
    float screen[8]{};
    for (size_t i = 0; i < corners.size(); ++i) {
        if (!Project(corners[i], screen[i * 2], screen[i * 2 + 1])) {
            overlay::SetTvQuad(nullptr);
            return false;
        }
    }
    overlay::SetTvQuad(screen);
    return true;
}

bool TvOverlay::Update(const Vector3& player_pos, bool allow_interaction, bool e_pressed, bool p_pressed,
                       int game_hour, int game_minute) {
    const unsigned long long now = GetTickCount64();
    if (!open_ && allow_interaction && now >= next_scan_at_) {
        nearest_tv_ = FindNearest(player_pos);
        next_scan_at_ = now + kScanIntervalMs;
    }

    if (open_ && (p_pressed || e_pressed)) {
        open_ = false;
        last_game_hour_ = -1;
        last_game_minute_ = -1;
        nearest_tv_ = 0;
        overlay::SetTvQuad(nullptr);
        cef::Close();
        return true;
    }

    if (!open_ && allow_interaction && e_pressed && nearest_tv_) {
        std::string error;
        if (!Initialize(error)) {
            Log("CEF TV başlatılamadı: " + error);
            return false;
        }
        const std::string page_url = "http://127.0.0.1:8766/webv2/tv/player.html?hour=" +
                                     std::to_string(game_hour) + "&minute=" + std::to_string(game_minute);
        open_ = cef::Open(page_url, kBrowserWidth, kBrowserHeight, error);
        if (!open_) {
            Log("CEF TV penceresi açılamadı: " + error);
            return false;
        }
    }

    if (open_) {
        if (game_hour != last_game_hour_ || game_minute != last_game_minute_) {
            last_game_hour_ = game_hour;
            last_game_minute_ = game_minute;
            cef::ExecuteJavaScript("window.setTvGameTime && window.setTvGameTime(" +
                                   std::to_string(game_hour) + "," + std::to_string(game_minute) + ");");
        }
        if (!PublishScreenQuad()) {
            open_ = false;
            last_game_hour_ = -1;
            last_game_minute_ = -1;
            cef::Close();
            return true;
        }
        cef::UpdateKeyboard();
        return true;
    }

    return false;
}

void TvOverlay::SetVolume(float volume) {
    const float normalized = std::clamp(volume, 0.0f, 1.0f);
    cef::ExecuteJavaScript("window.setTvVolume && window.setTvVolume(" + std::to_string(normalized) + ");");
}

void TvOverlay::ToggleOpen() {
    if (open_) {
        open_ = false;
        last_game_hour_ = -1;
        last_game_minute_ = -1;
        nearest_tv_ = 0;
        overlay::SetTvQuad(nullptr);
        cef::Close();
    } else if (nearest_tv_) {
        std::string error;
        if (!Initialize(error)) {
            Log("CEF TV başlatılamadı: " + error);
            return;
        }
        open_ = cef::Open("http://127.0.0.1:8766/webv2/tv/player.html",
                          kBrowserWidth, kBrowserHeight, error);
        if (!open_) Log("CEF TV penceresi açılamadı: " + error);
    }
}

}  // namespace hikaye
