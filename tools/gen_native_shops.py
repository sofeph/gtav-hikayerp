"""Oyunun kendi yasal dükkanları (24/7, LTD, Rob's Liquor, hırdavat, eğlence/balık dükkanı) kadar BİZİM dükkanı üretir.
Konumlar data/places/businesses.json'dan (qb-shops koordinatları). Zaten 25 m içinde bir dükkanımız varsa yenisi üretilmez.
Çalıştırma (proje kökünden): python tools/gen_native_shops.py   (yalnızca eksik dükkanları ekler, mevcutlara dokunmaz)
"""
import glob
import json
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHOPS = ROOT / "data" / "shops"
biz = json.loads((ROOT / "data" / "places" / "businesses.json").read_text(encoding="utf-8"))["items"]

# mevcut dükkanlar (konum, tür)
existing = []
for f in glob.glob(str(SHOPS / "*.json")):
    d = json.loads(pathlib.Path(f).read_text(encoding="utf-8"))
    if d.get("type") == "market":
        existing.append(d["pos"])

tmpl_market = json.loads((SHOPS / "bakkal_1.json").read_text(encoding="utf-8"))
tmpl_liquor = json.loads((SHOPS / "tekel_1.json").read_text(encoding="utf-8"))

# hırdavat envanteri
hardware_inv = [
    {"id": "cekic", "name": "Çekiç", "price": 18, "stock": -1, "effect": {"type": "give_item", "item": "cekic", "qty": 1}},
    {"id": "el_feneri", "name": "El feneri", "price": 14, "stock": -1, "effect": {"type": "give_item", "item": "el_feneri", "qty": 1}},
    {"id": "kablo", "name": "Uzatma kablosu", "price": 12, "stock": -1, "effect": {"type": "give_item", "item": "kablo", "qty": 1}},
    {"id": "boya", "name": "Boya kutusu", "price": 22, "stock": -1, "effect": {"type": "give_item", "item": "boya", "qty": 1}},
    {"id": "su", "name": "Su", "price": 2, "stock": -1, "effect": {"type": "give_item", "item": "su", "qty": 1}},
    {"id": "ilk_yardim", "name": "İlk yardım kiti", "price": 45, "stock": -1, "effect": {"type": "give_item", "item": "ilk_yardim", "qty": 1}},
]

# yeni eşyalar (kullanılamaz ama taşınır)
items_path = ROOT / "data" / "items.json"
items = json.loads(items_path.read_text(encoding="utf-8"))
for iid, name, w, desc in [("cekic", "Çekiç", 1.0, "Hırdavat çekici"), ("el_feneri", "El feneri", 0.4, "Pilli el feneri"),
                           ("kablo", "Uzatma kablosu", 0.6, "Uzatma kablosu"), ("boya", "Boya kutusu", 1.2, "Duvar boyası")]:
    items["items"].setdefault(iid, {"name": name, "weight": w, "desc": desc})
items_path.write_text(json.dumps(items, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

MODELS = [("a_f_y_hipster_01", "female", "Gracie Wise"), ("a_m_y_hipster_01", "male", "Ferran Simen"),
          ("a_f_m_fatwhite_01", "female", "Gracie Wise"), ("a_m_m_business_01", "male", "Ferran Simen")]
PERSON = {
    "supermarket": "Gece gündüz açık 24/7 market çalışanı; kısa ve rahat konuşur. Abur cubur, su ve sigara satar.",
    "ltd": "Benzinlik marketi çalışanı; yorgun ama kibar. Yol için su, atıştırmalık ve sigara satar.",
    "liquor": "İçki dükkanı sahibi; ciddi ve işini bilir. Yasal alkol ve sigara satar, yaşı küçük görünenlere ilk bakışta tereddüt eder.",
    "hardware": "Hırdavatçı; pratik ve konuşkan. Alet edevat satar, ne için lazım olduğunu sorar.",
    "shop": "Küçük bir dükkanın sahibi; güler yüzlü. Su ve atıştırmalık satar.",
}
NAMES = {"supermarket": "24/7 Market", "ltd": "LTD Market", "liquor": "Rob's İçki", "hardware": "Hırdavat", "shop": "Dükkan"}


def near(p, lst, r=25.0):
    return any(math.hypot(p[0] - q[0], p[1] - q[1]) < r for q in lst)


counts = {}
n_new = 0
for b in biz:
    t = b["type"]
    if t not in PERSON:
        continue
    counts[t] = counts.get(t, 0) + 1
    if near(b["pos"], existing):
        continue
    idx = counts[t]
    sid = f"{t}_{idx}"
    base = tmpl_liquor if t == "liquor" else tmpl_market
    d = json.loads(json.dumps(base))
    model, gender, voice = MODELS[(n_new) % len(MODELS)]
    d["id"] = sid
    d["name"] = NAMES[t]
    d["type"] = "market"
    d["pos"] = b["pos"]
    d["npc"] = {"model": model, "heading": 180.0, "gender": gender, "voice": {"speaker": voice}, "personality": PERSON[t], "profile": "random"}
    if t == "hardware":
        d["inventory"] = hardware_inv
    out = SHOPS / f"{sid}.json"
    if out.exists():
        continue
    out.write_text(json.dumps(d, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    existing.append(b["pos"])
    n_new += 1
print("yeni dükkan:", n_new, "| yerel tür sayıları:", counts)
