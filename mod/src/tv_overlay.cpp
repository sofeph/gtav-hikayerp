#include "tv_overlay.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include <fstream>
#include <cmath>

#include "common.h"
#include "json.hpp"
#include "overlay.h"
#include "vlc_player.h"

using json = nlohmann::json;

namespace hikaye {

namespace {

constexpr float kTvSearchRadius = 4.5f;
constexpr unsigned long long kScanIntervalMs = 500;
constexpr unsigned long long kYoutubeResolveTimeoutMs = 120000;
constexpr unsigned long long kSignalLostTimeoutMs = 5000;
constexpr size_t kMaxYoutubeResolverOutput = 1024 * 1024;
constexpr size_t kMaxYoutubePlaylistItems = 20;
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

bool IsFile(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool IsDirectory(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring AbsolutePath(const std::wstring& path) {
    const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (required == 0) return path;

    std::vector<wchar_t> buffer(static_cast<size_t>(required) + 1);
    const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()),
                                          buffer.data(), nullptr);
    if (length == 0 || length >= buffer.size()) return path;
    return std::wstring(buffer.data(), length);
}

std::wstring FindYtDlpPath() {
    std::vector<std::wstring> candidates;
    wchar_t configured[MAX_PATH]{};
    const DWORD configured_length = GetEnvironmentVariableW(L"HIKAYE_YTDLP_PATH", configured, MAX_PATH);
    if (configured_length > 0 && configured_length < MAX_PATH) {
        std::wstring path(configured, configured_length);
        if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"') {
            path = path.substr(1, path.size() - 2);
        }
        if (IsDirectory(path)) path += L"\\yt-dlp.exe";
        if (!path.empty()) candidates.push_back(path);
    }

    candidates.push_back(L"HikayeTurkRP\\tools\\yt-dlp.exe");
    candidates.push_back(L"tools\\yt-dlp.exe");
    candidates.push_back(L"yt-dlp.exe");
    for (const auto& candidate : candidates) {
        if (IsFile(candidate)) return candidate;
    }
    return {};
}

std::wstring FindNodePath() {
    std::vector<std::wstring> candidates;
    wchar_t configured[MAX_PATH]{};
    const DWORD configured_length = GetEnvironmentVariableW(L"HIKAYE_NODE_PATH", configured, MAX_PATH);
    if (configured_length > 0 && configured_length < MAX_PATH) {
        std::wstring path(configured, configured_length);
        if (IsDirectory(path)) path += L"\\node.exe";
        if (!path.empty()) candidates.push_back(path);
    }

    candidates.push_back(L"HikayeTurkRP\\tools\\node.exe");
    candidates.push_back(L"tools\\node.exe");
    candidates.push_back(L"node.exe");
    for (const auto& candidate : candidates) {
        if (IsFile(candidate)) return candidate;
    }

    wchar_t path[MAX_PATH]{};
    const DWORD length = SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, path, nullptr);
    return length > 0 && length < MAX_PATH ? std::wstring(path, length) : std::wstring();
}

std::wstring FindYoutubePotProviderRoot() {
    const std::vector<std::wstring> candidates = {
        L"HikayeTurkRP\\tools\\bgutil",
        L"tools\\bgutil",
        L"bgutil"
    };
    for (const auto& candidate : candidates) {
        if (IsFile(candidate + L"\\server\\build\\main.js") &&
            IsFile(candidate + L"\\plugins\\bgutil-ytdlp-pot-provider.zip")) {
            return AbsolutePath(candidate);
        }
    }
    return {};
}

std::wstring QuoteWindowsArgument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(character);
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
                            static_cast<int>(value.size()), &result[0], size) != size) {
        return {};
    }
    return result;
}

bool IsYoutubeUrl(const std::string& url) {
    const size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return false;
    const size_t host_start = scheme_end + 3;
    const size_t host_end = url.find_first_of("/:?#", host_start);
    std::string host = url.substr(host_start, host_end == std::string::npos
                                                  ? std::string::npos
                                                  : host_end - host_start);
    const size_t userinfo_end = host.rfind('@');
    if (userinfo_end != std::string::npos) host.erase(0, userinfo_end + 1);
    const size_t port_start = host.find(':');
    if (port_start != std::string::npos) host.resize(port_start);
    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (!host.empty() && host.back() == '.') host.pop_back();
    return host == "youtu.be" || host == "youtube.com" || host == "youtube-nocookie.com" ||
           (host.size() > 12 && host.compare(host.size() - 12, 12, ".youtube.com") == 0) ||
           (host.size() > 21 && host.compare(host.size() - 21, 21, ".youtube-nocookie.com") == 0);
}

}  // namespace

TvOverlay::TvOverlay() = default;

TvOverlay::~TvOverlay() {
    CloseVlcPlayer();
    StopYoutubePotProvider();
    RestoreCameraMode();
}

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

    for (size_t i = 0; i < channels_.size(); ++i) {
        if (!channels_[i].url.empty()) {
            current_channel_idx_ = static_cast<int>(i);
            break;
        }
    }

    const std::wstring provider_root = FindYoutubePotProviderRoot();
    if (!provider_root.empty()) {
        std::string provider_error;
        if (!StartYoutubePotProvider(provider_root, provider_error)) {
            Log("TV PO Token servisi baslatilamadi: " + provider_error);
        }
    }

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
                       "  ~y~[Kanal: Sol/Sag | Kapat: Esc/E]~s~";
    if (!player_status_.empty()) text += " ~r~[" + player_status_ + "]~s~";
    ShowHelp(text.c_str());
}

void TvOverlay::NextChannel() {
    if (channels_.empty()) return;
    current_channel_idx_ = (current_channel_idx_ + 1) % static_cast<int>(channels_.size());
    UpdateVlcPlayer();
    ShowChannelInfo();
}

void TvOverlay::PrevChannel() {
    if (channels_.empty()) return;
    current_channel_idx_ = (current_channel_idx_ - 1 + static_cast<int>(channels_.size())) % static_cast<int>(channels_.size());
    UpdateVlcPlayer();
    ShowChannelInfo();
}

void TvOverlay::UpdateVlcPlayer() {
    CloseVlcPlayer();
    player_status_.clear();
    const TvChannel* ch = CurrentChannel();
    if (!ch || ch->url.empty()) {
        Log("TV: secili kanal icin yayin URL'si yok.");
        player_status_ = "yayin adresi yok";
        signal_lost_ = true;
        overlay::SetTVSignalLost(true);
        return;
    }

    if (IsYoutubeUrl(ch->url)) {
        std::string error;
        player_status_ = "YouTube cozumluyor";
        if (!StartYoutubeResolver(ch->url, error)) {
            Log("TV YouTube cozme hatasi: " + error);
            player_status_ = error.find("yt-dlp.exe not found") != std::string::npos
                                 ? "yt-dlp bulunamadi"
                                 : error.find("node.exe not found") != std::string::npos
                                       ? "Node.js bulunamadi"
                                       : "YouTube cozme hatasi";
            signal_lost_ = true;
            overlay::SetTVSignalLost(true);
        }
        return;
    }

    StartVlcPlayer(ch->url);
}

void TvOverlay::StartVlcPlayer(const std::string& url, bool preserve_youtube_playlist) {
    if (!preserve_youtube_playlist) {
        youtube_playlist_urls_.clear();
        youtube_playlist_index_ = 0;
    }

    overlay::ClearTVVideoFrame();
    last_video_version_ = 0;
    if (!vlc_player_) vlc_player_ = std::make_unique<vlc::Player>();
    std::string error;
    if (!vlc_player_->Open(url, error)) {
        Log("TV libVLC hatasi: " + error);
        player_status_ = error.find("not found") != std::string::npos ? "VLC bulunamadi" : "VLC oynatma hatasi";
        if (preserve_youtube_playlist) {
            playback_started_ = true;
            last_video_frame_at_ = GetTickCount64();
            signal_lost_ = false;
            overlay::SetTVSignalLost(false);
            return;
        }
        signal_lost_ = true;
        overlay::SetTVSignalLost(true);
        return;
    }

    player_status_.clear();
    last_video_version_ = 0;
    last_video_frame_at_ = GetTickCount64();
    playback_started_ = true;
    signal_lost_ = false;
    overlay::SetTVSignalLost(false);
    Log("TV: libVLC yayini baslatti.");
}

void TvOverlay::StartVlcPlayer(const std::vector<std::string>& urls) {
    youtube_playlist_urls_ = urls;
    youtube_playlist_index_ = 0;
    if (youtube_playlist_urls_.empty()) return;

    StartYoutubePlaylistItem(0);
}

void TvOverlay::StartYoutubePlaylistItem(size_t index) {
    if (index >= youtube_playlist_urls_.size()) return;

    youtube_playlist_index_ = index;
    StartVlcPlayer(youtube_playlist_urls_[index], true);
}

bool TvOverlay::AdvanceYoutubePlaylist() {
    if (youtube_playlist_urls_.empty() || youtube_playlist_index_ + 1 >= youtube_playlist_urls_.size()) {
        return false;
    }

    const size_t next_index = youtube_playlist_index_ + 1;
    Log("TV YouTube: video karesi gelmedi; sonraki playlist ogesine geciliyor (" +
        std::to_string(next_index + 1) + "/" + std::to_string(youtube_playlist_urls_.size()) + ").");
    StartYoutubePlaylistItem(next_index);
    return true;
}

bool TvOverlay::StartYoutubeResolver(const std::string& url, std::string& error) {
    const std::wstring executable = FindYtDlpPath();
    const std::wstring node_executable = FindNodePath();
    const std::wstring provider_root = FindYoutubePotProviderRoot();
    const std::wstring wide_url = Utf8ToWide(url);
    if (executable.empty()) {
        error = "yt-dlp.exe not found. Set HIKAYE_YTDLP_PATH or install it under tools.";
        return false;
    }
    if (node_executable.empty()) {
        error = "node.exe not found. Set HIKAYE_NODE_PATH or install the bundled runtime.";
        return false;
    }
    if (provider_root.empty()) {
        error = "YouTube PO Token provider is missing. Rebuild the package to install it.";
        return false;
    }
    if (!StartYoutubePotProvider(provider_root, error)) {
        return false;
    }
    if (wide_url.empty()) {
        error = "YouTube URL is not valid UTF-8.";
        return false;
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE output_read = nullptr;
    HANDLE output_write = nullptr;
    if (!CreatePipe(&output_read, &output_write, &security, 0)) {
        error = "Failed to create yt-dlp output pipe (Win32 " + std::to_string(GetLastError()) + ").";
        return false;
    }
    if (!SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0)) {
        error = "Failed to configure yt-dlp output pipe (Win32 " + std::to_string(GetLastError()) + ").";
        CloseHandle(output_read);
        CloseHandle(output_write);
        return false;
    }

    HANDLE null_device = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
    if (null_device == INVALID_HANDLE_VALUE) {
        error = "Failed to open NUL device (Win32 " + std::to_string(GetLastError()) + ").";
        CloseHandle(output_read);
        CloseHandle(output_write);
        return false;
    }

    std::wstring command = QuoteWindowsArgument(executable);
    command += L" --ignore-config --no-progress --js-runtimes ";
    command += QuoteWindowsArgument(L"node:" + node_executable);
    command += L" --get-url --playlist-end ";
    command += std::to_wstring(kMaxYoutubePlaylistItems);
    command += L" --plugin-dirs ";
    command += QuoteWindowsArgument(provider_root + L"\\plugins");
    command += L" --extractor-args ";
    command += QuoteWindowsArgument(L"youtube:player-client=mweb,tv,web_safari");
    command += L" --format \"best[protocol=https]/best\" --socket-timeout 8 --retries 1 --extractor-retries 1 ";
    command += QuoteWindowsArgument(wide_url);
    std::vector<wchar_t> command_line(command.begin(), command.end());
    command_line.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = null_device;
    startup.hStdOutput = output_write;
    startup.hStdError = output_write;
    PROCESS_INFORMATION process_info{};
    const BOOL created = CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process_info);
    CloseHandle(null_device);
    CloseHandle(output_write);
    if (!created) {
        error = "Failed to start yt-dlp.exe (Win32 " + std::to_string(GetLastError()) + ").";
        CloseHandle(output_read);
        return false;
    }

    CloseHandle(process_info.hThread);
    youtube_process_ = process_info.hProcess;
    youtube_output_ = output_read;
    youtube_started_at_ = GetTickCount64();
    youtube_output_buffer_.clear();
    return true;
}

bool TvOverlay::StartYoutubePotProvider(const std::wstring& provider_root, std::string& error) {
    if (youtube_pot_provider_process_) {
        HANDLE process = static_cast<HANDLE>(youtube_pot_provider_process_);
        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) return true;

        DWORD exit_code = 1;
        GetExitCodeProcess(process, &exit_code);
        CloseHandle(process);
        youtube_pot_provider_process_ = nullptr;
        Log("TV PO Token servisi kapandi (cikis kodu " + std::to_string(exit_code) + ").");
    }

    const std::wstring node_executable = FindNodePath();
    const std::wstring server_script = provider_root + L"\\server\\build\\main.js";
    const std::wstring server_directory = provider_root + L"\\server";
    if (node_executable.empty()) {
        error = "node.exe not found. Set HIKAYE_NODE_PATH or install the bundled runtime.";
        return false;
    }
    if (!IsFile(server_script)) {
        error = "YouTube PO Token provider server is missing.";
        return false;
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE null_device = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
    if (null_device == INVALID_HANDLE_VALUE) {
        error = "Failed to open NUL device for the PO Token service (Win32 " +
                std::to_string(GetLastError()) + ").";
        return false;
    }

    std::wstring command = QuoteWindowsArgument(node_executable);
    command += L" ";
    command += QuoteWindowsArgument(server_script);
    std::vector<wchar_t> command_line(command.begin(), command.end());
    command_line.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = null_device;
    startup.hStdOutput = null_device;
    startup.hStdError = null_device;
    PROCESS_INFORMATION process_info{};
    const BOOL created = CreateProcessW(node_executable.c_str(), command_line.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, server_directory.c_str(),
                                        &startup, &process_info);
    CloseHandle(null_device);
    if (!created) {
        error = "Failed to start YouTube PO Token service (Win32 " +
                std::to_string(GetLastError()) + ").";
        return false;
    }

    CloseHandle(process_info.hThread);
    youtube_pot_provider_process_ = process_info.hProcess;
    Log("TV PO Token servisi baslatildi (yalnizca localhost).");
    return true;
}

void TvOverlay::UpdateYoutubeResolver() {
    if (!youtube_process_ || !youtube_output_) return;
    HANDLE process = static_cast<HANDLE>(youtube_process_);
    HANDLE output = static_cast<HANDLE>(youtube_output_);
    std::string error;

    DWORD available = 0;
    for (;;) {
        if (!PeekNamedPipe(output, nullptr, 0, nullptr, &available, nullptr)) {
            const DWORD pipe_error = GetLastError();
            if (pipe_error == ERROR_BROKEN_PIPE) {
                available = 0;
            } else {
                error = "Failed to read yt-dlp output (Win32 " + std::to_string(pipe_error) + ").";
            }
            break;
        }
        if (available == 0) break;

        char buffer[4096];
        DWORD bytes_read = 0;
        const DWORD bytes_to_read = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
        if (!ReadFile(output, buffer, bytes_to_read, &bytes_read, nullptr)) {
            error = "Failed to read yt-dlp output (Win32 " + std::to_string(GetLastError()) + ").";
            break;
        }
        youtube_output_buffer_.append(buffer, bytes_read);
        if (youtube_output_buffer_.size() > kMaxYoutubeResolverOutput) {
            error = "yt-dlp output exceeded the allowed size.";
            break;
        }
    }

    const bool timed_out = GetTickCount64() - youtube_started_at_ > kYoutubeResolveTimeoutMs;
    const bool process_exited = WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
    if (error.empty() && timed_out && !process_exited) {
        error = "YouTube URL resolution timed out.";
    }
    if (error.empty() && !process_exited) return;

    if (!error.empty() && !process_exited) {
        TerminateProcess(process, ERROR_CANCELLED);
        WaitForSingleObject(process, 1000);
    }
    DWORD exit_code = 1;
    GetExitCodeProcess(process, &exit_code);
    CloseHandle(process);
    CloseHandle(output);
    youtube_process_ = nullptr;
    youtube_output_ = nullptr;
    youtube_started_at_ = 0;

    std::vector<std::string> media_urls;
    if (error.empty()) {
        size_t line_start = 0;
        while (line_start < youtube_output_buffer_.size()) {
            const size_t line_end = youtube_output_buffer_.find_first_of("\r\n", line_start);
            const size_t length = (line_end == std::string::npos ? youtube_output_buffer_.size() : line_end) - line_start;
            const std::string line = youtube_output_buffer_.substr(line_start, length);
            if (line.rfind("https://", 0) == 0 || line.rfind("http://", 0) == 0) {
                media_urls.push_back(line);
                if (media_urls.size() >= kMaxYoutubePlaylistItems) break;
            }
            if (line_end == std::string::npos) break;
            line_start = youtube_output_buffer_.find_first_not_of("\r\n", line_end);
            if (line_start == std::string::npos) break;
        }
        if (media_urls.empty()) {
            error = exit_code != 0
                        ? "yt-dlp failed to resolve the YouTube URL (exit code " + std::to_string(exit_code) + ")."
                        : "yt-dlp returned no playable media URL.";
        }
    }
    if (!error.empty() && media_urls.empty()) {
        const size_t diagnostic_start = youtube_output_buffer_.find("ERROR:");
        if (diagnostic_start != std::string::npos) {
            const size_t diagnostic_end = youtube_output_buffer_.find_first_of("\r\n", diagnostic_start);
            const std::string diagnostic = youtube_output_buffer_.substr(
                diagnostic_start, std::min<size_t>(diagnostic_end == std::string::npos
                                                       ? youtube_output_buffer_.size() - diagnostic_start
                                                       : diagnostic_end - diagnostic_start,
                                                   240));
            error += " " + diagnostic;
        }
    }
    youtube_output_buffer_.clear();

    if (!error.empty()) {
        Log("TV YouTube cozme hatasi: " + error);
        player_status_ = "YouTube cozme hatasi";
        signal_lost_ = true;
        overlay::SetTVSignalLost(true);
    } else {
        Log("TV: YouTube URL cozumlendi (" + std::to_string(media_urls.size()) + " video).");
        StartVlcPlayer(media_urls);
    }
    ShowChannelInfo();
}

void TvOverlay::CancelYoutubeResolver() {
    if (youtube_process_) {
        HANDLE process = static_cast<HANDLE>(youtube_process_);
        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process, ERROR_CANCELLED);
            WaitForSingleObject(process, 1000);
        }
        CloseHandle(process);
        youtube_process_ = nullptr;
    }
    if (youtube_output_) {
        CloseHandle(static_cast<HANDLE>(youtube_output_));
        youtube_output_ = nullptr;
    }
    youtube_started_at_ = 0;
    youtube_output_buffer_.clear();
}

void TvOverlay::StopYoutubePotProvider() {
    if (!youtube_pot_provider_process_) return;

    HANDLE process = static_cast<HANDLE>(youtube_pot_provider_process_);
    if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
        TerminateProcess(process, ERROR_CANCELLED);
        WaitForSingleObject(process, 1000);
    }
    CloseHandle(process);
    youtube_pot_provider_process_ = nullptr;
}

void TvOverlay::CloseVlcPlayer() {
    CancelYoutubeResolver();
    if (vlc_player_) vlc_player_->Stop();
    youtube_playlist_urls_.clear();
    youtube_playlist_index_ = 0;
    overlay::ClearTVVideoFrame();
    last_video_version_ = 0;
    last_video_frame_at_ = 0;
    playback_started_ = false;
    signal_lost_ = false;
    overlay::SetTVSignalLost(false);
    overlay::SetTVScreen(false, screen_corners_);
}

void TvOverlay::UpdateTvScreen() {
    if (!open_ || !nearest_tv_ || !ENTITY::DOES_ENTITY_EXIST(nearest_tv_)) {
        overlay::SetTVScreen(false, screen_corners_);
        return;
    }

    Vector3 minimum{}, maximum{};
    GAMEPLAY::GET_MODEL_DIMENSIONS(ENTITY::GET_ENTITY_MODEL(nearest_tv_), &minimum, &maximum);
    const float width = maximum.x - minimum.x;
    const float height = maximum.z - minimum.z;
    if (width <= 0.05f || height <= 0.05f) {
        overlay::SetTVScreen(false, screen_corners_);
        return;
    }

    const Vector3 tv_pos = ENTITY::GET_ENTITY_COORDS(nearest_tv_, false);
    const Vector3 forward = ENTITY::GET_ENTITY_FORWARD_VECTOR(nearest_tv_);
    const Vector3 player_delta = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), false);
    const float facing_dot = (player_delta.x - tv_pos.x) * forward.x + (player_delta.y - tv_pos.y) * forward.y;
    const bool player_in_front = facing_dot >= 0.f;
    const float screen_y = player_in_front ? maximum.y + 0.012f : minimum.y - 0.012f;
    const float left = (minimum.x + maximum.x - width * 0.84f) * 0.5f;
    const float right = left + width * 0.84f;
    const float bottom = minimum.z + height * 0.15f;
    const float top = maximum.z - height * 0.12f;
    const std::array<Vector3, 4> world = {
        ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(nearest_tv_, left, screen_y, top),
        ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(nearest_tv_, right, screen_y, top),
        ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(nearest_tv_, right, screen_y, bottom),
        ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(nearest_tv_, left, screen_y, bottom)
    };

    std::array<float, 8> projected{};
    for (size_t i = 0; i < world.size(); ++i) {
        if (!GRAPHICS::_WORLD3D_TO_SCREEN2D(world[i].x, world[i].y, world[i].z,
                                            &projected[i * 2], &projected[i * 2 + 1])) {
            overlay::SetTVScreen(false, screen_corners_);
            return;
        }
    }
    screen_corners_ = projected;
    overlay::SetTVScreen(true, screen_corners_);
}

void TvOverlay::UseFirstPersonCamera() {
    if (camera_mode_saved_) return;

    const Ped player = PLAYER::PLAYER_PED_ID();
    if (!ENTITY::DOES_ENTITY_EXIST(player)) return;

    camera_was_in_vehicle_ = PED::IS_PED_IN_ANY_VEHICLE(player, false);
    if (camera_was_in_vehicle_) {
        previous_camera_mode_ = CAM::GET_FOLLOW_VEHICLE_CAM_VIEW_MODE();
        CAM::SET_FOLLOW_VEHICLE_CAM_VIEW_MODE(4);
    } else {
        previous_camera_mode_ = CAM::GET_FOLLOW_PED_CAM_VIEW_MODE();
        CAM::SET_FOLLOW_PED_CAM_VIEW_MODE(4);
    }
    camera_mode_saved_ = true;
}

void TvOverlay::RestoreCameraMode() {
    if (!camera_mode_saved_) return;

    if (camera_was_in_vehicle_) {
        CAM::SET_FOLLOW_VEHICLE_CAM_VIEW_MODE(previous_camera_mode_);
    } else {
        CAM::SET_FOLLOW_PED_CAM_VIEW_MODE(previous_camera_mode_);
    }
    camera_mode_saved_ = false;
}

bool TvOverlay::WatchPhoneChannel(int channel_no) {
    const auto channel = std::find_if(channels_.begin(), channels_.end(),
                                      [channel_no](const TvChannel& value) {
                                          return value.channel_no == channel_no;
                                      });
    if (channel == channels_.end()) return false;

    if (open_) {
        open_ = false;
        RestoreCameraMode();
        overlay::SetTVScreen(false, screen_corners_);
    }
    nearest_tv_ = 0;
    phone_playback_ = true;
    current_channel_idx_ = static_cast<int>(channel - channels_.begin());
    UpdateVlcPlayer();
    return true;
}

void TvOverlay::StopPhonePlayback() {
    if (!phone_playback_) return;
    phone_playback_ = false;
    CloseVlcPlayer();
}

void TvOverlay::CloseTv() {
    if (open_) {
        open_ = false;
        ShowHelp("~b~[TV]~s~ Kapatildi.");
    }
    CloseVlcPlayer();
    RestoreCameraMode();
    phone_playback_ = false;
    overlay::SetTVScreen(false, screen_corners_);
    nearest_tv_ = 0;
}

bool TvOverlay::Update(const Vector3& player_pos, bool allow_interaction, bool e_pressed, bool p_pressed,
                       bool escape_pressed, int game_hour, int game_minute) {
    (void)game_hour;
    (void)game_minute;

    const unsigned long long now = GetTickCount64();

    // TV tara (periyodik olarak)
    if (!open_ && allow_interaction && now >= next_scan_at_) {
        nearest_tv_ = FindNearest(player_pos);
        next_scan_at_ = now + kScanIntervalMs;
    }

    // TV açıkken uzaklaşma kontrolü
    if (open_ || phone_playback_) {
        if (phone_playback_ && !open_) {
            UpdateYoutubeResolver();
            if (vlc_player_) {
                std::vector<std::uint8_t> pixels;
                unsigned width = 0, height = 0;
                if (vlc_player_->CopyLatestFrame(pixels, width, height, last_video_version_)) {
                    overlay::SetTVVideoFrame(pixels.data(), width, height);
                    last_video_frame_at_ = now;
                    if (signal_lost_) {
                        signal_lost_ = false;
                        overlay::SetTVSignalLost(false);
                    }
                }
            }
            if (playback_started_ && !signal_lost_ &&
                now - last_video_frame_at_ >= kSignalLostTimeoutMs) {
                signal_lost_ = true;
                player_status_ = "Yayin baslatildi ancak video karesi alinamadi.";
                overlay::SetTVSignalLost(true);
                Log("TV: yayin baslatildi ancak 5 saniye icinde video karesi alinmadi.");
                if (AdvanceYoutubePlaylist()) return false;
            }
            return false;
        }

        if (nearest_tv_ && ENTITY::DOES_ENTITY_EXIST(nearest_tv_)) {
            const Vector3 tv_pos = ENTITY::GET_ENTITY_COORDS(nearest_tv_, false);
            if (DistanceSquared(tv_pos, player_pos) > (kTvSearchRadius + 2.0f) * (kTvSearchRadius + 2.0f)) {
                CloseTv();
                return false;
            }
        }

        // Kapatma (ESC, E veya telefon tuşu)
        if (escape_pressed || p_pressed || e_pressed) {
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

        UpdateYoutubeResolver();
        UpdateTvScreen();
        if (vlc_player_) {
            std::vector<std::uint8_t> pixels;
            unsigned width = 0, height = 0;
            if (vlc_player_->CopyLatestFrame(pixels, width, height, last_video_version_)) {
                overlay::SetTVVideoFrame(pixels.data(), width, height);
                last_video_frame_at_ = now;
                if (signal_lost_) {
                    signal_lost_ = false;
                    overlay::SetTVSignalLost(false);
                }
            }
        }
        if (playback_started_ && !signal_lost_ &&
            now - last_video_frame_at_ >= kSignalLostTimeoutMs) {
            signal_lost_ = true;
            player_status_ = "Yayin baslatildi ancak video karesi alinamadi.";
            overlay::SetTVSignalLost(true);
            Log("TV: yayin baslatildi ancak 5 saniye icinde video karesi alinmadi.");
            AdvanceYoutubePlaylist();
        }
        return true;
    }

    // Açma (E basıldığında TV varsa)
    if (allow_interaction && e_pressed && nearest_tv_ != 0) {
        open_ = true;
        phone_playback_ = false;
        UseFirstPersonCamera();
        UpdateVlcPlayer();
        ShowChannelInfo();
        return true;
    }

    return false;
}

}  // namespace hikaye
