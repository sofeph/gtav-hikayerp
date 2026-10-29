"""
Araç bayisi NPC konumlarını ve araç çıkış noktalarını GitHub'dan alır (satıcılar sokak ortasında durmasın).

Kaynak: qbcore-framework/qb-vehicleshop -> config.lua (GPL-3.0; sadece KOORDİNAT):
  FinanceZone = showroom içindeki satış masası -> satıcı NPC'nin konumu
  VehicleSpawn = satın alınan aracın çıkış noktası (x, y, z, yön)
Eşleşme: pdm -> galeri_davis, luxury -> galeri_premium, truck -> ticari_bayi.
Diğer bayiler (Paleto, Sandy, motor) bu repoda yoktur; onlar için oyun içinde yol üstündeyse kaldırıma kaydırma kodu (ShopManager::Spawn) devrede.
Çalıştırma: python tools/fetch_dealer_spots.py
"""
import json
import pathlib
import re
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHOPS = ROOT / "data" / "shops"
URL = "https://raw.githubusercontent.com/qbcore-framework/qb-vehicleshop/main/config.lua"
MAP = {"pdm": "galeri_davis", "luxury": "galeri_premium", "truck": "ticari_bayi"}


def main() -> None:
    t = urllib.request.urlopen(URL, timeout=60).read().decode("utf-8", "replace")
    keys = [(m.start(), m.group(1)) for m in re.finditer(r"^    \['(\w+)'\] = \{", t, re.M)] + [(len(t), "end")]
    for i in range(len(keys) - 1):
        name = keys[i][1]
        if name not in MAP:
            continue
        body = t[keys[i][0]:keys[i + 1][0]]

        def vec(k, n):
            m = re.search(r"\['%s'\]\s*=\s*vector%d\(([^)]*)\)" % (k, n), body)
            return [float(x) for x in m.group(1).split(",")] if m else None

        fin, spawn = vec("FinanceZone", 3), vec("VehicleSpawn", 4)
        path = SHOPS / f"{MAP[name]}.json"
        d = json.loads(path.read_text(encoding="utf-8"))
        old = d["pos"]
        if fin:
            d["pos"] = [round(v, 2) for v in fin]
        if spawn:
            d["vehicle_spawn"] = [round(v, 2) for v in spawn]
        d["_kaynak"] = f"qb-vehicleshop: {name} (FinanceZone/VehicleSpawn); eski konum {old}"
        path.write_text(json.dumps(d, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"{MAP[name]}: {old} -> {d['pos']}  spawn {d.get('vehicle_spawn')}")


if __name__ == "__main__":
    main()
