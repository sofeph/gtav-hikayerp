# HikayeTurkRP — Özellik Kataloğu

GTA V için Türkçe roleplay ASI modu. ScriptHookV + ImGui + CEF (opsiyonel) üzerine kurulu; oyun içi telefon, televizyon, araç sistemi, diyalog AI ve daha fazlasını içerir.

---

## Mod Mimarisi

| Katman | Teknoloji |
|---|---|
| Oyun kancası | ScriptHookV ASI (C++/MSVC x64) |
| UI motoru | Dear ImGui (DX11) |
| Web renderer | GTML / HML custom parser (C++) |
| Companion | Python 3.x (asyncio + WebSocket) |
| AI diyalog | LLM (Ollama / keyless API) |
| TTS | Piper / XTTS |
| STT | Whisper |
| Video | RAD VideoTools (Bink/Smacker) + HLS |
| Font | Segoe UI + Font Awesome 5 Solid (merge) |

---

## Oyun İçi Sistemler (C++ Modu)

### Telefon (`phone.cpp`, `phone.h`)
- Tek modül: `Phone` sınıfı (ana ekran, giriş, görüşme ekranı) + `hikaye::phone` ad alanı (uygulama sayfaları, ayarlar, pil, şebeke, numaralar)
- Material 3 tasarımlı akıllı telefon overlay'i
- Ana ekranı `Phone` çizer; uygulamalar tarayıcıda `phone/<sayfa>` olarak açılır, geri dönünce ana ekran gelir
- Ana ekran sayfalama: ızgara (sayfa başı 8 uygulama, boş yuva yok) + sabit dock (Alarm, Takvim, Notlar, Ayarlar)
- Uygulama kataloğu tek yerde (`phone::HomeApps`): ad, Font Awesome ikonu, renk; App Store kısayolları ikon/rengi sitenin `<page icon=".." color="..">` etiketinden alır
- Sayfa nokta indikatörü
- İkonlar SVG'den çizilir: `tools/fa_to_svg.py` Font Awesome glif'lerini `data/icons/<ad>.svg` dosyalarına dönüştürür, `svg_icon.cpp` path'i kendisi rasterleştirir (font gerekmez); yeni ikon için `data/icons.json`'a ad + kod ekleyip betiği çalıştır
- Duvar kağıdı: Ayarlar'dan `data/wallpapers/phone/` içindeki resimler ya da 4 renk teması seçilir; resim oranı korunarak ekranı doldurur
- Uygulama listesi: Telefon, Rehber, Haviland, Maps, Maze Bank, App Store, Saat, Canlı TV + dock
- Duvar kağıdı desteği (yuvarlatılmış köşe, tam ekran kırpma)
- Ayarlar: zil sesi, bildirim ve alarm tonu (`data/phone_sfx` içindeki .ogg dosyaları), titreşim (gamepad), tuş sesleri, telefon ses seviyesi
- Telefon sesleri ASI içinde çalınır (stb_vorbis + XAudio2, `phone_audio.cpp`)
- Uygulama taşıma: Shift+Enter ile başlat, ok tuşlarıyla taşı, Enter ile bırak, Esc ile iptal (yalnızca ızgara)
- HML tabanlı web sayfaları (dahili mini-browser)

### TV / Ekran (`tv_overlay.cpp`, `tv_overlay.h`)
- Oyun içi televizyon ve bilgisayar ekranı
- HLS akış oynatma
- YouTube entegrasyonu (yt-dlp + bgutil PO token sağlayıcısı); ilk video çözülür çözülmez oynatılır, playlist'in kalanı arka planda eklenir
- Bink/Smacker video oynatma (RAD VideoTools)
- Kanal yönetim sistemi (`data/tv/channels.json`)

### Envanter (`inventory.cpp`)
- Oyuncu eşya envanteri
- Ağırlık ve slot sistemi
- JSON tabanlı eşya tanımları (`data/items.json`)

### Radyo (`radio.cpp`, `radio.h`)
- Özel radyo istasyonu sistemi
- `data/radio/` dizininden istasyon yüklemesi

### Ekonomi ve İşletmeler
- Para / ekonomi sistemi (`economy.cpp`)
- İşletme ve mekan sistemi (`businesses.cpp`)
  - Türkçe mekan ismi eşlemeleri (`data/places/aliases_tr.json`)
  - Blip haritası entegrasyonu
- Esnaf / vendor sistemi (`vendors.cpp`)
- Dükkan yöneticisi (`shop_manager.cpp`)
- Pazar (`marketplace.cpp`)
- Mülk / gayrimenkul (`estate.cpp`)
- Yakıt sistemi (`fuel.cpp`)

### Görevler ve Hikaye
- Görev sistemi (`missions.cpp`, `data/missions/`)
- Hikaye addon'ları (`addons/`)
- Blip haritası entegrasyonu (`storyblips.cpp`)
- Görev araçları: `tools/gen_missions.py`, `tools/wire_story.py`

### Karakter ve Sosyal
- Profil sistemi (`profile.cpp`, `data/profile_pool.json`)
- Karma / itibar sistemi (`karma.cpp`)
- Suç / adli kayıt (`crimes.cpp`)
- Jail / hapishane sistemi (`jail.cpp`)
- Konuşma sistemi (`conversation.cpp`)
- Yolcu / otostop (`passenger.cpp`, `hitchhike.cpp`)
- Berber (`barber.cpp`) + Kıyafet (`binco.cpp`)

### Araç Sistemleri
- Araç kontrolü (`vehctl.cpp`)
- Araç özelleştirme (`customs.cpp`)
- Transit / otobüs sistemi (`transit.cpp`, `data/transit.json`)

### Diğer
- Kontrol / gamepad (`controls.cpp`, `xpad.cpp`)
- Duraklama menüsü (`pausemenu.cpp`)
- Mekan / iç mekan sistemi (`interiors.cpp`, `data/interiors.json`)
- Anlık etkinlik (`street_events.cpp`)
- Metin girişi overlay (`textinput.cpp`)
- Taşıma modu (`mode.cpp`)
- PC overlay / bilgisayar arayüzü (`pcos.cpp`)
- WebFeed haber akışı (`webfeed.cpp`)
- Proaktif NPC davranışları (`proactive.cpp`)
- Ek modlar / DLC (`addons.cpp`)
- Hizmetler (taksi, ambulans vb.) (`services.cpp`)

---

## HML / GTML Web Motoru

### C++ tarafı (`hml.h`, `gtml_renderer.cpp`)
- Özel HTML benzeri markup dili (tile, card, h1-h3, link, shape...)
- `WebItem` display list modeli
- SVG shape render desteği

### Python tarafı (`companion/webv2_backend/`)
- `engine.py` — Flask benzeri route/session motoru
- `gtml_compiler.py` — BeautifulSoup4 tabanlı statik HTML parser (Playwright'tan geçiş yapıldı, sıfır cold-start); `<style>` kurallarını uygular, `<script>` içeriklerini layout JSON'una koyar

### GTML JavaScript (ASI içinde, `gtml_js.cpp`)
- Sayfa scriptleri oyunun içinde QuickJS (quickjs-ng 0.10.1) ile çalışır
- Desteklenen API: `document.getElementById` / `querySelector('#id')`, `textContent`, `innerText`, `value`, `style.display/color/backgroundColor`, `addEventListener('click')`, `onclick`, `setTimeout` / `setInterval`, `fetch` (WinHTTP), `console.log`, `gta.trigger(olay, veri)`
- Güvenlik: her JS çağrısına süre sınırı (sonsuz döngü oyunu dondurmaz), 32 MB bellek sınırı, en fazla 8 eşzamanlı istek, 2 MB yanıt sınırı
- Desteklenmeyenler: yan yana dizilim (flex), `:hover`, eleman ekleme/silme (DOM yapısı sabit)
- `http_server.py` — yerel HTTP sunucu
- `plugin.py` — plugin yöneticisi
- `template.py` — şablon motoru
- `tv.py` — TV kanal yöneticisi

---

## Companion (Python AI Asistanı)

### Temel akış (`companion/main.py`, `server.py`)
- WebSocket üzerinden mod ↔ Python iletişimi
- `config.yaml` ile tüm ayarlar

### Konuşma / Diyalog
- LLM yönlendirme: Ollama (yerel) veya keyless API (`llm/router.py`)
- Çıktı parser — JSON tabanlı eylem ayrıştırma (`dialogue/output_parser.py`)
- Para/ekonomi ifade tanıyıcı (`dialogue/currency.py`)
- Kişisel bilgi anonimleştirici (`dialogue/anonymizer.py`)
- Otomatik menü sistemi (`automenu.py`)

### Ses
- STT: Whisper (`stt/whisper_stt.py`)
- TTS: Piper (hızlı) veya XTTS (kaliteli) seçilebilir
- Ses kayıt / oynatma (`audio/record.py`, `audio/playback.py`)

---

## Yapılandırma Dosyaları

| Dosya | Amaç |
|---|---|
| `data/icons.json` | FA5 ikon kod noktaları — derleme gerektirmeden güncellenebilir |
| `data/phone.json` | Telefon uygulama listesi ve ayarları |
| `data/controls.json` | Tuş bağlamaları |
| `data/mode.json` | Oyun modu ayarları |
| `data/items.json` | Envanter eşyaları |
| `data/fuel.json` | Yakıt fiyatları / istasyonlar |
| `data/transit.json` | Otobüs hatları |
| `data/services.json` | Acil servis noktaları |
| `data/vehicle.json` | Araç ek verileri |
| `data/world_clock.json` | Oyun dünyası saati |
| `data/marketplace.json` | Pazar eşyaları |
| `data/tv/channels.json` | TV kanalları |
| `data/radio/` | Radyo istasyonu verileri |
| `data/places/` | Mekan koordinatları ve meta |
| `data/missions/` | Görev tanımları |
| `data/wallpapers/` | Telefon/PC duvar kağıtları |
| `companion/config.yaml` | Companion sunucu ayarları |

---

## Derleme ve Kurulum

```powershell
# Derle + paketle
.\build.ps1

# Derle + paketle + oyuna kur
.\build.ps1 -Install

# ScriptHookV runtime'ı da kur
.\build.ps1 -Install -InstallRuntime

# Sadece paketle, oyuna kurma
.\build.ps1 -NoInstall
```

Çıktı: `mod/build/package/HikayeTurkRP_x64_release.zip`  
Oyun dizini: `GTA V/HikayeTurkRP/`

### Gereksinimler
- Visual Studio 2022 (C++ Desktop Development)
- ScriptHookV SDK (ilk çalıştırmada Downloads'tan otomatik bulunur)
- Python 3.10+ (`pip install -r companion/requirements.txt`)
- BeautifulSoup4 + lxml (`pip install beautifulsoup4 lxml`)

---

## Font Awesome İkon Sistemi

İkonlar `data/icons.json` dosyasından yüklenir. Yeni ikon eklemek için sadece bu dosyayı düzenlemek yeterlidir — yeniden derleme gerekmez.

```json
{
  "icons": {
    "phone": "F095",
    "settings": "F013",
    "myicon": "FXXX"
  }
}
```

FA5 Solid codepoint arama: [fontawesome.com/icons](https://fontawesome.com/icons?s=solid)

---

## Addon Sistemi

`addons/` klasörüne eklenen addon paketleri otomatik yüklenir. Her addon bir klasördür ve `addon.json` manifest dosyası içerir. Araç paketleri, yeni görev setleri veya mekan paketleri addon olarak dağıtılabilir.
