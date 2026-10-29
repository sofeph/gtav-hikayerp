# ======================================================================
# FİNAL: "Kaçış"  (oyunun sonu)
# Halcyon'un avukatları karşı dava açar; sahte delil iddiasıyla ana karakter hakkında yakalama kararı çıkar. Şule uyarır:
# bu gece şehirden çıkmak gerek. Oyuncu havalimanına gider ve sembolik olarak Vice City uçağına biner. Son yazı: "Vice City'de görüşürüz."
# Kapanış sahnesi KOD kararlı (interact + finale): ekran kararır, yazılar akar, sonra görev biter ve oyun_bitti bayrağı konur.
# ======================================================================
AIRPORT = [-1030.0, -2730.0, 13.8]

mission("sule_kacis", "Şule: Son Uyarı", 40, 8000, "sule", {"sule": NPCS["sule"]},
        [phone_talk("uyari", "sule", "Şule Tatlılıoğlu'nu ara",
                    "Şule telaşlı ama kontrollü: Halcyon'un avukatları karşı dava açtı, Karbonix delillerinin 'sahte' olduğunu iddia ediyor ve "
                    "oyuncu hakkında yakalama kararı çıkarmış. Bu gece tutuklanırsa ücretsiz serbest kalma hakkı bile yetmeyebilir; "
                    "yargılama aylar sürer. Tek güvenli yol şehri terk etmek: havalimanına git, gece yarısı kalkan Vice City uçağına bin. "
                    "Şule dosyayı kendisi sürdürecek; 'üzülme, birbirimizi buluruz' der. Ezgi, Yusuf, Burak ve "
                    "Osman'a vedayı mesajla yapabilirsin der. Duygusal ama net konuşur."),
         {"id": "arac", "type": "vehicle", "vehicle": "speedo", "pos": [-60.0, -785.0, 43.0], "heading": 90.0,
          "objective": "Maze Bank Plaza'nın önündeki araca bin (polis seni arıyor)",
          "guide": "Plazanın önündeki araca bin ve havalimanına sür. Sakin ol, aranıyor olsan da dikkat çekme."},
         {"id": "havaalani", "type": "drive_to", "pos": AIRPORT, "radius": 40.0,
          "objective": "Los Santos Havalimanı'na git (Vice City uçağı gece yarısı kalkıyor)"},
         {"id": "ucak", "type": "interact", "pos": AIRPORT, "radius": 60.0, "prompt": "Vice City ucagina bin",
          "objective": "Araçtan in ve Vice City uçağına bin",
          "guide": "Araçtan inip sarı noktada E'ye bas; uçak seni bekliyor.",
          "finale": {"seconds": 22, "lines": [
              "Yakalama kararı imzalandı. Dosya mahkemede, sen havaalanında.",
              "Şule davayı sürdürüyor. Yusuf Sofeph'i ayakta tutuyor. Burak şehrin havasını temizliyor.",
              "Osman'ın konseri bitti; Ezgi iskelede bir kahve daha ısmarladı.",
              "Uçak piste çıkıyor... Vice City'ye kalkıyor.",
              "Los Santos arkanda kalıyor.",
              "Vice City'de görüşürüz."]}}],
        requires=["sofeph_yeni_baslangic", "burak_temiz_hava", "osman_konser", "ayse_yeni_sayfa"],
        on_complete_flags={"bolum2_bekliyor": "1", "kardes_ayse": "1"}, clear_contacts=True, unlocks_contacts=["sule"])
