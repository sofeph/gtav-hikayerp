#include "vlc_player.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <string>
#include <vector>

namespace hikaye::vlc {

namespace {

using Instance = void;
using Media = void;
using MediaPlayer = void;
using MediaList = void;
using MediaListPlayer = void;
using VideoSetup = unsigned(__cdecl*)(void**, char*, unsigned*, unsigned*, unsigned*, unsigned*);
using VideoCleanup = void(__cdecl*)(void*);
using VideoLock = void*(__cdecl*)(void*, void**);
using VideoUnlock = void(__cdecl*)(void*, void*, void* const*);
using VideoDisplay = void(__cdecl*)(void*, void*);

constexpr unsigned kMaxFrameWidth = 1280;
constexpr unsigned kMaxFrameHeight = 720;
std::atomic<std::uint64_t> g_frame_version{0};

bool IsDirectory(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool IsFile(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring ParentDirectory(const std::wstring& path) {
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring() : path.substr(0, separator);
}

std::wstring FindVlcDirectory() {
    std::vector<std::wstring> candidates;
    wchar_t configured[MAX_PATH]{};
    const DWORD configured_length = GetEnvironmentVariableW(L"HIKAYE_VLC_PATH", configured, MAX_PATH);
    if (configured_length > 0 && configured_length < MAX_PATH) {
        std::wstring path(configured, configured_length);
        if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"') {
            path = path.substr(1, path.size() - 2);
        }
        if (IsFile(path)) path = ParentDirectory(path);
        if (!path.empty()) candidates.push_back(path);
    }

    candidates.push_back(L"HikayeTurkRP\\tools\\vlc");
    candidates.push_back(L"tools\\vlc");
    candidates.push_back(L"C:\\Program Files\\VideoLAN\\VLC");
    candidates.push_back(L"C:\\Program Files (x86)\\VideoLAN\\VLC");

    wchar_t program_files[MAX_PATH]{};
    DWORD length = GetEnvironmentVariableW(L"ProgramFiles", program_files, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        candidates.push_back(std::wstring(program_files, length) + L"\\VideoLAN\\VLC");
    }
    length = GetEnvironmentVariableW(L"ProgramFiles(x86)", program_files, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        candidates.push_back(std::wstring(program_files, length) + L"\\VideoLAN\\VLC");
    }

    for (const auto& candidate : candidates) {
        if (IsDirectory(candidate) && IsFile(candidate + L"\\libvlc.dll") &&
            IsDirectory(candidate + L"\\plugins")) {
            return candidate;
        }
    }
    return {};
}

template <typename T>
bool LoadFunction(HMODULE library, const char* name, T& function) {
    function = reinterpret_cast<T>(GetProcAddress(library, name));
    return function != nullptr;
}

class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(const wchar_t* name, const std::wstring& value) : name_(name) {
        std::vector<wchar_t> buffer(32768);
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetEnvironmentVariableW(name_.c_str(), buffer.data(),
                                                      static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            const DWORD error = GetLastError();
            if (error == ERROR_ENVVAR_NOT_FOUND) {
                had_original_ = false;
            } else if (error == ERROR_SUCCESS) {
                had_original_ = true;
            } else {
                error_ = error;
                return;
            }
        } else if (length >= static_cast<DWORD>(buffer.size())) {
            error_ = ERROR_BUFFER_OVERFLOW;
            return;
        } else {
            had_original_ = true;
            original_value_.assign(buffer.data(), length);
        }

        if (!SetEnvironmentVariableW(name_.c_str(), value.c_str())) {
            error_ = GetLastError();
            return;
        }
        changed_ = true;
    }

    ~ScopedEnvironmentVariable() {
        Restore();
    }

    bool IsSet() const {
        return changed_;
    }

    DWORD Error() const {
        return error_;
    }

    bool Restore() {
        if (!changed_) return true;
        changed_ = false;
        const wchar_t* value = had_original_ ? original_value_.c_str() : nullptr;
        if (!SetEnvironmentVariableW(name_.c_str(), value)) {
            error_ = GetLastError();
            return false;
        }
        return true;
    }

private:
    std::wstring name_;
    std::wstring original_value_;
    bool had_original_ = false;
    bool changed_ = false;
    DWORD error_ = ERROR_SUCCESS;
};

}  // namespace

struct Player::Api {
    using New = Instance*(__cdecl*)(int, const char* const*);
    using Release = void(__cdecl*)(Instance*);
    using MediaNewLocation = Media*(__cdecl*)(Instance*, const char*);
    using MediaRelease = void(__cdecl*)(Media*);
    using MediaPlayerNewFromMedia = MediaPlayer*(__cdecl*)(Media*);
    using MediaPlayerNew = MediaPlayer*(__cdecl*)(Instance*);
    using MediaPlayerRelease = void(__cdecl*)(MediaPlayer*);
    using MediaListNew = MediaList*(__cdecl*)(Instance*);
    using MediaListRelease = void(__cdecl*)(MediaList*);
    using MediaListAddMedia = int(__cdecl*)(MediaList*, Media*);
    using MediaListPlayerNew = MediaListPlayer*(__cdecl*)(Instance*);
    using MediaListPlayerRelease = void(__cdecl*)(MediaListPlayer*);
    using MediaListPlayerSetMediaPlayer = void(__cdecl*)(MediaListPlayer*, MediaPlayer*);
    using MediaListPlayerSetMediaList = void(__cdecl*)(MediaListPlayer*, MediaList*);
    using MediaListPlayerPlay = int(__cdecl*)(MediaListPlayer*);
    using MediaListPlayerStop = void(__cdecl*)(MediaListPlayer*);
    using MediaPlayerSetVideoFormatCallbacks = void(__cdecl*)(MediaPlayer*, VideoSetup, VideoCleanup);
    using MediaPlayerSetVideoCallbacks = void(__cdecl*)(MediaPlayer*, VideoLock, VideoUnlock, VideoDisplay, void*);
    using MediaPlayerPlay = int(__cdecl*)(MediaPlayer*);
    using MediaPlayerGetState = int(__cdecl*)(MediaPlayer*);
    using MediaPlayerStop = void(__cdecl*)(MediaPlayer*);
    using MediaPlayerSetVolume = int(__cdecl*)(MediaPlayer*, int);

    New create_instance = nullptr;
    Release release_instance = nullptr;
    MediaNewLocation create_media = nullptr;
    MediaRelease release_media = nullptr;
    MediaPlayerNewFromMedia create_player = nullptr;
    MediaPlayerNew create_empty_player = nullptr;
    MediaPlayerRelease release_player = nullptr;
    MediaListNew create_media_list = nullptr;
    MediaListRelease release_media_list = nullptr;
    MediaListAddMedia add_media_to_list = nullptr;
    MediaListPlayerNew create_media_list_player = nullptr;
    MediaListPlayerRelease release_media_list_player = nullptr;
    MediaListPlayerSetMediaPlayer set_media_list_player = nullptr;
    MediaListPlayerSetMediaList set_media_list = nullptr;
    MediaListPlayerPlay play_media_list = nullptr;
    MediaListPlayerStop stop_media_list = nullptr;
    MediaPlayerSetVideoFormatCallbacks set_video_format_callbacks = nullptr;
    MediaPlayerSetVideoCallbacks set_video_callbacks = nullptr;
    MediaPlayerPlay play = nullptr;
    MediaPlayerGetState get_state = nullptr;
    MediaPlayerStop stop = nullptr;
        MediaPlayerSetVolume set_volume = nullptr;
};

Player::Player() = default;

Player::~Player() {
    Stop();
}

bool Player::Open(const std::string& url, std::string& error) {
    return OpenInternal(url, error, true);
}

bool Player::OpenAudio(const std::string& url, std::string& error) {
    return OpenInternal(url, error, false);
}

bool Player::OpenInternal(const std::string& url, std::string& error, bool video_output) {
    Stop();
    if (url.empty()) {
        error = "Empty stream URL.";
        return false;
    }

    const std::wstring directory = FindVlcDirectory();
    if (directory.empty()) {
        error = "libVLC runtime or plugins not found. Install VLC or set HIKAYE_VLC_PATH.";
        return false;
    }

    const std::wstring library_path = directory + L"\\libvlc.dll";
    library_ = LoadLibraryExW(library_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!library_) {
        error = "Failed to load libvlc.dll (Win32 " + std::to_string(GetLastError()) + ").";
        return false;
    }

    api_ = std::make_unique<Api>();
    const HMODULE library = static_cast<HMODULE>(library_);
    const bool api_ready =
        LoadFunction(library, "libvlc_new", api_->create_instance) &&
        LoadFunction(library, "libvlc_release", api_->release_instance) &&
        LoadFunction(library, "libvlc_media_new_location", api_->create_media) &&
        LoadFunction(library, "libvlc_media_release", api_->release_media) &&
        LoadFunction(library, "libvlc_media_player_new_from_media", api_->create_player) &&
        LoadFunction(library, "libvlc_media_player_new", api_->create_empty_player) &&
        LoadFunction(library, "libvlc_media_player_release", api_->release_player) &&
        LoadFunction(library, "libvlc_media_list_new", api_->create_media_list) &&
        LoadFunction(library, "libvlc_media_list_release", api_->release_media_list) &&
        LoadFunction(library, "libvlc_media_list_add_media", api_->add_media_to_list) &&
        LoadFunction(library, "libvlc_media_list_player_new", api_->create_media_list_player) &&
        LoadFunction(library, "libvlc_media_list_player_release", api_->release_media_list_player) &&
        LoadFunction(library, "libvlc_media_list_player_set_media_player", api_->set_media_list_player) &&
        LoadFunction(library, "libvlc_media_list_player_set_media_list", api_->set_media_list) &&
        LoadFunction(library, "libvlc_media_list_player_play", api_->play_media_list) &&
        LoadFunction(library, "libvlc_media_list_player_stop", api_->stop_media_list) &&
        LoadFunction(library, "libvlc_video_set_format_callbacks", api_->set_video_format_callbacks) &&
        LoadFunction(library, "libvlc_video_set_callbacks", api_->set_video_callbacks) &&
        LoadFunction(library, "libvlc_media_player_play", api_->play) &&
        LoadFunction(library, "libvlc_media_player_get_state", api_->get_state) &&
                LoadFunction(library, "libvlc_media_player_stop", api_->stop) &&
                LoadFunction(library, "libvlc_audio_set_volume", api_->set_volume);
    if (!api_ready) {
        error = "Installed libVLC is missing required video-output functions.";
        Stop();
        return false;
    }

    const std::wstring plugin_directory = directory + L"\\plugins";
    std::wstring plugin_path = plugin_directory;
    wchar_t current_plugin_path[32768]{};
    SetLastError(ERROR_SUCCESS);
    const DWORD current_plugin_path_length = GetEnvironmentVariableW(
        L"VLC_PLUGIN_PATH", current_plugin_path, static_cast<DWORD>(std::size(current_plugin_path)));
    if (current_plugin_path_length > 0 && current_plugin_path_length < std::size(current_plugin_path)) {
        plugin_path += L";";
        plugin_path.append(current_plugin_path, current_plugin_path_length);
    }
    ScopedEnvironmentVariable plugin_path_environment(L"VLC_PLUGIN_PATH", plugin_path);
    if (!plugin_path_environment.IsSet()) {
        error = "Failed to set VLC_PLUGIN_PATH (Win32 " +
                std::to_string(plugin_path_environment.Error()) + ").";
        Stop();
        return false;
    }

    const char* args[] = {"--no-video-title-show", "--quiet", "--network-caching=1500"};
    instance_ = api_->create_instance(static_cast<int>(std::size(args)), args);
    const bool plugin_path_restored = plugin_path_environment.Restore();
    if (!plugin_path_restored) {
        if (instance_) {
            api_->release_instance(static_cast<Instance*>(instance_));
            instance_ = nullptr;
        }
        error = "Failed to restore VLC_PLUGIN_PATH (Win32 " +
                std::to_string(plugin_path_environment.Error()) + ").";
        Stop();
        return false;
    }
    if (!instance_) {
        error = "libVLC could not initialize its playback engine.";
        Stop();
        return false;
    }

    Media* media = api_->create_media(static_cast<Instance*>(instance_), url.c_str());
    if (!media) {
        error = "libVLC could not open the stream URL.";
        Stop();
        return false;
    }
    media_player_ = api_->create_player(media);
    api_->release_media(media);
    if (!media_player_) {
        error = "libVLC could not create a media player.";
        Stop();
        return false;
    }

    if (video_output) {
        api_->set_video_format_callbacks(static_cast<MediaPlayer*>(media_player_), SetupVideo, CleanupVideo);
        api_->set_video_callbacks(static_cast<MediaPlayer*>(media_player_), LockVideo, UnlockVideo, DisplayVideo, this);
    }
    if (api_->play(static_cast<MediaPlayer*>(media_player_)) != 0) {
        error = "libVLC failed to start playback.";
        Stop();
        return false;
    }
    return true;
}

Player::PlaybackState Player::State() const {
    if (!media_player_ || !api_ || !api_->get_state) return PlaybackState::Unknown;
    switch (api_->get_state(static_cast<MediaPlayer*>(media_player_))) {
        case 1: return PlaybackState::Opening;
        case 2: return PlaybackState::Buffering;
        case 3: return PlaybackState::Playing;
        case 4: return PlaybackState::Paused;
        case 5: return PlaybackState::Stopped;
        case 6: return PlaybackState::Ended;
        case 7: return PlaybackState::Error;
        default: return PlaybackState::Unknown;
    }
}

bool Player::SetVolume(int volume_percent) {
    if (!media_player_ || !api_ || !api_->set_volume) return false;
    const int vlc_volume = std::clamp(volume_percent, 0, 100);
    return api_->set_volume(static_cast<MediaPlayer*>(media_player_), vlc_volume) == 0;
}

bool Player::OpenPlaylist(const std::vector<std::string>& urls, std::string& error) {
    if (urls.empty()) {
        error = "Empty media playlist.";
        return false;
    }
    if (!Open(urls.front(), error) || urls.size() == 1) return media_player_ != nullptr;

    api_->stop(static_cast<MediaPlayer*>(media_player_));
    api_->release_player(static_cast<MediaPlayer*>(media_player_));
    media_player_ = api_->create_empty_player(static_cast<Instance*>(instance_));
    if (!media_player_) {
        error = "libVLC could not create a playlist media player.";
        Stop();
        return false;
    }

    media_list_ = api_->create_media_list(static_cast<Instance*>(instance_));
    media_list_player_ = api_->create_media_list_player(static_cast<Instance*>(instance_));
    if (!media_list_ || !media_list_player_) {
        error = "libVLC could not create a media playlist.";
        Stop();
        return false;
    }

    for (const auto& url : urls) {
        Media* media = api_->create_media(static_cast<Instance*>(instance_), url.c_str());
        if (!media) {
            error = "libVLC could not create a playlist item.";
            Stop();
            return false;
        }
        const int added = api_->add_media_to_list(static_cast<MediaList*>(media_list_), media);
        api_->release_media(media);
        if (added != 0) {
            error = "libVLC could not add a playlist item.";
            Stop();
            return false;
        }
    }

    api_->set_video_format_callbacks(static_cast<MediaPlayer*>(media_player_), SetupVideo, CleanupVideo);
    api_->set_video_callbacks(static_cast<MediaPlayer*>(media_player_), LockVideo, UnlockVideo, DisplayVideo, this);
    api_->set_media_list_player(static_cast<MediaListPlayer*>(media_list_player_),
                                static_cast<MediaPlayer*>(media_player_));
    api_->set_media_list(static_cast<MediaListPlayer*>(media_list_player_), static_cast<MediaList*>(media_list_));
    if (api_->play_media_list(static_cast<MediaListPlayer*>(media_list_player_)) != 0) {
        error = "libVLC failed to start playlist playback.";
        Stop();
        return false;
    }
    return true;
}

void Player::Stop() {
    if (media_list_player_ && api_) {
        api_->stop_media_list(static_cast<MediaListPlayer*>(media_list_player_));
        api_->release_media_list_player(static_cast<MediaListPlayer*>(media_list_player_));
        media_list_player_ = nullptr;
    }
    if (media_player_ && api_) {
        api_->stop(static_cast<MediaPlayer*>(media_player_));
        api_->release_player(static_cast<MediaPlayer*>(media_player_));
        media_player_ = nullptr;
    }
    if (media_list_ && api_) {
        api_->release_media_list(static_cast<MediaList*>(media_list_));
        media_list_ = nullptr;
    }
    if (instance_ && api_) {
        api_->release_instance(static_cast<Instance*>(instance_));
        instance_ = nullptr;
    }
    api_.reset();
    if (library_) {
        FreeLibrary(static_cast<HMODULE>(library_));
        library_ = nullptr;
    }
    std::lock_guard<std::mutex> lock(frame_mutex_);
    front_frame_.clear();
    write_frame_.clear();
    width_ = 0;
    height_ = 0;
    frame_version_ = 0;
}

bool Player::CopyLatestFrame(std::vector<std::uint8_t>& pixels, unsigned& width, unsigned& height,
                             std::uint64_t& version) {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (frame_version_ == 0 || frame_version_ == version || front_frame_.empty()) return false;
    pixels = front_frame_;
    width = width_;
    height = height_;
    version = frame_version_;
    return true;
}

unsigned __cdecl Player::SetupVideo(void** opaque, char* chroma, unsigned* width, unsigned* height,
                                    unsigned* pitches, unsigned* lines) {
    if (!opaque || !*opaque || !chroma || !width || !height || !pitches || !lines) return 0;
    chroma[0] = 'R';
    chroma[1] = 'V';
    chroma[2] = '3';
    chroma[3] = '2';
    return static_cast<Player*>(*opaque)->Setup(width, height, pitches, lines);
}

void __cdecl Player::CleanupVideo(void*) {}

void* __cdecl Player::LockVideo(void* opaque, void** planes) {
    if (!opaque || !planes) return nullptr;
    return static_cast<Player*>(opaque)->Lock(planes);
}

void __cdecl Player::UnlockVideo(void* opaque, void*, void* const*) {
    if (opaque) static_cast<Player*>(opaque)->Unlock();
}

void __cdecl Player::DisplayVideo(void* opaque, void*) {
    if (opaque) static_cast<Player*>(opaque)->Display();
}

unsigned Player::Setup(unsigned* width, unsigned* height, unsigned* pitches, unsigned* lines) {
    if (!width || !height || *width == 0 || *height == 0) return 0;

    const float scale = std::min({1.0f, static_cast<float>(kMaxFrameWidth) / *width,
                                  static_cast<float>(kMaxFrameHeight) / *height});
    *width = std::max(1u, static_cast<unsigned>(*width * scale));
    *height = std::max(1u, static_cast<unsigned>(*height * scale));
    *pitches = *width * 4;
    *lines = *height;

    std::lock_guard<std::mutex> lock(frame_mutex_);
    width_ = *width;
    height_ = *height;
    const size_t size = static_cast<size_t>(width_) * height_ * 4;
    front_frame_.assign(size, 0);
    write_frame_.assign(size, 0);
    frame_version_ = 0;
    return 1;
}

void* Player::Lock(void** planes) {
    frame_mutex_.lock();
    if (!planes || write_frame_.empty()) {
        frame_mutex_.unlock();
        return nullptr;
    }
    planes[0] = write_frame_.data();
    return this;
}

void Player::Unlock() {
    frame_mutex_.unlock();
}

void Player::Display() {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    front_frame_.swap(write_frame_);
    frame_version_ = ++g_frame_version;
}

}  // namespace hikaye::vlc
