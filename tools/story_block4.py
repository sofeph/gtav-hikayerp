# ======================================================================
# BÖLÜM 2: "Geride Kalanlar"  (oynanan karakter: kadın, Ezgi'nin KIZ KARDEŞİ; oluşturucuyla yaratılır)
# Bölüm 1'in sonunda erkek karakter Vice City'ye kaçtı. Bölüm 2'de Ezgi ölür; kardeşi Ezgi'nin son uyarısını takip eder, kendisi de
# suçlanır (hukuki sorun). Şule hem onu hem Vice City'deki erkek karakteri savunur. İki hukuki sorun bitince iki karakter buluşur ve
# serbest dolaşımda (F10) iki oluşturulmuş karakter arasında geçiş yapılabilir.
# Tüm aşamalar KOD kararlıdır (AI görevi bitirmez).
# ======================================================================
NPCS["komiser"] = {
    "name": "Selim Aydın", "gender": "male", "model": "s_m_y_cop_01",
    "fallback_models": ["s_m_m_ciasec_01", "s_m_y_sheriff_01"], "pos": [441.0, -982.0, 30.7], "heading": 90.0,
    "voice": {"speaker": "Ferran Simen"},
    "personality": "Los Santos polisinde komiser. Yorgun, ağzı sıkı ve temkinli; baskı altında olduğunu belli etmemeye çalışır. İşini ciddiye alır "
                   "ama üstlerinden gelen baskıdan rahatsız; sonunda doğruyu söyleme eğiliminde.",
    "profile": {"name": "Selim Aydın", "title": "Komiser", "gender": "male", "hometown": "Konya", "hometown_district": "Selçuklu",
                "military_unit": "Bolu", "past_jobs": ["polis"], "team": "Konyaspor"},
}
NPCS["alter"] = {
    "name": "Eş Karakter", "gender": "male", "model": "__alter__",
    "fallback_models": [], "pos": [-1026.0, -2728.0, 13.8], "heading": 90.0,
    "voice": {"speaker": "Damien Black"},
    "personality": "Bölüm 1'in baş karakteri; Ezgi'nin sevgilisiydi. Vice City'den dönüyor: yorgun, kederli ama kararlı. Kadın karakterle "
                   "(Ezgi'nin kız kardeşi) ilk kez yüz yüze görüşüyor; ikisi de aynı kaybı yaşadı. Duygularını az sözle ama içtenlikle anlatır.",
    "profile": {"name": "", "title": "", "gender": "male", "hometown": "", "past_jobs": [], "team": ""},
}
STATION = [441.0, -982.0, 30.7]
FLAT = [-1195.0, -1130.0, 7.0]

mission("ayse_son_arama", "Bölüm 2: Son Arama", 50, 500, "ayse", {"ayse": ayse(PIER)},
        [phone_talk("arama", "ayse", "Ezgi'nin aramasını bekle (telefonun çalacak)",
                    "OYUNCU AYŞE'NİN KIZ KARDEŞİDİR. Ezgi telaşlı ve korkmuş arıyor: Halcyon'un kayıtlarında kardeşinin adına imzalanmış sahte belgeler "
                    "bulmuş; bu yüzden birileri onu izliyor. Telefonda anlatamaz, Del Perro İskelesi'nde hemen buluşmak ister. 'Acele et' der."),
         {"id": "olum", "type": "scene", "npc": "ayse", "kill_npc": "ayse", "pos": PIER, "radius": 14.0,
          "objective": "Hızla geçen bir araç iskelede Ezgi'ye çarptı. Ezgi öldü.",
          "guide": "Del Perro İskelesi'ne git. Ezgi seni orada bekliyor."},
         visit("canta", PIER, "Cantayi al", "Ezgi'nin yere düşen çantasını al", 8.0)],
        requires=["sule_kacis"], start_by="incoming_call", call_delay_s=60,
        on_complete_flags={"ayse_oldu": "1"}, removes_contacts=["ayse"], unlocks_contacts=["osman"])

mission("sule_supheli", "Şule: Şüpheli", 51, 800, "sule", {"sule": NPCS["sule"], "komiser": NPCS["komiser"]},
        [phone_talk("brif", "sule", "Şule Tatlılıoğlu'nu ara",
                    "OYUNCU AYŞE'NİN KIZ KARDEŞİDİR. Şule yas tutan oyuncuya başsağlığı diler. Polis, Ezgi'yle son konuşan kişi olduğu için oyuncuyu "
                    "şüpheli sayıyor: bu bir hukuki sorun. Şule, Mission Row karakoluna gidip ifade vermesini, ama avukatsız bir şey imzalamamasını söyler."),
         visit("ifade", STATION, "Ifade ver", "Mission Row Karakolu'na git ve ifade ver", 8.0),
         face_talk("komiser", "komiser", "Komiser Selim Aydın'la konuş",
                   "Komiser soğuk ve temkinli: kaza raporunda 'kayıp araç' yazıyor ama araç bulunamamış. Oyuncuyu sorgular, ima eder. Üstlerinden "
                   "gelen baskıdan rahatsız olduğu hissedilir; kesin bir şey söylemez.")],
        requires=["ayse_son_arama"])

mission("osman_usb", "Osman: Ezgi'nin Son Kaydı", 52, 1500, "osman", {"osman": person("osman", OFFICE, 120.0)},
        [phone_talk("brif", "osman", "Osman Akbulut'u ara",
                    "OYUNCU AYŞE'NİN KIZ KARDEŞİDİR. Osman Ezgi'yi eski bir muhasebe arkadaşı olarak tanıyor, üzgün. Ezgi'nin çantasındaki USB'yi "
                    "incelemek istiyor ama önce Ezgi'nin evinden bir şifre kâğıdı almak gerek (Vespucci)."),
         visit("ev", FLAT, "Sifre kagidini al", "Ezgi'nin Vespucci'deki evinden şifre kâğıdını al", 8.0),
         {"id": "arac", "type": "vehicle", "vehicle": "speedo", "pos": [-1190.0, -1120.0, 7.0], "heading": 90.0,
          "objective": "Ezgi'nin evinin önündeki araca bin"},
         {"id": "tasi", "type": "drive_to", "pos": OFFICE, "radius": 20.0, "objective": "Osman'ın ofisine (Del Perro) git"},
         face_talk("osman", "osman", "Osman Akbulut'la USB'yi incele",
                   "Osman USB'yi açar: Halcyon, Ezgi'nin kız kardeşinin (oyuncunun) adına sahte belgeler imzalatmış; amaç onu günah keçisi yapmak. "
                   "Murat Kaya'nın hâlâ Halcyon için çalıştığını söyler; Murat'ı izlemek gerek.")],
        requires=["sule_supheli"])

mission("osman_takip", "Osman: Takip", 53, 2000, "osman", {"osman": person("osman", OFFICE, 120.0)},
        [phone_talk("brif", "osman", "Osman Akbulut'u ara",
                    "OYUNCU AYŞE'NİN KIZ KARDEŞİDİR. Osman Murat'ın aracını takip edecek bir plan kurmuş: Vinewood'daki ofisinin önünde araca bir takip "
                    "cihazı koy, sonra Murat'ın gittiği limana git ve kutuyu fotoğrafla. Dikkatli ol, kimseyle konuşma."),
         {"id": "arac", "type": "vehicle", "vehicle": "speedo", "pos": [-1380.0, -470.0, 33.0], "heading": 90.0,
          "objective": "Osman'ın ofisinin önündeki araca bin"},
         {"id": "git1", "type": "drive_to", "pos": VINEWOOD, "radius": 30.0, "objective": "Vinewood'daki Halcyon ofisinin önüne git"},
         {"id": "cihaz", "type": "interact", "pos": VINEWOOD, "radius": 40.0, "prompt": "Takip cihazini yerlestir",
          "objective": "Araçtan in ve Murat'ın aracına takip cihazı yerleştir"},
         {"id": "git2", "type": "drive_to", "pos": [1000.0, -2900.0, 6.0], "radius": 30.0, "objective": "Cihazın gösterdiği limana git"},
         {"id": "kutu", "type": "interact", "pos": [1000.0, -2900.0, 6.0], "radius": 40.0, "prompt": "Kutuyu fotografla",
          "objective": "Araçtan in ve limandaki kutuyu fotoğrafla"},
         phone_talk("rapor", "osman", "Osman Akbulut'u ara ve fotoğrafları anlat",
                    "Fotoğraflarda Karbonix'in atık kutuları ve Halcyon'un damgası görünüyor: ikisi aynı çatı altında. Osman delilleri Şule'ye iletecek.")],
        requires=["osman_usb"])

mission("komiser_not", "Şule: Komiserin Notu", 54, 1500, "sule", {"sule": NPCS["sule"], "komiser": NPCS["komiser"]},
        [phone_talk("brif", "sule", "Şule Tatlılıoğlu'nu ara",
                    "OYUNCU AYŞE'NİN KIZ KARDEŞİDİR. Komiser Selim Aydın kendi başına Şule'yi aramış: kaza dosyasının üzerine 'yukarıdan' baskı yapıldığını "
                    "ve kayıp aracın Halcyon'a ait olduğunu söyleyeceğini ima etmiş. Şule oyuncunun komiserle yüz yüze görüşmesini ister."),
         face_talk("komiser", "komiser", "Komiser Selim Aydın'la karakolda konuş",
                   "Komiser nihayet açılır: araç Halcyon'un filosunda; üstleri dosyayı kapatmasını istemiş. Tanıklık etmeye razı, ama koruma istiyor. "
                   "Oyuncuya zarfta aracın plakasını verir."),
         visit("adliye", COURT, "Zarfi teslim et", "Plaka zarfını adliyedeki Şule'ye teslim et", 8.0)],
        requires=["sule_supheli"])

mission("sule_durusma2", "Şule: İkinci Duruşma", 55, 5000, "sule", {"sule": person("sule", COURT, 90.0)},
        [phone_talk("brif", "sule", "Şule Tatlılıoğlu'nu ara",
                    "OYUNCU AYŞE'NİN KIZ KARDEŞİDİR. Duruşma günü: Osman'ın fotoğrafları, komiserin plaka bilgisi ve Ezgi'nin USB'si birleşti. Şule delil "
                    "kolisini adliyeye yetiştirmeni ister."),
         {"id": "arac", "type": "vehicle", "vehicle": "bison", "pos": [-1380.0, -470.0, 33.0], "heading": 90.0,
          "objective": "Osman'ın ofisinin önündeki delil aracına bin"},
         {"id": "tasi", "type": "drive_to", "pos": COURT, "radius": 20.0, "objective": "Delil kolisini adliyeye yetiştir"},
         face_talk("sule", "sule", "Adliyede Şule'ye delilleri teslim et",
                   "Şule hâkimin karşısına çıkar; kısa bir bekleyişten sonra gelir: oyuncunun üzerindeki suçlama düşmüş, Halcyon'un sahtecilik ve "
                   "cinayete azmettirme dosyası açılmış; Vice City'deki ilk karakterin yakalama kararı da kaldırılmış. Her iki hukuki sorun bitti. "
                   "Şule ilk karakterin havalimanına döndüğünü söyler.")],
        requires=["osman_takip", "komiser_not"],
        on_complete_flags={"kadin_temiz": "1", "erkek_temiz": "1"})

mission("serbest_dolasim", "Buluşma: İki Hayat", 56, 8000, "sule", {"sule": NPCS["sule"], "alter": NPCS["alter"]},
        [phone_talk("brif", "sule", "Şule Tatlılıoğlu'nu ara",
                    "OYUNCU AYŞE'NİN KIZ KARDEŞİDİR. Şule, Vice City'den dönen ilk karakterin havalimanında beklediğini söyler. İkisi de aynı kaybı "
                    "yaşadı; buluşmaları önemli."),
         {"id": "arac", "type": "vehicle", "vehicle": "speedo", "pos": [-60.0, -785.0, 43.0], "heading": 90.0,
          "objective": "Maze Bank Plaza'nın önündeki araca bin"},
         {"id": "havaalani", "type": "drive_to", "pos": AIRPORT, "radius": 40.0, "objective": "Los Santos Havalimanı'na git"},
         face_talk("alter", "alter", "Havalimanında Vice City'den dönen karakterle buluş",
                   "Karakter yorgun ama rahatlamış; Ezgi'nin ölümünü duymuş, kederli. Kız kardeşle ilk kez konuşuyor: ona başsağlığı diler, "
                   "Ezgi'yi ne kadar sevdiğini söyler, birlikte iskeleye gitmeyi önerir."),
         {"id": "iskele", "type": "interact", "pos": PIER, "radius": 40.0, "prompt": "Ayse icin cicek birak",
          "objective": "Araçtan in ve Del Perro İskelesi'nde Ezgi'ye çiçek bırak",
          "guide": "Araçtan in, sarı işaretli noktada E'ye bas.",
          "finale": {"seconds": 24, "lines": [
              "Ezgi'nin hesabı soruldu. İki ayrı hayat, tek bir hikâye oldu.",
              "Hukuki sorunlar bitti: biri Los Santos'ta, biri Vice City'de aklandı.",
              "Artık ikiniz de özgürsünüz.",
              "Serbest dolaşım açıldı.",
              "F10: karakter değiştir."]}}],
        requires=["sule_durusma2"], on_complete_flags={"serbest_dolasim": "1"})
