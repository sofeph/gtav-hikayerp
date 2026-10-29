# Yer verisi kaynakları

`zones.json` ve `pois.json`, `tools/fetch_places.py` ile üretilir (elle düzenleme: betiği tekrar çalıştırınca ezilir).

- **Kaynak:** [DurtyFree/gta-v-data-dumps](https://github.com/DurtyFree/gta-v-data-dumps) — GTA V oyun verisinden otomatik çıkarılmış dökümler
  (`zones.json`, `objectslocations/*`, `garages.json`).
- **Lisans:** Depoda lisans dosyası yok; veri oyunun kendi içeriğinden çıkarılmıştır. Mod ücretsiz ve ticari olmayan dağıtım içindir.
  Mod dağıtılırken kaynak ve yazar (DurtyFree) belirtilmelidir. Satış yapılırsa bu veri yeniden değerlendirilmelidir.
- **Oyun sürümü:** Döküm belirli bir oyun sürümüne göre çıkarılmıştır; koordinatlar yeni sürümlerde birkaç metre kayabilir.

## Hapishane koordinatları (`data/jail.json`)
- **Kaynak:** [qbcore-framework/qb-prison](https://github.com/qbcore-framework/qb-prison) `config.lua` (`Config.Locations`: `yard`, `outside`, `spawns`).
- **Lisans:** GPL-3.0. Yalnızca koordinat verisi alınmıştır, kaynak kod kopyalanmamıştır. Mod dağıtılırken kaynak belirtilmelidir.

## İçerik
| Dosya | Alan | Anlamı |
|---|---|---|
| `zones.json` | `id`, `name`, `bounds` | Mahalle/bölge (ör. "Vespucci Beach"); `bounds` = `[minx, miny, maxx, maxy]` dikdörtgenleri. İngilizce oyun adları. |
| `pois.json` | `atm`, `bus_stop`, `gas_station`, `phone_booth` | Yakın noktalar tek yerde toplanmıştır (`count` = kaç nesne). |
| `pois.json` | `garage` | Hikaye modu ev garajları. |

Hastane, polis ve itfaiye gibi hizmet noktaları bu dökümde yok; oyun içi harita işaretçilerinden (blip) okunacak.

## businesses.json (işletmeler)
`tools/fetch_businesses.py` GitHub'dan çeker: qbcore-framework/qb-shops, qb-vehicleshop, qb-clothing, qb-garages ve renzuzu/renzu_customs.
Yalnızca koordinat ve etiket alınır. Polis/ambulans/mekanik işi dükkanları, kara borsa ve hapishane kantini elenir. Koordinatlar oyunda doğrulanmadı.

## houses.json (evler)
`tools/fetch_houses.py` GitHub'dan çeker: Landelan7964/QB-HouseLocations -> houselocations.sql (92 ev: ad, kapı konumu, garaj konumu, kademe).
Depo lisanssızdır; yalnızca olgusal veri (koordinat, etiket) alınır, oyun içi fiyat/kira kodda yeniden hesaplanır. Koordinatlar oyunda doğrulanmadı.

## Seyyar satıcı konumları (data/shops/seyyar_*.json)
`tools/fetch_vendor_spots.py` GitHub'dan çeker: qbcore-framework/qb-drugs -> config.lua (GPL-3.0; sadece koordinat ve etiket):
5 sokak teslimat noktası (Stripclub, Vinewood Video, Taxi, Resort, Bahama Mamas) ve örnek Sandy Shores satıcısı. Satıcılar haritada gizlidir.

## Araç bayisi konumları (data/shops/galeri_davis, galeri_premium, ticari_bayi)
`tools/fetch_dealer_spots.py` GitHub'dan çeker: qbcore-framework/qb-vehicleshop -> config.lua (GPL-3.0; sadece koordinat): satış masası (FinanceZone) ve araç çıkış noktası (VehicleSpawn).
Yol üstünde kalan diğer dükkanlar için oyun içinde kaldırıma kaydırma devrede (mod.log: "kaldirima kaydirildi").
