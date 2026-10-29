# Hikaye RP — Eklentiler (Addons)

Oyun dosyalarına DOKUNMADAN yeni içerik eklemek için: `HikayeTurkRP/addons/<kimlik>/` klasörü oluştur.
Adı `_` ile başlayan klasörler **şablondur, yüklenmez** (`_ornek_arac_paketi`). Kendi eklentin için klasörü kopyalayıp `_` önekini kaldır.

## Klasör düzeni (`data/` ile aynı)
```
addons/<kimlik>/
  addon.json        {"id":"kimlik","name":"Gösterilen ad","version":"1.0","enabled":true}
  shops/*.json      ek dükkanlar (spawn_vehicle ile yeni/DLC araç satışı)
  items.json        ek envanter eşyaları ({"items":{...}} — data/items.json ile aynı biçim)
  vehicles.json     GeneralSale ikinci el pazarına araç modelleri
  missions/*.json   ek görevler
  web/<site>/*.hml  ek telefon siteleri (+ .hss stil dosyaları)
```
Aynı kimlikli eşya varsa eklentininki üzerine yazar. Eklenti klasörü silinince oyun eski haline döner.

## DLC / addon araç eklemek
1. Aracı oyuna ekle (addon/DLC araç paketi: OpenIV `mods\update\x64\dlcpacks` + dlclist kaydı). Bu mod paketinden bağımsızdır.
2. Aracın **model adını** (spawn adı) öğren, örn. `mycar`.
3. `shops/arac_bayi.json` içinde envantere ekle: `{"id":"mycar","name":"MyCar","price":25000,"stock":3,"effect":{"type":"spawn_vehicle","model":"mycar"}}`.
4. İstersen `vehicles.json` ile ikinci el pazarına da ekle: `{"vehicles":[{"model":"mycar","name":"MyCar","base":24000}]}`.
Model oyunda yüklü değilse satış güvenle iptal olur (para iade edilir), oyun çökmez.

## Yeni eşya / site
`items.json`: `{"items":{"enerji":{"name":"Enerji içeceği","weight":0.3,"use":{"type":"heal","pct":15}}}}` — `use.type`: heal · weapon · drunk · high · smoke; `legal:false` ise polis aramasında müsadere edilir.
`web/<alanadi.com>/index.hml`: telefondaki İnternet'ten U ile adres yazarak ya da Eyefind'da aratarak bulunur (HML etiketleri: `mod/src/hml.h`).
