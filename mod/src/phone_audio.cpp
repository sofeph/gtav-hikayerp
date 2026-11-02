#include "phone_audio.h"

#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "common.h"

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "ole32.lib")

extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);

namespace hikaye::phone_audio {

namespace fs = std::filesystem;

namespace {

constexpr size_t kCacheLimitBytes = 64u * 1024u * 1024u;
constexpr size_t kChannelCount = static_cast<size_t>(Channel::Count);

struct Pcm {
    std::vector<short> samples;
    int channels = 0;
    int rate = 0;
};

struct Request {
    Channel channel = Channel::Ui;
    fs::path file;  // empty = stop
    bool loop = false;
    float volume = 1.f;
};

struct Voice {
    IXAudio2SourceVoice* voice = nullptr;
    std::shared_ptr<Pcm> pcm;  // must outlive the voice: XAudio2 reads the buffer in place
};

std::once_flag g_start_once;
std::mutex g_queue_mu;
std::condition_variable g_queue_cv;
std::deque<Request> g_queue;

std::mutex g_voice_mu;
Voice g_voices[kChannelCount];

IXAudio2* g_xaudio = nullptr;
IXAudio2MasteringVoice* g_master = nullptr;
std::unordered_map<std::wstring, std::shared_ptr<Pcm>> g_cache;
size_t g_cache_bytes = 0;

std::string NormalizeDirName(std::string name) {
    // Some folders were named with Cyrillic "ѕ" (U+0455, UTF-8 D1 95) instead of a Latin "s".
    for (size_t p; (p = name.find("\xD1\x95")) != std::string::npos;) name.replace(p, 2, "s");
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name;
}

fs::path CategoryDir(const std::string& category) {
    const fs::path root = fs::u8path(std::string(kBaseDir) + "/data/phone_sfx");
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (entry.is_directory(ec) && NormalizeDirName(entry.path().filename().u8string()) == category) return entry.path();
    }
    return {};
}

std::shared_ptr<Pcm> Decode(const fs::path& file) {
    const auto cached = g_cache.find(file.wstring());
    if (cached != g_cache.end()) return cached->second;

    std::ifstream in(file, std::ios::binary);
    if (!in) return nullptr;
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    int channels = 0, rate = 0;
    short* out = nullptr;
    const int frames = stb_vorbis_decode_memory(bytes.data(), static_cast<int>(bytes.size()), &channels, &rate, &out);
    if (frames <= 0 || !out || channels <= 0) {
        if (out) std::free(out);
        Log("telefon sesi cozulemedi: " + file.u8string());
        return nullptr;
    }
    auto pcm = std::make_shared<Pcm>();
    pcm->samples.assign(out, out + static_cast<size_t>(frames) * static_cast<size_t>(channels));
    pcm->channels = channels;
    pcm->rate = rate;
    std::free(out);

    const size_t bytes_used = pcm->samples.size() * sizeof(short);
    if (g_cache_bytes + bytes_used > kCacheLimitBytes) {
        g_cache.clear();
        g_cache_bytes = 0;
    }
    g_cache[file.wstring()] = pcm;
    g_cache_bytes += bytes_used;
    return pcm;
}

void DestroyChannel(size_t index) {
    std::lock_guard<std::mutex> lock(g_voice_mu);
    Voice& v = g_voices[index];
    if (v.voice) v.voice->DestroyVoice();
    v.voice = nullptr;
    v.pcm.reset();
}

void Handle(const Request& r) {
    const size_t index = static_cast<size_t>(r.channel);
    DestroyChannel(index);
    if (r.file.empty() || !g_xaudio) return;

    std::shared_ptr<Pcm> pcm = Decode(r.file);
    if (!pcm) return;

    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = static_cast<WORD>(pcm->channels);
    wfx.nSamplesPerSec = static_cast<DWORD>(pcm->rate);
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = static_cast<WORD>(pcm->channels * 2);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    IXAudio2SourceVoice* voice = nullptr;
    if (FAILED(g_xaudio->CreateSourceVoice(&voice, &wfx))) return;
    XAUDIO2_BUFFER buffer{};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = static_cast<UINT32>(pcm->samples.size() * sizeof(short));
    buffer.pAudioData = reinterpret_cast<const BYTE*>(pcm->samples.data());
    buffer.LoopCount = r.loop ? XAUDIO2_LOOP_INFINITE : 0;
    if (FAILED(voice->SubmitSourceBuffer(&buffer))) {
        voice->DestroyVoice();
        return;
    }
    voice->SetVolume(std::clamp(r.volume, 0.f, 1.f));
    voice->Start(0);

    std::lock_guard<std::mutex> lock(g_voice_mu);
    g_voices[index].voice = voice;
    g_voices[index].pcm = std::move(pcm);
}

void Worker() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(XAudio2Create(&g_xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR)) ||
        FAILED(g_xaudio->CreateMasteringVoice(&g_master))) {
        Log("XAudio2 baslatilamadi: telefon sesleri calmayacak");
        if (g_xaudio) g_xaudio->Release();
        g_xaudio = nullptr;
    }
    for (;;) {
        Request r;
        {
            std::unique_lock<std::mutex> lock(g_queue_mu);
            g_queue_cv.wait(lock, [] { return !g_queue.empty(); });
            r = std::move(g_queue.front());
            g_queue.pop_front();
        }
        Handle(r);
    }
}

// The worker is intentionally never joined: joining during DLL unload can deadlock on the loader lock.
void Enqueue(Request r) {
    std::call_once(g_start_once, [] { std::thread(Worker).detach(); });
    {
        std::lock_guard<std::mutex> lock(g_queue_mu);
        g_queue.push_back(std::move(r));
    }
    g_queue_cv.notify_one();
}

}  // namespace

std::vector<std::string> List(const std::string& category) {
    std::vector<std::string> names;
    const fs::path dir = CategoryDir(category);
    if (dir.empty()) return names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = entry.path().extension().u8string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".ogg") names.push_back(entry.path().stem().u8string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string Label(const std::string& name) {
    std::string s = name.rfind("S_", 0) == 0 ? name.substr(2) : name;
    std::replace(s.begin(), s.end(), '_', ' ');
    return s;
}

void Play(Channel channel, const std::string& category, const std::string& name, bool loop, float volume) {
    if (name.empty() || volume <= 0.f) {
        Stop(channel);
        return;
    }
    const fs::path dir = CategoryDir(category);
    if (dir.empty()) return;
    const fs::path file = dir / fs::u8path(name + ".ogg");
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return;
    Enqueue({channel, file, loop, volume});
}

void Stop(Channel channel) { Enqueue({channel, {}, false, 0.f}); }

bool IsPlaying(Channel channel) {
    std::lock_guard<std::mutex> lock(g_voice_mu);
    IXAudio2SourceVoice* v = g_voices[static_cast<size_t>(channel)].voice;
    if (!v) return false;
    XAUDIO2_VOICE_STATE st{};
    v->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return st.BuffersQueued > 0;
}

void Shutdown() {
    for (size_t i = 0; i < kChannelCount; ++i) Stop(static_cast<Channel>(i));
}

}  // namespace hikaye::phone_audio
