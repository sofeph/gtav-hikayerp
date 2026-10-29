"""data/transit.json üretir: otobüs hatları (data/places/pois.json'daki GERÇEK otobüs durağı konumlarından) ve tramvay hatları.

Otobüs: her hat, başlangıç bölgesindeki duraktan en yakın komşu zinciriyle kurulur (hop <= 650 m, en fazla 9 durak). Durak adı, bulunduğu bölge adıdır
(data/places/zones.json). LST Metro: gerçek istasyon adları, yaklaşık konumlar; tarifeye bağlı sembolik hızlı geçiş.
Çalıştırma (proje kökünden):  python tools/gen_transit.py
"""
import json
import math
import pathlib

R = pathlib.Path(__file__).resolve().parents[1]
pois = json.loads((R / "data/places/pois.json").read_text(encoding="utf-8"))
zones = json.loads((R / "data/places/zones.json").read_text(encoding="utf-8"))
stops_all = [p["pos"] for p in pois["bus_stop"]]


def zone_name(x: float, y: float) -> str:
    for z in zones:
        for b in z["bounds"]:
            if b[0] <= x <= b[2] and b[1] <= y <= b[3]:
                return z["name"]
    return "Durak"


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def chain(seed, max_stops=9, hop=650.0, radius=1800.0):
    start = min(stops_all, key=lambda p: dist(p, seed))
    used = [start]
    while len(used) < max_stops:
        cand = [p for p in stops_all if p not in used and dist(p, seed) <= radius]
        if not cand:
            break
        nxt = min(cand, key=lambda p: dist(p, used[-1]))
        if dist(nxt, used[-1]) > hop:
            break
        used.append(nxt)
    return used


# 10 şehir otobüsü hattı: iki uç nokta arasındaki koridor (genişlik 450 m) içindeki GERÇEK otobüs durakları, uçtan uca sırayla dizilir.
# Duraklar hatlar arasında paylaşılabilir (aktarma). En az 3 durak yoksa hat atlanır.
CORRIDORS = [("B1", "Legion Square - Vespucci", (195.0, -934.0), (-1330.0, -1500.0)),
             ("B2", "Vespucci - Rockford", (-1330.0, -1500.0), (-1370.0, -480.0)),
             ("B3", "Rockford - Vinewood", (-1370.0, -480.0), (300.0, 200.0)),
             ("B4", "Vinewood - Mirror Park", (300.0, 200.0), (1060.0, -690.0)),
             ("B5", "Mirror Park - Liman", (1060.0, -690.0), (1000.0, -2900.0)),
             ("B6", "Davis - Legion Square", (100.0, -1900.0), (195.0, -934.0)),
             ("B7", "Havalimani - Davis", (-1030.0, -2730.0), (100.0, -1900.0)),
             ("B8", "Havalimani - Vespucci", (-1030.0, -2730.0), (-1330.0, -1500.0)),
             ("B9", "Pillbox - Rockford", (300.0, -600.0), (-1370.0, -480.0)),
             ("B10", "Maze Bank Plaza - La Mesa", (-53.0, -790.0), (900.0, -1600.0))]


def corridor(a, b, width=450.0, max_stops=10):
    ax, ay = a
    bx, by = b
    L = math.hypot(bx - ax, by - ay)
    out = []
    for p in stops_all:
        t = ((p[0] - ax) * (bx - ax) + (p[1] - ay) * (by - ay)) / (L * L)
        if t < -0.05 or t > 1.05:
            continue
        # koridor çizgisine dik uzaklık
        d = abs((bx - ax) * (ay - p[1]) - (ax - p[0]) * (by - ay)) / L
        if d <= width:
            out.append((t, p))
    out.sort(key=lambda x: x[0])
    pts = [p for _, p in out]
    if len(pts) > max_stops:  # eşit aralıkla seyrelt
        step = len(pts) / max_stops
        pts = [pts[int(i * step)] for i in range(max_stops)]
    return pts


lines = []
for lid, name, a, b in CORRIDORS:
    pts = []
    for width in (450.0, 700.0, 1000.0, 1400.0):  # koridoru gerekirse genişlet
        pts = corridor(a, b, width)
        if len(pts) >= 3:
            break
    if len(pts) < 3:
        print("atlandi (yetersiz durak):", lid, len(pts))
        continue
    seen = {}
    stops = []
    for p in pts:
        nm = zone_name(p[0], p[1])
        seen[nm] = seen.get(nm, 0) + 1
        stops.append({"name": nm if seen[nm] == 1 else f"{nm} {seen[nm]}", "pos": [round(p[0], 1), round(p[1], 1), round(p[2], 1)]})
    lines.append({"id": lid, "name": name, "type": "bus", "fare": 3, "dwell_s": 12, "stops": stops})

# LST Metro ("The Arrow", Los Santos Transit): istasyon adları oyundan, konumlar YAKLAŞIK (oyunda doğrulanmadı). Portola Drive konumu bilinmediği için yok.
# headway_s: sefer aralığı, leg_s: iki istasyon arası tarife süresi (sembolik hızlı geçiş).
METRO = [("LSIA Terminal 4", -1085.0, -2720.0, 13.8), ("LSIA Parking", -889.0, -2312.0, 13.9), ("Puerto Del Sol", -531.0, -2063.0, 8.0),
         ("Davis", 112.0, -1730.0, 29.5), ("Strawberry", 279.0, -1198.0, 29.4), ("Pillbox South", -217.0, -1031.0, 30.0),
         ("Little Seoul", -495.0, -698.0, 33.0), ("Burton", -286.0, -319.0, 10.0), ("Del Perro", -815.0, -125.0, 37.5)]
lines.append({"id": "M1", "name": "LST Metro (The Arrow)", "type": "metro", "fare": 4, "dwell_s": 14, "headway_s": 150, "leg_s": 25,
              "stops": [{"name": n, "pos": [x, y, z]} for n, x, y, z in METRO]})

# Bölgesel yolcu otobüsü (şehirler arası): otogarlar, bölge merkezine en yakın GERÇEK otobüs durağına oturtulur (400 m içinde yoksa merkez).
# R1 saat yönünde, R2 ters yönde; R2 yarım sefer aralığı kaydırılır (aynı otogarda iki yön aynı anda kalkmaz). Ücret mesafeye göre.
import os
biz = json.loads((R / "data/places/businesses.json").read_text(encoding="utf-8")) if os.path.exists(R / "data/places/businesses.json") else {"items": []}
cand_pts = list(stops_all) + [i["pos"] for i in biz["items"] if i["type"] in ("supermarket", "ltd")]
REGIONS = [("Los Santos", (195.0, -934.0)), ("Sandy Shores", (1960.0, 3740.0)), ("Grapeseed", (1700.0, 4920.0)),
           ("Paleto Bay", (-100.0, 6290.0)), ("Chumash", (-3150.0, 1090.0))]
terms = []
for nm, ctr in REGIONS:
    best = min(cand_pts, key=lambda p: dist(p, ctr))
    pos = best if dist(best, ctr) < 500 else [ctr[0], ctr[1], 30.0]
    terms.append({"name": nm, "pos": [round(pos[0], 1), round(pos[1], 1), round(pos[2], 1)]})
lines.append({"id": "R1", "name": "Bolgesel Otobus (ileri)", "type": "coach", "fare_per_km": 3.0, "fare_min": 12, "dwell_s": 14,
              "headway_s": 360, "leg_s": 40, "phase_s": 0, "stops": terms})
lines.append({"id": "R2", "name": "Bolgesel Otobus (ters)", "type": "coach", "fare_per_km": 3.0, "fare_min": 12, "dwell_s": 14,
              "headway_s": 360, "leg_s": 40, "phase_s": 180, "stops": list(reversed(terms))})

(R / "data/transit.json").write_text(json.dumps({"_not": "Toplu taşıma hatları. bus: gerçek otobüs durak konumları (pois.json). tram: sembolik, tarifeye bağlı.", "lines": lines}, ensure_ascii=False, indent=1), encoding="utf-8")
print("hat:", len(lines), {l["id"]: len(l["stops"]) for l in lines})
