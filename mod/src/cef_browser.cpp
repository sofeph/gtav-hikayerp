#include "cef_browser.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_client.h"

namespace hikaye::cef {
namespace {

std::mutex g_init_mutex;
std::mutex g_state_mutex;
std::mutex g_frame_mutex;
HMODULE g_cef_module = nullptr;
CefRefPtr<CefApp> g_app;
CefRefPtr<CefClient> g_client;
CefRefPtr<CefBrowser> g_browser;
std::string g_pending_url;
int g_width = 1280;
int g_height = 720;
std::atomic<bool> g_initialized{false};
bool g_wants_open = false;
std::shared_ptr<std::vector<unsigned char>> g_pixels;
std::uint64_t g_frame_revision = 0;
int g_frame_width = 0;
int g_frame_height = 0;

std::filesystem::path RuntimeRoot() {
    std::vector<wchar_t> value(32768);
    const DWORD env_size = GetEnvironmentVariableW(L"HIKAYE_CEF_DIR", value.data(),
                                                   static_cast<DWORD>(value.size()));
    if (env_size > 0 && env_size < value.size()) {
        return std::filesystem::absolute(std::filesystem::path(value.data()));
    }

    std::vector<wchar_t> executable(32768);
    const DWORD path_size = GetModuleFileNameW(nullptr, executable.data(),
                                               static_cast<DWORD>(executable.size()));
    if (path_size == 0 || path_size >= executable.size()) {
        throw std::filesystem::filesystem_error(
            "Could not resolve GTA executable path",
            std::error_code(static_cast<int>(GetLastError()), std::system_category()));
    }
    return std::filesystem::path(executable.data()).parent_path() / L"HikayeTurkRP" / L"cef";
}

std::wstring RuntimePath(const wchar_t* relative) {
    return (RuntimeRoot() / relative).wstring();
}

void CreateBrowserOnUiThread();
void LoadUrlOnUiThread(const std::string& url);
void CloseOnUiThread();
void GoBackOnUiThread();
void ExecuteJavaScriptOnUiThread(const std::string& script);

class BrowserClient final : public CefClient,
                            public CefRenderHandler,
                            public CefLifeSpanHandler {
public:
    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        std::string url;
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_browser = browser;
            url = g_pending_url;
        }
        if (!url.empty()) browser->GetMainFrame()->LoadURL(url);
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (g_browser == browser) {
            g_browser = nullptr;
            g_wants_open = false;
        }
    }

    void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            rect = CefRect(0, 0, g_width, g_height);
        }

    void OnPaint(CefRefPtr<CefBrowser>, PaintElementType, const RectList&, const void* buffer,
                 int width, int height) override {
        if (!buffer || width <= 0 || height <= 0 || width > 4096 || height > 4096) return;
        const size_t byte_count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        auto pixels = std::make_shared<std::vector<unsigned char>>(
            static_cast<const unsigned char*>(buffer),
            static_cast<const unsigned char*>(buffer) + byte_count);
        std::lock_guard<std::mutex> lock(g_frame_mutex);
        g_pixels = std::move(pixels);
        g_frame_width = width;
        g_frame_height = height;
        ++g_frame_revision;
    }

    IMPLEMENT_REFCOUNTING(BrowserClient);
};

class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
public:
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }

    void OnBeforeCommandLineProcessing(const CefString&, CefRefPtr<CefCommandLine> command_line) override {
        command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
    }

    void OnContextInitialized() override {
        CreateBrowserOnUiThread();
    }

    IMPLEMENT_REFCOUNTING(BrowserApp);
};

class CreateBrowserTask final : public CefTask {
public:
    void Execute() override { CreateBrowserOnUiThread(); }
    IMPLEMENT_REFCOUNTING(CreateBrowserTask);
};

class LoadUrlTask final : public CefTask {
public:
    explicit LoadUrlTask(std::string url) : url_(std::move(url)) {}
    void Execute() override { LoadUrlOnUiThread(url_); }
    IMPLEMENT_REFCOUNTING(LoadUrlTask);

private:
    std::string url_;
};

class CloseTask final : public CefTask {
public:
    void Execute() override { CloseOnUiThread(); }
    IMPLEMENT_REFCOUNTING(CloseTask);
};

class ResizeTask final : public CefTask {
public:
    void Execute() override {
        CefRefPtr<CefBrowser> browser;
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            browser = g_browser;
        }
        if (browser) browser->GetHost()->WasResized();
    }
    IMPLEMENT_REFCOUNTING(ResizeTask);
};

class GoBackTask final : public CefTask {
public:
    void Execute() override { GoBackOnUiThread(); }
    IMPLEMENT_REFCOUNTING(GoBackTask);
};

class ExecuteJavaScriptTask final : public CefTask {
public:
    explicit ExecuteJavaScriptTask(std::string script) : script_(std::move(script)) {}
    void Execute() override { ExecuteJavaScriptOnUiThread(script_); }
    IMPLEMENT_REFCOUNTING(ExecuteJavaScriptTask);

private:
    std::string script_;
};

class MouseMoveTask final : public CefTask {
public:
    MouseMoveTask(int x, int y, bool left_down, bool right_down)
        : x_(x), y_(y), left_down_(left_down), right_down_(right_down) {}

    void Execute() override {
        CefRefPtr<CefBrowser> browser;
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            browser = g_browser;
        }
        if (!browser) return;
        CefMouseEvent event;
        event.x = x_;
        event.y = y_;
        if (left_down_) event.modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
        if (right_down_) event.modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
        browser->GetHost()->SendMouseMoveEvent(event, false);
    }
    IMPLEMENT_REFCOUNTING(MouseMoveTask);

private:
    int x_;
    int y_;
    bool left_down_;
    bool right_down_;
};

class MouseClickTask final : public CefTask {
public:
    MouseClickTask(int x, int y, bool right_button, bool mouse_up)
        : x_(x), y_(y), right_button_(right_button), mouse_up_(mouse_up) {}

    void Execute() override {
        CefRefPtr<CefBrowser> browser;
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            browser = g_browser;
        }
        if (!browser) return;
        CefMouseEvent event;
        event.x = x_;
        event.y = y_;
        browser->GetHost()->SendMouseClickEvent(event, right_button_ ? MBT_RIGHT : MBT_LEFT, mouse_up_, 1);
    }
    IMPLEMENT_REFCOUNTING(MouseClickTask);

private:
    int x_;
    int y_;
    bool right_button_;
    bool mouse_up_;
};

void SendKeyOnUiThread(int key, bool key_up) {
    CefRefPtr<CefBrowser> browser;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        browser = g_browser;
    }
    if (!browser) return;
    CefKeyEvent event;
    event.type = key_up ? KEYEVENT_KEYUP : KEYEVENT_RAWKEYDOWN;
    event.windows_key_code = key;
    event.native_key_code = static_cast<int>(MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC));
    browser->GetHost()->SendKeyEvent(event);
}

class KeyTask final : public CefTask {
public:
    KeyTask(int key, bool key_up) : key_(key), key_up_(key_up) {}
    void Execute() override { SendKeyOnUiThread(key_, key_up_); }
    IMPLEMENT_REFCOUNTING(KeyTask);

private:
    int key_;
    bool key_up_;
};

void SendCharacterOnUiThread(wchar_t character) {
    CefRefPtr<CefBrowser> browser;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        browser = g_browser;
    }
    if (!browser) return;
    CefKeyEvent event;
    event.type = KEYEVENT_CHAR;
    event.character = character;
    event.unmodified_character = character;
    browser->GetHost()->SendKeyEvent(event);
}

class CharacterTask final : public CefTask {
public:
    explicit CharacterTask(wchar_t character) : character_(character) {}
    void Execute() override { SendCharacterOnUiThread(character_); }
    IMPLEMENT_REFCOUNTING(CharacterTask);

private:
    wchar_t character_;
};

void CreateBrowserOnUiThread() {
    std::string url;
    int width = 0;
    int height = 0;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (!g_wants_open || g_browser || !g_client) return;
        url = g_pending_url;
        width = g_width;
        height = g_height;
    }

    CefWindowInfo window_info;
    window_info.SetAsWindowless(nullptr);
    CefBrowserSettings browser_settings;
    browser_settings.windowless_frame_rate = 30;
    CefBrowserHost::CreateBrowser(window_info, g_client, url, browser_settings, nullptr, nullptr);
    (void)width;
    (void)height;
}

void LoadUrlOnUiThread(const std::string& url) {
    CefRefPtr<CefBrowser> browser;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_pending_url = url;
        browser = g_browser;
    }
    if (browser) browser->GetMainFrame()->LoadURL(url);
    else CreateBrowserOnUiThread();
}

void CloseOnUiThread() {
    CefRefPtr<CefBrowser> browser;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        browser = g_browser;
    }
    if (browser) browser->GetHost()->CloseBrowser(true);
}

void GoBackOnUiThread() {
    CefRefPtr<CefBrowser> browser;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        browser = g_browser;
    }
    if (browser && browser->CanGoBack()) browser->GoBack();
}

void ExecuteJavaScriptOnUiThread(const std::string& script) {
    CefRefPtr<CefBrowser> browser;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        browser = g_browser;
    }
    if (browser) {
        const auto frame = browser->GetMainFrame();
        frame->ExecuteJavaScript(script, frame->GetURL(), 0);
    }
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return "";
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

}  // namespace

bool Initialize(std::string& error) {
    std::lock_guard<std::mutex> init_lock(g_init_mutex);
    if (g_initialized) return true;

    std::wstring runtime;
    std::wstring subprocess;
    std::wstring resources;
    std::wstring locales;
    std::wstring cache;
    try {
        runtime = RuntimePath(L"libcef.dll");
        subprocess = RuntimePath(L"HikayeTurkRPSubprocess.exe");
        resources = RuntimePath(L"Resources");
        locales = RuntimePath(L"Resources\\locales");
        cache = std::filesystem::absolute(
            RuntimeRoot().parent_path() / L"saves" / L"cef-cache").wstring();
        if (!std::filesystem::is_regular_file(runtime) ||
            !std::filesystem::is_regular_file(subprocess) ||
            !std::filesystem::is_directory(resources) ||
            !std::filesystem::is_directory(locales)) {
            error = "CEF çalışma zamanı eksik veya hatalı konumda.";
            return false;
        }
        std::filesystem::create_directories(cache);
    } catch (const std::filesystem::filesystem_error& e) {
        error = "CEF yolları hazırlanamadı: " + std::string(e.what());
        return false;
    }

    g_cef_module = LoadLibraryExW(runtime.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_cef_module) {
        error = "CEF libcef.dll yüklenemedi: " + std::to_string(GetLastError());
        return false;
    }

    CefMainArgs main_args(GetModuleHandleW(nullptr));
    CefSettings settings;
    settings.no_sandbox = true;
    settings.multi_threaded_message_loop = false;
    settings.windowless_rendering_enabled = true;
    settings.log_severity = LOGSEVERITY_WARNING;
    CefString(&settings.browser_subprocess_path) = subprocess;
    CefString(&settings.resources_dir_path) = resources;
    CefString(&settings.locales_dir_path) = locales;
    CefString(&settings.cache_path) = cache;
    g_app = new BrowserApp();
    g_client = new BrowserClient();
    if (!CefInitialize(main_args, settings, g_app, nullptr)) {
        error = "CefInitialize başarısız oldu.";
        g_client = nullptr;
        g_app = nullptr;
        FreeLibrary(g_cef_module);
        g_cef_module = nullptr;
        return false;
    }
    g_initialized = true;
    return true;
}

void Shutdown() {
    if (!g_initialized) return;
    Close();
    for (int i = 0; i < 500; ++i) {
        CefDoMessageLoopWork();
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            if (!g_browser) break;
        }
        Sleep(10);
    }
    CefShutdown();
    g_initialized = false;
    g_browser = nullptr;
    g_client = nullptr;
    g_app = nullptr;
    if (g_cef_module) {
        FreeLibrary(g_cef_module);
        g_cef_module = nullptr;
    }
}

void DoMessageLoopWork() {
    if (g_initialized) CefDoMessageLoopWork();
}

bool Open(const std::string& url, int width, int height, std::string& error) {
    if (!Initialize(error)) return false;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_pending_url = url;
        g_width = std::clamp(width, 320, 4096);
        g_height = std::clamp(height, 200, 4096);
        g_wants_open = true;
    }
    CefPostTask(TID_UI, new CreateBrowserTask());
    return true;
}

void LoadUrl(const std::string& url) {
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_pending_url = url;
        g_wants_open = true;
    }
    if (CefCurrentlyOn(TID_UI)) LoadUrlOnUiThread(url);
    else CefPostTask(TID_UI, new LoadUrlTask(url));
}

void Close() {
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_wants_open = false;
    }
    CefPostTask(TID_UI, new CloseTask());
}

void GoBack() {
    CefPostTask(TID_UI, new GoBackTask());
}

void ExecuteJavaScript(const std::string& script) {
    CefPostTask(TID_UI, new ExecuteJavaScriptTask(script));
}

bool IsOpen() {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    return g_wants_open;
}

void Resize(int width, int height) {
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_width = std::clamp(width, 320, 4096);
        g_height = std::clamp(height, 200, 4096);
    }
    CefPostTask(TID_UI, new ResizeTask());
}

Frame LatestFrame() {
    std::lock_guard<std::mutex> lock(g_frame_mutex);
    return {g_pixels, g_frame_width, g_frame_height, g_frame_revision};
}

void MouseMove(int x, int y, bool left_down, bool right_down) {
    CefPostTask(TID_UI, new MouseMoveTask(x, y, left_down, right_down));
}

void MouseClick(int x, int y, bool right_button, bool mouse_up) {
    CefPostTask(TID_UI, new MouseClickTask(x, y, right_button, mouse_up));
}

void SendKey(int key, bool key_up) { CefPostTask(TID_UI, new KeyTask(key, key_up)); }

void SendCharacter(wchar_t character) {
    CefPostTask(TID_UI, new CharacterTask(character));
}

std::string LocalFileUrl(const std::string& relative_path) {
    const std::filesystem::path path = std::filesystem::absolute(std::filesystem::path(relative_path));
    const std::string utf8 = WideToUtf8(path.wstring());
    std::string url = "file:///";
    for (const unsigned char ch : utf8) {
        if (ch == '\\') {
            url += '/';
        } else if (ch == ' ') {
            url += "%20";
        } else if (ch == '#') {
            url += "%23";
        } else if (ch == '%') {
            url += "%25";
        } else {
            url += static_cast<char>(ch);
        }
    }
    return url;
}

std::string UrlEncode(const std::string& value) {
    static const char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char ch : value) {
        const bool safe = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (safe) {
            result += static_cast<char>(ch);
        } else {
            result += '%';
            result += hex[ch >> 4];
            result += hex[ch & 0x0F];
        }
    }
    return result;
}

}  // namespace hikaye::cef
