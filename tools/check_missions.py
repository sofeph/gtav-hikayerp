"""Görev dosyası doğrulayıcı (oyunu açmadan test): data/missions ve addons/<paket>/missions içindeki görevleri mod yükleyicisinin kurallarıyla denetler.

Kullanım (proje kökünden):
    python tools/check_missions.py                      # hepsi
    python tools/check_missions.py addons/benim_paket   # tek eklenti klasörü (ana görevlerle birlikte bağlantılar denetlenir)
    python tools/check_missions.py yol/gorev.json       # tek dosya
Çıkış kodu: 0 = hata yok, 1 = hata var. Uyarılar (WARN) hata sayılmaz.

Denetlenenler (mod/src/missions.cpp ile aynı kurallar + tasarım rehberi GOREV_TASARIM.md):
  - zorunlu alanlar (id, giver, npcs, stages), aşama türü, talk/install/escort/walk için npc, interact/install/escort/scene için pos
  - giver ve aşama npc'leri `npcs` içinde tanımlı mı, NPC'de model ve pos var mı
  - requires kimlikleri var mı, döngü var mı, id'ler benzersiz mi, aşama id'leri benzersiz mi
  - unlocks/removes_contacts NPC anahtarları, secret.npc, sets_relationship.npc
  - koordinat sayıları, radius, order çakışması, prompt'ta ASCII dışı karakter (uyarı), brief'te rakam/telefon (uyarı)
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAGE_TYPES = {"talk", "interact", "vehicle", "drive_to", "walk", "scene", "install", "escort"}
NEEDS_NPC = {"talk", "install", "escort", "walk"}
NEEDS_POS = {"interact", "install", "escort", "scene"}
REL = {"stranger", "acquaintance", "friend", "close_friend", "partner"}

errors, warns = [], []


def err(f, m): errors.append(f"HATA  {f}: {m}")
def warn(f, m): warns.append(f"WARN  {f}: {m}")


def is_pos(v):
    return isinstance(v, list) and len(v) >= 3 and all(isinstance(x, (int, float)) for x in v[:3])


def load_all(targets):
    files = []
    for t in targets:
        if os.path.isdir(t):
            files += glob.glob(os.path.join(t, "missions", "*.json")) + glob.glob(os.path.join(t, "*.json"))
        elif os.path.isfile(t):
            files.append(t)
    return sorted(set(files))


def check_file(f, d, ids_all):
    name = os.path.basename(f)
    for k in ("id", "giver", "npcs", "stages"):
        if k not in d:
            err(name, f"zorunlu alan yok: {k}")
    if "id" not in d or "npcs" not in d or "stages" not in d:
        return
    if not isinstance(d["npcs"], dict) or not d["npcs"]:
        err(name, "npcs boş")
    if not isinstance(d["stages"], list) or not d["stages"]:
        err(name, "asama yok (stages boş)")
        return
    if d.get("giver") not in d["npcs"]:
        err(name, f"giver '{d.get('giver')}' npcs içinde tanımlı değil")
    for key, n in d["npcs"].items():
        if "model" not in n:
            err(name, f"npc '{key}': model yok")
        if not is_pos(n.get("pos")):
            err(name, f"npc '{key}': pos [x,y,z] geçersiz")
        if "personality" not in n:
            warn(name, f"npc '{key}': personality yok (LLM'e kişilik gitmez)")
        if not (n.get("profile") or {}).get("name"):
            warn(name, f"npc '{key}': profile.name yok")
    seen = set()
    for i, s in enumerate(d["stages"]):
        sid = s.get("id", f"#{i}")
        t = s.get("type", "talk")
        if t not in STAGE_TYPES:
            err(name, f"aşama '{sid}': bilinmeyen tür '{t}' (geçerli: {', '.join(sorted(STAGE_TYPES))})")
        if sid in seen:
            err(name, f"aşama id tekrar: {sid}")
        seen.add(sid)
        if not s.get("id"):
            warn(name, f"aşama #{i}: id yok")
        if t in NEEDS_NPC:
            if not s.get("npc"):
                err(name, f"npc yok: {sid} ({t} aşaması npc ister)")
            elif s["npc"] not in d["npcs"]:
                err(name, f"aşama '{sid}': npc '{s['npc']}' npcs içinde yok")
        if t in NEEDS_POS and not is_pos(s.get("pos")):
            err(name, f"pos yok: {sid} ({t} aşaması pos ister)")
        if t in ("vehicle", "drive_to") and "pos" in s and not is_pos(s["pos"]):
            err(name, f"aşama '{sid}': pos geçersiz")
        if t == "vehicle" and not s.get("vehicle"):
            err(name, f"aşama '{sid}': vehicle (model adı) yok")
        if t == "scene" and s.get("kill_npc") and s["kill_npc"] not in d["npcs"]:
            err(name, f"aşama '{sid}': kill_npc '{s['kill_npc']}' npcs içinde yok")
        if "radius" in s and not isinstance(s["radius"], (int, float)):
            err(name, f"aşama '{sid}': radius sayı olmalı")
        if not s.get("objective"):
            warn(name, f"aşama '{sid}': objective boş (HUD'da hedef yazısı çıkmaz)")
        if t == "talk" and not s.get("brief") and i > 0:
            warn(name, f"aşama '{sid}': brief yok (NPC rolü belirsiz)")
        if s.get("prompt") and re.search(r"[^\x00-\x7f]", s["prompt"]):
            warn(name, f"aşama '{sid}': prompt ASCII dışı karakter içeriyor (oyun fontu Türkçe çizemeyebilir)")
        b = s.get("brief", "")
        if re.search(r"\d{3}[-\s]?\d{3,4}|\d{5,}", b):
            warn(name, f"aşama '{sid}': brief'te telefon/uzun sayı var (NPC uydurma veri söylemesin; secret/bayrak kullan)")
        if t == "install" and not isinstance(s.get("installer"), dict):
            err(name, f"aşama '{sid}': installer ayarı yok")
    for r in d.get("requires", []) or []:
        if r not in ids_all:
            err(name, f"requires '{r}' hiçbir görevde yok (görev sonsuza kadar kilitli kalır)")
    for key in (d.get("unlocks_contacts") or []) + (d.get("removes_contacts") or []):
        if key not in d["npcs"]:
            warn(name, f"rehber kişisi '{key}' bu görevin npcs'inde yok (başka görevde tanımlıysa sorun değil)")
    sec = d.get("secret")
    if isinstance(sec, dict):
        if sec.get("npc") not in d["npcs"]:
            err(name, f"secret.npc '{sec.get('npc')}' npcs içinde yok")
        n = len(d["stages"])
        if not (0 <= sec.get("from_stage", 0) <= sec.get("to_stage", n) <= n):
            warn(name, "secret aşama aralığı aşama sayısının dışında")
    rel = d.get("sets_relationship")
    if isinstance(rel, dict) and rel.get("status") not in REL:
        err(name, f"sets_relationship.status geçersiz ('{rel.get('status')}'; geçerli: {', '.join(sorted(REL))})")
    if d.get("start_by") not in (None, "contact", "incoming_call"):
        err(name, f"start_by geçersiz: {d.get('start_by')}")
    if not isinstance(d.get("reward", 0), int) or d.get("reward", 0) < 0:
        err(name, "reward negatif olmayan tam sayı olmalı")


def has_cycle(graph):
    state = {}

    def dfs(u):
        state[u] = 1
        for v in graph.get(u, []):
            if state.get(v) == 1 or (state.get(v) is None and dfs(v)):
                return True
        state[u] = 2
        return False

    return any(state.get(u) is None and dfs(u) for u in graph)


def main():
    targets = sys.argv[1:]
    base = glob.glob(os.path.join(ROOT, "data", "missions", "*.json"))
    check = load_all(targets) if targets else base + glob.glob(os.path.join(ROOT, "addons", "*", "missions", "*.json"))
    check = [c for c in check if not os.path.basename(os.path.dirname(os.path.dirname(c))).startswith("_") or targets]
    docs = {}
    for f in check:
        try:
            docs[f] = json.load(open(f, encoding="utf-8"))
        except Exception as e:  # noqa: BLE001
            err(os.path.basename(f), f"JSON okunamadı: {e}")
    # bağlantı denetimi için tüm kimlikler (ana görevler her zaman dahil)
    ids_all = {}
    for f in base + [x for x in glob.glob(os.path.join(ROOT, "addons", "*", "missions", "*.json")) if not os.path.basename(os.path.dirname(os.path.dirname(x))).startswith("_")] + list(docs):
        try:
            d = docs.get(f) or json.load(open(f, encoding="utf-8"))
            if "id" in d:
                if d["id"] in ids_all and ids_all[d["id"]] != f:
                    err(os.path.basename(f), f"görev id '{d['id']}' başka dosyada da var: {os.path.basename(ids_all[d['id']])}")
                ids_all[d["id"]] = f
        except Exception:  # noqa: BLE001
            pass
    orders = {}
    graph = {}
    for f, d in docs.items():
        check_file(f, d, ids_all)
        graph[d.get("id", f)] = list(d.get("requires", []) or [])
        orders.setdefault(d.get("order"), []).append(d.get("id"))
    full = dict(graph)
    for f in base:
        d = json.load(open(f, encoding="utf-8"))
        full.setdefault(d.get("id"), list(d.get("requires", []) or []))
    if has_cycle(full):
        err("(tüm görevler)", "requires döngüsü var (görevler birbirini bekliyor)")
    for o, ids in orders.items():
        if o is not None and len(ids) > 1:
            warn("(sıra)", f"order {o} birden çok görevde: {', '.join(ids)}")
    for m in errors + warns:
        print(m)
    print(f"\n{len(docs)} görev denetlendi: {len(errors)} hata, {len(warns)} uyarı")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
