"""GTA V işletme konumlarını GitHub'dan çekip data/places/businesses.json'a yazar (harita ikonları ve modifiye servisi için).

Kaynaklar (yalnızca KOORDİNAT/ETİKET verisi alınır, kod alınmaz):
  qbcore-framework/qb-shops        marketler, LTD, içki dükkanları, hırdavat, Ammu-Nation (GPL-3.0)
  qbcore-framework/qb-vehicleshop  araç/tekne/hava aracı bayileri (GPL-3.0)
  qbcore-framework/qb-clothing     kıyafet, berber, estetik cerrahi, dövme (GPL-3.0)
  qbcore-framework/qb-garages      halka açık garajlar (GPL-3.0)
  renzuzu/renzu_customs            modifiye atölyeleri (LS Customs / Benny's vb.) — yalnızca koordinatlar
Çalıştırma (proje kökünden):  python tools/fetch_businesses.py
"""
import json
import math
import pathlib
import re
import urllib.request

OUT = pathlib.Path(__file__).resolve().parents[1] / "data" / "places" / "businesses.json"
RAW = "https://raw.githubusercontent.com"


def get(repo: str, path: str, branches=("main", "master", "HEAD")) -> str:
    last = None
    for b in branches:
        try:
            with urllib.request.urlopen(f"{RAW}/{repo}/{b}/{path}", timeout=60) as r:
                return r.read().decode("utf-8", "ignore")
        except Exception as e:  # noqa: BLE001
            last = e
    raise RuntimeError(f"{repo}/{path}: {last}")


NUM = r"(-?\d+(?:\.\d+)?)"
items: list[dict] = []
sources: list[str] = []


def add(t: str, name: str, x: float, y: float, z: float) -> None:
    items.append({"type": t, "name": name, "pos": [round(float(x), 1), round(float(y), 1), round(float(z), 1)]})


# --- qb-shops ---
try:
    s = get("qbcore-framework/qb-shops", "config.lua")
    sources.append("qbcore-framework/qb-shops")
    for m in re.finditer(r"\['label'\]\s*=\s*'((?:[^'\\]|\\.)*)',\s*\n\s*\['coords'\]\s*=\s*vector4\(" + NUM + r",\s*" + NUM + r",\s*" + NUM, s):
        label = m.group(1).replace("\\'", "'")
        low = label.lower()
        if "24/7" in low:
            t = "supermarket"
        elif "ltd" in low:
            t = "ltd"
        elif "liq" in low:
            t = "liquor"
        elif "hardware" in low:
            t = "hardware"
        elif "ammu" in low or "gun" in low:
            t = "ammunation"
        else:
            t = "shop"
        # Rol/yasa dışı dükkanlar (polis, ambulans, mekanik işi, kara borsa, uyuşturucu, hapishane kantini) haritaya konmaz
        if any(b in low for b in ("police", "ambulance", "mechanic", "black market", "smoke", "canteen")):
            continue
        add(t, label, m.group(2), m.group(3), m.group(4))
except Exception as e:  # noqa: BLE001
    print("qb-shops alınamadı:", e)

# --- qb-vehicleshop ---
try:
    s = get("qbcore-framework/qb-vehicleshop", "config.lua")
    sources.append("qbcore-framework/qb-vehicleshop")
    for m in re.finditer(r"\['ShopLabel'\]\s*=\s*'((?:[^'\\]|\\.)*)'.*?\['Location'\]\s*=\s*vector3\(" + NUM + r",\s*" + NUM + r",\s*" + NUM, s, re.S):
        label = m.group(1)
        low = label.lower()
        t = "boat_shop" if ("boat" in low or "marina" in low) else "air_shop" if ("air" in low or "heli" in low) else "truck_shop" if "truck" in low else "car_dealer"
        add(t, label, m.group(2), m.group(3), m.group(4))
except Exception as e:  # noqa: BLE001
    print("qb-vehicleshop alınamadı:", e)

# --- qb-clothing (kıyafet/berber/cerrah/dövme) ---
try:
    s = get("qbcore-framework/qb-clothing", "config.lua")
    sources.append("qbcore-framework/qb-clothing")
    names = {"clothing": "Kiyafet Magazasi", "barber": "Berber", "surgeon": "Estetik Cerrah", "tattoo": "Dovme Salonu"}
    for m in re.finditer(r"shopType\s*=\s*'(\w+)',\s*coords\s*=\s*vector3\(" + NUM + r",\s*" + NUM + r",\s*" + NUM, s):
        t = m.group(1)
        add(t if t in names else "clothing", names.get(t, "Kiyafet Magazasi"), m.group(2), m.group(3), m.group(4))
except Exception as e:  # noqa: BLE001
    print("qb-clothing alınamadı:", e)

# --- qb-garages (halka açık garajlar) ---
try:
    s = get("qbcore-framework/qb-garages", "config.lua")
    sources.append("qbcore-framework/qb-garages")
    for m in re.finditer(r"label\s*=\s*'((?:[^'\\]|\\.)*)'[^{}]*?(?:takeVehicle|putVehicle)\s*=\s*vector[34]\(" + NUM + r",\s*" + NUM + r",\s*" + NUM, s, re.S):
        add("garage", m.group(1).replace("\\'", "'"), m.group(2), m.group(3), m.group(4))
except Exception as e:  # noqa: BLE001
    print("qb-garages alınamadı:", e)

# --- modifiye atölyeleri (renzu_customs): her atölyenin 'shopcoord' noktası ---
try:
    s = get("renzuzu/renzu_customs", "config.lua")
    sources.append("renzuzu/renzu_customs")
    shops = list(re.finditer(r"shopcoord\s*=\s*vector[34]\(" + NUM + r",\s*" + NUM + r",\s*" + NUM, s))
    # atölye adı: shopcoord'dan önce gelen en yakın ['isim'] anahtarı; bulunamazsa genel ad
    for i, m in enumerate(shops, 1):
        before = s[max(0, m.start() - 1500):m.start()]
        nm = re.findall(r"\[\s*['\"]([^'\"]{3,40})['\"]\s*\]\s*=\s*\{", before)
        name = (nm[-1] if nm else f"Modifiye Atolyesi {i}")
        add("customs", name, m.group(1), m.group(2), m.group(3))
except Exception as e:  # noqa: BLE001
    print("renzu_customs alınamadı:", e)

# bilinen LS Customs / Benny's konumları da dahil olsun (kaynakta yoksa eklenir; yakınlarda zaten varsa atlanır)
KNOWN_CUSTOMS = [("Los Santos Customs (Burton)", -337.4, -136.9, 38.6), ("Los Santos Customs (Havalimani)", -1155.5, -2007.2, 12.7),
                 ("Los Santos Customs (La Mesa)", 731.8, -1088.8, 21.7), ("Los Santos Customs (Harmony)", 1175.0, 2640.2, 37.5),
                 ("Beeker's Garage (Paleto)", 110.8, 6626.4, 31.8), ("Benny's Original Motor Works", -205.7, -1312.5, 31.3)]
for n, x, y, z in KNOWN_CUSTOMS:
    if not any(i["type"] == "customs" and math.hypot(i["pos"][0] - x, i["pos"][1] - y) < 60 for i in items):
        add("customs", n, x, y, z)

# yakın tekrarları ele (aynı tür, 12 m içinde)
uniq: list[dict] = []
for it in items:
    if not any(u["type"] == it["type"] and math.hypot(u["pos"][0] - it["pos"][0], u["pos"][1] - it["pos"][1]) < 12 for u in uniq):
        uniq.append(it)

OUT.write_text(json.dumps({"sources": sources, "items": uniq}, ensure_ascii=False, indent=1), encoding="utf-8")
count: dict[str, int] = {}
for u in uniq:
    count[u["type"]] = count.get(u["type"], 0) + 1
print("toplam:", len(uniq), count)
