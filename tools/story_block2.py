# ======================================================================
# SENARYO: "Sofeph Sırrı"  (Perde 2-4; Perde 1 story_block.py'de)
# Örgü: Burak'ın bulduğu kirlilik (Karbonix Enerji) -> Osman'ın muhasebede bulduğu para izi (Halcyon Capital hem Sofeph'in
# hem Karbonix'in finansörü) -> Yusuf'un gizli sunucusundaki arka kapı kaydı -> Şule'nin dava dosyası -> duruşma günü -> finaller.
# ======================================================================
NPCS["murat"] = {
    "name": "Murat Kaya", "gender": "male", "model": "a_m_m_business_01",
    "fallback_models": ["a_m_y_business_03", "a_m_y_business_02"], "pos": [300.0, 200.0, 104.0], "heading": 160.0,
    "voice": {"speaker": "Dionisio Schuyler"},
    "personality": "Halcyon Capital'in mali işler müdürü. Pürüzsüz, kibar ama kaçamak konuşan bir kurumsal tip; sorulardan ustaca sıyrılır, "
                   "gerekirse rüşvet ima eder ama asla açık söylemez. Kendi çıkarını korur.",
    "profile": {"name": "Murat Kaya", "title": "Halcyon Capital mali işler müdürü", "company": "Halcyon Capital", "gender": "male",
                "hometown": "İzmir", "hometown_district": "Alsancak", "past_jobs": ["bankacı"], "team": "Göztepe"},
}
PLAZA = [-53.0, -790.0, 44.0]
OFFICE = [-1447.0, -537.0, 34.0]
GARAGE = [-50.0, -800.0, 29.0]
COURT = [233.0, -410.0, 48.0]
VINEWOOD = [300.0, 200.0, 104.0]
KARBONIX = [900.0, -2150.0, 30.0]
BOWL = [680.0, 570.0, 130.0]
CAR_PLAZA = {"id": "arac", "type": "vehicle", "vehicle": "speedo", "pos": [-60.0, -785.0, 43.0], "heading": 90.0,
             "objective": "Maze Bank Plaza'nın önündeki araca bin"}

# --- Perde 2: izler ---
mission("sofeph_fon", "Sofeph: Fon Sorunu", 20, 1500, "yusuf",
        {"yusuf": NPCS["yusuf"], "osman": person("osman", OFFICE, 120.0)},
        [phone_talk("brif", "yusuf", "Yusuf Cansever'i ara",
                    "Sunucu odası işi bitti ama Sofeph'in parası hâlâ sıkışık; fonun nereye aktığını anlamak için bir muhasebeciye ihtiyacı var. "
                    "Burak'ın arkadaşı Osman'a güveniyor. Üç aylık defterleri plazadan alıp Osman'ın ofisine götürmeni ister."),
         visit("defter", PLAZA, "Defterleri al", "Maze Bank Plaza'da Yusuf'un defterlerini al", 8.0),
         CAR_PLAZA,
         {"id": "tasi", "type": "drive_to", "pos": OFFICE, "radius": 20.0, "objective": "Defterleri Osman'ın ofisine (Del Perro) götür"},
         face_talk("osman", "osman", "Osman Akbulut'a defterleri teslim et",
                   "Osman defterlere göz gezdirir, bir şeylerin tutmadığını söyler: Sofeph'e yatırım yapan Halcyon Capital'in ödemelerinde tuhaf "
                   "kalemler var. Daha fazla belge gerektiğini, Halcyon'un Vinewood'daki ofisinden evrak alınması gerektiğini söyler.")],
        requires=["sofeph_sunucu_odasi", "osman_sule_tanisma"])

mission("osman_defter", "Osman: Rakamlar Yalan Söylemez", 21, 1400, "osman", {"osman": person("osman", OFFICE, 120.0)},
        [phone_talk("brif", "osman", "Osman Akbulut'u ara",
                    "Osman, Halcyon Capital'in hesap hareketlerini karşılaştırmak için resmî evrak kopyalarına ihtiyaç duyuyor. Vinewood'daki "
                    "Halcyon ofisinin resepsiyonundan müşteri evraklarının kopyasını alacaksın (gizlice değil; Sofeph'in yatırımcısı olarak "
                    "talep edeceksin)."),
         visit("evrak", VINEWOOD, "Evraklari iste", "Vinewood'daki Halcyon Capital ofisinden evrak kopyalarını al", 8.0),
         phone_talk("rapor", "osman", "Osman Akbulut'u ara ve evrakları aldığını söyle",
                    "Osman evraklara bakar: Halcyon, Karbonix Enerji adlı şirketin de ana kredi vereni; para Sofeph'ten Karbonix'e dolaylı akıyor. "
                    "Murat Kaya adlı bir mali işler müdürüyle yüz yüze konuşmak gerek.")],
        requires=["sofeph_fon"])

mission("burak_kaynak", "Ekomvet: Kaynağı Bul", 22, 1700, "burak", {"burak": person("burak", LB_BURAK, 330.0, exact_z=True)},
        [phone_talk("teklif", "burak", "Burak Gül'ü ara",
                    "Numune sonuçları kirleticinin soğutma sıvısı katkısı olduğunu gösteriyor, ama kaynağı bilinmiyor. Nehir boyunca iki noktadan "
                    "daha numune alıp kaynağı daraltacaksın.")]
        + [VAN] + field_leg(1, [700.0, -1100.0, 22.0], "Los Santos Nehri") + field_leg(2, [800.0, -2800.0, 5.0], "Terminal")
        + [phone_talk("rapor", "burak", "Burak Gül'ü ara ve sonuçları sor",
                      "Katkı maddesinin izi tek bir adrese çıkıyor: Cypress Flats'teki Karbonix Enerji tesisi. Burak oraya resmî numune almak "
                      "istiyor ve bir kanıt zinciri kurulması gerektiğini söylüyor.")],
        requires=["burak_sizinti"])

mission("osman_toplanti", "Osman: Halcyon'la Yüzleşme", 23, 1800, "osman",
        {"osman": person("osman", OFFICE, 120.0), "murat": NPCS["murat"]},
        [phone_talk("brif", "osman", "Osman Akbulut'u ara",
                    "Osman senin Halcyon'un mali işler müdürü Murat Kaya ile görüşmeni istiyor: Sofeph'in yatırımcısı olarak 'kredi şartlarını' "
                    "soracaksın. Osman ne soracağını anlatır; sertleşmeden, ama net olmanı ister."),
         face_talk("murat", "murat", "Vinewood'da Murat Kaya ile görüş",
                   "Murat kibar ve kaçamak: Karbonix'i tanıdığını inkâr etmez ama 'bağımsız bir müşteri' der; baskı görünce ima yollu bir 'anlaşma' "
                   "teklif eder. Rüşveti açıkça söylemez. Oyuncu net sorarsa lafı değiştirir."),
         phone_talk("rapor", "osman", "Osman Akbulut'u ara ve görüşmeyi anlat",
                    "Osman, Murat'ın tutumundan şüphesini doğrulanmış sayar: bu bir hukuk meselesi. Şule'ye iletmek gerek.")],
        requires=["osman_defter"])

mission("burak_kanit", "Ekomvet: Kanıt Numunesi", 24, 1600, "burak", {"burak": person("burak", LB_BURAK, 330.0, exact_z=True)},
        [phone_talk("teklif", "burak", "Burak Gül'ü ara",
                    "Resmî kanıt için Karbonix tesisinin dış çitinde numune alınacak. Burak: 'Çitten içeri girme, hukuken kanıt sayılmaz; "
                    "yalnızca halka açık noktadan al.'"),
         VAN,
         {"id": "git", "type": "drive_to", "pos": KARBONIX, "radius": 30.0, "objective": "Saha aracıyla Karbonix Enerji tesisine git"},
         {"id": "numune", "type": "interact", "pos": KARBONIX, "radius": 25.0, "prompt": "Numune al",
          "objective": "Araçtan in ve tesisin halka açık çiti yanından numune al",
          "guide": "Araçtan in, sarı noktada E'ye bas. Çitten içeri girmene gerek yok."},
         phone_talk("rapor", "burak", "Burak Gül'ü ara",
                    "Numune laboratuvarda doğrulandı: katkı maddesi Karbonix'in. Burak sonuçları Şule'ye iletecek.")],
        requires=["burak_kaynak"])

mission("yusuf_kayit", "Yusuf: Gizli Dinleme", 25, 2000, "yusuf", {"yusuf": NPCS["yusuf"]},
        [face_talk("brif", "yusuf", "Yusuf Cansever'le plazada konuş",
                   "Yusuf, sunucu günlüklerinde dışarıdan düzenli bağlantılar fark etmiş: Halcyon Capital'in adresi. Yatırımcı sessizce izliyor. "
                   "Bunu belgelemek için otopark katındaki sunucu panelinden günlük dosyasını indirmeni ister."),
         visit("kayit", GARAGE, "Gunlugu indir", "Plaza otoparkındaki panelden sunucu günlüğünü indir", 6.0),
         face_talk("donus", "yusuf", "Günlüğü Yusuf'a ver",
                   "Yusuf günlüğe bakar, arka kapının izini bulur; öfkelenir ama sakin kalır. Hukuki yola gitmeye karar verir.")],
        requires=["osman_toplanti"])

mission("sule_ihbar", "Şule: Resmî İhbar", 26, 1500, "sule", {"sule": NPCS["sule"]},
        [phone_talk("brif", "sule", "Şule Tatlılıoğlu'nu ara",
                    "Şule dosyayı topluyor: Burak'ın numune raporu, Osman'ın muhasebe notları. Noterden onaylı suretleri alıp adliyeye teslim etmen "
                    "gerekiyor. Her belgenin sırasını ve kimden geldiğini titizlikle söyler."),
         visit("noter", [300.0, -600.0, 43.0], "Suretleri al", "Pillbox Hill'deki noterden onaylı suretleri al", 8.0),
         visit("adliye", COURT, "Dosyayi teslim et", "Dosyayı adliyeye teslim et", 8.0)],
        requires=["burak_kanit", "osman_toplanti"])

mission("sule_durusma_gunu", "Şule: Duruşma Günü", 27, 3000, "sule", {"sule": person("sule", COURT, 90.0)},
        [phone_talk("brif", "sule", "Şule Tatlılıoğlu'nu ara",
                    "Duruşma günü. Şule delil kolisini adliyeye yetiştirmeni ister; koli Osman'ın ofisinde. Geç kalmamak çok önemli."),
         {"id": "arac", "type": "vehicle", "vehicle": "bison", "pos": [-1380.0, -470.0, 33.0], "heading": 90.0,
          "objective": "Osman'ın ofisinin önündeki delil aracına bin"},
         {"id": "tasi", "type": "drive_to", "pos": COURT, "radius": 20.0, "objective": "Delil kolisini adliyeye yetiştir"},
         face_talk("sule", "sule", "Adliyede Şule'ye delilleri teslim et",
                   "Şule delilleri alır, hâkimin karşısına çıkar. Kısa bir bekleyişten sonra dışarı çıkar: dava lehlerine ilerliyor, Karbonix "
                   "savunmada zorlanıyor. Teşekkür eder.")],
        requires=["sule_ihbar", "yusuf_kayit"])

# --- Perde 4: finaller ---
mission("sofeph_yeni_baslangic", "Sofeph: Yeni Başlangıç", 30, 6000, "yusuf", {"yusuf": NPCS["yusuf"]},
        [face_talk("final", "yusuf", "Maze Bank Plaza'da Yusuf'la konuş",
                   "Halcyon geri çekilmiş, Karbonix'e ağır para cezası ve tesis kapatma kararı çıkmış. Sofeph'in arka kapı sorunu çözülmüş. "
                   "Yusuf rahatlamış; oyuncuyu ortak gibi görmeye başlamış ve büyük bir ödeme yapar. Gelecek planlarını anlatır.")],
        requires=["sule_durusma_gunu"])

mission("burak_temiz_hava", "Ekomvet: Temiz Hava", 31, 2500, "burak", {"burak": person("burak", LB_BURAK, 330.0, exact_z=True)},
        [phone_talk("teklif", "burak", "Burak Gül'ü ara",
                    "Karbonix kapanınca Burak bir kutlama olarak Mirror Park'a fidan dikmek istiyor. Fidanı kendisi bıraktı; sen dikeceksin."),
         visit("fidan", [1060.0, -690.0, 57.0], "Fidani dik", "Mirror Park'ta fidanı dik", 10.0),
         phone_talk("rapor", "burak", "Burak Gül'ü ara",
                    "Burak çok mutlu; 'artık bu şehirde nefes alınabilir' der ve oyuncuya teşekkür eder.")],
        requires=["sule_durusma_gunu"])

mission("osman_konser", "Osman: Kapanış Konseri", 32, 2000, "osman",
        {"osman": person("osman", BOWL, 180.0), "sule": person("sule", [684.0, 574.0, 130.0], 180.0)},
        [phone_talk("davet", "osman", "Osman Akbulut'u ara",
                    "Osman davayı kutlamak için Vinewood Bowl'da küçük bir konser düzenliyor; kendisi de sahne alacak (müzik hobisi). Seni ve Şule'yi davet eder."),
         face_talk("osman", "osman", "Vinewood Bowl'da Osman'la konuş",
                   "Osman sahne arkasında heyecanlı; müzikten ve girişimden bahseder, teşekkür eder."),
         face_talk("sule", "sule", "Şule'yle de konuş",
                   "Şule rahat ve neşeli; dava sonrası rahatlamış. Oyuncuyla bir şaka yapar, gerektiğinde yine yardım edeceğini söyler.")],
        requires=["sule_durusma_gunu"])

mission("ayse_yeni_sayfa", "Ezgi: Yeni Sayfa", 33, 1000, "ayse", {"ayse": ayse(PIER)},
        [phone_talk("davet", "ayse", "Ezgi Güler'i ara",
                    "Ezgi, Sofeph'in başından geçenleri Yusuf'tan duymuş ve gurur duyuyor. İskelede, ilk tanıştığınız yerde buluşmayı önerir."),
         face_talk("bulus", "ayse", "Del Perro İskelesi'nde Ezgi'yle buluş",
                   "İlk tanıştıkları yere dönmüş olmaktan duygulanır; hafif, sıcak ve ölçülü bir sohbet. Sevgili olduklarını biliyor; "
                   "gelecek için küçük planlar yapar. Cinsel/erotik ayrıntı YOK.")],
        requires=["ayse_gun_batimi", "sofeph_yeni_baslangic"])
