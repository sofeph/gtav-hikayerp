"""GTA V yer verisini GitHub'dan çekip modun kullanacağı sade biçime çevirir.

Kaynak: https://github.com/DurtyFree/gta-v-data-dumps (oyun verisinden otomatik çıkarılmış dökümler).
Çıktı (data/places/):
  zones.json  mahalle/bölge sınırları  [{id, name, bounds: [[minx, miny, maxx, maxy], ...]}]
  pois.json   {atm, bus_stop, gas_station, phone_booth, garage}: [{pos: [x, y, z], ...}]

Çalıştırma (proje kökünden):  python tools/fetch_places.py
"""
import json
import math
import pathlib
import urllib.request

BASE = "https://raw.githubusercontent.com/DurtyFree/gta-v-data-dumps/master"
OUT = pathlib.Path(__file__).resolve().parent.parent / "data" / "places"


def fetch(path: str):
    with urllib.request.urlopen(f"{BASE}/{path}", timeout=60) as r:
        return json.loads(r.read().decode("utf-8-sig"))


def pos(o: dict) -> list[float]:
    p = o["Position"]
    return [round(p["X"], 1), round(p["Y"], 1), round(p["Z"], 1)]


def cluster(points: list[list[float]], radius: float) -> list[dict]:
    """Yakın noktaları tek yerde toplar (ör. bir benzinlikteki 8 pompa -> 1 istasyon)."""
    groups: list[list[list[float]]] = []
    for p in points:
        for g in groups:
            cx = sum(q[0] for q in g) / len(g)
            cy = sum(q[1] for q in g) / len(g)
            if math.hypot(p[0] - cx, p[1] - cy) <= radius:
                g.append(p)
                break
        else:
            groups.append([p])
    return [{"pos": [round(sum(q[i] for q in g) / len(g), 1) for i in range(3)], "count": len(g)} for g in groups]


def story_garages(raw: list[dict]) -> list[dict]:
    """Sadece hikaye modu ev garajları ("Michael - Beverly Hills" gibi); tekrarlar ve Online garajları elenir."""
    seen: dict[str, dict] = {}
    for g in raw:
        if " - " in g["Name"] and not g.get("IsMultiplayerGarage"):
            seen.setdefault(g["Name"], {"name": g["Name"], "pos": pos(g)})
    return list(seen.values())


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)

    zones = []
    for z in fetch("zones.json"):
        bounds = [[b["Minimum"]["X"], b["Minimum"]["Y"], b["Maximum"]["X"], b["Maximum"]["Y"]] for b in z["Bounds"]]
        zones.append({"id": z["Name"], "name": z["DisplayName"] or z["Name"], "bounds": bounds})
    (OUT / "zones.json").write_text(json.dumps(zones, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")

    pois = {
        "atm": cluster([pos(o) for o in fetch("objectslocations/worldAtms.json")], 6),
        "bus_stop": cluster([pos(o) for o in fetch("objectslocations/worldBusStops.json")], 8),
        "gas_station": cluster([pos(o) for o in fetch("objectslocations/worldGasPumps.json")], 25),
        "phone_booth": cluster([pos(o) for o in fetch("objectslocations/worldPublicPhones.json")], 6),
        "garage": story_garages(fetch("garages.json")),
    }
    (OUT / "pois.json").write_text(json.dumps(pois, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")

    print(f"bolge: {len(zones)}")
    for k, v in pois.items():
        print(f"{k}: {len(v)}")


if __name__ == "__main__":
    main()
