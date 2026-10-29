"""
Ev listesini GitHub'dan çeker ve data/places/houses.json üretir (emlak sistemi için).

Kaynak: Landelan7964/QB-HouseLocations -> houselocations.sql (qb-houses tablosu: ad, etiket, kapı konumu, fiyat, kademe, garaj).
Sadece KOORDİNAT ve etiket gibi olgusal veriler alınır (kod/içerik değil); depo lisanssızdır, bu yüzden çıktı yeniden
üretilebilir bırakıldı: python tools/fetch_houses.py. Kaynak ve not data/places/SOURCES.md içinde.
Fiyatlar (FiveM ekonomisi için yazılmış) oyundaki ev fiyatına çevrilir: tier'a göre alt/üst sınır, kira = fiyatın ~%0.9'u.
"""
import json
import pathlib
import re
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "data" / "places" / "houses.json"
URL = "https://raw.githubusercontent.com/Landelan7964/QB-HouseLocations/main/houselocations.sql"

ROW = re.compile(r"\((\d+), '((?:[^'\\]|\\.)*)', '((?:[^'\\]|\\.)*)', '(\{.*?\})', (\d+), (\d+), (\d+), '(\{.*?\})'\)(?:,|;)\s*$", re.M)


def unescape(s: str) -> str:
    return s.replace('\\"', '"').replace("\\'", "'")


def main() -> None:
    sql = urllib.request.urlopen(URL, timeout=60).read().decode("utf-8", "replace")
    houses = []
    for m in ROW.finditer(sql):
        _id, name, label, coords_s, _owned, price, tier, garage_s = m.groups()
        try:
            coords = json.loads(unescape(coords_s))
            garage = json.loads(unescape(garage_s))
        except json.JSONDecodeError:
            continue
        enter = coords.get("enter")
        if not enter or "x" not in enter:
            continue
        houses.append({
            "id": name.strip().replace(" ", "_").lower(),
            "label": unescape(label).strip(),
            "pos": [round(enter["x"], 1), round(enter["y"], 1), round(enter["z"], 1)],
            "heading": round(enter.get("h", 0.0), 1),
            "garage": [round(garage["x"], 1), round(garage["y"], 1), round(garage["z"], 1)] if garage.get("x") else None,
            "tier": int(tier),
            "price_src": int(price),
        })
    # tekrar eden kimlikleri ayıkla
    seen, uniq = set(), []
    for h in houses:
        if h["id"] in seen:
            continue
        seen.add(h["id"])
        uniq.append(h)
    # oyun içi fiyat: tier'a göre makul aralık (ABD dolarıyla), kaynak fiyattan türetilir
    bands = {1: (90000, 180000), 2: (60000, 120000), 3: (35000, 80000), 4: (20000, 50000), 5: (12000, 30000)}
    for h in uniq:
        lo, hi = bands.get(h["tier"], (30000, 90000))
        h["price"] = int(round(min(hi, max(lo, h.pop("price_src") * 2)) / 500.0) * 500)
        h["rent"] = int(round(h["price"] * 0.009 / 10.0) * 10)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({"_not": "tools/fetch_houses.py ile uretilir (kaynak: data/places/SOURCES.md)", "items": uniq},
                              ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{len(uniq)} ev -> {OUT}")


if __name__ == "__main__":
    main()
