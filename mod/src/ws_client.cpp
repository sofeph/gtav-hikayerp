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
    // Thread detach edilir: DLL_PROCESS_DETACH sırasında join etmek loader lock'ta kilitlenir.
    std::thread(&WsClient::Run, this).detach();
}

void WsClient::Send(const std::string& text) {
    if (!connected_) return;
    {
        std::lock_guard<std::mutex> lock(out_mu_);
        if (out_.size() > 64) out_.pop_front();  // şişmesin
        out_.push_back(text);
    }
    out_cv_.notify_one();
}

bool WsClient::Poll(std::string& out) {
    std::lock_guard<std::mutex> lock(in_mu_);
    if (in_.empty()) return false;
    out = std::move(in_.front());
    in_.pop_front();
    return true;
}

void WsClient::SenderLoop(void* ws_handle) {
    HINTERNET ws = static_cast<HINTERNET>(ws_handle);
    while (connected_) {
        std::string msg;
        {
            std::unique_lock<std::mutex> lock(out_mu_);
            out_cv_.wait_for(lock, std::chrono::milliseconds(500), [&] { return !out_.empty() || !connected_; });
            if (out_.empty()) continue;
            msg = std::move(out_.front());
            out_.pop_front();
        }
        if (WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, msg.data(),
                                 static_cast<DWORD>(msg.size())) != NO_ERROR) {
            connected_ = false;  // Run() döngüsü yeniden bağlanır
            return;
        }
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
            WinHttpSetTimeouts(session, 2000, 2000, 5000, 0);  // receive: sonsuz (WS boşta bekler)
            if (WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) &&
                WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                ws = WinHttpWebSocketCompleteUpgrade(req, 0);
            }
        }
        if (ws) {
            WinHttpCloseHandle(req);
            req = nullptr;
            connected_ = true;
            Log("companion baglandi");
            std::thread sender(&WsClient::SenderLoop, this, static_cast<void*>(ws));

            std::string message;
            char buf[8192];
            while (connected_) {
                DWORD read = 0;
                WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
                if (WinHttpWebSocketReceive(ws, buf, sizeof(buf), &read, &type) != NO_ERROR) break;
                if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
                message.append(buf, read);
                if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {  // mesaj tamam
                    std::lock_guard<std::mutex> lock(in_mu_);
                    if (in_.size() > 256) in_.pop_front();
                    in_.push_back(std::move(message));
                    message.clear();
                }
            }
            connected_ = false;
            out_cv_.notify_all();
            sender.join();
            WinHttpCloseHandle(ws);
            Log("companion baglantisi koptu");
        }
        if (req) WinHttpCloseHandle(req);
        if (conn) WinHttpCloseHandle(conn);
        if (session) WinHttpCloseHandle(session);
        {
            std::lock_guard<std::mutex> lock(out_mu_);
            out_.clear();  // eski oturumun mesajları yeni bağlantıya gitmesin
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));  // yeniden bağlanma aralığı
    }
}

}  // namespace hikaye
