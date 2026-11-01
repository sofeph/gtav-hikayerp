#include <windows.h>
#include <DbgHelp.h>
#include <shellapi.h>
#include <cstdlib>
#pragma comment(lib, "Dbghelp.lib")

#include <fstream>
#include "json.hpp"

#include "common.h"
#include "SafeNative.h"
#include "blips.h"
#include "controls.h"
#include "conversation.h"
#include "tv_overlay.h"
#include "radio.h"
#include "incomingcall.h"
#include "economy.h"
#include "businesses.h"
#include "fuel.h"
#include "transit.h"
#include "vehctl.h"
#include "garage.h"
#include "hangout.h"
#include "installer.h"
#include "interiors.h"
#include "interaction.h"
#include "mode.h"
#include "missions.h"
#include "nativephone.h"
#include "storyblips.h"
#include "overlay.h"
#include "passenger.h"
#include "pausemenu.h"
#include "phone.h"
#include "webbrowser.h"
#include "hitchhike.h"
#include "phoneos.h"
#include "pcos.h"
#include "customs.h"
#include "clerks.h"
#include "barber.h"
#include "binco.h"
#include "admin.h"
#include "xpad.h"
#include "minigames.h"
#include "karma.h"
#include "marketplace.h"
#include "estate.h"
#include "webfeed.h"
#include "proactive.h"
#include "crimes.h"
#include "inventory.h"
#include "vendors.h"
#include "death.h"
#include "addons.h"
#include "street_events.h"
#include "weaponwatch.h"
#include "shop_manager.h"
#include "textinput.h"
#include "ws_client.h"

namespace hikaye {

void Log(const std::string& msg) {
    try {
        std::error_code ec;
        std::filesystem::create_directories(kBaseDir, ec);
        std::ofstream out(std::string(kBaseDir) + "/mod.log", std::ios::app);
        if (out) {
            out << msg << "\n";
            out.flush();
        }
    } catch (...) {}
    OutputDebugStringA(("[HikayeRP] " + msg + "\n").c_str());
}

// Command line parsing for -noIntro
namespace {
bool g_skipIntro = false;
}

void ParseCommandLine() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 0; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"-noIntro" || arg == L"/noIntro") {
                g_skipIntro = true;
                Log("Command line: -noIntro detected, skipping intro video");
            }
        }
        LocalFree(argv);
    }
}

bool IsSkipIntro() {
    return g_skipIntro;
}

}  // namespace hikaye

// Crash dump filter - minidump yazıp SHV'nin de yakalamasını sağla
// Global namespace'de olmalı (DllMain'den çağrılacak)
namespace hikaye {
const char* g_active_stage = "Baslangic";
}  // namespace hikaye

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    if (ep && ep->ExceptionRecord) {
        DWORD code = ep->ExceptionRecord->ExceptionCode;
        // Ignore benign exceptions like thread naming (0x406D1388) or C++ standard throws (0xE06D7363)
        if (code != EXCEPTION_ACCESS_VIOLATION &&
            code != EXCEPTION_ILLEGAL_INSTRUCTION &&
            code != EXCEPTION_STACK_OVERFLOW &&
            code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED &&
            code != EXCEPTION_DATATYPE_MISALIGNMENT) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        void* addr = ep->ExceptionRecord->ExceptionAddress;
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "\n========================================\n"
            "*** COKME (CRASH) YAKALANDI ***\n"
            "Hata Kodu: 0x%08X\n"
            "Hata Adresi: 0x%p\n"
            "Son Calisan Alt Sistem: %s\n"
            "========================================",
            code, addr, hikaye::g_active_stage);
        hikaye::Log(buf);

        if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
            ULONG_PTR op = ep->ExceptionRecord->ExceptionInformation[0];
            ULONG_PTR target = ep->ExceptionRecord->ExceptionInformation[1];
            const char* op_str = (op == 0) ? "Okuma (Read)" : (op == 1 ? "Yazma (Write)" : "Yurutme / DEP (Execute)");
            std::snprintf(buf, sizeof(buf), "Erisim Ihlali Detayi: %s | Hedef Bellek: 0x%p", op_str, (void*)target);
            hikaye::Log(buf);
        }
    }

    wchar_t path[MAX_PATH];
    GetTempPathW(MAX_PATH, path);
    wcscat_s(path, L"HikayeTurkRP_crash.dmp");
    
    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei = { GetCurrentThreadId(), ep, TRUE };
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &mei, NULL, NULL);
        CloseHandle(hFile);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

namespace hikaye {

constexpr int kCompanionPort = 8765;  // TODO: config'ten okunacak

// DLL kapanırken thread'leri durdurmaya çalışmamak için bilerek sızdırılır (bkz. ws_client.cpp).
WsClient* g_ws = nullptr;

// GTML layout JSON from Companion WebSocket
std::string g_gtml_layout_json;
std::mutex g_gtml_layout_mutex;

namespace {

void ScriptLoop() {
    if (hikaye::overlay::PopNotificationSoundRequest()) {
        AUDIO::PLAY_SOUND_FRONTEND(-1, "Text_Arrive_Tone", "Phone_SoundSet_Default", 1);
    }
    Log("ScriptLoop baslatildi.");
    // ---- Açılış ekranı geçişi ----
    // Oyun yükleme ekranından çıkana kadar bekle
    for (int i = 0; i < 600; ++i) {
        if (!DLC2::GET_IS_LOADING_SCREEN_ACTIVE()) break;
        WAIT(100);
    }
    SCRIPT::SHUTDOWN_LOADING_SCREEN();

    // Oyuncu karakteri dünyada oluşana kadar bekle
    for (int i = 0; i < 600; ++i) {
        Ped p = PLAYER::PLAYER_PED_ID();
        if (p != 0 && ENTITY::DOES_ENTITY_EXIST(p)) break;
        WAIT(100);
    }
    WAIT(500);
    Log("Oyun ve oyuncu hazir.");

    xpad::Disarm();  // betik başladıysa hikâye yüklendi: Geri tuşu enjeksiyonu biter
    ApplyPendingReset();  // "Sıfırdan başla" seçildiyse: kayıtlar yedeklenir, hikaye sıfırdan başlar (modüller kayıt okumadan önce)
    controls::Load();
    ShopManager shops;
    Interaction interaction;
    addons::Scan();   // HikayeTurkRP/addons/*: eklenti dükkanları, eşyalar, araçlar, görevler, siteler
    shops.LoadAll();  // sadece JSON okur; dünyada hiçbir şey oluşturmaz (NPC'ler mod aktifken doğar)

    // WsClient, moda ilk girilene kadar başlatılmaz (hikayede ağ bağlantısı bile kurulmaz).
    static WsClient dummy;
    WsClient* ws = &dummy;
    Conversation conversation(*ws);
    // Veri yükleme hatası tüm modu öldürmesin: ilgili özellik devre dışı kalır, hata loglanır.
    JailSystem jail;
    try {
        jail.Load();
    } catch (const std::exception& e) {
        Log(std::string("jail.Load HATA: ") + e.what());
    }
    conversation.SetJail(&jail);
    Services services;
    try {
        services.Load();
    } catch (const std::exception& e) {
        Log(std::string("services.Load HATA: ") + e.what());
    }
    conversation.SetServices(&services);
    // ImGui katmanı ve klavye kancası DllMain'de kaydedilir (SDK kuralı: "must be called on dll attach").
    Phone phone;
    WebBrowser browser;
    TvOverlay tv;
    std::string tv_init_error;
    tv.Initialize(tv_init_error);
    if (!tv_init_error.empty()) {
        Log("TV Initialize HATA: " + tv_init_error);
    }
    Radio radio;
    std::string radio_init_error;
    if (!radio.Initialize(radio_init_error)) {
        Log("Radyo Initialize HATA: " + radio_init_error);
    }
    std::string pending_qr;  // durakta QR okutuldu: telefon tarayıcısı bu adreste açılır
    std::string requested_gtml_url;
    Marketplace market;
    Estate estate;
    WebFeed web_feed;
    ProactiveFavor proactive;
    NativePhone native_phone;  // oyunun telefon rehberine "Süper Rehber" kişisi (sadece mod içinde)
    native_phone.Load();
    Places places;
    try {
        places.Load();
        for (const auto& s : shops.shops()) places.AddNamed(s.name, s.pos);  // "Davis Oto Galeri nerede?"
    } catch (const std::exception& e) {
            Log(std::string("places.Load HATA: ") + e.what());
        }
        conversation.SetPlaces(&places);

        IncomingCall incoming;  // karakterin sana telefon açması (örn. Tracey)
    Missions::Talk incoming_talk;
    Missions missions;  // görev NPC'leri ve ilerleme (mod aktifken çalışır)
    jail.SetFreeReleaseProvider([&missions](bool consume) { return consume ? missions.ConsumeFreeRelease() : missions.HasFreeRelease(); });
    Interiors interiors;  // kapalı iç mekanlar (IPL) ve giriş/çıkış noktaları
    interiors.Load();
    Garage garage;  // satın alınan araçlar (karaktere özel)
    int garage_slot = -1;
    static Garage* s_garage = nullptr;
    s_garage = &garage;
    economy::SetVehicleBoughtHook([](Vehicle v, const std::string& model) { if (s_garage) s_garage->Register(v, model); });
    fuel::Load();
    businesses::Load();
    market.Load();
    market.SetGarage(&garage);
    estate.Load();
    web_feed.Load();
    inventory::Load();
    phoneos::Bind({&services, &missions, &places, &shops, &jail});
    phoneos::Load();
    conversation.SetVendorAsk([&]() {
        Vector3 coords;
        Ped playerPed;
        if (safe::GetPlayerPed(PLAYER::PLAYER_ID(), playerPed) && safe::GetCoords(playerPed, coords)) {
            return vendors::AskAround(shops.shops(), coords);
        }
        return nlohmann::json();
    });
    weaponwatch::SetJail(&jail);
    weaponwatch::SetSafeZone([&shops](const Vector3& p) {  // dükkân çevresi (30 m): silah ihbarı/çevirme yok
        for (const auto& s : shops.shops()) {
            const float dx = p.x - s.pos.x, dy = p.y - s.pos.y;
            if (dx * dx + dy * dy < 30.f * 30.f) return true;
        }
        return false;
    });
    static street_events::PoliceCallEvent crime_call;  // suçu gören tanığın polisi araması (taciz sistemiyle aynı olay)
    crimes::SetPoliceCaller([&jail](Ped witness, int stars, const std::string& crime_kind) {
        if (jail.active() || crime_call.active()) return false;
        karma::Toast("Bir tanık polisi arıyor!", 5);
        return crime_call.Start(witness, [&jail, stars, crime_kind](bool player_armed) {
            CaseFacts f;
            f.player_armed = player_armed;
            f.stars = stars;
            f.offense = crimes::Title(crime_kind);
            jail.OnPoliceCalled(f);
        });
    });
    weaponwatch::SetDialogueStarter([&](Ped cop, const std::string& reason) {
        Vector3 coords;
        Ped playerPed;
        if (safe::GetPlayerPed(PLAYER::PLAYER_ID(), playerPed) && safe::GetCoords(playerPed, coords)) {
            return conversation.StartPoliceStop(cop, coords, shops.pool(), nlohmann::json{{"reason", reason}});
        }
        return false;
    });
    web_feed.SetExtraTopics([]() { return crimes::Topics(5); });
    crimes::SetWitnessExclusion([&shops, &garage](Ped p) {
        if (garage.ValetDriver() != 0 && garage.ValetDriver() == p) return true;  // Vale gorevlisi tanik olamaz
        for (const auto& s : shops.shops()) {
            if (s.ped == p) return true;
        }
        return IsNativeVendorModel(ENTITY::GET_ENTITY_MODEL(p));
    });
    crimes::SetServiceExemption([&services, &garage](Ped p, Vehicle v) {
        if (garage.IsOwned(v) || (garage.ValetVeh() != 0 && garage.ValetVeh() == v)) return true;
        if (garage.ValetDriver() != 0 && garage.ValetDriver() == p) return true;
        return services.IsHiredService(p, v);
    },
    [&services](const Vector3& pos, float r) { return services.HiredServiceNear(pos, r); });
    crimes::SetOwnedPredicate([&garage](Vehicle v) { return garage.IsOwned(v); });
    crimes::SetCarTheftWitnessBlindSpot([&garage](const Vector3& pos) {
        constexpr float radius_sq = 25.f * 25.f;
        for (const Vehicle v : garage.Handles()) {
            Vector3 owned_pos;
            if (safe::GetCoords(v, owned_pos)) {
                const float dx = pos.x - owned_pos.x;
                const float dy = pos.y - owned_pos.y;
                if (dx * dx + dy * dy <= radius_sq) return true;
            }
        }
        return false;
    });
    proactive.Load();
    transit::Load();
    hitchhike::Load();
    conversation.SetGarage(&garage);
    web_feed.SetSender([&conversation](const std::string& s) { conversation.SendRaw(s); });
    conversation.SetPostsHandler([&web_feed](const nlohmann::json& j) { web_feed.OnPosts(j); });
    Hangout hangout;  // arkadaşı arabaya davet (GTA V hangout mantığı)
    conversation.SetHangout(&hangout);
    conversation.SetEstate(&estate);
    Installer installer;  // bilgisayar başında kurulum sihirbazı (Burak görevi)
    try {
        missions.Load();
    } catch (const std::exception& e) {
        Log(std::string("missions.Load HATA: ") + e.what());
    }
    conversation.SetMissions(&missions);
    StoryBlips story_blips;  // mod aktifken hikaye görev blipleri gizlenir
    story_blips.Load();
    Mode mode;
    death::SetLoadoutRestorer([&mode]() { mode.RestoreRpLoadout(); });
    mode.Load();
    barber::SetHairColorProvider([&mode](int& color, int& highlight) {
        color = mode.profile().hair_color;
        highlight = mode.profile().hair_highlight;
    });
    missions.SetAlterSpawner([&mode](float x, float y, float z, float h, std::string& name) {
        return mode.SpawnAlter(x, y, z, h, name);
    });
    PauseMenu pause_probe;
    pause_probe.Load();
    Log("HikayeTurkRP hazir (hikaye modunda uyuyor), dukkan sayisi: " + std::to_string(shops.shops().size()));

    int frame = 0;
    bool ws_started = false;
    int crashCount = 0;
    
    #define TRACE_STEP(name) do { \
        g_active_stage = name; \
        if (frame < 2) Log(std::string("Adim: ") + name); \
    } while(0)

    while (true) {
        // Frame logic with C++ try/catch for C++ exceptions
        try {
            TRACE_STEP("pause_probe.Update");
            pause_probe.Update();
            if (pause_probe.ConsumeOnlineTabSelected()) {
                UI::SET_PAUSE_MENU_ACTIVE(false);
                mode.RequestMenu();
            }
            // 1) Hikaye RP modu: hikayede uyur, sadece etkinleştirme tuşunu dinler. Menü/oluşturucu/geçişleri yönetir.
            TRACE_STEP("mode.Update");
            mode.Update(shops.pool());
            // Bölüm 1 bitti (final sahnesinden sonra): kadın karakter oluşturucu açılır; serbest dolaşımda karakter değiştirme tuşu çalışır.
            if (mode.active() && missions.TakeFlag("bolum2_bekliyor")) mode.BeginChapter2(shops.pool());
            mode.SetSwitchAllowed(missions.FlagIs("serbest_dolasim", "1"));
            if (mode.ConsumeExited()) {  // moddan çıkıldı: dünyaya bıraktığımız her şeyi temizle
                conversation.Reset();
                shops.DespawnAll();
                services.Shutdown();
                jail.Reset();
                death::Update(false);
                PLAYER::SET_WANTED_LEVEL_MULTIPLIER(1.0f);  // mod kapanınca oyunun kendi suç sistemi geri gelir
                phone.Close();
                missions.Shutdown();
                interiors.Disable();
                hangout.Shutdown();
                garage.Shutdown();
                karma::Shutdown();
                crimes::Shutdown();
                inventory::Shutdown();
                vendors::Shutdown();
                weaponwatch::Reset();
                estate.Shutdown();
                phoneos::Shutdown();
                barber::Shutdown();
                binco::Shutdown();
                admin::Reset();
                binco::Reset();
                barber::Reset();
                customs::Reset();
                pcos::Shutdown();
                market.Reset();
                garage_slot = -1;
                fuel::Reset();
                vehctl::Reset();
                businesses::Reset();
                transit::Reset();
                hitchhike::Reset();
                passenger::Clear();
                missions.SetInCarWith("");
                incoming.Hangup();
                Log("mod kapandi: sistemler temizlendi");
            }
            if (mode.ConsumeEntered()) {
                interiors.Enable();
                if (!ws_started) {
                    g_ws = new WsClient();  // bilerek sızdırılır
                    g_ws->Start(L"127.0.0.1", kCompanionPort);
                    ws = g_ws;
                    conversation.SetWs(g_ws);
                    // GTML WebSocket handler: receives Display List JSON from Companion
                    g_ws->SetMessageHandler([](const std::string& type, const std::string& payload) {
                        if (type == "gtml_layout") {
                            std::lock_guard<std::mutex> lock(g_gtml_layout_mutex);
                            g_gtml_layout_json = payload;
                        } else if (type == "gtml_event") {
                            // gta.trigger event from headless browser
                            Log("GTML event: " + payload);
                        }
                    });
                    ws_started = true;
                }
            }


            // Hikaye görev blipleri: mod aktifken gizli (bizim görev blipimiz hariç), çıkınca geri gelir.
            {   // Oyunun kendi dükkanları (Ammu-Nation, kıyafet, berber, dövme) mod aktifken kapalıdır: alışveriş modun marketlerinden yapılır.
                // Dükkân denetleyicisi (shop_controller) ÇALIŞMAYA DEVAM EDER: kapıları açık tutar, mağazalar kapalı kalmaz; sadece alışveriş arayüzü scriptleri kapatılır.
                static bool native_shops_off = false;
                static unsigned long long native_shop_t = 0;
                const unsigned long long ns_now = GetTickCount64();
                if (mode.active()) {
                    if (ns_now - native_shop_t > 2000ULL) {
                        native_shop_t = ns_now;
                        for (const char* n : {"clothes_shop_sp", "barbershop", "gunclub_shop", "tattoo_shop", "carmod_shop"}) {
                            GAMEPLAY::TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME(const_cast<char*>(n));
                        }
                        native_shops_off = true;
                    }
                    // Oyunun kendi satıcıları (market tezgâhtarı, Ammu-Nation, kıyafet, berber, dövme) silinir; modun kendi satıcıları dokunulmaz
                    static unsigned long long clerk_t = 0;
                    if (ns_now - clerk_t > 1500ULL) {
                        clerk_t = ns_now;
                        int world_peds[128];
                        const int np = worldGetAllPeds(world_peds, 128);
                        for (int i = 0; i < np; ++i) {
                            Ped wp = world_peds[i];
                            if (wp == PLAYER::PLAYER_PED_ID() || !safe::Valid(wp)) continue;
                            Hash model;
                            if (!safe::GetModel(wp, model)) continue;
                            if (!IsNativeVendorModel(model)) continue;
                            bool ours = false;
                            for (const auto& s : shops.shops()) ours = ours || s.ped == wp;
                            if (ours) continue;
                            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(wp, true, true);
                            ENTITY::DELETE_ENTITY(&wp);
                        }
                    }
                } else if (native_shops_off) {
                    native_shops_off = false;  // alt dükkân scriptleri oyunun denetleyicisi tarafından yeniden başlatılır
                }
            }

            if (mode.active()) {
                const Ped player = PLAYER::PLAYER_PED_ID();
                if (!ENTITY::DOES_ENTITY_EXIST(player)) {
                    WAIT(0);
                    continue;
                }
                const Vector3 pos = ENTITY::GET_ENTITY_COORDS(player, true);
                const bool e_pressed = interaction.InteractPressed();
                // Telefon tuşu (data/controls.json, varsayılan O; P oyunun Pause tuşuyla çakışır).
                // Ekran klavyesi açıkken yazılan harf tetiklemesin.
                static bool p_was_down = false;
                const bool p_down = (GetAsyncKeyState(controls::PhoneKey()) & 0x8000) != 0;
                const bool p_pressed = p_down && !p_was_down && !conversation.typing();
                p_was_down = p_down;
                static bool escape_was_down = false;
                const bool escape_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
                const bool escape_pressed = escape_down && !escape_was_down;
                escape_was_down = escape_down;
                const bool tv_ui_busy = conversation.active() || conversation.typing() || phone.open() ||
                                        browser.active() || jail.active() || installer.active() || pcos::Active();
                const bool tv_consumed = tv.Update(pos, !tv_ui_busy, e_pressed, p_pressed, escape_pressed,
                                                   TIME::GET_CLOCK_HOURS(), TIME::GET_CLOCK_MINUTES());
                if (!tv_ui_busy && !tv.IsOpen() && tv.CanUse()) ShowHelp("~INPUT_PICKUP~ Televizyonu kullan");
                // TV overlay notification (Material style channel info)
                if (tv.IsOpen()) {
                    const TvChannel* ch = tv.CurrentChannel();
                    if (ch) {
                        overlay::SetTVVisible(true, "CH " + std::to_string(ch->channel_no) + ": " + ch->name);
                    }
                } else {
                    overlay::SetTVVisible(false, "");
                }
                const bool world_e = e_pressed && !tv_consumed;

                // Hikaye görev blipleri: mod aktifken gizle (mode.json hide_story_blips ayarlarına göre)
                story_blips.Update(mode.active(), [&](Blip b) {
                    // Kendi bliplerimizi koru: görev blipleri, dükkân blipleri, ev blipleri, benzin blipleri, garaj blipleri
                    if (shops.OwnsBlip(b)) return true;
                    if (estate.OwnsBlip(b)) return true;
                    if (fuel::OwnsBlip(b)) return true;
                    if (garage.OwnsBlip(b)) return true;
                    if (missions.OwnsBlip(b)) return true;
                    return false;
                });

                if (frame++ % 5 == 0) shops.Update(pos);  // spawn/despawn her frame kontrol edilmez
                // Kurulum sihirbazı açıkken diğer etkileşimler durur.
                if (installer.active()) {
                    bool ok = false;
                    int mistakes = 0;
                    if (installer.Update(ok, mistakes)) missions.InstallFinished(ok, mistakes);
                }
                const bool portal_used = !installer.active() && !conversation.active() && interiors.Update(pos, world_e);
                {
                    bool entering = false;
                    float dest[4];
                    Vector3 from{};
                    if (interiors.ConsumeTransition(entering, dest, from)) missions.FollowThroughPortal(from, entering, dest);
                }
                const bool free_e = world_e && !portal_used && !installer.active() && !conversation.active();  // (benzin E'si aşağıda ayrıca işlenir)
                if (garage_slot != mode.slot()) {  // karakter değişince garaj da değişir
                    if (garage_slot >= 0) garage.Save();
                    conversation.SetSaveSlot(mode.slot());
                    karma::Load(mode.slot());
                    crimes::Load(mode.slot());
                    inventory::LoadSave(mode.slot());
                    vendors::LoadSave(mode.slot());
                    estate.LoadSave(mode.slot());
                    phoneos::LoadSave(mode.slot());
                    pcos::LoadSave(mode.slot());
                    barber::LoadSave(mode.slot());
                    binco::LoadSave(mode.slot());
                    market.Reset();
                    garage_slot = mode.slot();
                    garage.Load(garage_slot);
                    fuel::Reset();
                }
                TRACE_STEP("garage.Update");
                garage.Update();
                karma::Update();
                crimes::Update();
                death::Update(true);
                vendors::Update(shops.shops(), pos);
                inventory::Update(conversation.active() || phone.open() || browser.active() || jail.active() || installer.active() || conversation.typing());
                crime_call.Update();
                // Oyunun kendi suçları kapalı: yıldızı sadece bizim sistem verir (tanık ihbarı, cinayet izleme). Başka kaynaktan gelen yıldız silinir.
                if (!jail.active() && !jail.OwnWanted() && PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) > 0) PLAYER::CLEAR_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID());
                PLAYER::SET_WANTED_LEVEL_MULTIPLIER(0.0f);  // oyunun kendi suç->yıldız sistemi kapalı: suçları script yönetir (tanık polisi arar)
                weaponwatch::Update();
                proactive.Update(conversation.active() || phone.open() || browser.active() || jail.active() || installer.active() ||
                                     incoming.active(),
                                 pos, &places,
                                 [&](Ped p, const nlohmann::json& f) { return conversation.StartProactive(p, pos, shops.pool(), f); });
                market.Update();
                estate.Update(pos, world_e && !conversation.active());
                garage.SetExtraGarages(estate.GarageSpots());
                bool fuel_e_used = false;
                fuel::Update(pos, world_e && !conversation.active(), fuel_e_used);
                radio.Update(mode.active());
                businesses::Update(pos, world_e && !conversation.active());
                garage.UpdateGarageSpots(pos, world_e && !conversation.active());
                {
                    const bool ui_busy = conversation.active() || conversation.typing() || phone.open() || browser.active() || jail.active() || installer.active() || binco::MenuOpen() || estate.MenuOpen() || tv.IsOpen();
                    TRACE_STEP("transit::Update");
                    transit::Update(pos, world_e && !conversation.active(), ui_busy);
                    TRACE_STEP("hitchhike::Update");
                    hitchhike::Update(pos, world_e && !conversation.active(), ui_busy);
                    const std::vector<Vector3> owned_homes = estate.OwnedPositions();
                    TRACE_STEP("phoneos::Update");
                    phoneos::Update(pos, phone.open(), owned_homes);
                    TRACE_STEP("pcos::Update");
                    pcos::Update(pos, owned_homes);
                    TRACE_STEP("customs::Update");
                    customs::Update(pos, world_e && !conversation.active(), ui_busy, [&](Vehicle v) { return garage.IsOwned(v); });
                    TRACE_STEP("barber::Update");
                    barber::Update(pos, world_e && !conversation.active(), ui_busy);
                    TRACE_STEP("binco::Update");
                    binco::Update(pos, world_e && !conversation.active(), ui_busy);
                    TRACE_STEP("admin::Update");
                    admin::Update(ui_busy, [&]() {  // hile menüsü ışınlanma hedefleri: modda ne varsa
                        std::vector<admin::Dest> d;
                        for (const auto& s : shops.shops()) d.push_back({"Dükkânlar", s.name + " (" + s.id + ")", s.pos});
                        for (const char* t : {"barber", "clothing", "customs", "garage"}) {
                            const char* cat = std::string(t) == "barber" ? "Berberler" : std::string(t) == "clothing" ? "Kıyafet (Binco)" : std::string(t) == "customs" ? "Modifiye" : "Garajlar";
                            for (const auto& sp : businesses::Spots(t)) d.push_back({cat, sp.name, sp.pos});
                        }
                        for (const auto& l : transit::MapLines()) {
                            int i = 0;
                            for (const auto& pt : l.pts) d.push_back({"Duraklar", "Hat " + l.id + " durak " + std::to_string(++i), pt});
                        }
                        for (const auto& h : estate.AllHouses()) d.push_back({"Evler", h.first, h.second});
                        Vector3 mt{};
                        std::string label;
                        if (missions.CurrentTargetPos(mt, label)) d.push_back({"Görev", "Görev hedefi: " + label, mt});
                        return d;
                    });
                    if (pcos::ConsumeLaptopRequest() && !ui_busy) {  // envanterden dizüstü kullanıldı
                        std::string why;
                        if (pcos::TryOpenLaptop(owned_homes, why)) browser.Open("pc/home");
                        else karma::Toast(why, 6);
                    }
                    std::string qr;
                    if (!jail.active() && transit::ConsumeQr(qr)) pending_qr = qr;
                }
                TRACE_STEP("vehctl::AutoDrive");
                vehctl::AutoDrive(conversation.typing());  // araçta yazarken otomatik sürüş
                vehctl::Update(conversation.typing() || conversation.active() || installer.active(),
                               [&](Vehicle v) { return garage.IsOwned(v); }, [&]() { return garage.Handles(); });
                hangout.Update();
                passenger::Tick();
                TRACE_STEP("missions.SetInCarWith");
                missions.SetInCarWith(hangout.Seated() ? hangout.Who().npc_id : std::string());
                TRACE_STEP("missions.Update");
                if (!jail.active()) missions.Update(pos, free_e);  // hapisteyken görev NPC/blipi yok
                {
                    Missions::InstallSpec spec;
                    if (!installer.active() && missions.ConsumeInstallRequest(spec)) {  // iş bilgisayarı: önce masaüstü (İnternet, Kurulum)
                        pcos::OpenWork(spec);
                        browser.Open("pc/home");
                    }
                    Missions::InstallSpec chosen;
                    if (!installer.active() && pcos::ConsumeInstall(chosen)) {
                        browser.Close();
                        pcos::Close();
                        installer.Begin(chosen);
                    }
                }
                conversation.UpdateEvents();              // araba sahnesi, polis arama gibi oturumdan bağımsız olaylar
                TRACE_STEP("services.Update");
                services.Update(interaction);                        // çağrılan araçlar ve taksi/helikopter yolculuğu
                jail.SetInputBlocked(conversation.typing());
                TRACE_STEP("jail.Update");
                jail.Update();                            // tazer, tutuklama, hücre, ceza, cinayet izleme

                // Telefon aranma sırasında kullanılabilir; tazer ve gözaltında kapalı kalır.
                if (!jail.CanUsePhone()) phone.Close();
                if (!phone.open() && !pcos::Active()) browser.Close();
                std::vector<Missions::Contact> contacts = missions.Contacts();  // görev bitince eklenen kişiler
                {
                    Missions::Contact nav;  // telefon uygulaması: toplu taşıma navigasyonu
                    nav.id = "transit_nav";
                    nav.name = "Toplu Taşıma Navigasyon";
                    contacts.insert(contacts.begin(), nav);
                    Missions::Contact web;  // telefon tarayıcısı: Bleeter, Weazel News, GeneralSale
                    web.id = "web_browser";
                    web.name = "Internet (Bleeter, Weazel News...)";
                    contacts.insert(contacts.begin() + 1, web);
                    Missions::Contact mech, park;  // rehberde hazır hatlar: tamirci ve otopark görevlisi (araç işleri)
                    mech.id = "mechanic_line";
                    mech.name = "Mekanik / Tamirci (onarim)";
                    park.id = "parking_line";
                    park.name = "Otopark Gorevlisi (arac getirir)";
                    contacts.insert(contacts.begin() + 2, mech);
                    contacts.insert(contacts.begin() + 3, park);
                }
                std::string call_contact;
                hml::Context web_ctx;  // tarayıcı sayfalarının görüp koşul olarak kullanabildiği oyun durumu
                web_ctx.flag = [&](const std::string& k) {
                    if (k.rfind("karma", 0) == 0 || k.rfind("count_", 0) == 0) return karma::Lookup(k);
                    return missions.FlagValue(k);
                };
                web_ctx.mission_state = [&](const std::string& id) { return missions.MissionState(id); };
                web_ctx.source = [&web_ctx, &estate, &market, &web_feed, &shops, &tv](const std::string& n) {
                    if (n == "crime_news") return crimes::NewsItems(6);
                    if (n == "vendor_hints") return vendors::HintItems(shops.shops());
                    if (n == "tv_status") {
                        hml::Item status;
                        status.text = tv.IsPhoneWatching()
                                          ? (tv.PlaybackStatus().empty() ? "Yayin acik" : tv.PlaybackStatus())
                                          : "Bir kanal secerek baslat.";
                        return std::vector<hml::Item>{status};
                    }
                    if (n == "tv_channels") {
                        std::vector<hml::Item> channels;
                        for (const auto& channel : tv.Channels()) {
                            hml::Item item;
                            item.kind = hml::Kind::Card;
                            item.text = "CH " + std::to_string(channel.channel_no) + ": " + channel.name;
                            item.sub = channel.streamtype;
                            item.href = "act:tvwatch:" + std::to_string(channel.channel_no);
                            item.selectable = true;
                            channels.push_back(std::move(item));
                        }
                        return channels;
                    }
                    if (n.rfind("ai_", 0) == 0) return web_feed.Source(n, web_ctx);
                    return n.rfind("estate_", 0) == 0 ? estate.Source(n) : market.Source(n);
                };
                web_ctx.action = [&](const std::string& c) {
                    if (c.rfind("ph:", 0) == 0) return phoneos::Action(c.substr(3));
                    if (c.rfind("pc:", 0) == 0) return pcos::Action(c.substr(3));
                    if (c.rfind("tvwatch:", 0) == 0) {
                        const int channel_no = std::atoi(c.c_str() + 8);
                        return tv.WatchPhoneChannel(channel_no) ? std::string("Canli yayin baslatiliyor.")
                                                               : std::string("Kanal bulunamadi.");
                    }
                    if (c == "tvstop") {
                        tv.StopPhonePlayback();
                        return std::string("Yayin durduruldu.");
                    }
                    if (c.rfind("look:", 0) == 0) return estate.Action(c);
                    return market.Action(c);
                };
                web_ctx.cash = economy::GetCash();
                web_ctx.hour = TIME::GET_CLOCK_HOURS();
                phone.UpdateCursorToggle();
                browser.SetMouse(browser.active() && (pcos::Active() || phone.cursor()),
                                 !pcos::Active() && !phone.cursor());
                const bool browser_was_active = browser.active();
                if (browser_was_active && browser.Update(web_ctx, escape_pressed)) {
                    phone.Close();
                    pcos::Close();
                }
                const std::string current_web_url = browser.CurrentUrl();
                const bool is_example_site_path = current_web_url == "example.com/index" ||
                                                  current_web_url.rfind("example.com/", 0) == 0;
                const bool is_example_site = is_example_site_path &&
                    !(current_web_url.size() >= 4 &&
                      current_web_url.compare(current_web_url.size() - 4, 4, ".hml") == 0);
                const bool is_local_gtml = !is_example_site && current_web_url.size() >= 5 &&
                    current_web_url.compare(current_web_url.size() - 5, 5, ".gtml") == 0;
                const bool python_gtml_page = browser.active() && (is_example_site || is_local_gtml);
                if (python_gtml_page && g_ws && g_ws->connected()) {
                    if (requested_gtml_url != current_web_url) {
                        requested_gtml_url = current_web_url;
                        {
                            std::lock_guard<std::mutex> lock(g_gtml_layout_mutex);
                            g_gtml_layout_json.clear();
                        }
                        nlohmann::json request{{"type", "gtml_open"}};
                        std::string path = current_web_url;
                        if (is_example_site && (path.size() < 5 ||
                            path.compare(path.size() - 5, 5, ".gtml") != 0)) {
                            path += ".gtml";
                        }
                        request["path"] = path;
                        g_ws->Send(request.dump());
                    }
                } else {
                    if (!python_gtml_page) {
                        requested_gtml_url.clear();
                        std::lock_guard<std::mutex> lock(g_gtml_layout_mutex);
                        g_gtml_layout_json.clear();
                    } else {
                        requested_gtml_url.clear();
                    }
                }
                if (tv.IsPhoneWatching() &&
                (!browser.active() ||
                 (browser.CurrentUrl().rfind("phone/tv", 0) != 0 &&
                  browser.CurrentUrl().rfind("pc/tv", 0) != 0))) {
                tv.StopPhonePlayback();
                }
                const bool escape_ended_phone_call = escape_pressed && !browser_was_active && phone.open() &&
                                                     conversation.active() && !conversation.RideConversation() &&
                                                     !conversation.typing();
                if (escape_ended_phone_call) {
                    conversation.End("esc ile telefon gorusmesi sonlandirildi");
                }
                if (!pending_qr.empty()) {  // QR: telefon açılır, tarayıcı durağın sayfasına gider
                    if (!phone.open()) phone.Open();
                    browser.OpenAt(pending_qr, web_ctx);
                    pending_qr.clear();
                }
                const bool want_call = phone.Update(conversation.active() && !conversation.RideConversation(),
                                                    conversation.typing() || browser.active(),
                                                    escape_pressed && !escape_ended_phone_call && !browser_was_active &&
                                                        jail.CanUsePhone() && !browser.active(),
                                                    services, contacts, call_contact);
                // Oyunun telefonu (↑) → Rehber → "Süper Rehber": native telefon kapanır, bizim telefon açılır.
                // Oyunun kendi telefonu iptal: ↑ tuşu bizim telefonumuzu açar (Haviland, rehber, internet, Maps, Maze Bank, App Store)
                CONTROLS::DISABLE_CONTROL_ACTION(0, 27, true);  // INPUT_PHONE
                if (browser.active()) {
                    if (pcos::Active() || browser.typing()) {
                        CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);  // bilgisayar başında ya da yazı yazarken: harfler yürüyüşü tetiklemesin
                    } else {
                        // Telefon: yürüyüş/koşu/kamera serbest; sadece telefonun kendi tuşları (yön okları, Enter/Geri) oyuna gitmesin
                        for (int c : {27, 172, 173, 174, 175, 176, 177, 201, 202}) CONTROLS::DISABLE_CONTROL_ACTION(0, c, true);
                    }
                    for (int c : {27, 172, 173, 174, 175, 176, 177}) CONTROLS::DISABLE_CONTROL_ACTION(2, c, true);
                }
                {
                    static bool up_was = false;
                    const bool up_down = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
                    const bool up_pressed = up_down && !up_was;
                    up_was = up_down;
                    const bool busy = conversation.active() || conversation.typing() || phone.open() || browser.active() || pcos::Active() || tv.IsOpen() || customs::MenuOpen() || barber::MenuOpen() || binco::MenuOpen() || admin::MenuOpen() || (jail.active() && !jail.CanUsePhone()) || installer.active() ||
                                      inventory::MenuOpen() || garage.MenuOpen() || estate.MenuOpen();
                    if (up_pressed && !busy && !phoneos::Dead()) phone.Open();
                    else if (up_pressed && !busy) karma::Toast("Telefonun şarjı bitti. Market, otobüs, araç çakmaklığı ya da evde şarja tak.", 4);
                }
                if (phone.open() && phoneos::Dead()) phone.Close();  // pil bitince açılmaz
                {
                    static bool phone_in_hand = false;
                    static Ped phone_ped = 0;
                    const bool should_hold_phone = phone.open();
                    if (should_hold_phone && !phone_in_hand) {
                        AI::TASK_USE_MOBILE_PHONE(player, 1);
                        phone_in_hand = true;
                        phone_ped = player;
                    } else if (!should_hold_phone && phone_in_hand) {
                        AI::TASK_USE_MOBILE_PHONE(phone_ped, 0);
                        phone_in_hand = false;
                        phone_ped = 0;
                    }
                }
                // Açık telefon = ana ekran (eski liste arayüzü yok): görüşme bitince ana ekrana dönülür
                if (phone.open() && !browser.active() && (!conversation.active() || conversation.RidePaused())) browser.Open();
                // Bilgisayar: oyun açıkken tarayıcı yok, oyun ekranı çizilir; oyundan çıkınca masaüstüne dönülür
                if (pcos::GameActive()) {
                    browser.Close();
                    CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
                    if (pcos::UpdateGame()) browser.Open("pc/home");
                } else if (pcos::Active() && !browser.active() && !conversation.active() && !installer.active()) {
                    browser.Open("pc/home");
                }
                if (phoneos::ConsumeAdminRequest()) {  // arama kutusuna phpmyadmin yazıldı: telefon kapanır, hile menüsü açılır
                    browser.Close();
                    phone.Close();
                    admin::Open();
                }
                {
                    std::string pc;
                    if (phoneos::ConsumeCall(pc)) {  // telefondan arama başlatıldı
                        call_contact = pc;
                        browser.Close();
                    }
                }
                conversation.SetPhoneVisible(phone.open());

                // Gelen arama: sıradaki görev arayarak başlıyorsa (örn. Tracey) uygun anda çalar. Cevaplarsan görüşme başlar.
                {   // teşhis: gelen arama neden çalmıyor (10 sn'de bir, sadece engel varsa)
                    static unsigned long long diag_at = 0;
                    const unsigned long long now_ms = GetTickCount64();
                    Missions::Talk probe;
                    if (now_ms > diag_at && !incoming.active()) {
                        diag_at = now_ms + 10000;
                        if (conversation.active() || jail.active() || phone.open() || conversation.typing() ||
                            PED::IS_PED_IN_ANY_VEHICLE(player, false)) {
                            Log(std::string("gelen arama engelli:") + (conversation.active() ? " konusma" : "") +
                                (jail.active() ? " hapis" : "") + (phone.open() ? " telefon" : "") +
                                (conversation.typing() ? " yazma" : "") +
                                (PED::IS_PED_IN_ANY_VEHICLE(player, false) ? " arac" : ""));
                        }
                    }
                }
                if (!incoming.active() && !conversation.active() && !jail.active() && !phone.open() && !conversation.typing() &&
                    !PED::IS_PED_IN_ANY_VEHICLE(player, false) && !phoneos::Dead()) {
                    if (phoneos::ConsumeIncomingCall(incoming_talk)) {
                        incoming.Ring(incoming_talk.name);
                    } else if (missions.IncomingCall(incoming_talk)) {
                        incoming.Ring(incoming_talk.name);
                    }
                }
                switch (incoming.Update()) {
                    case IncomingCall::Result::Answered:
                        if (incoming_talk.npc_id.rfind("op_switch:", 0) == 0) {
                            phoneos::ConfirmSwitch();
                        } else {
                            missions.IncomingHandled(true);
                        }
                        conversation.StartMissionCall(incoming_talk);
                        break;
                    case IncomingCall::Result::Declined:
                    case IncomingCall::Result::Missed:
                        if (incoming_talk.npc_id.rfind("op_switch:", 0) == 0) {
                            phoneos::RetrySwitchCall();
                        } else {
                            missions.IncomingHandled(false);
                        }
                        break;
                    default:
                        break;
                }
                if (incoming.in_call() && !conversation.active()) incoming.Hangup();  // görüşme bitti/başlamadı: telefonu cebe koy
                incoming.Draw();

                Missions::Talk talk;
                TRACE_STEP("conversation.Update");
                if (conversation.active()) {
                    conversation.Update(pos, e_pressed, p_pressed);
                } else {
                    conversation.Update(pos, false);
                    TRACE_STEP("conversation:after_update");
                    if (installer.active()) {
                    } else if (services.ConsumeRideStart()) {
                        conversation.StartRide(shops.pool());
                    } else if (hitchhike::ConsumeBoarded()) {
                        conversation.StartHitch(shops.pool());
                    } else if (call_contact == "haviland") {
                        conversation.StartPhone(shops.pool(), "Haviland: telefonun yapay zekâ asistanısın. Rehberdeki kayıtlı kişileri arayabilir; ayrıca istenirse taksi, helikopter, polis, ambulans, itfaiye, tamirci ve otopark hizmetlerini yönlendirebilirsin.", "haviland");
                    } else if (call_contact == "santral") {
                        conversation.StartPhone(shops.pool());
                    } else if (call_contact == "bar_lawyer" && jail.HasWantedCase()) {
                        conversation.StartWantedLawyer({}, shops.pool(), true);
                    } else if (call_contact == "sule" && jail.HasWantedCase()) {
                        Missions::Talk sule;
                        if (missions.PersonTalk("sule", sule)) conversation.StartWantedLawyer(sule.npc_json, shops.pool(), false);
                    } else if (call_contact.rfind("person:", 0) == 0) {
                        nlohmann::json npc;
                        if (phoneos::PersonNpc(call_contact, npc)) conversation.StartPersonCall(npc, call_contact);
                    } else if (call_contact == "web_browser") {
                        browser.Open();
                    } else if (call_contact == "transit_nav") {
                        conversation.StartPhone(shops.pool());
                    } else if (call_contact == "mechanic_line") {
                        conversation.StartPhone(shops.pool(), "tamirci (aracı onarır ve deposunu doldurur; araç getirmez, bunun için otopark görevlisini öner)", "tamirci");
                    } else if (call_contact == "parking_line") {
                        conversation.StartPhone(shops.pool(), "otopark görevlisi (garajdaki aracı getirir, dışarıdaki aracı garaja alır, aracın yerini söyler; onarım yapmaz, bunun için tamirciyi öner)", "otopark");
                    } else if (call_contact.rfind("musteri_hizmetleri:", 0) == 0) {
                        const int op_idx = std::atoi(call_contact.substr(19).c_str());
                        static const char* const kMusteriPrompt[] = {
                            "Whiz müşteri hizmetleri: Los Santos ve San Andreas genelinde en hızlı 5G kapsamasını sağlayan Amerikan mobil operatörüsün. Müşteriye Los Santos'ta hizmet verdiğini belirt, tarife bilgisi ver, paket öner (Hızlı/Standart/Premium/Kurumsal), dakika düşmez.",
                            "Graham Telecom müşteri hizmetleri: Los Santos merkezli dengeli ve popüler telekom şirketisiniz. Müşteriye Los Santos, Amerika genelinde kesintisiz hat sunduğunu söyle, paket öner (Başlangıç/Standart/Plus/Sınırsız), dakika düşmez.",
                            "Modatel müşteri hizmetleri: Los Santos'un bütçe dostu operatörüsün. Los Santos sokaklarında ekonomik iletişim sağladığını belirt, paket öner (Cep/Temel/Artı/Sınırsız), dakika düşmez.",
                        };
                        static const char* const kMusteriRol[] = {"whiz_musteri", "graham_musteri", "modatel_musteri"};
                        const int idx = op_idx >= 0 && op_idx < 3 ? op_idx : 1;
                        conversation.StartPhone(shops.pool(), kMusteriPrompt[idx], kMusteriRol[idx]);
                    } else if (!call_contact.empty()) {
                        Missions::Talk ct;
                        if (missions.CallTarget(call_contact, ct)) conversation.StartMissionCall(ct);
                    } else if (want_call) {
                        conversation.StartPhone(shops.pool());
                    } else if (phone.open()) {
                    } else if (jail.active()) {
                        if (jail.ConsumeLawyerRequest()) conversation.StartLawyer(shops.pool());
                    } else if (hangout.Seated()) {
                        ShowHelp(("~INPUT_PICKUP~ Konus: " + hangout.Who().name).c_str());
                        if (e_pressed) conversation.StartHangout(hangout.Who(), pos);
                    } else if (hangout.Arrived()) {
                        ShowHelp(("~INPUT_PICKUP~ Bulusmada konus: " + hangout.Who().name).c_str());
                        if (e_pressed) conversation.StartHangout(hangout.Who(), pos);
                    } else if (const Vehicle boardable = services.BoardableVehicle(pos)) {
                        ShowHelp("~INPUT_PICKUP~ Araca bin");
                        if (e_pressed) services.BoardPlayer(boardable);
                    } else {
                        TRACE_STEP("missions.NearestTalkable");
                        if (missions.NearestTalkable(pos, talk)) {
                            ShowHelp(("~INPUT_PICKUP~ Konus: " + talk.name).c_str());
                            if (e_pressed) conversation.StartMission(talk);
                        } else {
                            TRACE_STEP("interaction.Update");
                            if (Shop* focus = interaction.Update(shops, pos)) {
                                if (e_pressed) conversation.Start(*focus, pos);
                            } else {
                                TRACE_STEP("interaction.UpdateStreet");
                                if (const Ped street = interaction.UpdateStreet(shops, pos)) {
                                    if (e_pressed && !conversation.IsEventPed(street)) conversation.StartStreet(street, pos, shops.pool());
                                }
                            }
                        }
                    }
                }
                TRACE_STEP("conversation.Draw");
                conversation.Draw();
                TRACE_STEP("jail.Draw");
                jail.Draw();
                TRACE_STEP("missions.DrawHud");
                if (!jail.active()) missions.DrawHud();
                TRACE_STEP("phone.Publish");
                if (!installer.active()) {
                    phone.Publish(conversation.GetPhoneView(), services, contacts,
                                  !browser.active() && !escape_ended_phone_call);
                    const std::string app_url = phone.TakeAppLaunchUrl();
                    if (!app_url.empty()) browser.OpenAt(app_url, web_ctx);
                }
                TRACE_STEP("browser.Publish");
                if (browser.active() || pcos::GameActive()) {
                    char clk[8];
                    std::snprintf(clk, sizeof(clk), "%02d:%02d", TIME::GET_CLOCK_HOURS(), TIME::GET_CLOCK_MINUTES());
                    if (pcos::GameActive()) overlay::SetPhone(pcos::GameView(clk));
                    else browser.Publish(clk);
                } else if (phone.open() && phone.in_call()) {
                    // Çağrı arayüzü: telefon açıkken ve görüşme varken chat ekranını göster
                    overlay::PhoneView call_view = phone.current_view();
                    call_view.web_url = "chat://call";
                    overlay::SetPhone(call_view);
                }
                TRACE_STEP("overlay::SetStatusBar");
                {
                    char clk[8];
                    std::snprintf(clk, sizeof(clk), "%02d:%02d", TIME::GET_CLOCK_HOURS(), TIME::GET_CLOCK_MINUTES());
                    overlay::SetStatusBar(clk, phoneos::SignalText(), phoneos::BatteryText(), phoneos::Battery() <= 15 && !phoneos::Charging());
                }
            }
        } catch (const std::exception& e) {
            static int errors = 0;
            if (errors++ < 50) Log(std::string("HATA [") + g_active_stage + "]: " + e.what());
        } catch (...) {
            static int unk = 0;
            if (unk++ < 50) Log(std::string("BILINMEYEN HATA [") + g_active_stage + "] yakalandi!");
        }
        TRACE_STEP("overlay::Commit");
        overlay::Commit();  // hazırlanan altyazı, telefon ve menü verisi render thread'ine geçer
        WAIT(0);
    }
}

}  // namespace
}  // namespace hikaye

void ScriptMain() {
    hikaye::Log("ScriptMain baslatildi.");
    hikaye::ScriptLoop();
}

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            AddVectoredExceptionHandler(1, CrashFilter);
            SetUnhandledExceptionFilter(CrashFilter);
            hikaye::overlay::Init();    // Present kancası (ImGui çizimi): SDK "dll attach" şartı
            hikaye::textinput::Init();  // klavye kancası (T ile yazma, Türkçe karakterler)
            scriptRegister(hInstance, ScriptMain);
            break;
        case DLL_PROCESS_DETACH:
                    hikaye::textinput::Shutdown();
                    hikaye::overlay::Shutdown();  // kancaları kaldır (DLL gidince çağrılmasın)-
                    scriptUnregister(hInstance);
                    break;
    }
    return TRUE;
}