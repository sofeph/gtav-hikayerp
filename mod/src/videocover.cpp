#include "videocover.h"

#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")

namespace hikaye::videocover {

namespace {
std::atomic<bool> g_stop{false}, g_running{false}, g_has_frame{false};
std::thread g_thread;
std::mutex g_mu;
std::vector<uint8_t> g_frame;
int g_fw = 0, g_fh = 0;
bool g_fresh = false;
std::wstring g_intro, g_loop;

template <class T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// waveOut ses kuyruğu: her ses örneği kendi tamponuyla yazılır, çalınanlar temizlenir
struct Audio {
    HWAVEOUT h = nullptr;
    std::deque<WAVEHDR*> q;
    bool Open(int channels, int rate, int bits) {
        WAVEFORMATEX wf{};
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = static_cast<WORD>(channels);
        wf.nSamplesPerSec = static_cast<DWORD>(rate);
        wf.wBitsPerSample = static_cast<WORD>(bits);
        wf.nBlockAlign = static_cast<WORD>(wf.nChannels * wf.wBitsPerSample / 8);
        wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
        return waveOutOpen(&h, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
    }
    void Reap(bool all) {
        while (!q.empty()) {
            WAVEHDR* w = q.front();
            if (!all && !(w->dwFlags & WHDR_DONE)) break;
            waveOutUnprepareHeader(h, w, sizeof(WAVEHDR));
            delete[] w->lpData;
            delete w;
            q.pop_front();
        }
    }
    void Write(const BYTE* data, DWORD len) {
        if (!h || len == 0) return;
        Reap(false);
        WAVEHDR* w = new WAVEHDR{};
        w->lpData = new char[len];
        std::memcpy(w->lpData, data, len);
        w->dwBufferLength = len;
        if (waveOutPrepareHeader(h, w, sizeof(WAVEHDR)) != MMSYSERR_NOERROR || waveOutWrite(h, w, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            delete[] w->lpData;
            delete w;
            return;
        }
        q.push_back(w);
    }
    void Close(bool wait) {
        if (!h) return;
        if (!wait) waveOutReset(h);
        else {  // intro biterken kalan sesin çalmasını bekle (en fazla 2 sn)
            for (int i = 0; i < 40 && !q.empty() && !g_stop.load(); ++i) {
                Reap(false);
                Sleep(50);
            }
            waveOutReset(h);
        }
        Reap(true);
        waveOutClose(h);
        h = nullptr;
    }
};

// Bir dosyayı baştan sona oynatır (gerçek zamanlı hızda). Dönüş: false = dosya açılamadı
bool PlayFile(const std::wstring& path) {
    IMFByteStream* bs = nullptr;
    IMFAttributes* attr = nullptr;
    IMFSourceReader* rd = nullptr;
    IMFMediaType* mt = nullptr;
    IMFMediaType* cur = nullptr;
    bool ok = false;
    Audio audio;
    if (FAILED(MFCreateFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST, MF_FILEFLAGS_NONE, path.c_str(), &bs))) return false;
    {
        IMFAttributes* ba = nullptr;
        if (SUCCEEDED(bs->QueryInterface(IID_PPV_ARGS(&ba)))) {
            ba->SetString(MF_BYTESTREAM_CONTENT_TYPE, L"video/mp4");  // uzantıdan bağımsız: .vid de açılır
            ba->Release();
        }
    }
    MFCreateAttributes(&attr, 1);
    attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);  // RGB32'ye dönüştürme
    if (FAILED(MFCreateSourceReaderFromByteStream(bs, attr, &rd))) {
        SafeRelease(attr);
        SafeRelease(bs);
        return false;
    }
    // Video: RGB32
    MFCreateMediaType(&mt);
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(rd->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, mt))) {
        SafeRelease(mt);
        SafeRelease(rd);
        SafeRelease(attr);
        SafeRelease(bs);
        return false;
    }
    SafeRelease(mt);
    UINT32 w = 0, h = 0;
    if (SUCCEEDED(rd->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &cur))) {
        MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &w, &h);
        SafeRelease(cur);
    }
    if (w == 0 || h == 0) {
        SafeRelease(rd);
        SafeRelease(attr);
        SafeRelease(bs);
        return false;
    }
    // Ses: 16 bit PCM
    MFCreateMediaType(&mt);
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    mt->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    mt->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (SUCCEEDED(rd->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, mt)) &&
        SUCCEEDED(rd->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &cur))) {
        const UINT32 ch = MFGetAttributeUINT32(cur, MF_MT_AUDIO_NUM_CHANNELS, 2), rate = MFGetAttributeUINT32(cur, MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
        audio.Open(static_cast<int>(ch), static_cast<int>(rate), 16);
        SafeRelease(cur);
    }
    SafeRelease(mt);

    // Gerçek akış numaraları (ANY_STREAM ile gelen örneklerde sembolik FIRST_* sabitleri değil, 0,1,.. numarası döner)
    DWORD vstream = 0xFFFFFFFF, astream = 0xFFFFFFFF;
    for (DWORD i = 0; i < 8; ++i) {
        IMFMediaType* st = nullptr;
        if (FAILED(rd->GetCurrentMediaType(i, &st)) || !st) break;
        GUID major{};
        if (SUCCEEDED(st->GetGUID(MF_MT_MAJOR_TYPE, &major))) {
            if (major == MFMediaType_Video && vstream == 0xFFFFFFFF) vstream = i;
            else if (major == MFMediaType_Audio && astream == 0xFFFFFFFF) astream = i;
        }
        st->Release();
    }
    const auto start = std::chrono::steady_clock::now();
    LONGLONG base = -1;
    ok = true;
    std::vector<uint8_t> tmp(static_cast<size_t>(w) * h * 4);
    while (!g_stop.load()) {
        DWORD stream = 0, flags = 0;
        LONGLONG ts = 0;
        IMFSample* sample = nullptr;
        if (FAILED(rd->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_ANY_STREAM), 0, &stream, &flags, &ts, &sample))) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            SafeRelease(sample);
            break;
        }
        if (!sample) continue;
        if (base < 0) base = ts;
        // örneğin zamanını bekle (video ve ses aynı saate bağlı)
        const auto due = start + std::chrono::nanoseconds((ts - base) * 100);
        while (!g_stop.load() && std::chrono::steady_clock::now() < due) Sleep(2);
        if (g_stop.load()) {
            SafeRelease(sample);
            break;
        }
        IMFMediaBuffer* buf = nullptr;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && buf) {
            if (stream == vstream) {
                // Video karesi: 2B tampon (satır adımı eksi olabilir: alttan üste); yoksa düz tampon
                IMF2DBuffer* b2 = nullptr;
                BYTE* scan0 = nullptr;
                LONG pitch = 0;
                bool got = false;
                if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&b2))) && b2) {
                    if (SUCCEEDED(b2->Lock2D(&scan0, &pitch))) {
                        for (UINT32 y = 0; y < h; ++y) std::memcpy(tmp.data() + static_cast<size_t>(y) * w * 4, scan0 + static_cast<LONGLONG>(y) * pitch, static_cast<size_t>(w) * 4);
                        b2->Unlock2D();
                        got = true;
                    }
                    b2->Release();
                }
                if (!got) {
                    BYTE* data = nullptr;
                    DWORD maxlen = 0, curlen = 0;
                    if (SUCCEEDED(buf->Lock(&data, &maxlen, &curlen)) && curlen >= static_cast<DWORD>(w) * h * 4) {
                        for (UINT32 y = 0; y < h; ++y) std::memcpy(tmp.data() + static_cast<size_t>(y) * w * 4, data + static_cast<size_t>(y) * w * 4, static_cast<size_t>(w) * 4);  // düz tampon: kaynak okuyucu RGB32'yi üstten alta verir (testle doğrulandı)
                        buf->Unlock();
                        got = true;
                    }
                }
                if (got) {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_frame = tmp;
                    g_fw = static_cast<int>(w);
                    g_fh = static_cast<int>(h);
                    g_fresh = true;
                    g_has_frame = true;
                }
            } else if (stream == astream && audio.h) {
                BYTE* data = nullptr;
                DWORD maxlen = 0, curlen = 0;
                if (SUCCEEDED(buf->Lock(&data, &maxlen, &curlen))) {
                    audio.Write(data, curlen);
                    buf->Unlock();
                }
            }
            buf->Release();
        }
        SafeRelease(sample);
    }
    audio.Close(!g_stop.load());
    SafeRelease(rd);
    SafeRelease(attr);
    SafeRelease(bs);
    return ok;
}

void Run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(MFStartup(MF_VERSION))) {
        if (!g_stop.load() && !PlayFile(g_intro)) {  // intro açılamazsa doğrudan loop
            Sleep(10);
        }
        while (!g_stop.load()) {
            if (!PlayFile(g_loop)) break;  // loop açılamazsa kapak resme döner
        }
        MFShutdown();
    }
    CoUninitialize();
    g_running = false;
}
}  // namespace

bool Start(const wchar_t* intro, const wchar_t* loop) {
    if (g_running.load()) return true;
    g_intro = intro;
    g_loop = loop;
    g_stop = false;
    g_has_frame = false;
    g_fresh = false;
    g_running = true;
    try {
        g_thread = std::thread(Run);
    } catch (...) {
        g_running = false;
        return false;
    }
    return true;
}

void Stop() {
    g_stop = true;
    if (g_thread.joinable()) g_thread.join();
    g_running = false;
    std::lock_guard<std::mutex> lk(g_mu);
    g_frame.clear();
    g_fresh = false;
}

bool Running() { return g_running.load(); }
bool HasFrame() { return g_has_frame.load(); }

bool TakeFrame(std::vector<uint8_t>& out, int& w, int& h) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_fresh) return false;
    out.swap(g_frame);
    g_frame.clear();
    w = g_fw;
    h = g_fh;
    g_fresh = false;
    return true;
}

}  // namespace hikaye::videocover
