# Üçüncü taraf bileşenler ve kaynaklar

Mod ücretsiz ve ticari olmayan dağıtım içindir. Aşağıdaki bileşenler kullanılmıştır:

| Bileşen | Kullanım | Lisans |
|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) 1.93.0 | Oyun içi telefon ve altyazı çizimi (`mod/third_party/imgui`) | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) 3.11.3 | JSON okuma (`mod/third_party/json`) | MIT |
| [iFruitAddon2](https://github.com/Bob74/iFruitAddon2) (Bob74) | "Süper Rehber" kişisini oyunun telefonuna ekleme *tekniği* (`mod/src/nativephone.*`, .NET'siz C++ karşılığı; kod kopyalanmadı) | MIT |
| [DurtyFree/gta-v-data-dumps](https://github.com/DurtyFree/gta-v-data-dumps) | Bölge sınırları ve nesne konumları (`data/places/`) | Lisans dosyası yok; oyun verisi dökümü |
| [qbcore-framework/qb-prison](https://github.com/qbcore-framework/qb-prison) | Bolingbroke hapishane koordinatları (`data/jail.json`) — yalnızca koordinat verisi | GPL-3.0 |
| [qbcore-framework](https://github.com/qbcore-framework) qb-shops / qb-vehicleshop / qb-clothing / qb-garages | İşletme konumları ve adları (`data/places/businesses.json`, `tools/fetch_businesses.py`) — yalnızca koordinat/etiket verisi, kod alınmadı | GPL-3.0 |
| [renzuzu/renzu_customs](https://github.com/renzuzu/renzu_customs) | Modifiye atölyesi koordinatları (`data/places/businesses.json`) — yalnızca koordinat | Lisansı depodan kontrol edilmeli |
| ScriptHookV (Alexander Blade) | Oyuna script yükleme (ayrı indirilir, repoya konmaz) | Kendi şartları |
| Coqui XTTS-v2 | Yerel çok sesli TTS | CPML (ticari olmayan) |
| faster-whisper, Piper | Yerel STT / yedek TTS | MIT |

Ayrıntılı veri kaynakları için `data/places/SOURCES.md` dosyasına bakın.

## Konum verileri (GitHub)
Ev konumları (Landelan7964/QB-HouseLocations) ve seyyar satıcı konumları (qbcore-framework/qb-drugs, GPL-3.0) yalnızca koordinat/etiket olarak alınmıştır; kod alınmamıştır. Ayrıntı: `data/places/SOURCES.md`.

## Başlatıcı (launcher/)
Electron (MIT), @electron/packager (BSD-2-Clause) ve bağımlılıkları. Başlatıcı orijinal GTA V klasörüne yazmaz; mod için ayrı oyun kopyası kullanır (büyük dosyalar bağlanır).
ScriptHookV, OpenIV.asi, Menyoo ve Rockstar dosyaları pakete konmaz.

## xdelta3 (Türkçe yama fark yaması)
xdelta3 3.1.0 — (c) Joshua MacDonald, GPL-2.0-or-later. Ayrı bir araç olarak çalıştırılır, değiştirilmeden dağıtılır.
Kaynak: https://github.com/jmacd/xdelta-gpl/releases/tag/v3.1.0
Türkçe yama içeriği: kullanıcının mevcut Türkçe yaması (OpenIV paketi). Yamanın yazarı/lisansı BELİRTİLMELİDİR (şu an bilinmiyor; yayınlamadan önce ekle).
Pakete yalnızca fark dosyaları (tr-yama/*.xd3) girer; Rockstar'ın update.rpf/update2.rpf dosyaları dağıtılmaz.
