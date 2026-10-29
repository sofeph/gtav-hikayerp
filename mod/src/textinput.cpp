#include "textinput.h"

#include <windows.h>

#include <atomic>
#include <deque>
#include <mutex>

#include "common.h"

namespace hikaye::textinput {

namespace {

constexpr size_t kMaxChars = 120;

struct Event {
    enum class Kind { Char, Backspace, Enter, Escape } kind;
    wchar_t ch = 0;
};

std::mutex g_mu;
std::deque<Event> g_events;  // kancadan (başka thread olabilir) gelir, Pump() script thread'inde işler
std::atomic<bool> g_active{false};
std::wstring g_buf;          // sadece script thread dokunur
bool g_submitted = false, g_cancelled = false;
std::atomic<unsigned long long> g_begin_tick{0};  // T tuşunun tekrar eden olayları metne girmesin

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// Basılan tuşu aktif klavye düzenine göre karaktere çevirir (ş, ı, ğ, İ... dahil). Kanca hangi thread'de
// çalışırsa çalışsın doğru olsun diye değiştirici tuşlar sistem genelinden okunur.
wchar_t ToChar(DWORD vk, BYTE scan) {
    BYTE ks[256] = {};
    auto down = [](int k) { return (GetAsyncKeyState(k) & 0x8000) != 0; };
    if (down(VK_SHIFT)) ks[VK_SHIFT] = ks[VK_LSHIFT] = 0x80;
    if (down(VK_CONTROL)) ks[VK_CONTROL] = ks[VK_LCONTROL] = 0x80;
    if (down(VK_MENU)) ks[VK_MENU] = ks[VK_LMENU] = 0x80;  // Türkçe Q'da AltGr = Ctrl+Alt
    if (GetKeyState(VK_CAPITAL) & 1) ks[VK_CAPITAL] = 0x01;
    HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
    wchar_t out[4] = {};
    // 0x4: ölü tuş durumunu bozma (Windows 10 1607+)
    const int n = ToUnicodeEx(vk, scan, ks, out, 4, 0x4, layout);
    return n == 1 ? out[0] : 0;  // ölü tuş/çoklu karakter yok sayılır
}

void OnKey(DWORD key, WORD, BYTE scan, BOOL, BOOL, BOOL, BOOL is_up) {
    if (!g_active || is_up) return;
    Event e{Event::Kind::Char, 0};
    if (key == VK_BACK) e.kind = Event::Kind::Backspace;
    else if (key == VK_RETURN) e.kind = Event::Kind::Enter;
    else if (key == VK_ESCAPE) e.kind = Event::Kind::Escape;
    else {
        if (GetTickCount64() - g_begin_tick < 200) return;  // yazmayı başlatan T tuşunun tekrarları
        e.ch = ToChar(key, scan);
        if (e.ch < 0x20 || e.ch == 0x7F) return;  // kontrol karakterleri yazılmaz
    }
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_events.size() < 64) g_events.push_back(e);
}

}  // namespace

void Init() { keyboardHandlerRegister(OnKey); }
void Shutdown() { keyboardHandlerUnregister(OnKey); }

void Begin() {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_events.clear();
    }
    g_buf.clear();
    g_submitted = g_cancelled = false;
    g_begin_tick = GetTickCount64();
    g_active = true;
}

void Cancel() {
    g_active = false;
    g_submitted = g_cancelled = false;
    g_buf.clear();
}

bool Active() { return g_active; }

void Pump() {
    if (!g_active) return;
    std::deque<Event> events;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        events.swap(g_events);
    }
    for (const Event& e : events) {
        switch (e.kind) {
            case Event::Kind::Char:
                if (g_buf.size() < kMaxChars) g_buf.push_back(e.ch);
                break;
            case Event::Kind::Backspace:
                if (!g_buf.empty()) g_buf.pop_back();
                break;
            case Event::Kind::Enter:
                g_submitted = true;
                g_active = false;
                return;
            case Event::Kind::Escape:
                g_cancelled = true;
                g_active = false;
                return;
        }
    }
}

std::string Current() { return ToUtf8(g_buf); }

bool ConsumeSubmitted(std::string& out) {
    if (!g_submitted) return false;
    g_submitted = false;
    out = ToUtf8(g_buf);
    return true;
}

bool ConsumeCancelled() {
    const bool c = g_cancelled;
    g_cancelled = false;
    return c;
}

}  // namespace hikaye::textinput
