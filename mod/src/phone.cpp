#include "phone.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>

#include "controls.h"
#include "economy.h"
#include "phoneos.h"

namespace hikaye {

namespace {
// Hizmetin neden verilemediğini oyuncuya Türkçe söyler (kararı veren Services::Dispatch).
std::string ReasonText(const std::string& reason) {
    if (reason == "already_on_the_way") return "Zaten yolda";
    if (reason == "cooldown") return "Biraz bekle, yakın zamanda çağırdın";
    if (reason == "wanted_level") return "Arandığın için hizmet verilemiyor";
    if (reason == "no_road_nearby") return "Yakında uygun yol yok";
    if (reason == "busy_in_ride") return "Şu an yolculuktasın";
    return "Şu an hizmet verilemiyor";
}

const ServiceKind kKinds[] = {ServiceKind::Taxi, ServiceKind::HeliTaxi, ServiceKind::Police, ServiceKind::Ambulance, ServiceKind::Fire, ServiceKind::Mechanic};

static bool IsKeyJustPressed(int vKey) {
    return (GetAsyncKeyState(vKey) & 0x0001) != 0;
}

static std::vector<overlay::WebItem> BuildHomeWebItems() {
    static const std::vector<std::pair<std::string, std::pair<std::string, int>>> apps = {
        {"phone/telefon", {"Telefon", 0x2E7D32}}, {"phone/rehber", {"Rehber", 0x1976D2}},
        {"phone/haviland", {"Haviland", 0x9B6BFF}}, {"phone/maps", {"Maps", 0x2AA7C9}},
        {"phone/maze", {"Maze Bank", 0xD4A017}}, {"phone/store", {"App Store", 0xE0508A}},
        {"phone/saat", {"Saat", 0x2A8F8A}}, {"phone/tv", {"TV", 0xC62828}},
            {"phone/settings", {"Ayarlar", 0x56607A}}, {"phone/alarm", {"Alarm", 0xD9534F}}
    };

    std::vector<std::string> urls = phoneos::HomeAppUrls();
    std::vector<overlay::WebItem> items;
    items.reserve(urls.size());
    for (size_t i = 0; i < urls.size(); ++i) {
        overlay::WebItem item;
        item.meta = urls[i];
        const auto app = std::find_if(apps.begin(), apps.end(), [&](const auto& entry) { return entry.first == urls[i]; });
        item.text = app != apps.end() ? app->second.first : urls[i];
        item.selectable = true;
        item.color = app != apps.end() ? app->second.second : 0x56607A;
        items.push_back(item);
    }
    return items;
}

std::string HandlePhoneInput(overlay::PhoneView& phone, HWND game_hwnd, int screen_w, int screen_h) {
    if (!phone.open) return {};
    int activated = -1;

    const bool is_home = phone.web_url.empty() || phone.web_url == "phone/home" ||
                         phone.web_url == "telefon://home" || phone.web_url == "home";

    if (phone.cursor) {
        UI::_SHOW_CURSOR_THIS_FRAME();
        POINT pt{};
        if (game_hwnd && GetCursorPos(&pt) && ScreenToClient(game_hwnd, &pt)) {
            phone.cursor_x = std::clamp(static_cast<float>(pt.x) / static_cast<float>(std::max(1, screen_w)), 0.0f, 1.0f);
            phone.cursor_y = std::clamp(static_cast<float>(pt.y) / static_cast<float>(std::max(1, screen_h)), 0.0f, 1.0f);
        } else {
            phone.cursor_x = std::clamp(phone.cursor_x, 0.0f, 1.0f);
            phone.cursor_y = std::clamp(phone.cursor_y, 0.0f, 1.0f);
        }

        if (IsKeyJustPressed(VK_LBUTTON)) {
            phone.click_seq++;
        }
    } else {
        if (is_home) {
            const int item_count = static_cast<int>(phone.web_items.size());
            if (IsKeyJustPressed(VK_RIGHT)) {
                if (phone.web_selected >= 0 && phone.web_selected < item_count - 1 &&
                    (phone.web_selected % 4 != 3 || phone.web_selected % 12 == 11)) {
                    phone.web_selected++;
                }
            }
            if (IsKeyJustPressed(VK_LEFT)) {
                if (phone.web_selected > 0 &&
                    (phone.web_selected % 4 != 0 || phone.web_selected % 12 == 0)) {
                    phone.web_selected--;
                }
            }
            if (IsKeyJustPressed(VK_DOWN)) {
                if (phone.web_selected == -5) {
                    phone.web_selected = 0;
                } else if (phone.web_selected >= 0 && phone.web_selected + 4 < item_count) {
                    phone.web_selected += 4;
                }
            }
            if (IsKeyJustPressed(VK_UP)) {
                if (phone.web_selected >= 4) {
                    phone.web_selected -= 4;
                } else if (phone.web_selected >= 0) {
                    phone.web_selected = -5;
                }
            }
        } else {
            const int total_items = static_cast<int>(phone.web_items.size());
            if (total_items > 0) {
                if (IsKeyJustPressed(VK_DOWN)) {
                    phone.web_selected = std::min(phone.web_selected + 1, total_items - 1);
                }
                if (IsKeyJustPressed(VK_UP)) {
                    phone.web_selected = std::max(phone.web_selected - 1, 0);
                }
            }
        }

        if (IsKeyJustPressed(VK_RETURN) || IsKeyJustPressed(VK_SPACE)) {
            if (is_home) {
                if (phone.web_selected == -5) {
                    hikaye::overlay::SetInput("", true);
                } else {
                    activated = phone.web_selected;
                }
            } else {
                phone.click_seq++;
            }
        }
    }

    if (is_home && phone.cursor) {
        const int clicked = overlay::TakeWebClick();
        if (clicked == -5) {
            overlay::SetInput("", true);
        } else if (clicked >= 0) {
            activated = clicked;
        }
    }

    if (is_home && activated >= 0 && activated < static_cast<int>(phone.web_items.size())) {
        return phone.web_items[static_cast<size_t>(activated)].meta;
    }

    if (IsKeyJustPressed(VK_BACK) || IsKeyJustPressed(VK_ESCAPE)) {
        if (!is_home) {
            phone.web_url = "phone/home";
            phone.web_selected = 0;
        } else {
            phone.open = false;
            phone.cursor = false;
        }
    }
    return {};
}

}  // namespace

bool Phone::Edge(int vk, bool& was_down) const {
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool pressed = down && !was_down;
    was_down = down;
    return pressed;
}

void Phone::UpdateCursorToggle() {
    if (open_ && Edge(VK_OEM_3, was_cursor_toggle_down_)) {
        state_.cursor = !state_.cursor;
        if (!state_.cursor && state_.web_selected < 0) state_.web_selected = 0;
    }
}

bool Phone::Update(bool conversation_active, bool typing, bool escape_pressed, Services& services,
                   const std::vector<Missions::Contact>& contacts, std::string& call_contact) {
    call_contact.clear();
    if (typing) return false;  // ekran klavyesi açıkken yazılan harfler telefonu tetiklemesin
    bool want_call = false;
    // Satırlar: hizmetler (5) + rehberdeki kişiler + "Santrali ara".
    const int n_contacts = static_cast<int>(contacts.size());
    const int call_row = kServiceCount + n_contacts;
    const int total = call_row + 1;
    if (selected_ >= total) selected_ = 0;  // kişi sayısı değiştiyse

    // Telefon açıkken oyunun kendi telefonu (yukarı ok) ve ilgili tuşlar çalışmasın.
    if (open_) {
        for (int c : {27 /*INPUT_PHONE*/, 172, 173, 174, 175, 176, 177}) CONTROLS::DISABLE_CONTROL_ACTION(0, c, true);
    }

    const bool up = Edge(VK_UP, was_up_), down = Edge(VK_DOWN, was_down_), enter = Edge(VK_RETURN, was_enter_),
               back = Edge(VK_BACK, was_back_), c = Edge('C', was_c_);

    if (escape_pressed && open_ && !conversation_active) {
        open_ = false;
        return false;
    }
    if (!open_ || conversation_active) return false;

    if (back) {
        open_ = false;
        return false;
    }
    if (up) selected_ = (selected_ + total - 1) % total;
    if (down) selected_ = (selected_ + 1) % total;
    if (c || (enter && selected_ == call_row)) {
        want_call = true;  // sesli görüşme: santral görevlisiyle konuş
    } else if (enter && selected_ >= kServiceCount && selected_ < call_row) {
        call_contact = contacts[static_cast<size_t>(selected_ - kServiceCount)].id;  // rehberdeki kişiyi ara
    } else if (enter && selected_ < kServiceCount) {
        const ServiceKind kind = kKinds[selected_];
        const nlohmann::json facts = services.Dispatch(kind);  // hızlı arama: karar yine kodda
        if (facts.value("accepted", false)) {
            const int eta = facts.value("eta_seconds", 0);
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s yola çıktı (~%d dk)", Services::Label(kind), (eta + 59) / 60);
            notice_ = buf;
        } else {
            notice_ = std::string(Services::Label(kind)) + ": " + ReasonText(facts.value("reason", ""));
        }
        notice_until_ = GetTickCount64() + 5000;
    }
    return want_call;
}

std::string Phone::TakeAppLaunchUrl() {
    std::string url;
    url.swap(pending_app_url_);
    return url;
}

void Phone::Publish(const CallView& call, const Services& services, const std::vector<Missions::Contact>& contacts,
                    bool allow_input) {
    (void)services;
    (void)contacts;
    static std::vector<overlay::ChatLine> hist;
    static std::string last_caller, last_player;
    static bool was_thinking = false;

    overlay::PhoneView v = state_;
    v.open = open_;
    const bool is_home = v.web_url.empty() || v.web_url == "phone/home" || v.web_url == "telefon://home" || v.web_url == "home";
    if (v.open && is_home) {
        v.web_items = BuildHomeWebItems();
        v.web_selected = std::clamp(v.web_selected, -5, std::max(-1, static_cast<int>(v.web_items.size()) - 1));
    } else {
        v.web_items.clear();
    }
    // Android 14+ Material You device frame
    v.material_ui = true;
    v.corner_radius = 32.f;
    v.show_status_bar = true;
    v.show_gesture_pill = true;
    v.show_camera_cutout = true;
    v.elevation_level = 3;
    v.surface_color = 0x0B0D11;       // Deep AMOLED
    v.on_surface_color = 0xE8EEFC;
    v.outline_color = 0x44474F;
    v.error_color = 0xBA1A1A;
    
        // YENİ: Ayarları overlay'e aktar
        v.settings.wallpaper = static_cast<overlay::WallpaperTheme>(phoneos::GetWallpaperTheme());
        v.settings.ringtone_id = phoneos::GetRingtoneId();
        v.settings.notification_id = phoneos::GetNotificationId();
        v.settings.master_volume = phoneos::GetMasterVolume();
        v.settings.vibrate = phoneos::GetVibrate();
    if (!call.in_call) {
        hist.clear();
        last_caller.clear();
        last_player.clear();
        was_thinking = false;
        v.in_call = false;
        v.chat.clear();
        v.caller.clear();
        v.caller_line.clear();
        v.player_line.clear();
        v.listening = false;
        v.speaking = false;
        v.typing = false;
        v.thinking = false;
        v.assistant = false;
        if (v.web_url == "chat://call") {
            v.web_url = "phone/home";
            v.web_selected = 0;
        }
        HWND game_hwnd = GetForegroundWindow();
        int screen_w = GetSystemMetrics(SM_CXSCREEN);
        int screen_h = GetSystemMetrics(SM_CYSCREEN);
        RECT rc{};
        if (game_hwnd && GetClientRect(game_hwnd, &rc) && rc.right > 0 && rc.bottom > 0) {
            screen_w = rc.right;
            screen_h = rc.bottom;
        }
        if (allow_input) {
            const std::string launch_url = HandlePhoneInput(v, game_hwnd, screen_w, screen_h);
            if (!launch_url.empty()) pending_app_url_ = launch_url;
        }
        state_ = v;
        overlay::SetPhone(v);
        return;
    }
    // Arama SMS gibi gösterilir: her yeni satır bir balon
    if (call.thinking) was_thinking = true;
    if (!call.caller_line.empty() && call.caller_line != last_caller) {
        if (was_thinking) {  // düşünme bitti: "düşündü", ardından cevap
            hist.push_back({false, "düşündü"});
            was_thinking = false;
        }
        hist.push_back({false, call.caller_line});
        last_caller = call.caller_line;
    }
    if (!call.player_line.empty() && call.player_line != last_player) {
        hist.push_back({true, call.player_line});
        last_player = call.player_line;
    }
    if (hist.size() > 12) hist.erase(hist.begin());
    char clock[8];
    std::snprintf(clock, sizeof(clock), "%02d:%02d", TIME::GET_CLOCK_HOURS(), TIME::GET_CLOCK_MINUTES());
    v.clock = clock;
    v.signal = phoneos::SignalText();
    v.open = true;
    v.in_call = true;
    v.web_url = "chat://call";
    v.caller = call.caller;
    v.listening = call.listening;
    v.speaking = call.speaking;
    v.typing = call.typing;
    v.thinking = call.thinking;
    v.assistant = call.assistant;
    v.chat = call.previous_history;
    if (!v.chat.empty() && !hist.empty()) v.chat.push_back({false, "YENI GORUSME", true});
    v.chat.insert(v.chat.end(), hist.begin(), hist.end());
    if (GetTickCount64() < notice_until_) v.notice = notice_;
    const std::string talk = controls::KeyName(controls::TalkKey()), phone = controls::KeyName(controls::PhoneKey());
    v.hint = talk + ": konuş   T: yaz   ESC: görüşmeyi bitir";

    HWND game_hwnd = GetForegroundWindow();
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    RECT rc{};
    if (game_hwnd && GetClientRect(game_hwnd, &rc) && rc.right > 0 && rc.bottom > 0) {
        screen_w = rc.right;
        screen_h = rc.bottom;
    }
    if (allow_input) {
        const std::string launch_url = HandlePhoneInput(v, game_hwnd, screen_w, screen_h);
        if (!launch_url.empty()) pending_app_url_ = launch_url;
    }
    state_ = v;

    overlay::SetPhone(v);
}

}  // namespace hikaye
