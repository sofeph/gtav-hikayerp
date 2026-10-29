# ---- Hikaye görevleri (sıfırdan tasarım) ----
# Kurallar: konuşma aşamaları 'auto_advance' (kod ilerletir, AI bitirmez; tek istisna IT yoklaması: AI sadece seviye seçer),
# bitişler kodun algıladığı eylemlerdir (konuma var, E'ye bas, araç teslim). Her görev paralel zincirler için 'requires' ile bağlıdır.
# Yedek: backups/ klasöründe eski sürüm var.
for old in OUT.glob("*.json"):
    old.unlink()  # dinamik görevler kaldırıldı: eski dosyalar silinir, hikaye yeniden üretilir

LB_PC = [-1050.24, -242.76, 44.02]          # Lifeinvader üst kat: Jay Norris'in masası (oyuncu ölçümü)
LB_BURAK = [-1048.7, -240.2, 44.02]
VAN_AT = [-1035.0, -220.0, 37.5]            # Ekomvet saha aracı: Lifeinvader önü
PIER = [-1850.0, -1231.0, 13.0]


def person(pid, pos, heading=None, exact_z=False):
    p = dict(NPCS[pid])
    p["pos"] = list(pos)
    if heading is not None:
        p["heading"] = heading
    if exact_z:
        p["exact_z"] = True
    return p


def ayse(pos, heading=90.0):
    return person("ayse", pos, heading)


# Buluşma yeri: LLM'in profil bilgisiyle (memleket, ilçe) karıştırmaması için ayrı alan olarak verilir (QuestState: meeting_place).
PLACES = {
    "ayse_tanisma": "Del Perro İskelesi'ndeki kafe",
    "ayse_sahil": "Vespucci Plajı",
    "ayse_aksam_yemegi": "Mirror Park'taki restoran",
    "ayse_gun_batimi": "Gözlemevi",
    "ayse_yeni_sayfa": "Del Perro İskelesi",
    "ayse_son_arama": "Del Perro İskelesi",
}


def mission(mid, title, order, reward, giver, npc_map, stages, requires=None, **extra):
    for s in stages:
        if s.get("type") == "talk" and mid in PLACES:
            s["place"] = PLACES[mid]
    d = {"id": mid, "title": title, "order": order, "reward": reward, "giver": giver, "giver_always": False,
         "npcs": npc_map, "stages": stages}
    if requires:
        d["requires"] = requires
    d.update(extra)
    write(mid, d)


def phone_talk(sid, npc, obj, brief):
    return talk(sid, npc, obj, brief, in_person=False)


def face_talk(sid, npc, obj, brief):
    return talk(sid, npc, obj, brief, in_person=True)


def field_leg(n, pos, name, radius=30.0):
    return [
        {"id": f"git{n}", "type": "drive_to", "pos": pos, "radius": radius, "objective": f"Saha aracıyla {name} noktasına git"},
        {"id": f"olc{n}", "type": "interact", "pos": pos, "radius": 40.0, "prompt": "Olcum al",
         "objective": f"Araçtan in ve {name} noktasında ölçüm al",
         "guide": "Araçtan in ve sarı işaretli noktada E'ye bas; sonra araca dön."},
    ]


VAN = {"id": "arac", "type": "vehicle", "vehicle": "speedo", "pos": VAN_AT, "heading": 120.0,
       "objective": "Ekomvet saha aracına bin (Lifeinvader binasının önünde)"}

# --- Yusuf ---
mission("sofeph_ilk_teslimat", "Sofeph: İlk Gün", 1, 1500, "yusuf",
        {"yusuf": NPCS["yusuf"], "ayse": ayse(PIER)},
        [face_talk("tanisma", "yusuf", "Maze Bank Plaza'da Yusuf Cansever ile tanış",
                   "Oyuncu Los Santos'ta yeni; Yusuf onu Sofeph'e davet ediyor. İlk iş: prototip USB'yi Del Perro İskelesi'ndeki müşteri "
                   "Ezgi Güler'e teslim etmek. Yusuf kendini tanıtır (İstanbul'dan gurbetçi, Sofeph CEO'su), oyuncuyu sıcak karşılar."),
         face_talk("teslim", "ayse", "Del Perro İskelesi'nde müşteri Ezgi Güler'e USB'yi teslim et",
                   "Ezgi iskelede bekliyor, kibar ve biraz meraklı. USB'yi alır, Sofeph'e teşekkür eder, Yusuf'a selam söyler."),
         face_talk("donus", "yusuf", "Maze Bank Plaza'ya dön ve Yusuf'a haber ver",
                   "Teslimat tamam. Yusuf rahatlar, ödemeyi yapar, oyuncunun numarasını rehbere kaydeder.")],
        unlocks_contacts=["yusuf"])

mission("sofeph_ofis_malzemesi", "Sofeph: Ofis Sarf Malzemesi", 2, 900, "yusuf", {"yusuf": NPCS["yusuf"]},
        [phone_talk("kabul", "yusuf", "Yusuf Cansever'i ara (rehber) ya da plazada konuş",
                    "Ofisin kâğıt, toner ve kablo sarf malzemesi La Mesa'daki depoda hazır. Yusuf'un işi başından aşkın; oyuncudan "
                    "depodaki kamyoneti alıp malzemeleri Maze Bank Plaza'ya getirmesini ister. Araca zarar vermemesini tembihler."),
         {"id": "kamyonet", "type": "vehicle", "vehicle": "bison", "pos": [880.0, -1600.0, 30.0], "heading": 90.0,
          "objective": "La Mesa'daki depoya git ve kamyonete bin"},
         {"id": "tasima", "type": "drive_to", "pos": [-53.0, -790.0, 44.0], "radius": 18.0,
          "objective": "Malzemeleri kamyonetle Maze Bank Plaza'ya taşı (aracı sağlam getir)"},
         face_talk("teslim", "yusuf", "Yusuf Cansever'e malzemeleri teslim et",
                   "Malzemeler geldi. Yusuf kontrol eder, ödemeyi yapar ve teşekkür eder.")],
        requires=["sofeph_ilk_teslimat"])

burak_pc_stage = {
    "id": "kurulum", "type": "install", "npc": "burak", "pos": LB_PC, "radius": 2.0, "prompt": "Bilgisayari kullan",
    "objective": "Üst kattaki Jay Norris'in masasında Burak'ın bilgisayarına Facade Office'i kur",
    "guide": "Önce Burak'tan ürün anahtarını iste (E ile konuş), sonra bilgisayarın başında E'ye bas. Özel kurulumu seç, D: sürücüsünü kullan.",
    "help": "Özel kurulumu seç. Yazı, tablo ve sunum bileşenleri gerekli; posta, araç çubuğu ve bulut eklentisi gereksiz. C: diskinde yer yok, D: diskini seç. Ürün anahtarını Burak'tan iste.",
    "installer": {
        "title": "Facade Office Kurulumu",
        "components": [{"id": "yazi", "label": "Facade Yazı"}, {"id": "tablo", "label": "Facade Tablo"},
                      {"id": "sunum", "label": "Facade Sunum"}, {"id": "posta", "label": "Facade Posta"},
                      {"id": "toolbar", "label": "Arama Araç Çubuğu"}, {"id": "bulut", "label": "Bulut Eşitleme"}],
        "required": ["yazi", "tablo", "sunum"], "avoid": ["posta", "toolbar", "bulut"], "key": "FCD-7K2M-9XQ4"}}
mission("sofeph_burak_destek", "Sofeph: Burak'a Teknik Destek", 4, 1200, "yusuf",
        {"yusuf": NPCS["yusuf"], "burak": person("burak", LB_BURAK, 330.0, exact_z=True)},
        [{**phone_talk("yoklama", "yusuf", "Yusuf Cansever'i ara (telefon rehberi) veya plazada konuş",
                       "Yusuf'un çevre mühendisi tanıdığı Burak Gül (Ekomvet) bilgisayarına ofis programı kuramıyor; Yusuf gidemiyor ve "
                       "oyuncudan teknik destek vermesini istiyor. Göreve geçmeden önce 1-2 kısa IT sorusu sorar (örn. özel kurulum ile hızlı "
                       "kurulum farkı, lisans anahtarı). Oyuncu bilmese de 'öğrenirsin' diyerek geçiştirir, işi yine de verir ve 'sorun olursa "
                       "beni ara' der."),
          "auto_advance": False, "item_options": ["knows", "partial", "clueless"],
          "advance_when": "Yoklama yapıldıktan ve oyuncu işi kabul ettikten sonra. item: bilgili 'knows', yarım 'partial', hiç 'clueless'."},
         {**face_talk("burak_gorus", "burak", "Burak Gül'ün ofisine git: Rockford Hills'teki Lifeinvader, üst kat",
                      "Burak oyuncuyu karşılar: Facade Office'i kuramıyor. İhtiyacı: yazı, tablo ve sunum programları; yan programlar gereksiz. "
                      "Ürün anahtarını oyuncu isterse söyler."),
          "guide": "Lifeinvader'a (Rockford Hills) gir: kapının önünde E ile içeri geç, üst kata çık. Burak bilgisayarın yanında bekliyor."},
         burak_pc_stage,
         {**face_talk("teyit", "burak", "Burak'a kurulumun bittiğini söyle",
                      "Kurulum bitti. Burak programı denedi, teşekkür eder; 'install_mistakes' > 0 ise hafifçe espriyle fark eder. İleride "
                      "saha ölçümleri için yardım isteyeceğini söyler. Telefon numarası VERMEZ (rehbere zaten eklendi)."),
          "guide": "Burak'a git ve kurulumun bittiğini söyle (E ile konuş)."},
         {**phone_talk("rapor", "yusuf", "Yusuf Cansever'i ara ve işin bittiğini bildir",
                       "Oyuncu Burak'ın kurulumunu tamamladı. Yusuf teşekkür eder. install_mistakes > 0 ise 'ilk seferde olmaz, öğrenirsin' der; "
                       "it_level 'clueless' idiyse 'bak, öğrendin işte' der."),
          "guide": "Telefonu aç (O), rehberden Yusuf Cansever'i ara ve işin bittiğini söyle."}],
        requires=["sofeph_ofis_malzemesi"], unlocks_contacts=["burak"],
        secret={"npc": "burak", "label": "Facade Office ürün anahtarı", "value": "FCD-7K2M-9XQ4", "from_stage": 1, "to_stage": 3})

mission("sofeph_sunucu_odasi", "Sofeph: Sunucu Odası", 9, 2500, "yusuf", {"yusuf": NPCS["yusuf"]},
        [face_talk("itiraf", "yusuf", "Yusuf Cansever'le Maze Bank Plaza'da konuş",
                   "Yusuf gergin: binada elektrik/ısı sorunları var. İtiraf eder: plazanın otopark katının altında kiraladığı gizli bir "
                   "sunucu odasında yapay zekâ modelini eğitiyor; şirket fonu bitmek üzere olduğu için gizli tutmuş. Oyuncu Burak'ın (Ekomvet) "
                   "anormal ısı/elektrik okumasından bahsederse 'demek o ölçüm sizdendi' diye şaşırır. Sessiz kalmasını ve yedek soğutma "
                   "ünitesini La Mesa'daki depodan getirmesini ister."),
         {"id": "unite", "type": "vehicle", "vehicle": "bison", "pos": [880.0, -1600.0, 30.0], "heading": 90.0,
          "objective": "La Mesa deposundan yedek soğutma ünitesini taşıyacak kamyonete bin"},
         {"id": "tasi", "type": "drive_to", "pos": [-50.0, -800.0, 29.0], "radius": 15.0,
          "objective": "Soğutma ünitesini plaza otoparkına götür"},
         {"id": "panel", "type": "interact", "pos": [-50.0, -800.0, 29.0], "radius": 6.0, "prompt": "Klima panelini bagla",
          "objective": "Araçtan in ve klima panelini yedek üniteye bağla"},
         face_talk("kapanis", "yusuf", "Yusuf'a panelin durumunu bildir",
                   "Soğutma düzeldi. Yusuf rahatlar, ödemeyi yapar; sır konusunda anlaşırlar. Daha büyük işler için oyuncuya güvenir.")],
        requires=["sofeph_burak_destek"])

# --- Burak ---
mission("burak_sensorler", "Ekomvet: Hava Sensörleri", 7, 1500, "burak",
        {"burak": person("burak", LB_BURAK, 330.0, exact_z=True)},
        [phone_talk("teklif", "burak", "Burak Gül'ü ara (telefon rehberi)",
                    "Ekomvet şehirde hava kalitesi izleyecek; 3 küçük sensörü Legion Square, Del Perro İskelesi ve havalimanı girişine koymak "
                    "gerekiyor. Saha aracı Lifeinvader'ın önünde. Burak sensör noktasında E'ye basılacağını söyler.")]
        + [VAN] + field_leg(1, [195.0, -934.0, 30.7], "Legion Square") + field_leg(2, [-1850.0, -1230.0, 13.0], "Del Perro İskelesi")
        + field_leg(3, [-1030.0, -2730.0, 13.8], "Havalimanı girişi")
        + [phone_talk("rapor", "burak", "Burak Gül'ü ara ve sensörlerin kurulduğunu söyle",
                      "Sensörler çalışıyor. Burak verilere bakar, Maze Bank Plaza civarında anormal ısı ve elektrik okuması görür; ne olduğunu "
                      "bilmiyor, Yusuf'la konuşmasını önerir. Ayrıca arkadaşı Osman Akbulut'u (Kompetan File CEO'su, muhasebe ve girişim "
                      "uzmanı) tavsiye eder.")],
        requires=["sofeph_burak_destek"], unlocks_contacts=["osman"])

mission("burak_sizinti", "Ekomvet: Sızıntı", 10, 1800, "burak", {"burak": person("burak", LB_BURAK, 330.0, exact_z=True)},
        [phone_talk("teklif", "burak", "Burak Gül'ü ara",
                    "Sensörler limanda ve Davis'te kirletici izleri gösteriyor. Burak numune istiyor: saha aracıyla iki noktada numune alınacak.")]
        + [VAN] + field_leg(1, [1000.0, -2900.0, 6.0], "Liman") + field_leg(2, [100.0, -1900.0, 21.0], "Davis")
        + [phone_talk("rapor", "burak", "Burak Gül'ü ara ve numune sonuçlarını sor",
                      "Sonuçlar kirleticinin tek bir kaynaktan geldiğini gösteriyor; kaynağı henüz bilmiyor, araştırmaya devam edecek.")],
        requires=["burak_sensorler"])

# --- Osman ---
mission("osman_tanisma", "Kompetan File: Muhasebe Dosyaları", 11, 1000, "osman", {"osman": NPCS["osman"]},
        [phone_talk("brif", "osman", "Osman Akbulut'u ara (telefon rehberi)",
                    "Burak seni Osman'a tavsiye etmiş. Osman Kompetan File'ın CEO'su; üç aylık muhasebe dosyalarını ofisinden alıp "
                    "mali müşavire yetiştirecek güvenilir birini arıyor. Kendini tanıtır, girişim ve muhasebeden, müzikten bahseder."),
         visit("ofis", [-1447.0, -537.0, 34.0], "Dosyalari al", "Kompetan File ofisine git ve dosyaları al", 6.0)],
        requires=["burak_sensorler"])

mission("osman_studyo", "Osman: Stüdyo Ekipmanı", 12, 1300, "osman", {"osman": NPCS["osman"]},
        [phone_talk("brif", "osman", "Osman Akbulut'u ara",
                    "Osman'ın müzik stüdyosu için sipariş ettiği ekipman Vinewood'daki depoda bekliyor; aracı alıp ofisine getirmeni ister. "
                    "Müzik zevkinden söz eder.")]
        + drive("tasi", "speedo", [350.0, -1000.0, 29.0], [-1447.0, -537.0, 34.0], "Vinewood'daki depoya git ve araca bin",
                "Ekipmanı Osman'ın ofisine götür"),
        requires=["osman_tanisma"])

mission("osman_sule_tanisma", "Osman: Akşam Yemeği", 13, 800, "osman",
        {"osman": NPCS["osman"], "sule": person("sule", [-1195.0, -1130.0, 7.0], 90.0)},
        [phone_talk("davet", "osman", "Osman Akbulut'u ara",
                    "Osman seni sevgilisi Şule'yle tanıştırmak istiyor: Vespucci'de akşam yemeği. Şule avukat, ileride işine yarayabilir."),
         face_talk("sule", "sule", "Vespucci'deki restoranda Şule'yle tanış",
                   "Şule seni Osman'dan duymuş, sıcak karşılar. Avukat olduğunu söyler; başın belaya girerse yardım edebileceğini ima eder.")],
        requires=["osman_studyo"], unlocks_contacts=["sule"])

# --- Şule ---
mission("sule_hukuk", "Şule: Vekalet Belgesi", 14, 600, "sule", {"sule": NPCS["sule"]},
        [phone_talk("brif", "sule", "Şule Tatlılıoğlu'nu ara",
                    "Şule vekalet belgesini adliyeye yetiştirmeni istiyor. Göreve başlarken söyler: bir kez tutuklanırsan ücretsiz avukatlık "
                    "yapıp seni serbest bıraktıracak (bir kerelik hak)."),
         visit("adliye", [233.0, -410.0, 48.0], "Belgeyi teslim et", "Belgeyi adliyeye teslim et", 8.0)],
        requires=["osman_sule_tanisma"], on_start_flags={"sule_ucretsiz_hak": "1"})

mission("sule_durusma", "Şule: Duruşma Hazırlığı", 15, 1200, "sule", {"sule": NPCS["sule"]},
        [phone_talk("brif", "sule", "Şule Tatlılıoğlu'nu ara",
                    "Şule'nin duruşması yarın; noterden bir belge alıp adliyeye teslim etmen gerekiyor."),
         visit("noter", [300.0, -600.0, 43.0], "Belgeyi al", "Pillbox Hill'deki noterden belgeyi al", 8.0),
         visit("adliye", [233.0, -410.0, 48.0], "Belgeyi teslim et", "Belgeyi adliyedeki Şule'ye teslim et", 8.0)],
        requires=["sule_hukuk"])

# --- Ezgi (ilişki zinciri: tanışma -> arkadaş -> yakın arkadaş -> sevgili). Romantik içerik ima edilir, ayrıntı verilmez. ---
mission("ayse_tanisma", "Ezgi: Tanışma", 3, 0, "ayse", {"ayse": ayse(PIER)},
        [phone_talk("arama", "ayse", "Ezgi Güler'in aramasını bekle (telefonun çalacak)",
                    "Ezgi arıyor: Yusuf numaranı vermiş. Teslim ettiğin USB'deki prototip için teşekkür ediyor ve Del Perro İskelesi'ndeki "
                    "kafede bir kahve ısmarlamak istiyor."),
         face_talk("kahve", "ayse", "Del Perro İskelesi'nde Ezgi'yle kahve iç",
                   "Ezgi iskelede, rahat ve samimi. Hobilerinden, memleketinden (Ankara) bahseder; oyuncuyla tanışmaktan memnun.")],
        requires=["sofeph_ofis_malzemesi"], start_by="incoming_call", call_delay_s=90,
        sets_relationship={"npc": "ayse", "status": "acquaintance"})

mission("ayse_sahil", "Ezgi: Sahil Yürüyüşü", 5, 0, "ayse", {"ayse": ayse([-1330.0, -1500.0, 4.5])},
        [phone_talk("davet", "ayse", "Ezgi Güler'i ara (rehber)", "Ezgi sahilde yürüyüşe çıkmak istiyor; Vespucci Plajı'nda buluşmayı teklif eder."),
         face_talk("karsilas", "ayse", "Vespucci Plajı'nda Ezgi'yle buluş",
                   "Ezgi plajda bekliyor, hava güzel. Yürüyüşe çıkmayı önerir ve yanında yürüyeceğini söyler."),
         {"id": "yuruyus", "type": "walk", "npc": "ayse", "distance": 180.0,
          "objective": "Vespucci Plajı'nda Ezgi'yle yan yana yürü (sohbet edebilirsin)",
          "guide": "Ezgi'nin yanına git ve yürümeye başla; o sana eşlik eder. Sohbet için E ile konuş."}],
        requires=["ayse_tanisma"], sets_relationship={"npc": "ayse", "status": "friend"})

mission("ayse_aksam_yemegi", "Ezgi: Akşam Yemeği", 6, 0, "ayse", {"ayse": ayse([1060.0, -690.0, 57.0])},
        [phone_talk("davet", "ayse", "Ezgi Güler'i ara", "Ezgi akşam yemeğine davet ediyor; Mirror Park'taki restoranda buluşacaklar."),
         face_talk("yemek", "ayse", "Mirror Park'ta Ezgi'yle akşam yemeği ye",
                   "Samimi bir akşam yemeği; Ezgi artık oyuncuya yakın arkadaş gözüyle bakıyor, hafif flört eder (sadece iltifat).")],
        requires=["ayse_sahil"], sets_relationship={"npc": "ayse", "status": "close_friend"})

mission("ayse_gun_batimi", "Ezgi: Gün Batımı", 8, 0, "ayse", {"ayse": ayse([-413.0, 1173.0, 325.0])},
        [phone_talk("davet", "ayse", "Ezgi Güler'i ara", "Ezgi gün batımını gözlemevinde izlemeyi öneriyor."),
         face_talk("gunbatimi", "ayse", "Gözlemevi'nde Ezgi'yle gün batımını izle",
                   "Romantik ama ölçülü bir akşam: Ezgi duygularını açar, oyuncu kabul ederse sevgili olurlar. Cinsel/erotik ayrıntı YOK; "
                   "sadece sıcak, sevecen cümleler.")],
        requires=["ayse_aksam_yemegi"], sets_relationship={"npc": "ayse", "status": "partner"})
