#include <windows.h>

#include <fstream>

#include "common.h"
#include "controls.h"
#include "conversation.h"
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
    std::ofstream out(std::string(kBaseDir) + "/mod.log", std::ios::app);
    out << msg << "\n";
}

namespace {

constexpr int kCompanionPort = 8765;  // TODO: config'ten okunacak

// DLL kapanırken thread'leri durdurmaya çalışmamak için bilerek sızdırılır (bkz. ws_client.cpp).
WsClient* g_ws = nullptr;

void ScriptLoop() {
    // ---- Açılış ekranı geçişi ----
    // 100% save yüklü olduğu için native story içeriği çıkmaz; sadece loading screen kapatılır.
    for (int i = 0; i < 600; ++i) {  // en fazla 60 sn: yükleme bitene kadar bekle
        if (!DLC2::GET_IS_LOADING_SCREEN_ACTIVE()) break;
        WAIT(100);
    }
    SCRIPT::SHUTDOWN_LOADING_SCREEN();  // yükleme / "press any key" ekranını kapat
    WAIT(300);
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
    std::string pending_qr;  // durakta QR okutuldu: telefon tarayıcısı bu adreste açılır
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
    phoneos::Bind({&services, &missions, &places, &shops});
    phoneos::Load();
    conversation.SetVendorAsk([&]() { return vendors::AskAround(shops.shops(), ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true)); });
    weaponwatch::SetJail(&jail);
    weaponwatch::SetSafeZone([&shops](const Vector3& p) {  // dükkân çevresi (30 m): silah ihbarı/çevirme yok
        for (const auto& s : shops.shops()) {
            const float dx = p.x - s.pos.x, dy = p.y - s.pos.y;
            if (dx * dx + dy * dy < 30.f * 30.f) return true;
        }
        return false;
    });
    static street_events::PoliceCallEvent crime_call;  // suçu gören tanığın polisi araması (taciz sistemiyle aynı olay)
    crimes::SetPoliceCaller([&jail](Ped witness, int stars) {
        if (jail.active() || crime_call.active()) return false;
        karma::Toast("Bir tanık polisi arıyor!", 5);
        return crime_call.Start(witness, [&jail, stars](bool player_armed) {
            CaseFacts f;
            f.player_armed = player_armed;
            f.stars = stars;
            jail.OnPoliceCalled(f);
        });
    });
    weaponwatch::SetDialogueStarter([&](Ped cop, const std::string& reason) {
        return conversation.StartPoliceStop(cop, ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true), shops.pool(), nlohmann::json{{"reason", reason}});
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
    proactive.Load();
    transit::Load();
    hitchhike::Load();
    conversation.SetGarage(&garage);
    web_feed.SetSender([&conversation](const std::string& s) { conversation.SendRaw(s); });
    conversation.SetPostsHandler([&web_feed](const nlohmann::json& j) { web_feed.OnPosts(j); });
    Hangout hangout;  // arkadaşı arabaya davet (GTA V hangout mantığı)
    conversation.SetHangout(&hangout);
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
    while (true) {
      // Bir karedeki hata tüm scripti öldürmesin: logla, bir sonraki kareden devam et.
      try {
        // 0) Pause menüsündeki Online sekmesi (deneysel): sadece okunur; eşleşirse pause kapanır, mod menüsü açılır.
        pause_probe.Update();
        if (pause_probe.ConsumeOnlineTabSelected()) {
            UI::SET_PAUSE_MENU_ACTIVE(false);
            mode.RequestMenu();
        }
        // 1) Hikaye RP modu: hikayede uyur, sadece etkinleştirme tuşunu dinler. Menü/oluşturucu/geçişleri yönetir.
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
                        if (wp == PLAYER::PLAYER_PED_ID() || !ENTITY::DOES_ENTITY_EXIST(wp)) continue;
                        if (!IsNativeVendorModel(ENTITY::GET_ENTITY_MODEL(wp))) continue;
                        bool ours = false;
                        for (const auto& s : shops.shops()) ours = ours || s.ped == wp;
                        if (ours) continue;
                        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(wp, true, true);
                        PED::DELETE_PED(&wp);
                    }
                }
            } else if (native_shops_off) {
                native_shops_off = false;  // alt dükkân scriptleri oyunun denetleyicisi tarafından yeniden başlatılır
            }
        }
        if (overlay::LoadingCoverActive() && mode.engaged()) {  // hikâyeye/oluşturucuya girildi: kapak 2 sn sonra kararıp kalkar
            static unsigned long long engaged_at = 0;
            if (engaged_at == 0) engaged_at = GetTickCount64();
            if (GetTickCount64() - engaged_at > 2000ULL) overlay::SetLoadingCover(false);
        }
        story_blips.Update(mode.active(), [&](Blip b) { return missions.OwnsBlip(b) || shops.OwnsBlip(b) || fuel::OwnsBlip(b) || businesses::OwnsBlip(b) || transit::OwnsBlip(b) || garage.OwnsBlip(b) || estate.OwnsBlip(b) || customs::OwnsBlip(b) || barber::OwnsBlip(b) || binco::OwnsBlip(b); });

        if (mode.active()) {
            const Ped player = PLAYER::PLAYER_PED_ID();
            const Vector3 pos = ENTITY::GET_ENTITY_COORDS(player, true);
            const bool e_pressed = interaction.InteractPressed();
            // Telefon tuşu (data/controls.json, varsayılan O; P oyunun Pause tuşuyla çakışır).
            // Ekran klavyesi açıkken yazılan harf tetiklemesin.
            static bool p_was_down = false;
            const bool p_down = (GetAsyncKeyState(controls::PhoneKey()) & 0x8000) != 0;
            const bool p_pressed = p_down && !p_was_down && !conversation.typing();
            p_was_down = p_down;

            if (frame++ % 5 == 0) shops.Update(pos);  // spawn/despawn her frame kontrol edilmez
            // Kurulum sihirbazı açıkken diğer etkileşimler durur.
            if (installer.active()) {
                bool ok = false;
                int mistakes = 0;
                if (installer.Update(ok, mistakes)) missions.InstallFinished(ok, mistakes);
            }
            const bool portal_used = !installer.active() && !conversation.active() && interiors.Update(pos, e_pressed);
            {
                bool entering = false;
                float dest[4];
                Vector3 from{};
                if (interiors.ConsumeTransition(entering, dest, from)) missions.FollowThroughPortal(from, entering, dest);
            }
            const bool free_e = e_pressed && !portal_used && !installer.active() && !conversation.active();  // (benzin E'si aşağıda ayrıca işlenir)
            if (garage_slot != mode.slot()) {  // karakter değişince garaj da değişir
                if (garage_slot >= 0) garage.Save();
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
            estate.Update(pos, e_pressed && !conversation.active());
            garage.SetExtraGarages(estate.GarageSpots());
            bool fuel_e_used = false;
            fuel::Update(pos, e_pressed && !conversation.active(), fuel_e_used);
            businesses::Update(pos, e_pressed && !conversation.active());
            garage.UpdateGarageSpots(pos, e_pressed && !conversation.active());
            {
                const bool ui_busy = conversation.active() || conversation.typing() || phone.open() || browser.active() || jail.active() || installer.active() || binco::MenuOpen() || estate.MenuOpen();
                transit::Update(pos, e_pressed && !conversation.active(), ui_busy);
                hitchhike::Update(pos, e_pressed && !conversation.active(), ui_busy);
                const std::vector<Vector3> owned_homes = estate.OwnedPositions();
                phoneos::Update(pos, phone.open(), owned_homes);
                pcos::Update(pos, owned_homes);
                customs::Update(pos, e_pressed && !conversation.active(), ui_busy, [&](Vehicle v) { return garage.IsOwned(v); });
                barber::Update(pos, e_pressed && !conversation.active(), ui_busy);
                binco::Update(pos, e_pressed && !conversation.active(), ui_busy);
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
            vehctl::AutoDrive(conversation.typing());  // araçta yazarken otomatik sürüş
            vehctl::Update(conversation.typing() || conversation.active() || installer.active(),
                           [&](Vehicle v) { return garage.IsOwned(v); }, [&]() { return garage.Handles(); });
            hangout.Update();
            passenger::Tick();
            missions.SetInCarWith(hangout.Seated() ? hangout.Who().npc_id : std::string());
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
            services.Update();                        // çağrılan araçlar ve taksi/helikopter yolculuğu
            jail.SetInputBlocked(conversation.typing());
            jail.Update();                            // tazer, tutuklama, hücre, ceza, cinayet izleme

            // Telefon: arayüzü aç/kapat, hızlı arama, santrali sesli ara. Polis kovalamacası/hapisteyken kapalı.
            if (jail.active()) phone.Close();
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
            web_ctx.source = [&web_ctx, &estate, &market, &web_feed, &shops](const std::string& n) {
                if (n == "crime_news") return crimes::NewsItems(6);
                if (n == "vendor_hints") return vendors::HintItems(shops.shops());
                if (n.rfind("ai_", 0) == 0) return web_feed.Source(n, web_ctx);
                return n.rfind("estate_", 0) == 0 ? estate.Source(n) : market.Source(n);
            };
            web_ctx.action = [&](const std::string& c) {
                if (c.rfind("ph:", 0) == 0) return phoneos::Action(c.substr(3));
                if (c.rfind("pc:", 0) == 0) return pcos::Action(c.substr(3));
                if (c.rfind("look:", 0) == 0) return estate.Action(c);
                return market.Action(c);
            };
            web_ctx.cash = economy::GetCash();
            web_ctx.hour = TIME::GET_CLOCK_HOURS();
            browser.SetMouse(browser.active() && pcos::Active(), !pcos::Active());  // bilgisayar: sadece fare (yön tuşu yok); telefon: fare YOK, ok tuşları
            if (browser.active() && browser.Update(web_ctx, p_pressed)) {
                phone.Close();
                pcos::Close();
            }
            if (!pending_qr.empty()) {  // QR: telefon açılır, tarayıcı durağın sayfasına gider
                if (!phone.open()) phone.Open();
                browser.OpenAt(pending_qr, web_ctx);
                pending_qr.clear();
            }
            const bool want_call = phone.Update(conversation.active(), conversation.typing() || browser.active(), p_pressed && !jail.active() && !browser.active(),
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
                const bool busy = conversation.active() || conversation.typing() || phone.open() || browser.active() || pcos::Active() || customs::MenuOpen() || barber::MenuOpen() || binco::MenuOpen() || admin::MenuOpen() || jail.active() || installer.active() ||
                                  inventory::MenuOpen() || garage.MenuOpen() || estate.MenuOpen();
                if (up_pressed && !busy && !phoneos::Dead()) phone.Open();
                else if (up_pressed && !busy) karma::Toast("Telefonun şarjı bitti. Market, otobüs, araç çakmaklığı ya da evde şarja tak.", 4);
            }
            if (phone.open() && phoneos::Dead()) phone.Close();  // pil bitince açılmaz
            // Açık telefon = ana ekran (eski liste arayüzü yok): görüşme bitince ana ekrana dönülür
            if (phone.open() && !browser.active() && !conversation.active()) browser.Open();
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
            if (conversation.active()) {
                conversation.Update(pos, e_pressed || p_pressed);  // E veya telefon tuşu görüşmeyi kapatır
            } else {
                conversation.Update(pos, false);  // gelen mesajları boşalt
                if (installer.active()) {
                    // sihirbaz açık: başka etkileşim yok
                } else if (services.ConsumeRideStart()) {
                    conversation.StartRide(shops.pool());  // taksiye/helikoptere bindin: şoförle diyalog
                } else if (hitchhike::ConsumeBoarded()) {
                    conversation.StartHitch(shops.pool());  // otostopla bindin: sürücüyle sohbet
                } else if (call_contact == "haviland") {
                    conversation.StartPhone(shops.pool(), "Haviland: telefonun sesli asistanı (taksi, helikopter, polis, ambulans, itfaiye çağırır; toplu taşıma rotası verir)", "haviland");
                } else if (call_contact == "santral") {
                    conversation.StartPhone(shops.pool());  // 911 / santral
                } else if (call_contact.rfind("person:", 0) == 0) {
                    nlohmann::json npc;
                    if (phoneos::PersonNpc(call_contact, npc)) conversation.StartPersonCall(npc);  // rehberdeki sokak tanıdığı
                } else if (call_contact == "web_browser") {
                    browser.Open();
                } else if (call_contact == "transit_nav") {
                    conversation.StartPhone(shops.pool());  // navigasyon: santralle aynı hat, 'transit_route' niyeti ile rota verir
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
                    Missions::Talk ct;  // rehberden bir kişi arandı (örn. Yusuf Cansever: yeni görev)
                    if (missions.CallTarget(call_contact, ct)) conversation.StartMissionCall(ct);
                } else if (want_call) {
                    conversation.StartPhone(shops.pool());  // telefondan "santrali ara"
                } else if (phone.open()) {
                    // telefon açıkken sokak/dükkan etkileşimi başlamaz
                } else if (jail.active()) {
                    // Polis kovalamacası veya hapisteyken yeni konuşma başlamaz; hücrede L avukatı çağırır.
                    if (jail.ConsumeLawyerRequest()) conversation.StartLawyer(shops.pool());
                } else if (hangout.Seated()) {
                    // Arkadaş arabada: E ile sohbet (yolculuk boyunca).
                    ShowHelp(("~INPUT_PICKUP~ Konus: " + hangout.Who().name).c_str());
                    if (e_pressed) conversation.StartMission(hangout.Who());
                } else if (const Vehicle boardable = services.BoardableVehicle(pos)) {
                    // Çağırdığın taksi/helikopter yanında bekliyor: E ile binersin (kod arka koltuğa bindirir).
                    ShowHelp("~INPUT_PICKUP~ Araca bin");
                    if (e_pressed) services.BoardPlayer(boardable);
                } else if (missions.NearestTalkable(pos, talk)) {
                    // Görev NPC'si (Yusuf Cansever, müşteri...) dükkan ve sokak sohbetinden önce gelir.
                    ShowHelp(("~INPUT_PICKUP~ Konus: " + talk.name).c_str());
                    if (e_pressed) conversation.StartMission(talk);
                } else if (Shop* focus = interaction.Update(shops, pos)) {
                    if (e_pressed) conversation.Start(*focus, pos);
                } else if (const Ped street = interaction.UpdateStreet(shops, pos)) {
                    if (e_pressed && !conversation.IsEventPed(street)) conversation.StartStreet(street, pos, shops.pool());
                }
            }
            conversation.Draw();
            jail.Draw();
            if (!jail.active()) missions.DrawHud();
            if (!installer.active()) phone.Publish(conversation.GetPhoneView(), services, contacts);  // sihirbaz kendi pencere görünümünü yayınlar
            if (browser.active() || pcos::GameActive()) {
                char clk[8];
                std::snprintf(clk, sizeof(clk), "%02d:%02d", TIME::GET_CLOCK_HOURS(), TIME::GET_CLOCK_MINUTES());
                if (pcos::GameActive()) overlay::SetPhone(pcos::GameView(clk));
                else browser.Publish(clk);
            }
            // Kalıcı HUD durum çubuğu (sağ üst): saat + sinyal çubukları + pil — telefon kapalıyken de görünür
            {
                char clk[8];
                std::snprintf(clk, sizeof(clk), "%02d:%02d", TIME::GET_CLOCK_HOURS(), TIME::GET_CLOCK_MINUTES());
                overlay::SetStatusBar(clk, phoneos::SignalText(), phoneos::BatteryText(), phoneos::Battery() <= 15 && !phoneos::Charging());
            }
        }
      } catch (const std::exception& e) {
        static int errors = 0;
        if (errors++ < 20) Log(std::string("HATA (kare atlandi): ") + e.what());  // log şişmesin
      }
        overlay::Commit();  // hazırlanan altyazı, telefon ve menü verisi render thread'ine geçer
        WAIT(0);
    }
}

}  // namespace
}  // namespace hikaye

void ScriptMain() {
    hikaye::ScriptLoop();
}

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            hikaye::overlay::Init();    // Present kancası (ImGui çizimi): SDK "dll attach" şartı
            hikaye::textinput::Init();  // klavye kancası (T ile yazma, Türkçe karakterler)
            scriptRegister(hInstance, ScriptMain);
            break;
        case DLL_PROCESS_DETACH:
            hikaye::textinput::Shutdown();
            hikaye::overlay::Shutdown();  // kancaları kaldır (DLL gidince çağrılmasın)
            scriptUnregister(hInstance);
            break;
    }
    return TRUE;
}
