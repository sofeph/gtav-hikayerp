#include "cef_browser.h"

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

namespace {
constexpr uint16_t kFramePort = 27015;      // Frame data port
constexpr uint16_t kCommandPort = 27016;    // Command port
constexpr const char* kLocalHost = "127.0.0.1";

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

constexpr uint32_t kFrameMagic = 0x48545250; // HTRP
constexpr uint32_t kFrameVersion = 1;

std::atomic<bool> g_running{true};

bool SendAll(SOCKET sock, const void* buf, size_t len) {
    const char* ptr = static_cast<const char*>(buf);
    size_t remaining = len;
    while (remaining > 0) {
        int sent = send(sock, ptr, static_cast<int>(remaining), 0);
        if (sent == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) {
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

bool RecvLine(SOCKET sock, std::string& out) {
    out.clear();
    char ch;
    while (true) {
        int r = recv(sock, &ch, 1, 0);
        if (r == 0) return false; // connection closed
        if (r == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            return false;
        }
        if (ch == '\n') break;
        out.push_back(ch);
    }
    return true;
}

void HandleCommand(const std::string& line) {
    std::istringstream ss(line);
    std::string token;
    std::getline(ss, token, '\t');
    if (token == "OPEN") {
        std::string url, wstr, hstr;
        std::getline(ss, url, '\t');
        std::getline(ss, wstr, '\t');
        std::getline(ss, hstr, '\t');
        int w = std::stoi(wstr);
        int h = std::stoi(hstr);
        std::string err;
        hikaye::cef::Open(url, w, h, err);
    } else if (token == "LOAD") {
        std::string url; std::getline(ss, url, '\t');
        hikaye::cef::LoadUrl(url);
    } else if (token == "CLOSE") {
        hikaye::cef::Close();
    } else if (token == "BACK") {
        hikaye::cef::GoBack();
    } else if (token == "JS") {
        std::string script; std::getline(ss, script, '\t');
        hikaye::cef::ExecuteJavaScript(script);
    } else if (token == "MOVE") {
        std::string xs, ys, ld, rd;
        std::getline(ss, xs, '\t'); std::getline(ss, ys, '\t');
        std::getline(ss, ld, '\t'); std::getline(ss, rd, '\t');
        hikaye::cef::MouseMove(std::stoi(xs), std::stoi(ys), ld == "1", rd == "1");
    } else if (token == "CLICK") {
        std::string xs, ys, rb, up;
        std::getline(ss, xs, '\t'); std::getline(ss, ys, '\t');
        std::getline(ss, rb, '\t'); std::getline(ss, up, '\t');
        hikaye::cef::MouseClick(std::stoi(xs), std::stoi(ys), rb == "1", up == "1");
    } else if (token == "KEY") {
        std::string k, down;
        std::getline(ss, k, '\t'); std::getline(ss, down, '\t');
        hikaye::cef::SendKey(std::stoi(k), down != "1");
    } else if (token == "CHAR") {
        std::string code; std::getline(ss, code, '\t');
        wchar_t ch = static_cast<wchar_t>(std::stoul(code));
        hikaye::cef::SendCharacter(ch);
    } else if (token == "STOP") {
        g_running = false;
    }
    // ignore unknown commands
}

void CommandListenerThread() {
    OutputDebugStringA("Command listener starting\n");
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        OutputDebugStringA("WSAStartup failed for command listener\n");
        return;
    }
    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        OutputDebugStringA("Command socket creation failed\n");
        return;
    }
    // Allow address reuse
    BOOL reuse = TRUE;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kCommandPort);
    inet_pton(AF_INET, kLocalHost, &addr.sin_addr);
    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        OutputDebugStringA("Command socket bind failed\n");
        closesocket(listenSock);
        return;
    }
    if (listen(listenSock, 1) == SOCKET_ERROR) {
        OutputDebugStringA("Command socket listen failed\n");
        closesocket(listenSock);
        return;
    }
    OutputDebugStringA("Command listener ready on port\n");
    SOCKET client = accept(listenSock, nullptr, nullptr);
    closesocket(listenSock);
    if (client == INVALID_SOCKET) {
        OutputDebugStringA("Command socket accept failed\n");
        return;
    }
    // set non‑blocking for graceful shutdown
    u_long mode = 1; ioctlsocket(client, FIONBIO, &mode);
    std::string line;
    while (g_running && RecvLine(client, line)) {
        HandleCommand(line);
    }
    closesocket(client);
    WSACleanup();
    OutputDebugStringA("Command listener exiting\n");
}

void FrameSenderThread() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;
    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) return;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kFramePort);
    inet_pton(AF_INET, kLocalHost, &addr.sin_addr);
    bind(listenSock, (sockaddr*)&addr, sizeof(addr));
    listen(listenSock, 1);
    SOCKET client = accept(listenSock, nullptr, nullptr);
    closesocket(listenSock);
    if (client == INVALID_SOCKET) return;
    u_long mode = 1; ioctlsocket(client, FIONBIO, &mode);
    uint64_t last_rev = 0;
    while (g_running) {
        // Run CEF message loop work each iteration
        hikaye::cef::DoMessageLoopWork();
        auto frame = hikaye::cef::LatestFrame();
        if (frame.revision != last_rev && frame.pixels && frame.width && frame.height) {
            FrameHeader hdr{};
            hdr.magic = kFrameMagic;
            hdr.version = kFrameVersion;
            hdr.revision = frame.revision;
            hdr.timestamp = 0; // no timestamp available
            hdr.width = static_cast<uint32_t>(frame.width);
            hdr.height = static_cast<uint32_t>(frame.height);
            hdr.stride = hdr.width * 4;
            hdr.payload_size = static_cast<uint32_t>(frame.pixels->size());
            if (!SendAll(client, &hdr, sizeof(hdr))) break;
            if (!SendAll(client, frame.pixels->data(), hdr.payload_size)) break;
            last_rev = frame.revision;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    closesocket(client);
    WSACleanup();
}

} // anonymous namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Initialize CEF
    std::string err;
        OutputDebugStringA("WinMain: Starting CEF initialization\n");
        if (!hikaye::cef::Initialize(err)) {
            OutputDebugStringA(("CEF Init failed: " + err + "\n").c_str());
            MessageBoxA(nullptr, err.c_str(), "CEF Init Error", MB_ICONERROR);
            return 1;
        }
        OutputDebugStringA("WinMain: CEF initialized, starting threads\n");
        // Start command and frame threads
        OutputDebugStringA("WinMain: Creating command thread\n");
        std::thread cmdThread(CommandListenerThread);
        OutputDebugStringA("WinMain: Creating frame thread\n");
        std::thread frameThread(FrameSenderThread);
        OutputDebugStringA("WinMain: Threads started, waiting for command thread\n");
        // Wait for termination
        cmdThread.join();
        OutputDebugStringA("WinMain: Command thread joined\n");
        g_running = false; // ensure frame thread exits
        frameThread.join();
        OutputDebugStringA("WinMain: Frame thread joined\n");
        // Shutdown CEF
        hikaye::cef::Shutdown();
        OutputDebugStringA("WinMain: CEF shutdown complete\n");
        return 0;
}
