#include "ipc_bridge.h"

#include <windows.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <mutex>

namespace hikaye::ipc {
namespace {

constexpr uint32_t kMaxCommandBytes = 256 * 1024;
HANDLE g_mapping = nullptr;
void* g_view = nullptr;
std::mutex g_mu;
std::atomic<bool> g_shutting_down{false};
std::string g_last_error;
uint64_t g_last_revision = 0;
Frame g_last_frame;
std::array<bool, 256> g_key_was_down{};

SharedFrameHeader* Header() {
    return static_cast<SharedFrameHeader*>(g_view);
}

uint8_t* Slot(int index) {
    return static_cast<uint8_t*>(g_view) + sizeof(SharedFrameHeader) +
           static_cast<size_t>(index) * kSharedFrameBytes;
}

std::string Encode(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (const unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~' ||
            ch == '/' || ch == ':' || ch == '?' || ch == '&' || ch == '=' || ch == '#') {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[ch >> 4]);
            out.push_back(hex[ch & 0x0f]);
        }
    }
    return out;
}

bool WriteCommand(const std::string& command) {
    if (command.empty() || command.size() > kMaxCommandBytes) {
        std::lock_guard<std::mutex> lock(g_mu);
        g_last_error = "IPC komutu boş veya sınırın üzerinde.";
        return false;
    }
    if (!WaitNamedPipeW(kCommandPipeName, 0)) {
        std::lock_guard<std::mutex> lock(g_mu);
        g_last_error = "CEF host named pipe hazır değil.";
        return false;
    }
    HANDLE pipe = CreateFileW(kCommandPipeName, GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        std::lock_guard<std::mutex> lock(g_mu);
        g_last_error = "CEF host pipe açılamadı: " + std::to_string(GetLastError());
        return false;
    }
    const std::string message = command + "\n";
    DWORD written = 0;
    const BOOL ok = WriteFile(pipe, message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
    const DWORD write_error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(pipe);
    if (!ok || written != message.size()) {
        std::lock_guard<std::mutex> lock(g_mu);
        g_last_error = "CEF host pipe yazımı başarısız: " + std::to_string(write_error);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_last_error.clear();
    }
    return true;
}

}  // namespace

IPCBridge& IPCBridge::Instance() {
    static IPCBridge bridge;
    return bridge;
}

bool IPCBridge::Initialize(std::string& error) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_shutting_down) {
        error = "IPC köprüsü kapanıyor.";
        return false;
    }
    if (g_view) return true;
    g_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kFrameMappingName);
    if (!g_mapping) {
        error = "CEF host shared memory bulunamadı.";
        g_last_error = error;
        return false;
    }
    g_view = MapViewOfFile(g_mapping, FILE_MAP_READ, 0, 0, kSharedMappingBytes);
    if (!g_view) {
        const DWORD code = GetLastError();
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        error = "CEF host shared memory eşlenemedi: " + std::to_string(code);
        g_last_error = error;
        return false;
    }
    const auto* header = Header();
    if (header->magic != kSharedFrameMagic || header->version != kSharedFrameVersion) {
        UnmapViewOfFile(g_view);
        CloseHandle(g_mapping);
        g_view = nullptr;
        g_mapping = nullptr;
        error = "CEF host shared memory sürümü uyumsuz.";
        g_last_error = error;
        g_last_error = error;
        return false;
    }
    g_last_error.clear();
    return true;
}

void IPCBridge::Shutdown() {
    g_shutting_down = true;
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_view) UnmapViewOfFile(g_view);
    if (g_mapping) CloseHandle(g_mapping);
    g_view = nullptr;
    g_mapping = nullptr;
    g_last_frame = {};
    g_last_revision = 0;
}

Frame IPCBridge::LatestFrame() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_view) return {};
    auto* header = Header();
    if (header->magic != kSharedFrameMagic || header->version != kSharedFrameVersion) return {};

    for (int attempt = 0; attempt < 2; ++attempt) {
        const long slot = InterlockedCompareExchange(&header->active_slot, 0, 0);
        if (slot < 0 || slot > 1) return {};
        const long long before = InterlockedCompareExchange64(&header->slot_sequence[slot], 0, 0);
        if (before == 0 || (before & 1) != 0) return {};
        const long width = InterlockedCompareExchange(&header->width[slot], 0, 0);
        const long height = InterlockedCompareExchange(&header->height[slot], 0, 0);
        const long stride = InterlockedCompareExchange(&header->stride[slot], 0, 0);
        const long long revision = before;
        const long long timestamp = InterlockedCompareExchange64(&header->timestamp[slot], 0, 0);
        if (width <= 0 || width > kMaxFrameWidth || height <= 0 || height > kMaxFrameHeight ||
            stride < width * 4 || stride > kMaxFrameWidth * 4) return {};
        if (static_cast<uint64_t>(revision) == g_last_revision && g_last_frame.pixels) return g_last_frame;

        auto pixels = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(width) * height * 4);
        const uint8_t* source = Slot(static_cast<int>(slot));
        for (long y = 0; y < height; ++y) {
            std::copy_n(source + static_cast<size_t>(y) * stride,
                        static_cast<size_t>(width) * 4,
                        pixels->data() + static_cast<size_t>(y) * width * 4);
        }
        MemoryBarrier();
        const long long after = InterlockedCompareExchange64(&header->slot_sequence[slot], 0, 0);
        const long active_after = InterlockedCompareExchange(&header->active_slot, 0, 0);
        if (before != after || (after & 1) != 0 || active_after != slot) continue;

        g_last_revision = static_cast<uint64_t>(revision);
        g_last_frame = {std::move(pixels), static_cast<int>(width), static_cast<int>(height),
                        static_cast<uint64_t>(revision), static_cast<uint64_t>(timestamp)};
        return g_last_frame;
    }
    return {};
}

bool IPCBridge::IsConnected() {
    if (WaitNamedPipeW(kCommandPipeName, 0)) return true;
    std::lock_guard<std::mutex> lock(g_mu);
    g_last_error = "CEF host named pipe hazır değil.";
    return false;
}

std::string IPCBridge::LastError() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_last_error;
}

bool IPCBridge::Open(const std::string& url, int width, int height, std::string& error) {
    if (!Initialize(error)) return false;
    const bool ok = WriteCommand("OPEN\t" + Encode(url) + "\t" +
                                 std::to_string(std::clamp(width, 320, kMaxFrameWidth)) + "\t" +
                                 std::to_string(std::clamp(height, 200, kMaxFrameHeight)));
    if (!ok) error = LastError();
    return ok;
}

bool IPCBridge::LoadUrl(const std::string& url) { return WriteCommand("LOAD\t" + Encode(url)); }
bool IPCBridge::Close() { return WriteCommand("CLOSE"); }
bool IPCBridge::GoBack() { return WriteCommand("BACK"); }
bool IPCBridge::ExecuteJavaScript(const std::string& script) { return WriteCommand("JS\t" + Encode(script)); }

bool IPCBridge::MouseMove(int x, int y, bool left_down, bool right_down) {
    return WriteCommand("MOVE\t" + std::to_string(x) + "\t" + std::to_string(y) + "\t" +
                        (left_down ? "1" : "0") + "\t" + (right_down ? "1" : "0"));
}

bool IPCBridge::MouseClick(int x, int y, bool right_button, bool mouse_up) {
    return WriteCommand("CLICK\t" + std::to_string(x) + "\t" + std::to_string(y) + "\t" +
                        (right_button ? "1" : "0") + "\t" + (mouse_up ? "1" : "0"));
}

void IPCBridge::UpdateKeyboard() {
    for (int key = 1; key < static_cast<int>(g_key_was_down.size()); ++key) {
        if (key == 'P' || key == 'U' || key == VK_LBUTTON || key == VK_RBUTTON) continue;
        const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        if (down == g_key_was_down[static_cast<size_t>(key)]) continue;
        WriteCommand("KEY\t" + std::to_string(key) + "\t" + (down ? "1" : "0"));
        if (down && key >= ' ' && key <= 'Z') {
            BYTE state[256]{};
            if (GetKeyboardState(state)) {
                WCHAR chars[4]{};
                const int count = ToUnicodeEx(static_cast<UINT>(key),
                    MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC),
                    state, chars, 4, 0, GetKeyboardLayout(0));
                for (int i = 0; i < count; ++i) {
                    WriteCommand("CHAR\t" + std::to_string(static_cast<unsigned int>(chars[i])));
                }
            }
        }
        g_key_was_down[static_cast<size_t>(key)] = down;
    }
}

}  // namespace hikaye::ipc
