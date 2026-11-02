#include "radio.h"

#include <windows.h>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "common.h"
#include "json.hpp"
#include "overlay.h"
#include "vlc_player.h"

namespace hikaye {
namespace {

using json = nlohmann::json;

constexpr unsigned long long kNoiseDurationMs = 900;
constexpr unsigned long long kConnectTimeoutMs = 20000;

std::string FileUrl(const std::wstring& path) {
    std::vector<wchar_t> absolute_path(32768);
    const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(absolute_path.size()),
                                          absolute_path.data(), nullptr);
    if (length == 0 || length >= absolute_path.size()) return {};

    const int utf8_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, absolute_path.data(),
                                               static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (utf8_length <= 0) return {};
    std::string utf8(static_cast<size_t>(utf8_length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, absolute_path.data(),
                            static_cast<int>(length), &utf8[0], utf8_length, nullptr, nullptr) != utf8_length) {
        return {};
    }

    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result = "file:///";
    for (unsigned char ch : utf8) {
        if (ch == '\\') {
            result.push_back('/');
        } else if (std::isalnum(ch) || ch == '/' || ch == ':' || ch == '-' || ch == '_' ||
                   ch == '.' || ch == '~') {
            result.push_back(static_cast<char>(ch));
        } else {
            result.push_back('%');
            result.push_back(hex[ch >> 4]);
            result.push_back(hex[ch & 0x0F]);
        }
    }
    return result;
}

bool IsHttpUrl(const std::string& url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

}  // namespace

Radio::Radio() = default;

Radio::~Radio() {
    Shutdown();
}

bool Radio::Initialize(std::string& error) {
    if (initialized_) return true;

    const std::string path = std::string(kBaseDir) + "/data/radio/stations.json";
    std::ifstream file(path);
    if (!file) {
        error = "İstasyon listesi açılamadı: " + path;
        return false;
    }

    try {
        const json config = json::parse(file);
        if (!config.is_array()) {
            error = "stations.json bir JSON dizisi olmalı.";
            return false;
        }

        stations_.clear();
        for (size_t i = 0; i < config.size(); ++i) {
            const auto& item = config[i];
            if (!item.is_object() || !item.contains("name") || !item["name"].is_string() ||
                !item.contains("genre") || !item["genre"].is_string() ||
                !item.contains("url") || !item["url"].is_string()) {
                error = "stations.json içindeki istasyon kaydı geçersiz (sıra " +
                        std::to_string(i + 1) + ").";
                return false;
            }
            Station station{item["name"].get<std::string>(),
                            item["genre"].get<std::string>(),
                            item["url"].get<std::string>()};
            if (!IsHttpUrl(station.url)) {
                Log("Radyo: geçersiz URL nedeniyle istasyon atlandı: " + station.name);
                continue;
            }
            stations_.push_back(std::move(station));
        }
    } catch (const std::exception& e) {
        error = "stations.json ayrıştırılamadı: " + std::string(e.what());
        return false;
    }

    if (stations_.empty()) {
        error = "stations.json içinde istasyon yok.";
        return false;
    }

    initialized_ = true;
    Log("Radyo: " + std::to_string(stations_.size()) + " istasyon yüklendi.");
    return true;
}

void Radio::Update(bool enabled) {
    const Ped player = PLAYER::PLAYER_PED_ID();
    const bool in_vehicle = enabled && player != 0 && PED::IS_PED_IN_ANY_VEHICLE(player, false);
    const Vehicle vehicle = in_vehicle ? PED::GET_VEHICLE_PED_IS_IN(player, false) : 0;
    const unsigned long long now = GetTickCount64();

    if (!in_vehicle || !vehicle) {
        overlay::TakeMouseWheelDelta();
        StopPlayers();
        RestoreNativeRadio();
        active_ = false;
        status_.clear();
        q_was_down_ = false;
        wheel_remainder_ = 0;
        Publish(false);
        return;
    }

    if (muted_vehicle_ && muted_vehicle_ != vehicle) {
        AUDIO::SET_VEHICLE_RADIO_ENABLED(muted_vehicle_, true);
        muted_vehicle_ = 0;
    }
    AUDIO::SET_USER_RADIO_CONTROL_ENABLED(false);
    AUDIO::SET_VEHICLE_RADIO_ENABLED(vehicle, false);
    CONTROLS::DISABLE_CONTROL_ACTION(0, 81, true);
    CONTROLS::DISABLE_CONTROL_ACTION(0, 82, true);
    CONTROLS::DISABLE_CONTROL_ACTION(0, 85, true);
    muted_vehicle_ = vehicle;
    if (!initialized_ || stations_.empty()) {
        StopPlayers();
        active_ = false;
        q_was_down_ = false;
        Publish(false);
        return;
    }
    if (!active_) {
        active_ = true;
        station_index_ %= stations_.size();
        char off[] = "OFF";
        AUDIO::SET_RADIO_TO_STATION_NAME(off);
        status_ = radio_on_ ? "FREKANS KAPALI" : "KAPALI";
        if (radio_on_) StartStation(now);
    }

    const bool q_down = (GetAsyncKeyState('Q') & 0x8000) != 0;
    if (q_down && !q_was_down_) {
        radio_on_ = !radio_on_;
        if (radio_on_) {
            StartStation(now);
        } else {
            StopPlayers();
            status_ = "KAPALI";
        }
    }
    q_was_down_ = q_down;

        // M tuşu ile sessize alma (mute)
        const bool m_down = (GetAsyncKeyState('M') & 0x8000) != 0;
        if (m_down && !m_was_down_) {
            muted_ = !muted_;
            if (stream_player_) {
                stream_player_->SetVolume(muted_ ? 0 : volume_);
            }
            if (noise_player_) {
                noise_player_->SetVolume(muted_ ? 0 : volume_);
            }
        }
        m_was_down_ = m_down;

        // Ses seviyesi kontrolü: Shift + Mouse Wheel veya +/- tuşları
        const bool shift_down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        if (shift_down) {
            wheel_remainder_ += overlay::TakeMouseWheelDelta();
            const int detents = std::clamp(wheel_remainder_ / WHEEL_DELTA, -8, 8);
            wheel_remainder_ %= WHEEL_DELTA;
            for (int i = 0; i < std::abs(detents); ++i) {
                volume_ = std::clamp(volume_ + (detents > 0 ? 5 : -5), 0, 100);
                if (stream_player_ && !muted_) stream_player_->SetVolume(volume_);
                if (noise_player_ && !muted_) noise_player_->SetVolume(volume_);
            }
        } else {
            // Normal mouse wheel ile istasyon değiştirme
            wheel_remainder_ += overlay::TakeMouseWheelDelta();
            const int detents = std::clamp(wheel_remainder_ / WHEEL_DELTA, -8, 8);
            wheel_remainder_ %= WHEEL_DELTA;
            for (int i = 0; i < std::abs(detents); ++i) {
                ChangeStation(now, detents > 0 ? -1 : 1);
            }
        }

        // +/- tuşları ile de ses kontrolü (Shift gerekmez)
        const bool plus_down = (GetAsyncKeyState(VK_OEM_PLUS) & 0x8000) != 0 || (GetAsyncKeyState(0xBB) & 0x8000) != 0; // =/+ tuşu
        const bool minus_down = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0 || (GetAsyncKeyState(0xBD) & 0x8000) != 0; // -/_ tuşu
        static bool plus_was_down = false;
        static bool minus_was_down = false;
        if (plus_down && !plus_was_down) {
            volume_ = std::clamp(volume_ + 5, 0, 100);
            if (stream_player_ && !muted_) stream_player_->SetVolume(volume_);
            if (noise_player_ && !muted_) noise_player_->SetVolume(volume_);
        }
        if (minus_down && !minus_was_down) {
            volume_ = std::clamp(volume_ - 5, 0, 100);
            if (stream_player_ && !muted_) stream_player_->SetVolume(volume_);
            if (noise_player_ && !muted_) noise_player_->SetVolume(volume_);
        }
        plus_was_down = plus_down;
        minus_was_down = minus_down;

            if (radio_on_ && noise_player_ && now >= noise_until_) StartStream(now);
    if (radio_on_ && stream_player_) {
        switch (stream_player_->State()) {
            case vlc::Player::PlaybackState::Playing:
                status_ = "YAYINDA";
                station_error_logged_ = false;
                break;
            case vlc::Player::PlaybackState::Error:
            case vlc::Player::PlaybackState::Ended:
            case vlc::Player::PlaybackState::Stopped:
                status_ = "YAYIN HATASI";
                if (!station_error_logged_) {
                    Log("Radyo: yayın başlatılamadı: " + stations_[station_index_].name);
                    station_error_logged_ = true;
                }
                stream_player_->Stop();
                stream_player_.reset();
                break;
            default:
                if (now - stream_started_at_ >= kConnectTimeoutMs) {
                    status_ = "BAĞLANTI ZAMAN AŞIMI";
                    if (!station_error_logged_) {
                        Log("Radyo: yayın bağlantısı zaman aşımına uğradı: " +
                            stations_[station_index_].name);
                        station_error_logged_ = true;
                    }
                    stream_player_->Stop();
                    stream_player_.reset();
                } else {
                    status_ = "BAĞLANIYOR";
                }
                break;
        }
    }

    if (!radio_on_) status_ = "KAPALI";
        if (muted_ && radio_on_) status_ += "  |  SESSİZ";
        Publish(true);
    }

void Radio::StartStation(unsigned long long now) {
    StopPlayers();
    station_error_logged_ = false;
    if (!IsHttpUrl(stations_[station_index_].url)) {
        status_ = "GEÇERSİZ İSTASYON";
        Log("Radyo: geçersiz yayın URL'si, istasyon atlandı: " + stations_[station_index_].name);
        return;
    }

    const std::string noise_url = FileUrl(L"HikayeTurkRP\\data\\radio\\parazit.mp3");
    if (!noise_url.empty()) {
        noise_player_ = std::make_unique<vlc::Player>();
        std::string error;
        if (noise_player_->OpenAudio(noise_url, error)) {
            noise_until_ = now + kNoiseDurationMs;
            status_ = "FREKANS ARANIYOR";
            return;
        }
        Log("Radyo parazit sesi açılamadı: " + error);
        noise_player_.reset();
    } else {
        Log("Radyo parazit sesi yolu oluşturulamadı.");
    }
    StartStream(now);
}

void Radio::StartStream(unsigned long long now) {
    if (noise_player_) {
        noise_player_->Stop();
        noise_player_.reset();
    }
    noise_until_ = 0;
    stream_player_ = std::make_unique<vlc::Player>();
    std::string error;
    if (!stream_player_->OpenAudio(stations_[station_index_].url, error)) {
        Log("Radyo yayın hatası (" + stations_[station_index_].name + "): " + error);
        stream_player_.reset();
        status_ = "YAYIN HATASI";
        station_error_logged_ = true;
        return;
    }
    stream_player_->SetVolume(muted_ ? 0 : volume_);
    stream_started_at_ = now;
    status_ = "BAĞLANIYOR";
}

void Radio::ChangeStation(unsigned long long now, int direction) {
    const size_t count = stations_.size();
    station_index_ = (station_index_ + count + (direction < 0 ? count - 1 : 1)) % count;
    if (radio_on_) {
        StartStation(now);
    } else {
        status_ = "KAPALI";
    }
}

void Radio::StopPlayers() {
    if (noise_player_) {
        noise_player_->Stop();
        noise_player_.reset();
    }
    if (stream_player_) {
        stream_player_->Stop();
        stream_player_.reset();
    }
    noise_until_ = 0;
}

void Radio::RestoreNativeRadio() {
    if (muted_vehicle_) {
        AUDIO::SET_VEHICLE_RADIO_ENABLED(muted_vehicle_, true);
        muted_vehicle_ = 0;
    }
    AUDIO::SET_USER_RADIO_CONTROL_ENABLED(true);
}

void Radio::Publish(bool active) {
    const Station* station = active && !stations_.empty() ? &stations_[station_index_] : nullptr;
    overlay::SetRadio(active, station ? station->name : std::string(),
                      station ? station->genre : std::string(), active ? status_ : std::string(),
                      volume_, muted_);
}

void Radio::Shutdown() {
    StopPlayers();
    RestoreNativeRadio();
    active_ = false;
    radio_on_ = false;
    initialized_ = false;
    Publish(false);
}

}  // namespace hikaye
