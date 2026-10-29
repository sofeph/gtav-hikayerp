"""Osman/Şule hikaye görevlerini ve 50 dinamik (emsal) görevi data/missions/ altına üretir.

Tüm bu görevler KOD kararlıdır: konuşma aşaması 'auto_advance' (oyuncu NPC ile konuşunca kod ilerletir), bitiş konuma
varma / E'ye basma / araç teslimi gibi kodun algıladığı eylemlerdir. LLM görevi bitirmez.
Çalıştırma: python tools/gen_missions.py
"""
import json
import pathlib

OUT = pathlib.Path(__file__).resolve().parents[1] / "data" / "missions"

# ---- NPC'ler ----
NPCS = {
    "yusuf": {
        "name": "Yusuf Cansever", "gender": "male", "model": "ig_yusufamir",
        "fallback_models": ["csb_yusufamir", "a_m_y_business_02"], "pos": [-53.0, -790.0, 44.0], "heading": 160.0,
        "voice": {"speaker": "Viktor Eka"},
        "personality": "Samimi, hızlı konuşan yazılımcı ve Sofeph CEO'su; oyuncuya 'kardeşim' der. Biraz stresli ama yardımsever.",
        "profile": {"name": "Yusuf Cansever", "title": "Sofeph CEO'su", "gender": "male", "hometown": "İstanbul",
                    "hometown_district": "Kadıköy", "military_unit": "İzmir", "past_jobs": ["yazılımcı"], "team": "Beşiktaş"},
    },
    "ayse": {
        "name": "Ezgi Güler", "gender": "female", "model": "ig_tracydisanto",
        "fallback_models": ["cs_tracydisanto", "a_f_y_business_02"], "pos": [-1850.0, -1231.0, 13.0], "heading": 90.0,
        "voice": {"speaker": "Gracie Wise"},
        "personality": "Sıcak, meraklı ve nazik; Sofeph'in müşterisi, muhasebeci. Ankara'lı, Galatasaray taraftarı. Oyuncuyla ilişkisi ilerledikçe samimileşir ama her zaman ölçülüdür.",
        "profile": {"name": "Ezgi Güler", "title": "Muhasebeci", "gender": "female", "hometown": "Ankara",
                    "hometown_district": "Çankaya", "past_jobs": ["muhasebeci"], "team": "Galatasaray"},
    },
    "burak": {
        "name": "Burak Gül", "gender": "male", "model": "a_m_y_business_01",
        "fallback_models": ["a_m_y_business_02", "a_m_m_business_01"], "pos": [-1048.7, -240.2, 44.02], "heading": 330.0, "exact_z": True,
        "voice": {"speaker": "Damien Black"},
        "personality": "Ekomvet adlı şirketin çevre mühendisi. Sakin, nazik, bilgisayardan pek anlamayan ama öğrenmeye açık; çevre ölçümlerinden hevesle bahseder.",
        "profile": {"name": "Burak Gül", "title": "Çevre mühendisi, Ekomvet", "company": "Ekomvet", "gender": "male",
                    "hometown": "İzmir", "hometown_district": "Karşıyaka", "military_unit": "Isparta",
                    "past_jobs": ["çevre mühendisi"], "team": "Galatasaray"},
    },
    "osman": {
        "name": "Osman Akbulut", "gender": "male", "model": "a_m_m_business_01",
        "fallback_models": ["a_m_y_business_03", "a_m_y_business_02"], "pos": [-1447.0, -537.0, 34.0], "heading": 120.0,
        "voice": {"speaker": "Baldur Sanjin"},
        "personality": "Kompetan File şirketinin CEO'su; Burak'ın arkadaşı. Girişimcilik ve muhasebe konusunda uzman, rakamlarla düşünen, planlı ve güvenilir. Boş zamanlarında müzik yapar/dinler, konuşurken müzik benzetmeleri kullanır. Sevgilisi avukat Şule Tatlılıoğlu.",
        "profile": {"name": "Osman Akbulut", "title": "Kompetan File CEO'su", "company": "Kompetan File", "gender": "male",
                    "expertise": "girişimcilik ve muhasebe", "hobby": "müzik", "hometown": "Eskişehir",
                    "hometown_district": "Tepebaşı", "military_unit": "Kırklareli", "past_jobs": ["muhasebeci", "girişimci"],
                    "team": "Galatasaray"},
    },
    "sule": {
        "name": "Şule Tatlılıoğlu", "gender": "female", "model": "a_f_y_business_01",
        "fallback_models": ["a_f_y_business_02", "a_f_m_business_02"], "pos": [-1195.0, -1130.0, 7.0], "heading": 90.0,
        "voice": {"speaker": "Claribel Dervla"},
        "personality": "Avukat; Osman Akbulut'un sevgilisi. Ciddi, keskin zekâlı ama sıcak, hukuki konularda net konuşur. Oyuncuya güvenir; başı derde girerse yardım edeceğini söyler.",
        "profile": {"name": "Şule Tatlılıoğlu", "title": "Avukat", "gender": "female", "hometown": "Bursa",
                    "hometown_district": "Nilüfer", "past_jobs": ["avukat"], "team": "Beşiktaş"},
    },
}


def npcs(*ids):
    return {i: NPCS[i] for i in ids}


def talk(sid, npc, obj, brief, in_person=False):
    st = {"id": sid, "type": "talk", "npc": npc, "in_person": in_person, "auto_advance": True,
          "objective": obj, "brief": brief}
    if npc == "ayse":  # Ezgi tek cümleyle ghosting yapmasın: görev en az 5 karşılıklı diyalogla ilerler
        st["min_turns"] = 5
    return st


def visit(sid, pos, prompt, obj, radius=5.0):
    return {"id": sid, "type": "interact", "pos": pos, "radius": radius, "prompt": prompt, "objective": obj}


def drive(sid, veh, start, dest, obj_start, obj_drive, radius=15.0):
    return [
        {"id": sid + "_arac", "type": "vehicle", "vehicle": veh, "pos": start, "heading": 90.0, "objective": obj_start},
        {"id": sid + "_teslim", "type": "drive_to", "pos": dest, "radius": radius, "objective": obj_drive},
    ]


def write(mid, data):
    (OUT / f"{mid}.json").write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


exec(compile((pathlib.Path(__file__).parent / "story_block.py").read_text(encoding="utf-8"), "story_block.py", "exec"))

exec(compile((pathlib.Path(__file__).parent / "story_block2.py").read_text(encoding="utf-8"), "story_block2.py", "exec"))

exec(compile((pathlib.Path(__file__).parent / "story_block3.py").read_text(encoding="utf-8"), "story_block3.py", "exec"))

exec(compile((pathlib.Path(__file__).parent / "story_block4.py").read_text(encoding="utf-8"), "story_block4.py", "exec"))

print("hikaye gorevi:", len(list(OUT.glob("*.json"))))
