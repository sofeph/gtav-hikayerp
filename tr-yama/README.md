# Turkce yama

`update2.xd3`, standart tek oyunculu pause menusu basligini `Grand Theft Auto V Ultra Pro Max` olarak degistirir. Baslik, `update.rpf` icindeki `FE_MENU_VERSION_SP_PAUSE` / `PM_PAUSE_HDR` tanimindan gelir; gorunen yazi `update2.rpf` icindeki `american_rel.rpf\global.gxt2` tablosunda tutulur. Bu nedenle runtime hook gerekmez.

Yama, kullanicinin kendi oyun arsivine `manifest.json` icindeki SHA-256 ve boyut kontrolleriyle uygulanir. Fark yamasini yeni bir Turkce arsivden yeniden uretirken, `PM_PAUSE_HDR` etiketinin ayni degere ayarlandigi `update2.rpf` hedefini kullanmadan once `tools\make_tr_delta.ps1` calistirmayin; aksi halde varsayilan baslik geri gelir.
