#include "pausemenu.h"

#include <windows.h>

#include <cstdio>
#include <fstream>

#include "json.hpp"

using nlohmann::json;

namespace hikaye {

void PauseMenu::Load() {
    try {
        std::ifstream in(std::string(kBaseDir) + "/data/mode.json");
        if (!in) return;
        const json cfg = json::parse(in);
        const json probe = cfg.value("pause_probe", json::object());
        debug_ = probe.value("debug", false);
        if (probe.contains("online_tab") && probe["online_tab"].is_object()) {
            const json& t = probe["online_tab"];
            has_signal_ = t.contains("state") || t.contains("hash");
            if (t.contains("state")) want_state_ = t["state"].get<int>();
            if (t.contains("hash")) {
                want_hash_ = t["hash"].get<unsigned>();
                want_hash_set_ = true;
            }
        }
    } catch (const std::exception& e) {
        Log(std::string("pause_probe okunamadi: ") + e.what());
    }
    Log(std::string("pause_probe: debug=") + (debug_ ? "acik" : "kapali") + ", sinyal=" + (has_signal_ ? "tanimli" : "yok"));
}

bool PauseMenu::ConsumeOnlineTabSelected() {
    const bool r = matched_;
    matched_ = false;
    return r;
}

namespace {
// SDK'da adı olmayan, parametresiz, değer döndüren pause menüsü civarı getter'ları. Hangisinin sekmeyle değiştiğini
// bilmiyoruz; debug açıkken hepsi izlenir, değişen aday bulunursa mode.json'a eklenir. (Sadece okuma çağrıları.)
using Getter = int (*)();
struct Candidate {
    const char* name;
    Getter fn;
};
const Candidate kCandidates[] = {
    {"1121", [] { return static_cast<int>(UI::_0x1121BFA1A1A522A8()); }},
    {"2F05", [] { return static_cast<int>(UI::_0x2F057596F2BD0061()); }},
    {"2A25", [] { return static_cast<int>(UI::_0x2A25ADC48F87841F()); }},
    {"DE03", [] { return static_cast<int>(UI::_0xDE03620F8703A9DF()); }},
    {"359A", [] { return static_cast<int>(UI::_0x359AF31A4B52F5ED()); }},
    {"13C4", [] { return static_cast<int>(UI::_0x13C4B962653A5280()); }},
    {"3BAB", [] { return static_cast<int>(UI::_0x3BAB9A4E4F2FF5C7()); }},
    {"66E7", [] { return static_cast<int>(UI::_0x66E7CB63C97B7D20()); }},
    {"593F", [] { return static_cast<int>(UI::_0x593FEAE1F73392D4()); }},
    {"4E3C", [] { return static_cast<int>(UI::_0x4E3CD0EF8A489541()); }},
    {"F284", [] { return static_cast<int>(UI::_0xF284AC67940C6812()); }},
    {"2E22", [] { return static_cast<int>(UI::_0x2E22FEFA0100275E()); }},
    {"F13F", [] { return static_cast<int>(UI::_0xF13FE2A80C05C561()); }},
    {"6F72", [] { return static_cast<int>(UI::_0x6F72CD94F7B5B68C()); }},
};
constexpr size_t kCandidateCount = sizeof(kCandidates) / sizeof(kCandidates[0]);
int g_last_extra[kCandidateCount];
bool g_extra_init = false;

// Pause menüsü sekmelerini oyunun kendi script'leri yönetir; Online sekmesine gelince onunki çalışmaya başlamalı.
// Adlar tahmin: kaç thread çalışıyorsa sayısı izlenir, değişen olursa loglanır.
const char* const kScripts[] = {
    "pausemenu",           "pausemenu_example",   "pausemenu_map",        "pausemenu_gallery",   "pausemenu_sp_repeat",
    "pausemenu_multiplayer", "pausemenu_launchpad", "pausemenu_game",      "pausemenu_stats",     "pausemenu_settings",
    "pausemenu_store",     "pausemenu_brief",     "pausemenu_online",     "landing_pre",         "landing_page",
    "landing_post",        "pausemenu_info",      "pausemenu_replay",     "pausemenu_savegame",  "pausemenu_audio",
    "social_controller",   "socialclub",          "rockstar_editor",      "replay_controller",   "pause_menu_pages_online",
    "ob_telescope",        "startup",             "startup_install",      "startup_positioning", "launcher_basejump"};
constexpr size_t kScriptCount = sizeof(kScripts) / sizeof(kScripts[0]);
Hash g_script_hash[kScriptCount];
int g_script_last[kScriptCount];
bool g_script_init = false;
}  // namespace

void PauseMenu::Update() {
    if (!debug_ && !has_signal_) return;  // kapalıyken hiçbir native çağrılmaz

    const bool active = UI::IS_PAUSE_MENU_ACTIVE() != 0;
    if (!active) {
        if (was_active_ && debug_) Log("pause: kapandi");
        was_active_ = false;
        last_state_ = -2;
        last_hash_ = 0;
        g_extra_init = false;  // bir sonraki açılışta ilk değerler yeniden yazılsın
        g_script_init = false;
        return;
    }
    if (!was_active_ && debug_) Log("pause: acildi");
    was_active_ = true;

    const int state = UI::GET_PAUSE_MENU_STATE();
    const unsigned hash = static_cast<unsigned>(UI::_0x2309595AD6145265());
    const bool changed = state != last_state_ || hash != last_hash_;
    if (changed && debug_) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "pause: state=%d hash=0x%08X", state, hash);
        Log(buf);
    }
    if (changed && has_signal_) {
        const bool ok_state = want_state_ < 0 || state == want_state_;
        const bool ok_hash = !want_hash_set_ || hash == want_hash_;
        if (ok_state && ok_hash) {
            matched_ = true;
            if (debug_) Log("pause: ONLINE SEKMESI sinyali eslesti");
        }
    }
    last_state_ = state;
    last_hash_ = hash;

    // Aday sinyaller: değişen olursa adıyla ve yeni değeriyle logla (sekmeler arasında gezinince hangisi değişiyor?).
    if (debug_) {
        for (size_t i = 0; i < kCandidateCount; ++i) {
            const int v = kCandidates[i].fn();
            if (!g_extra_init) {
                g_last_extra[i] = v;
                continue;
            }
            if (v != g_last_extra[i]) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "pause: aday %s: %d -> %d", kCandidates[i].name, g_last_extra[i], v);
                Log(buf);
                g_last_extra[i] = v;
            }
        }
        if (!g_extra_init) {
            std::string init = "pause: aday ilk degerler:";
            for (size_t i = 0; i < kCandidateCount; ++i) init += std::string(" ") + kCandidates[i].name + "=" + std::to_string(g_last_extra[i]);
            Log(init);
            g_extra_init = true;
        }

        // Çalışan script thread'leri: pause açıkken hangi script'ler çalışıyor / sekme değişince ne değişiyor?
        if (!g_script_init) {
            std::string running = "pause: calisan scriptler:";
            for (size_t i = 0; i < kScriptCount; ++i) {
                g_script_hash[i] = HashKey(kScripts[i]);
                g_script_last[i] = SCRIPT::_GET_NUMBER_OF_INSTANCES_OF_STREAMED_SCRIPT(g_script_hash[i]);
                if (g_script_last[i] > 0) running += std::string(" ") + kScripts[i] + "=" + std::to_string(g_script_last[i]);
            }
            Log(running);
            g_script_init = true;
        } else {
            for (size_t i = 0; i < kScriptCount; ++i) {
                const int c = SCRIPT::_GET_NUMBER_OF_INSTANCES_OF_STREAMED_SCRIPT(g_script_hash[i]);
                if (c != g_script_last[i]) {
                    Log(std::string("pause: script ") + kScripts[i] + ": " + std::to_string(g_script_last[i]) + " -> " + std::to_string(c));
                    g_script_last[i] = c;
                }
            }
        }
    }
}

}  // namespace hikaye
