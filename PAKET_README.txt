Hikaye RP - Gelistirici: Sofeph Games OSP Foundaton (ucretsiz, ticari olmayan)

HIKAYE RP - GTA V hikaye modu icin Turkce rol yapma modu (ucretsiz, ticari olmayan)
=========================================================================

NASIL CALISIR (FiveM mantigi - ORIJINAL GTA V'YE ASLA DOKUNULMAZ)
  Baslat.bat -> Baslatici acilir -> "Oyunu Baslat":
    1) Mod icin AYRI bir oyun kopyasi hazirlanir (varsayilan: %LOCALAPPDATA%\HikayeTurkRP\Game).
       Buyuk dosyalar (.rpf, update, x64) orijinalden BAGLANIR (disk yeri harcamaz); GTA5.exe ve kucuk dosyalar kopyalanir.
       ScriptHookV, OpenIV.asi, HikayeTurkRP.asi, kayitlar ve Turkce yama SADECE bu kopyadadir.
    2) Yapay zeka (Python) sunucusu acilir,
    3) GTA V KOPYADAN baslar ve dogrudan Hikaye RP'ye gecer (menu yok; kayitli karakter yoksa karakter olusturucu),
    4) Oyun kapaninca sunucu da kapanir.
  Orijinal GTA V klasorunden sadece OKUNUR. Mod'dan vazgecersen "Kopyayi sil" yeterli (kayitlar korunur); orijinal oyun aynen kalir.
  Steam guncellemesi gelince kopya bir sonraki baslatmada kendini yeniler.

GEREKENLER (paketle gelmez)
  - GTA V (Legacy surum, Steam) - Steam acik olmali           - Python 3.10+ ve sunucu paketleri:
        python -m pip install -r companion\requirements-full.txt
    (NVIDIA GPU icin once PyTorch'u kendi CUDA surumunle kur: pytorch.org)
  - ScriptHookV (dev-c.com/gtav/scripthookv): ScriptHookV.dll + dinput8.dll   -> Baslatici > "ScriptHookV / OpenIV ekle"
  - (Turkce yama icin) OpenIV.asi                                              -> ayni dugme
  - Buyuk dosya baglantisi icin Windows "Gelistirici Modu" ACIK olmali (Ayarlar > Gizlilik ve guvenlik > Gelistiriciler),
    ya da oyun kopyasi klasoru orijinal GTA V ile AYNI SURUCUDE olmali (sabit baglanti kullanilir). Aksi halde 30+ GB kopyalamak
    yerine hata verir.
  - Mikrofon (sesle konusma icin) ve internet (ucretsiz LLM saglayicilari icin)

KURULUM
  1) Paketi bir klasore cikar (orn. C:\HikayeTurkRP). Yolda Turkce karakter olmasin.
  2) Baslat.bat -> Ayarlar'da orijinal GTA V klasorunu ve Python yolunu kontrol et.
  3) "ScriptHookV / OpenIV ekle" ile gerekli dosyalari sec (bir kere).
  4) "Oyunu Baslat". Ilk acilista kopya hazirlanir ve ses modeli yuklenir (20-60 sn).
     Oyun acilirken "BattlEye Protected Launch Failed ... Disable BattlEye protection and retry in single-player mode?" cikarsa
     RETRY'a bas: mod icin gereken BattlEye'siz tek oyunculu mod budur (kopyada BattlEye dosyalari bilerek yok). Her acilista cikabilir.
  5) Oyunda: telefon O, envanter I, konusma B (ses) / T (yazi). F9 menusu hala calisir.

TURKCE YAMA
  Rockstar'in update.rpf/update2.rpf dosyalari pakete KONMAZ. Pakette sadece orijinal ile yamali dosya arasindaki FARK vardir (yaklasik 6 MB,
  xdelta3). Baslatici, yamali dosyalari senin KENDI orijinal GTA V dosyalarindan oyun kopyasinda uretir ve SHA-256 ile dogrular
  (birkac saniye-dakika). Orijinal klasore yazilmaz. GTA V surumun yamayla uyusmazsa "surum uyusmuyor" der ve yama uretmez.
  Ayarlar > "Turkce yama" varsayilan aciktir. mods klasorunun oyunda yuklenmesi icin OpenIV.asi gerekir (pakette yok: "ScriptHookV / OpenIV ekle").
  Kapatmak mods klasorunu mods.kapali yapar, silmez. Menyoo ve orijinal klasore dokunulmaz.

GUNCELLEME (GitHub)
  Baslatici > Ayarlar > "GitHub deposu" (kullanici/depo) gir. "Guncellemeleri denetle" yeni surum varsa gosterir;
  "Indir ve guncelle" paketi indirir, mod/veri/sunucu dosyalarini yeniler, oyun kopyasini gunceller. Kayitlar ve ayarlar korunur.
  Ozel depo icin GitHub erisim anahtari (salt okuma) girilebilir; isletim sistemi sifrelemesiyle saklanir.

LISANS NOTLARI
  Ucretsiz ve ticari olmayan. XTTS-v2 (CPML) ve koordinat verileri icin THIRD_PARTY.md'ye bak.
  Menyoo, OpenIV, ScriptHookV ve Rockstar dosyalari pakette YOKTUR.
