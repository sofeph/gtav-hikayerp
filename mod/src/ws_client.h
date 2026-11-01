#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <functional>

namespace hikaye {

// Companion ile WebSocket bağlantısı (Windows WinHTTP, harici kütüphane yok).
// Tüm ağ işi kendi thread'lerinde yapılır; script thread'i sadece Send/Poll kuyruklarına dokunur
// (CLAUDE.md §11: ana döngüde bloklama yok, native'ler sadece script thread'inden).
// Bağlantı koparsa kendiliğinden yeniden dener. Companion kapalıysa mod çökmez.
class WsClient {
public:
    using MessageHandler = std::function<void(const std::string& type, const std::string& payload)>;
    
    void Start(const std::wstring& host, int port);
    bool connected() const { return connected_; }
    void Send(const std::string& text);
    bool Poll(std::string& out);  // gelen mesajı kuyruktan al (yoksa false)
    void SetMessageHandler(MessageHandler handler) { message_handler_ = std::move(handler); }

private:
    void Run();
    void SenderLoop(void* ws_handle);
    void HandleMessage(const std::string& message);

    std::wstring host_;
    int port_ = 0;
    std::atomic<bool> started_{false};

    std::mutex in_mu_, out_mu_;
    std::deque<std::string> in_, out_;
    std::condition_variable out_cv_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> has_in_items_{false};
    MessageHandler message_handler_;
};

}  // namespace hikaye
