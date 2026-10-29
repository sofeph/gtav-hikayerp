"""Yeni dükkanları üretir (data/shops/*.json) ve kod yamalarını uygular: zemine oturan NPC, 'refuel' etkisi, ayarlar.
Araç satın alma: spawn_vehicle etkisi garaja kaydeder (plaka, dolu depo). Çalıştırma: python tools/gen_shops.py
"""
import json
import pathlib

R = pathlib.Path(__file__).resolve().parents[1]
OUT = R / "data" / "shops"
OUT.mkdir(parents=True, exist_ok=True)

BARGAIN = {"enabled": True, "max_discount": 0.15, "absolute_cap": 0.30, "patience": 3, "lie_detect_chance": 0.4,
           "closing_hour_bonus": 0.03}


def car(cid, name, price, stock, model=None):
    return {"id": cid, "name": name, "price": price, "stock": stock, "effect": {"type": "spawn_vehicle", "model": model or cid}}


def shop(sid, name, stype, pos, model, gender, voice, personality, inventory, hours=(8, 22), radius=2.5, bargain=True):
    d = {"id": sid, "name": name, "type": stype, "pos": pos, "interact_radius": radius,
         "npc": {"model": model, "heading": 180.0, "gender": gender, "voice": {"speaker": voice},
                 "personality": personality, "profile": "random"},
         "hours": {"open": hours[0], "close": hours[1]}, "inventory": inventory}
    if bargain:
        d["bargain"] = BARGAIN
    (OUT / f"{sid}.json").write_text(json.dumps(d, ensure_ascii=False, indent=2), encoding="utf-8")


BUY = " Satın alınan araç alıcıya aittir, deposu doludur ve alıcı onu telefondan çağırabilir."

shop("galeri_premium", "Rockford Lüks Galeri", "car_dealer", [-1290.0, -330.0, 36.7], "a_f_y_business_01", "female", "Claribel Dervla",
     "Şık, kendinden emin, lüks araç satan bir satış danışmanı. Müşteriye 'efendim' der, her arabanın hikâyesini anlatır; pazarlıkta yumuşak ama ödün vermez." + BUY,
     [car("felon", "Felon", 38000, 2), car("oracle2", "Oracle XS", 42000, 2), car("schafter2", "Schafter", 52000, 2),
      car("jackal", "Jackal", 60000, 1), car("comet2", "Comet", 95000, 1), car("carbonizzare", "Carbonizzare", 110000, 1)])

shop("galeri_sandy", "Sandy Shores İkinci El", "car_dealer", [1980.0, 3750.0, 32.2], "a_m_m_hillbilly_01", "male", "Baldur Sanjin",
     "Yorgun, esprili, çöl kasabasının eski kurdu bir ikinci el satıcısı. Araçları 'çalışır, yeter' diye satar; pazarlığı sever." + BUY,
     [car("emperor", "Emperor", 2200, 3), car("stratum", "Stratum", 3200, 3), car("regina", "Regina", 2800, 3), car("ingot", "Ingot", 3600, 2),
      car("tornado", "Tornado", 5200, 2), car("peyote", "Peyote", 7800, 1)])

shop("galeri_paleto", "Paleto Oto Pazarı", "car_dealer", [-120.0, 6300.0, 31.5], "a_m_m_farmer_01", "male", "Ferran Simen",
     "Sakin, dürüst, kasabanın güvenilir pickup ve arazi aracı satıcısı. Kısa ve az konuşur; arabanın yük çekmediğini söylemez." + BUY,
     [car("bison", "Bison", 9000, 3), car("rebel", "Rebel", 12000, 2), car("sandking", "Sandking", 23000, 2), car("mesa", "Mesa", 15000, 2),
      car("bodhi2", "Bodhi", 6500, 2), car("kalahari", "Kalahari", 8200, 2)])

shop("motor_bayi", "Vespucci Motosiklet", "car_dealer", [-1180.0, -1180.0, 5.6], "a_m_y_motox_01", "male", "Damien Black",
     "Motosiklet tutkunu, hızlı konuşan, şakacı bir bayi. Her motorun sesini taklit eder; kask takmayı tembihler." + BUY,
     [car("faggio", "Faggio", 1500, 5, "faggio2"), car("sanchez", "Sanchez", 6500, 3, "sanchez"), car("pcj", "PCJ-600", 9000, 2, "pcj"),
      car("hexer", "Hexer", 12000, 2, "hexer"), car("double", "Double-T", 15000, 2, "double"), car("bati", "Bati 801", 18000, 2, "bati")])

shop("ticari_bayi", "Cypress Ticari Araç", "car_dealer", [900.0, -2100.0, 30.5], "s_m_y_construct_01", "male", "Viktor Eka",
     "Pratik, işine odaklı, kamyonet ve minibüs satan bir ticari araç bayisi. Yük kapasitesinden ve yakıt tüketiminden bahseder." + BUY,
     [car("speedo", "Speedo", 11000, 3), car("burrito", "Burrito", 13000, 3, "burrito3"), car("rumpo", "Rumpo", 12500, 2),
      car("pony", "Pony", 14000, 2), car("mule", "Mule", 26000, 1), car("taco", "Taco Van", 17000, 1)])

shop("market_benzin", "LTD Benzin & Market", "market", [-70.0, -1760.0, 29.5], "a_f_y_hipster_01", "female", "Gracie Wise",
     "Tezgâhtar; gece vardiyasından yorgun ama güler yüzlü bir market çalışanı. Kısa ve samimi konuşur; benzinin pompadan alınacağını hatırlatır.",
     [{"id": "su", "name": "Su", "price": 2, "stock": -1, "effect": {"type": "heal"}},
      {"id": "sandvic", "name": "Sandviç", "price": 6, "stock": -1, "effect": {"type": "heal"}},
      {"id": "enerji", "name": "Enerji içeceği", "price": 9, "stock": -1, "effect": {"type": "armor"}},
      {"id": "bidon", "name": "Yakıt bidonu (aracın deposuna +%50)", "price": 45, "stock": -1, "effect": {"type": "refuel", "pct": 50}}],
     hours=(0, 24), bargain=False)

(R / "data" / "fuel.json").write_text(json.dumps({
    "_not": "Benzin sistemi. price_per_pct: yakıtın her yüzdesi kaç dolar (depo ~ $120). station_radius_m: istasyona bu kadar yakındaysan E ile depo doldurursun.",
    "price_per_pct": 1.2, "station_radius_m": 14}, ensure_ascii=False, indent=2), encoding="utf-8")

# services.json: tamirci
sp = R / "data" / "services.json"
cfg = json.loads(sp.read_text(encoding="utf-8"))
cfg["mechanic"] = {"vehicle": "towtruck", "model": "s_m_y_construct_01", "base_fee": 120, "repair_per_hp": 0.15,
                   "fuel_per_pct": 1.2, "avg_speed_mps": 14, "drive_speed_mps": 24}
sp.write_text(json.dumps(cfg, ensure_ascii=False, indent=2), encoding="utf-8")


def edit(rel, pairs):
    p = R / rel
    c = p.read_text(encoding="utf-8-sig")
    for old, new in pairs:
        assert old in c, (rel, old[:70])
        c = c.replace(old, new, 1)
    p.write_text(c, encoding="utf-8")


# ShopManager: NPC'yi zemine oturt (yeni dükkanların koordinatı yaklaşık)
edit("mod/src/shop_manager.h", [("    Ped ped = 0;  // 0: spawn edilmemiş", "    Ped ped = 0;  // 0: spawn edilmemiş\n    int ground_tries = 0;")])
edit("mod/src/shop_manager.cpp", [
    ("    const int ped_type = shop.npc_gender == \"female\" ? 5 /*PED_TYPE_CIVFEMALE*/ : 4 /*PED_TYPE_CIVMALE*/;\n    Ped ped = PED::CREATE_PED(ped_type, model, shop.pos.x, shop.pos.y, shop.pos.z,\n                              shop.npc_heading, false, false);",
     "    // Zemine oturt: dükkan koordinatları yaklaşık olabilir (çarpışma yüklenene kadar kısa süre bekler).\n"
     "    float z = shop.pos.z;\n"
     "    STREAMING::REQUEST_COLLISION_AT_COORD(shop.pos.x, shop.pos.y, z);\n"
     "    float gz = 0.f;\n"
     "    if (GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(shop.pos.x, shop.pos.y, z + 3.0f, &gz, false) && std::fabs(gz - z) < 6.f) {\n"
     "        z = gz;\n"
     "        shop.pos.z = gz;  // etkileşim mesafesi de gerçek zemine göre\n"
     "    } else if (++shop.ground_tries < 90) {\n"
     "        return false;\n"
     "    }\n"
     "    const int ped_type = shop.npc_gender == \"female\" ? 5 /*PED_TYPE_CIVFEMALE*/ : 4 /*PED_TYPE_CIVMALE*/;\n"
     "    Ped ped = PED::CREATE_PED(ped_type, model, shop.pos.x, shop.pos.y, z,\n"
     "                              shop.npc_heading, false, false);"),
    ("    shop.ped = 0;\n}\n\nvoid ShopManager::Update", "    shop.ped = 0;\n    shop.ground_tries = 0;\n}\n\nvoid ShopManager::Update"),
])
# economy: refuel etkisi
edit("mod/src/economy.cpp", [
    ('#include <string>', '#include <string>\n\n#include "fuel.h"'),
    ('    if (type == "spawn_vehicle") {',
     '    if (type == "refuel") {  // bidon: son kullanılan araç yakındaysa deposuna yakıt ekler\n'
     '        const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(player, true);\n'
     '        if (v == 0 || !ENTITY::DOES_ENTITY_EXIST(v)) return false;\n'
     '        const Vector3 a = ENTITY::GET_ENTITY_COORDS(player, true), b = ENTITY::GET_ENTITY_COORDS(v, true);\n'
     '        if ((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) > 40.f * 40.f) return false;\n'
     '        const float cur = fuel::Get(v) < 0.f ? 30.f : fuel::Get(v);\n'
     '        fuel::Set(v, cur + effect.value("pct", 50.f));\n'
     '        VEHICLE::SET_VEHICLE_ENGINE_ON(v, true, true, false);\n'
     '        return true;\n'
     '    }\n'
     '    if (type == "spawn_vehicle") {'),
])
print("ok")
