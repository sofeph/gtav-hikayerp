#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cef_browser.h"
#include "ipc_bridge.h"

namespace {

std::atomic<bool> g_stop{false};
std::atomic<uint64_t> g_published_revision{0};
HANDLE g_mapping = nullptr;
void* g_view = nullptr;
std::mutex g_log_mutex;

void Log(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    wchar_t runtime[32768]{};
    const DWORD size = GetEnvironmentVariableW(L"HIKAYE_CEF_DIR", runtime,
                                                static_cast<DWORD>(std::size(runtime)));
    if (size == 0 || size >= std::size(runtime)) return;
    std::wstring path(runtime, size);
    path += L"\\browser-host.log";
    std::ofstream out(path, std::ios::app);
    if (out) out << message << '\n';
}

std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    for (;;) {
        const size_t tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab == std::string::npos ? tab : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return fields;
}

int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

std::string Decode(const std::string& text) {
    std::string decoded;
    decoded.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = Hex(text[i + 1]);
            const int lo = Hex(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                decoded.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        decoded.push_back(text[i]);
    }
    return decoded;
}

void HandleCommand(const std::string& line) {
    const auto fields = SplitTabs(line);
    if (fields.empty()) return;
    if (fields[0] == "STOP") {
        g_stop = true;
    } else if (fields[0] == "OPEN" && fields.size() == 4) {
        std::string error;
        hikaye::cef::Open(Decode(fields[1]), std::stoi(fields[2]), std::stoi(fields[3]), error);
    } else if (fields[0] == "LOAD" && fields.size() == 2) {
        hikaye::cef::LoadUrl(Decode(fields[1]));
    } else if (fields[0] == "CLOSE") {
        hikaye::cef::Close();
    } else if (fields[0] == "BACK") {
        hikaye::cef::GoBack();
    } else if (fields[0] == "JS" && fields.size() == 2) {
        hikaye::cef::ExecuteJavaScript(Decode(fields[1]));
    } else if (fields[0] == "MOVE" && fields.size() == 5) {
        hikaye::cef::MouseMove(std::stoi(fields[1]), std::stoi(fields[2]),
                               fields[3] == "1", fields[4] == "1");
    } else if (fields[0] == "CLICK" && fields.size() == 5) {
        hikaye::cef::MouseClick(std::stoi(fields[1]), std::stoi(fields[2]),
                                fields[3] == "1", fields[4] == "1");
    } else if (fields[0] == "KEY" && fields.size() == 3) {
        hikaye::cef::SendKey(std::stoi(fields[1]), fields[2] != "1");
    } else if (fields[0] == "CHAR" && fields.size() == 2) {
        hikaye::cef::SendCharacter(static_cast<wchar_t>(std::stoul(fields[1])));
    }
}

void PipeServer() {
    while (!g_stop) {
        HANDLE pipe = CreateNamedPipeW(
            hikaye::ipc::kCommandPipeName, PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 0, 4096, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(250);
            continue;
        }
        const BOOL connected = ConnectNamedPipe(pipe, nullptr) ||
                               GetLastError() == ERROR_PIPE_CONNECTED;
        if (connected) {
            std::string message;
            char buffer[1024];
            DWORD read = 0;
            while (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
                const auto end = std::find(buffer, buffer + read, '\n');
                message.append(buffer, static_cast<size_t>(end - buffer));
                if (end != buffer + read || message.size() > 256 * 1024) break;
            }
            if (!message.empty() && message.size() <= 256 * 1024) {
                try {
                    HandleCommand(message);
                } catch (const std::exception& error) {
                    Log("IPC command rejected: " + std::string(error.what()));
                }
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

bool CreateFrameMapping() {
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                   static_cast<DWORD>(hikaye::ipc::kSharedMappingBytes >> 32),
                                   static_cast<DWORD>(hikaye::ipc::kSharedMappingBytes & 0xffffffff),
                                   hikaye::ipc::kFrameMappingName);
    if (!g_mapping) return false;
    g_view = MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                           hikaye::ipc::kSharedMappingBytes);
    if (!g_view) return false;
    std::memset(g_view, 0, hikaye::ipc::kSharedMappingBytes);
    auto* header = static_cast<hikaye::ipc::SharedFrameHeader*>(g_view);
    header->magic = hikaye::ipc::kSharedFrameMagic;
    header->version = hikaye::ipc::kSharedFrameVersion;
    MemoryBarrier();
    return true;
}

void PublishLatestFrame() {
    const hikaye::cef::Frame frame = hikaye::cef::LatestFrame();
    if (!frame.pixels || frame.revision == g_published_revision ||
        frame.width <= 0 || frame.width > hikaye::ipc::kMaxFrameWidth ||
        frame.height <= 0 || frame.height > hikaye::ipc::kMaxFrameHeight) return;

    auto* header = static_cast<hikaye::ipc::SharedFrameHeader*>(g_view);
    const long active = InterlockedCompareExchange(&header->active_slot, 0, 0);
    const long back = active == 0 ? 1 : 0;
    const long long current = InterlockedCompareExchange64(&header->publish_sequence, 0, 0);
    const long long writing = (current + 1) * 2 + 1;
    InterlockedExchange64(&header->slot_sequence[back], writing);

    const long stride = frame.width * 4;
    const size_t bytes = static_cast<size_t>(stride) * frame.height;
    if (frame.pixels->size() < bytes) {
        InterlockedExchange64(&header->slot_sequence[back], writing + 1);
        return;
    }
    auto* destination = static_cast<uint8_t*>(g_view) + sizeof(*header) +
                       static_cast<size_t>(back) * hikaye::ipc::kSharedFrameBytes;
    std::memcpy(destination, frame.pixels->data(), bytes);
    InterlockedExchange(&header->width[back], frame.width);
    InterlockedExchange(&header->height[back], frame.height);
    InterlockedExchange(&header->stride[back], stride);
    InterlockedExchange64(&header->timestamp[back], static_cast<long long>(GetTickCount64()));
    MemoryBarrier();
    InterlockedExchange64(&header->slot_sequence[back], writing + 1);
    InterlockedExchange64(&header->publish_sequence, current + 1);
    InterlockedExchange(&header->active_slot, back);
    g_published_revision = frame.revision;
}

void WakePipeServer() {
    if (!WaitNamedPipeW(hikaye::ipc::kCommandPipeName, 100)) return;
    HANDLE pipe = CreateFileW(hikaye::ipc::kCommandPipeName, GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return;
    const char stop[] = "STOP\n";
    DWORD written = 0;
    WriteFile(pipe, stop, sizeof(stop) - 1, &written, nullptr);
    CloseHandle(pipe);
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\HikayeTurkRP.BrowserHost.v1");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return 2;
    }
    if (!CreateFrameMapping()) {
        if (g_view) UnmapViewOfFile(g_view);
        if (g_mapping) CloseHandle(g_mapping);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 3;
    }

    std::string error;
    if (!hikaye::cef::Initialize(error)) {
        Log("CEF initialization failed: " + error);
        UnmapViewOfFile(g_view);
        CloseHandle(g_mapping);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 4;
    }
    Log("CEF browser host ready.");

    std::thread pipe_thread(PipeServer);
    while (!g_stop) {
        hikaye::cef::DoMessageLoopWork();
        PublishLatestFrame();
        Sleep(8);
    }
    hikaye::cef::Close();
    for (int i = 0; i < 10; ++i) {
        hikaye::cef::DoMessageLoopWork();
        Sleep(8);
    }
    WakePipeServer();
    if (pipe_thread.joinable()) pipe_thread.join();
    hikaye::cef::Shutdown();
    Log("CEF browser host stopped.");
    UnmapViewOfFile(g_view);
    CloseHandle(g_mapping);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
