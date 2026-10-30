#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace hikaye::ipc {

constexpr int kMaxFrameWidth = 1920;
constexpr int kMaxFrameHeight = 1080;
constexpr wchar_t kFrameMappingName[] = L"Local\\HikayeTurkRP.Frame.v1";
constexpr wchar_t kCommandPipeName[] = L"\\\\.\\pipe\\HikayeTurkRP.Browser.v1";

struct Frame {
    std::shared_ptr<const std::vector<uint8_t>> pixels;
    int width = 0;
    int height = 0;
    uint64_t revision = 0;
    uint64_t timestamp = 0;
};

// Fixed-width ABI shared by the ASI and the Python-managed CEF host.
struct alignas(8) SharedFrameHeader {
    uint32_t magic;
    uint32_t version;
    volatile long long publish_sequence;
    volatile long active_slot;
    volatile long long slot_sequence[2];
    volatile long width[2];
    volatile long height[2];
    volatile long stride[2];
    volatile long long timestamp[2];
};

constexpr uint32_t kSharedFrameMagic = 0x48545250;  // HTRP
constexpr uint32_t kSharedFrameVersion = 1;
constexpr size_t kSharedFrameBytes =
    static_cast<size_t>(kMaxFrameWidth) * kMaxFrameHeight * 4;
constexpr size_t kSharedMappingBytes = sizeof(SharedFrameHeader) + 2 * kSharedFrameBytes;

class IPCBridge final {
public:
    static IPCBridge& Instance();

    bool Initialize(std::string& error);
    void Shutdown();
    Frame LatestFrame();
    bool IsConnected();
    std::string LastError();

    bool Open(const std::string& url, int width, int height, std::string& error);
    bool LoadUrl(const std::string& url);
    bool Close();
    bool GoBack();
    bool ExecuteJavaScript(const std::string& script);
    bool MouseMove(int x, int y, bool left_down, bool right_down);
    bool MouseClick(int x, int y, bool right_button, bool mouse_up);
    void UpdateKeyboard();

private:
    IPCBridge() = default;
};

}  // namespace hikaye::ipc
