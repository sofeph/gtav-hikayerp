#include "ws_client.h"

#include <windows.h>
#include <winhttp.h>

#include <chrono>

#include "common.h"

#pragma comment(lib, "winhttp.lib")

namespace hikaye {

void WsClient::Start(const std::wstring& host, int port) {
    if (started_.exchange(true)) return;
    host_ = host;
    port_ = port;
    std::thread(&WsClient::Run, this).detach();
}

void WsClient::Send(const std::string& text) {
    if (!connected_.load(std::memory_order_relaxed)) return;
    {
        std::lock_guard<std::mutex> lock(out_mu_);
        if (out_.size() > 64) out_.pop_front();  // şişmesin
        out_.push_back(text);
    }
    out_cv_.notify_one();
}

// OYUN THREAD'İ İÇİN KRİTİK OPTİMİZASYON:
// Kuyruk boşsa mutex kilidine ASLA girmeyerek GTA V'in ana thread'ini bekletmez.
bool WsClient::Poll(std::string& out) {
    if (!has_in_items_.load(std::memory_order_relaxed)) return false;

    std::unique_lock<std::mutex> lock(in_mu_, std::try_to_lock);
    if (!lock.owns_lock() || in_.empty()) return false;

    out = std::move(in_.front());
    in_.pop_front();
    if (in_.empty()) {
        has_in_items_.store(false, std::memory_order_relaxed);
    }
    return true;
}

void WsClient::SenderLoop(void* ws_handle) {
    HINTERNET ws = static_cast<HINTERNET>(ws_handle);
    while (connected_.load(std::memory_order_relaxed)) {
        std::string msg;
        {
            std::unique_lock<std::mutex> lock(out_mu_);
            out_cv_.wait_for(lock, std::chrono::milliseconds(200), [&] {
                return !out_.empty() || !connected_.load(std::memory_order_relaxed);
            });
            if (!connected_.load(std::memory_order_relaxed)) break;
            if (out_.empty()) continue;
            msg = std::move(out_.front());
            out_.pop_front();
        }
        if (WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, msg.data(),
                                 static_cast<DWORD>(msg.size())) != NO_ERROR) {
            connected_.store(false, std::memory_order_release);
            return;
        }
    }
}

void WsClient::HandleMessage(const std::string& message) {
    // Parse JSON message from Companion
    // Expected format: {"type": "gtml_layout", "payload": {...}}
    try {
        // Simple JSON parsing for type field
        size_t type_pos = message.find("\"type\"");
        if (type_pos != std::string::npos) {
            size_t colon = message.find(':', type_pos);
            size_t quote1 = message.find('"', colon);
            size_t quote2 = message.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                std::string type = message.substr(quote1 + 1, quote2 - quote1 - 1);
                
                // Find payload
                size_t payload_pos = message.find("\"payload\"");
                if (payload_pos != std::string::npos) {
                    size_t colon2 = message.find(':', payload_pos);
                    size_t brace = message.find('{', colon2);
                    if (brace != std::string::npos) {
                        // Find matching closing brace
                        int depth = 0;
                        size_t end = brace;
                        for (size_t i = brace; i < message.size(); ++i) {
                            if (message[i] == '{') depth++;
                            else if (message[i] == '}') {
                                depth--;
                                if (depth == 0) {
                                    end = i;
                                    break;
                                }
                            }
                        }
                        std::string payload = message.substr(brace, end - brace + 1);
                        if (message_handler_) {
                            message_handler_(type, payload);
                        }
                    }
                }
            }
        }
    } catch (...) {
        // Ignore parse errors
    }
}

void WsClient::Run() {
    while (true) {
        HINTERNET session = WinHttpOpen(L"HikayeTurkRP/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        HINTERNET conn = session ? WinHttpConnect(session, host_.c_str(), static_cast<INTERNET_PORT>(port_), 0) : nullptr;
        HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", L"/", nullptr, WINHTTP_NO_REFERER,
                                                  WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : nullptr;
        HINTERNET ws = nullptr;
        if (req) {
            // Zaman aşımları: bağlanma 1s, gönderme 1s, alma 2s
            WinHttpSetTimeouts(session, 1000, 1000, 2000, 2000);
            if (WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) &&
                WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                ws = WinHttpWebSocketCompleteUpgrade(req, 0);
            }
        }

        if (ws) {
            WinHttpCloseHandle(req);
            req = nullptr;
            connected_.store(true, std::memory_order_release);
            Log("companion baglandi");
            std::thread sender(&WsClient::SenderLoop, this, static_cast<void*>(ws));

            std::string message;
            message.reserve(4096);
            char buf[8192];
            while (connected_.load(std::memory_order_relaxed)) {
                DWORD read = 0;
                WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
                DWORD err = WinHttpWebSocketReceive(ws, buf, sizeof(buf), &read, &type);
                if (err != NO_ERROR || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                    break;
                }
                message.append(buf, read);
                if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {  // Mesaj tamamlandı
                    {
                        std::lock_guard<std::mutex> lock(in_mu_);
                        if (in_.size() > 256) in_.pop_front();
                        in_.push_back(std::move(message));
                        has_in_items_.store(true, std::memory_order_relaxed);
                    }
                    if (message_handler_) {
                        HandleMessage(message);
                    }
                    message.clear();
                }
            }
            connected_.store(false, std::memory_order_release);
            out_cv_.notify_all();
            if (sender.joinable()) sender.join();
            WinHttpCloseHandle(ws);
            Log("companion baglantisi koptu");
        }

        if (req) WinHttpCloseHandle(req);
        if (conn) WinHttpCloseHandle(conn);
        if (session) WinHttpCloseHandle(session);

        {
            std::lock_guard<std::mutex> lock(out_mu_);
            out_.clear();
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));  // Yeniden bağlanma beklemesi
    }
}

}  // namespace hikaye