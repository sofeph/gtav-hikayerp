#include "ipc_bridge.h"

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <array>

#pragma comment(lib, "ws2_32.lib")

namespace hikaye::ipc {
namespace {

constexpr int kSocketTimeoutMs = 100;
constexpr int kMaxCommandBytes = 256 * 1024;
constexpr char kCommandDelimiter = '\n';

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

}  // namespace

IPCBridge& IPCBridge::Instance() {
    static IPCBridge bridge;
    return bridge;
}

IPCBridge::~IPCBridge() {
    Shutdown();
}

bool IPCBridge::Initialize(std::string& error) {
    if (shutting_down_) {
        error = "IPC köprüsü kapanıyor.";
        return false;
    }
    if (connected_) return true;

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        error = "Winsock başlatılamadı.";
        SetLastError(error);
        return false;
    }

    // Connect to frame socket
    if (!ConnectFrameSocket(error)) {
        WSACleanup();
        return false;
    }

    // Connect to command socket
    if (!ConnectCommandSocket(error)) {
        closesocket(frame_socket_);
        frame_socket_ = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    // Start frame receiver thread
    frame_receiver_thread_ = std::thread(&IPCBridge::FrameReceiverLoop, this);
    
    connected_ = true;
    last_error_.clear();
    return true;
}

void IPCBridge::Shutdown() {
    shutting_down_ = true;
    connected_ = false;

    if (frame_receiver_thread_.joinable()) {
        frame_receiver_thread_.join();
    }

    if (frame_socket_ != INVALID_SOCKET) {
        closesocket(frame_socket_);
        frame_socket_ = INVALID_SOCKET;
    }
    if (command_socket_ != INVALID_SOCKET) {
        closesocket(command_socket_);
        command_socket_ = INVALID_SOCKET;
    }

    WSACleanup();

    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        last_frame_ = {};
        last_revision_ = 0;
        frame_buffer_.clear();
    }
    shutting_down_ = false;
}

bool IPCBridge::ConnectFrameSocket(std::string& error) {
    frame_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (frame_socket_ == INVALID_SOCKET) {
        error = "Frame socket oluşturulamadı: " + std::to_string(WSAGetLastError());
        SetLastError(error);
        return false;
    }

    // Set timeout
    DWORD timeout = kSocketTimeoutMs;
    setsockopt(frame_socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(frame_socket_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

    // Disable Nagle's algorithm for low latency
    BOOL noDelay = TRUE;
    setsockopt(frame_socket_, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kFramePort);
    inet_pton(AF_INET, kLocalHost, &addr.sin_addr);

    if (connect(frame_socket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        closesocket(frame_socket_);
        frame_socket_ = INVALID_SOCKET;
        error = "Frame socket bağlanamadı (port " + std::to_string(kFramePort) + "): " + std::to_string(err);
        SetLastError(error);
        return false;
    }

    return true;
}

bool IPCBridge::ConnectCommandSocket(std::string& error) {
    command_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (command_socket_ == INVALID_SOCKET) {
        error = "Command socket oluşturulamadı: " + std::to_string(WSAGetLastError());
        SetLastError(error);
        return false;
    }

    DWORD timeout = kSocketTimeoutMs;
    setsockopt(command_socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(command_socket_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

    BOOL noDelay = TRUE;
    setsockopt(command_socket_, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kCommandPort);
    inet_pton(AF_INET, kLocalHost, &addr.sin_addr);

    if (connect(command_socket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        closesocket(command_socket_);
        command_socket_ = INVALID_SOCKET;
        error = "Command socket bağlanamadı (port " + std::to_string(kCommandPort) + "): " + std::to_string(err);
        SetLastError(error);
        return false;
    }

    return true;
}

void IPCBridge::FrameReceiverLoop() {
    frame_buffer_.resize(sizeof(FrameHeader) + kMaxFramePayload);

    while (!shutting_down_ && connected_) {
        FrameHeader header;
        if (!RecvAll(frame_socket_, &header, sizeof(header))) {
            if (shutting_down_) break;
            // Soket koptuysa durumu güncelle ve çık:
            connected_ = false;
            SetLastError("Frame soket bağlantısı kesildi.");
            break;
        }

        if (header.magic != kFrameMagic || header.version != kFrameVersion) {
            continue;
        }

        if (header.payload_size > kMaxFramePayload || header.width > kMaxFrameWidth || header.height > kMaxFrameHeight) {
            continue;
        }

        uint8_t* payload_ptr = frame_buffer_.data() + sizeof(FrameHeader);
        if (!RecvAll(frame_socket_, payload_ptr, header.payload_size)) {
            connected_ = false;
            break;
        }

        auto pixels = std::make_shared<std::vector<uint8_t>>(header.payload_size);
        std::memcpy(pixels->data(), payload_ptr, header.payload_size);

        Frame frame;
        frame.pixels = std::move(pixels);
        frame.width = static_cast<int>(header.width);
        frame.height = static_cast<int>(header.height);
        frame.revision = header.revision;
        frame.timestamp = header.timestamp;

        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            last_frame_ = std::move(frame);
            last_revision_ = header.revision;
        }
    }
}
bool IPCBridge::RecvAll(SOCKET sock, void* buf, size_t len) {
    char* ptr = static_cast<char*>(buf);
    size_t remaining = len;
    while (remaining > 0 && !shutting_down_) {
        int received = recv(sock, ptr, static_cast<int>(remaining), 0);
        if (received == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            return false;
        }
        if (received == 0) {
            return false; // Connection closed
        }
        ptr += received;
        remaining -= received;
    }
    return true;
}

bool IPCBridge::SendAll(SOCKET sock, const void* buf, size_t len) {
    const char* ptr = static_cast<const char*>(buf);
    size_t remaining = len;
    while (remaining > 0 && !shutting_down_) {
        int sent = send(sock, ptr, static_cast<int>(remaining), 0);
        if (sent == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            return false;
        }
        ptr += sent;
        remaining -= sent;
    }
    return true;
}

void IPCBridge::SetLastError(const std::string& error) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error;
}

Frame IPCBridge::LatestFrame() {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    return last_frame_;
}

bool IPCBridge::IsConnected() {
    return connected_ && frame_socket_ != INVALID_SOCKET && command_socket_ != INVALID_SOCKET;
}

std::string IPCBridge::LastError() {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

bool IPCBridge::SendCommand(const std::string& command) {
    if (!connected_ || command_socket_ == INVALID_SOCKET) {
        SetLastError("IPC bağlı değil.");
        return false;
    }
    if (command.empty() || command.size() > kMaxCommandBytes) {
        SetLastError("IPC komutu boş veya sınırın üzerinde.");
        return false;
    }

    std::string message = command + kCommandDelimiter;
    if (!SendAll(command_socket_, message.data(), message.size())) {
        SetLastError("Komut gönderilemedi.");
        return false;
    }
    return true;
}

bool IPCBridge::Open(const std::string& url, int width, int height, std::string& error) {
    if (!Initialize(error)) return false;
    const bool ok = SendCommand("OPEN\t" + Encode(url) + "\t" +
                                std::to_string(std::clamp(width, 320, kMaxFrameWidth)) + "\t" +
                                std::to_string(std::clamp(height, 200, kMaxFrameHeight)));
    if (!ok) error = LastError();
    return ok;
}

bool IPCBridge::LoadUrl(const std::string& url) {
    return SendCommand("LOAD\t" + Encode(url));
}

bool IPCBridge::Close() {
    return SendCommand("CLOSE");
}

bool IPCBridge::GoBack() {
    return SendCommand("BACK");
}

bool IPCBridge::ExecuteJavaScript(const std::string& script) {
    return SendCommand("JS\t" + Encode(script));
}

bool IPCBridge::MouseMove(int x, int y, bool left_down, bool right_down) {
    return SendCommand("MOVE\t" + std::to_string(x) + "\t" + std::to_string(y) + "\t" +
                       (left_down ? "1" : "0") + "\t" + (right_down ? "1" : "0"));
}

bool IPCBridge::MouseClick(int x, int y, bool right_button, bool mouse_up) {
    return SendCommand("CLICK\t" + std::to_string(x) + "\t" + std::to_string(y) + "\t" +
                       (right_button ? "1" : "0") + "\t" + (mouse_up ? "1" : "0"));
}

void IPCBridge::UpdateKeyboard() {
    static std::array<bool, 256> key_was_down{};
    for (int key = 1; key < 256; ++key) {
        if (key == 'P' || key == 'U' || key == VK_LBUTTON || key == VK_RBUTTON) continue;
        const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        if (down == key_was_down[static_cast<size_t>(key)]) continue;
        SendCommand("KEY\t" + std::to_string(key) + "\t" + (down ? "1" : "0"));
        if (down && key >= ' ' && key <= 'Z') {
            BYTE state[256]{};
            if (GetKeyboardState(state)) {
                WCHAR chars[4]{};
                const int count = ToUnicodeEx(static_cast<UINT>(key),
                    MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC),
                    state, chars, 4, 0, GetKeyboardLayout(0));
                for (int i = 0; i < count; ++i) {
                    SendCommand("CHAR\t" + std::to_string(static_cast<unsigned int>(chars[i])));
                }
            }
        }
        key_was_down[static_cast<size_t>(key)] = down;
    }
}

}  // namespace hikaye::ipc