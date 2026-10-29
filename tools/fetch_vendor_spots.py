"""
Seyyar satıcı konumlarını GitHub'dan çeker ve data/shops/seyyar_*.json dosyalarını yeniden yazar.

Kaynak: qbcore-framework/qb-drugs -> config.lua (GPL-3.0; sadece KOORDİNAT ve etiket alınır):
  - Config.DeliveryLocations: 5 sokak teslimat noktası (Stripclub, Vinewood Video, Taxi, Resort, Bahama Mamas)
  - Config.Dealers örnek bloğundaki Sandy Shores satıcısı (yorum satırı: coords {x=,y=,z=})
Mevcut satıcıların envanteri/kişiliği korunur; yalnızca konum ve ad güncellenir, eksik satıcı kopyalanarak eklenir.
Çalıştırma: python tools/fetch_vendor_spots.py
"""
import json
import pathlib
import re
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHOPS = ROOT / "data" / "shops"
URL = "https://raw.githubusercontent.com/qbcore-framework/qb-drugs/main/config.lua"

LABELS_TR = {
    "Stripclub": "Strip kulübünün arkası",
    "Vinewood Video": "Vinewood Video arka sokağı",
    "Taxi": "Taksi durağı arkası",
    "Resort": "Tatil köyü servis yolu",
    "Bahama Mamas": "Bahama Mamas arka kapısı",
    "Sandy Dealer": "Sandy Shores köşesi",
}


def main() -> None:
    lua = urllib.request.urlopen(URL, timeout=60).read().decode("utf-8", "replace")
    spots = []
    for m in re.finditer(r"\['label'\]\s*=\s*'([^']+)'\s*,\s*\['coords'\]\s*=\s*vector3\(\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*\)", lua):
        spots.append((m.group(1), [float(m.group(2)), float(m.group(3)), float(m.group(4))]))
    m = re.search(r"\['([^']*Dealer[^']*)'\]\s*=\s*\{.*?coords\s*=\s*\{x\s*=\s*(-?[\d.]+),\s*y\s*=\s*(-?[\d.]+),\s*z\s*=\s*(-?[\d.]+)\}", lua, re.S)
    if m:
        spots.append((m.group(1), [float(m.group(2)), float(m.group(3)), float(m.group(4))]))
    if not spots:
        raise SystemExit("konum bulunamadı (kaynak biçimi değişmiş olabilir)")
    template = json.loads((SHOPS / "seyyar_1.json").read_text(encoding="utf-8"))
    for i, (label, pos) in enumerate(spots, 1):
        path = SHOPS / f"seyyar_{i}.json"
        shop = json.loads(path.read_text(encoding="utf-8")) if path.exists() else json.loads(json.dumps(template))
        shop["id"] = f"seyyar_{i}"
        shop["name"] = "Seyyar Satıcı"
        shop["pos"] = [round(v, 2) for v in pos]
        shop["hidden"] = True
        shop["_kaynak"] = f"qb-drugs: {label} ({LABELS_TR.get(label, label)})"
        shop["npc"]["model"] = ["a_m_y_hipster_01", "a_m_m_skater_01", "a_m_y_stbla_01"][i % 3]
        path.write_text(json.dumps(shop, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"seyyar_{i}: {label} -> {shop['pos']}")
    # fazlalık eski dosyaları kaldır
    for p in SHOPS.glob("seyyar_*.json"):
        n = int(p.stem.split("_")[1])
        if n > len(spots):
            p.unlink()
            print("silindi:", p.name)


if __name__ == "__main__":
    main()
