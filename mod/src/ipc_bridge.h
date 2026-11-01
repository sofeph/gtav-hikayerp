#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

namespace hikaye::ipc {

constexpr int kMaxFrameWidth = 1920;
constexpr int kMaxFrameHeight = 1080;
constexpr uint16_t kFramePort = 27015;      // Frame data port
constexpr uint16_t kCommandPort = 27016;    // Command port
constexpr const char* kLocalHost = "127.0.0.1";

struct Frame {
    std::shared_ptr<const std::vector<uint8_t>> pixels;
    int width = 0;
    int height = 0;
    uint64_t revision = 0;
    uint64_t timestamp = 0;
};

// Frame header sent over socket (packed for network)
#pragma pack(push, 1)
struct FrameHeader {
    uint32_t magic;        // 0x48545250 (HTRP)
    uint32_t version;      // 1
    uint64_t revision;     // Frame revision
    uint64_t timestamp;    // Timestamp
    uint32_t width;        // Frame width
    uint32_t height;       // Frame height
    uint32_t stride;       // Row stride (width * 4)
    uint32_t payload_size; // Size of pixel data following header
};
#pragma pack(pop)

constexpr uint32_t kFrameMagic = 0x48545250;  // HTRP
constexpr uint32_t kFrameVersion = 1;
constexpr size_t kMaxFramePayload = static_cast<size_t>(kMaxFrameWidth) * kMaxFrameHeight * 4;

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
    ~IPCBridge();

    // Socket handles
    SOCKET frame_socket_ = INVALID_SOCKET;
    SOCKET command_socket_ = INVALID_SOCKET;
    
    // Frame buffer
    std::mutex frame_mutex_;
    Frame last_frame_;
    uint64_t last_revision_ = 0;
    std::vector<uint8_t> frame_buffer_;
    
    // Connection state
    std::atomic<bool> connected_{false};
    std::atomic<bool> shutting_down_{false};
    std::string last_error_;
    std::mutex error_mutex_;
    
    // Background frame receiver thread
    std::thread frame_receiver_thread_;
    
    // Helper methods
    bool ConnectFrameSocket(std::string& error);
    bool ConnectCommandSocket(std::string& error);
    void FrameReceiverLoop();
    bool SendCommand(const std::string& command);
    bool RecvAll(SOCKET sock, void* buf, size_t len);
    bool SendAll(SOCKET sock, const void* buf, size_t len);
    void SetLastError(const std::string& error);
};

}  // namespace hikaye::ipc
