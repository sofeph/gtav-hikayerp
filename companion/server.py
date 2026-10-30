import os
import sys

_ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
if _ROOT_DIR not in sys.path:
    sys.path.insert(0, _ROOT_DIR)

"""Mod <-> companion WebSocket sunucusu (CLAUDE.md §10).

Çalıştırma (companion/ klasöründen):  python server.py
Mod bağlanınca: session_start -> ptt_down/ptt_up -> transcript, npc_reply, speaking -> session_end.
Fiyat/clamp/satış kararı modda; burada sadece ses ve LLM işi yapılır.
"""
import asyncio
import json
import logging
import pathlib
import re
import threading
import time

# torch, faster-whisper'dan ÖNCE yüklenmeli: ikisi de kendi cudnn64_9.dll'ini getirir ve ilk yüklenen kazanır.
# Whisper önce yüklenince XTTS sentezi "cudnnGetLibConfig" hatasıyla süreci öldürüyordu (çıkış kodu 9).
import torch  # noqa: F401
import websockets
import yaml

import automenu
from browser_host import BrowserHostManager
from dialogue.output_parser import guard_explicit, is_fallback, parse_reply, salvage_text
from dialogue.prompt_builder import INTENTS, build_messages
from dialogue.price_guard import stated_prices, validated_ad_price
from llm.router import AllProvidersFailed, LLMRouter
from main import load_tts

ROOT = pathlib.Path(__file__).resolve().parent
log = logging.getLogger("server")


class Session:
    def __init__(self, msg: dict) -> None:
        self.context = msg.get("context", "shop")
        if self.context not in INTENTS:
            self.context = "shop"
        self.npc = msg["npc"]
        self.state = msg.get("state", {})
        self.history: list[dict] = []


class Companion:
    def __init__(self, cfg: dict) -> None:
        self.cfg = cfg
        self.router = LLMRouter(cfg["llm"])
        self.redact = cfg["privacy"]["redact_terms"]
        self.history_turns = cfg["dialogue"]["history_turns"]
        from audio.record import PushToTalkRecorder
        from stt.whisper_stt import WhisperSTT
        log.info("Whisper yükleniyor...")
        self.stt = WhisperSTT(**{k: cfg["stt"][k] for k in ("model", "device", "compute_type")})
        self.recorder = PushToTalkRecorder(cfg["stt"]["mic_device"])
        self.turn_lock = asyncio.Lock()
        self.tts = None
        self.tts_loading = False

        # TTS arka planda yüklenir; sunucunun açılmasını veya oyunun başlamasını ASLA bekletmez
        def _load_tts_worker():
            try:
                log.info("TTS arka planda yukleniyor (sunucu/oyun acilisi bekletilmedi)...")
                loaded = load_tts(cfg)
                self.tts = loaded
                if self.tts:
                    log.info("TTS hazir: seslendirme aktif")
                else:
                    log.info("TTS yok: NPC sadece yazi/altyazi ile konusacak")
            except Exception as e:
                log.warning("TTS yukleme hatasi (%s), altyaziyla devam ediliyor", e)
            finally:
                self.tts_loading = False

        self.tts_loading = True
        threading.Thread(target=_load_tts_worker, daemon=True).start()

    async def handle(self, ws) -> None:
        log.info("mod bağlandı")
        automenu.mod_connected.set()  # menü geçişi (pyautogui) dursun
        session: Session | None = None
        try:
            async for raw in ws:
                try:
                    msg = json.loads(raw)
                    kind = msg.get("type")
                    if kind == "session_start":
                        session = Session(msg)
                        await ws.send(json.dumps({"type": "session_ready"}))
                        log.info("oturum: %s", msg.get("id"))
                    elif kind == "state" and session:
                        session.state = msg.get("state", session.state)
                    elif kind == "ptt_down" and session:
                        self.recorder.start()
                    elif kind == "ptt_up" and session:
                        audio = self.recorder.stop()
                        asyncio.create_task(self.turn(ws, session, audio))
                    elif kind == "followup" and session:
                        asyncio.create_task(self.followup(ws, session, msg.get("purpose", "directions"),
                                                          msg.get("facts", {})))
                    elif kind == "text_input" and session:
                        asyncio.create_task(self.respond(ws, session, str(msg.get("text", ""))))
                    elif kind == "gen_posts":
                        asyncio.create_task(self.gen_posts(ws, msg))  # oturum gerektirmez (telefon tarayıcısı içerikleri)
                    elif kind == "session_end":
                        session = None
                        self.recorder.stop()
                except (KeyError, json.JSONDecodeError) as e:
                    log.warning("bozuk mesaj yok sayıldı: %s", e)
        finally:
            log.info("mod ayrıldı")
            automenu.mod_connected.clear()  # oyun yeniden açılırsa yükleme basışı yeniden başlasın


    # Telefon tarayıcısı içerikleri (Bleeter gönderileri/şikayetler, Weazel News, GeneralSale ilanları): LLM üretir, statik değil.
    FEED_SPECS = {
        "complaints": {
            "fields": ["user", "handle", "text"],
            "rule": ("Bleeter'da halkın günlük şikayetleri: komşu gürültüsü, zam, otobüs/metro gecikmesi, market kuyruğu, mekanik, "
                     "trafik, yakıt, kötü hizmet. Her biri farklı bir kişi (meslek/rol ile: 'Kasiyer Leyla', 'Taksici Mehmet'), 1-2 kısa cümle, "
                     "samimi ve gerçekçi. 'handle' küçük harf, boşluksuz."),
        },
        "bleets": {
            "fields": ["user", "handle", "text"],
            "rule": ("Bleeter gündem akışı: şehir gündemi, espriler, hava, spor sohbeti, yeni açılan yerler, günlük yorumlar. "
                     "Her biri farklı kişi veya kurum, 1-2 kısa cümle, hashtag olabilir. 'handle' küçük harf, boşluksuz."),
        },
        "news": {
            "fields": ["title", "tag", "body"],
            "rule": ("Weazel News kamusal haberleri: altyapı, çevre, güvenlik, ulaşım, ekonomi, sağlık, belediye. Tarafsız haber dili; "
                     "'title' kısa manşet, 'tag' tek kelime kategori (Çevre, Ulaşım, Güvenlik, Ekonomi...), 'body' en çok 2 cümle."),
        },
        "ads": {
            "fields": ["title", "price", "body", "seller"],
            "rule": ("GeneralSale ikinci el ilanları: eşya, ev eşyası, elektronik, hizmet, kiralık oda vb. (ARAÇ ilanı yazma). "
                     "Fiyatı kaynak olaylarda açıkça verilmemiş ilanlarda 'price' alanını boş bırak; hiçbir zaman fiyat tahmin etme veya uydurma. "
                     "Açıkça belirtilmiş fiyat varsa yalnızca o tutarı kullan. 'body' en çok 2 cümle, 'seller' sadece ilk ad."),
        },
    }

    async def gen_posts(self, ws, msg: dict) -> None:
        kind = str(msg.get("kind", ""))
        spec = self.FEED_SPECS.get(kind)
        if spec is None:
            return
        count = max(1, min(8, int(msg.get("count", 6) or 6)))
        topics = [str(t)[:200] for t in (msg.get("topics") or [])][:14]
        fields = spec["fields"]
        allowed_prices = stated_prices(topics) if kind == "ads" else {}
        price_context = (
            " Kaynak olaylarda açıkça belirtilmiş kullanılabilecek fiyatlar: "
            + (", ".join(allowed_prices.values()) if allowed_prices else "yok")
            + ". Başka fiyat yazma; yoksa price alanı boş metin olsun."
            if kind == "ads" else ""
        )
        system = (
            "Los Santos'ta geçen kurgusal bir GTA rol yapma oyunu için telefon içeriği üretiyorsun. Türkçe yaz. "
            + spec["rule"] + price_context +
            " Gerçek marka, gerçek kişi veya gerçek olay adı kullanma; nefret söylemi, cinsel içerik ve ağır küfür YOK. "
            "Parayı dolar ($) olarak yaz. Aşağıdaki 'olaylar' şehirde gerçekten olanlardır: bazı içerikler bunlara dolaylı değinebilir "
            "(hepsine değinmek zorunda değilsin), diğerleri bağımsız günlük hayat olsun. SADECE geçerli bir JSON dizisi döndür: "
            f"her eleman {{{', '.join(chr(34) + f + chr(34) + ': \"...\"' for f in fields)}}} alanlarını içeren bir nesne."
        )
        user = f"{count} adet üret. Olaylar: " + (json.dumps(topics, ensure_ascii=False) if topics else "yok (günlük hayat)")
        try:
            raw = ""
            async for chunk in self.router.stream([{"role": "system", "content": system}, {"role": "user", "content": user}]):
                raw += chunk
        except AllProvidersFailed as e:
            log.error("içerik üretilemedi (%s): %s", kind, e)
            await ws.send(json.dumps({"type": "posts", "kind": kind, "posts": [], "error": True}))
            return
        posts: list[dict] = []
        a, b = raw.find("["), raw.rfind("]")
        try:
            arr = json.loads(raw[a:b + 1]) if a >= 0 and b > a else []
        except json.JSONDecodeError:
            arr = []
        for it in arr:
            if not isinstance(it, dict):
                continue
            row = {}
            for f in fields:
                v = it.get(f)
                row[f] = re.sub(r"[<>]", "", str(v)).strip()[:240] if v is not None else ""
            if kind == "ads":
                row["price"] = validated_ad_price(row.get("price", ""), allowed_prices)
            if all(row.get(f) for f in fields if f not in ("seller", "price")):
                if "text" in row:
                    row["text"], _ = guard_explicit(row["text"], "")
                if "body" in row:
                    row["body"], _ = guard_explicit(row["body"], "")
                if "handle" in row:
                    row["handle"] = re.sub(r"[^a-z0-9_]", "", row["handle"].lower())[:24]
                posts.append(row)
        log.info("içerik (%s): %d öğe, sağlayıcı %s", kind, len(posts), self.router.last_provider)
        await ws.send(json.dumps({"type": "posts", "kind": kind, "posts": posts[:count]}, ensure_ascii=False))

    async def turn(self, ws, session: Session, audio) -> None:
        """Sesli tur: mikrofon kaydı -> STT -> ortak cevap hattı."""
        async with self.turn_lock:
            loop = asyncio.get_running_loop()
            t0 = time.perf_counter()
            text = await loop.run_in_executor(None, self.stt.transcribe, audio)
            log.info("STT %.2fs: %r", time.perf_counter() - t0, text)
            if text:
                await self._reply(ws, session, text, t0)

    async def respond(self, ws, session: Session, text: str) -> None:
        """Yazılı tur (oyunda T): STT atlanır, ses turuyla aynı hattan geçer."""
        text = text.strip()[:300]
        async with self.turn_lock:
            if text:
                log.info("yazılı giriş: %r", text)
                await self._reply(ws, session, text, time.perf_counter())

    # Modun hesapladığı GERÇEK bilgiyi NPC'ye anlattıran talimatlar (amaca göre). Karar kodda, LLM sadece anlatır.
    FOLLOWUP = {
        "directions": (
            "Oyuncu yol sordu. Aşağıdaki GERÇEK bilgilere dayanarak 1-2 kısa cümleyle tarif et. Bu bilgiler dışında yer, "
            "sokak, bina veya mesafe uydurma. 'direction' bir pusula yönüdür; mesafeyi yuvarla (ör. 'yaklaşık 2 "
            "kilometre', 300 m altında 'yakında'). Haritaya işaretlendiğini söyle. 'error' varsa orayı bilmediğini "
            "nazikçe söyle."
        ),
        "ride": (
            "Eğer bilgide 'kind' = 'speed_request' ise yolcu hız ricasında bulundu: 'granted' true ise ricayı kabul "
            "ettiğini ve hızlandığını ('direction' faster) ya da yavaşladığını (slower) 1 kısa cümleyle söyle. 'granted' "
            "false ise 'reason' nedenini kibarca söyle (driver_declines: şu an olmaz, güvenli değil/trafik; "
            "helicopter_refuses: helikopterde hız ayarı yapmıyorsun, güvenlik; already_fastest: zaten en hızlı; "
            "already_slowest: zaten en yavaş; no_destination: önce nereye gideceğini söylesin; too_soon: biraz sonra). "
            "Aksi hâlde yolculuk bilgisi aşağıda (GERÇEK). 'accepted' true ise hedefe gittiğinizi, yaklaşık ücreti ve süreyi 1-2 "
            "kısa cümleyle söyle. 'accepted' false ise 'reason' nedenini nazikçe açıkla (insufficient_funds: parası "
            "yetmiyor; unknown_destination: orayı bilmiyorsun; too_close_for_heli: helikopter için çok yakın; "
            "already_there: zaten yakındasınız; no_waypoint: haritada işaret yok) ve başka bir yer sor. 'arrived' true "
            "ise vardığınızı, 'fare' ücretini söyle ve iyi günler dile. Bilgi dışında rakam uydurma."
        ),
        "number": (
            "Oyuncu telefon numaranı istedi; sonuç aşağıda (GERÇEK). 'accepted' true ise numarayı ('number') rakam rakam, "
            "doğal bir cümleyle (ör. 'yaz: üç bir sıfır, ...') söyle ve görüşmek istediğini belirt; 'already_saved' true ise "
            "numaranın zaten onda olduğunu söyle. 'accepted' false ise kibarca vermek istemediğini söyle (doesnt_trust: "
            "oyuncuya güvenmiyorsun; not_comfortable: tanımadığın birine numara vermiyorsun). Başka numara uydurma."
        ),
        "hitch": (
            "Otostop sonucu aşağıda (GERÇEK). 'accepted' true ise oyuncuyu 'destination' yerine götüreceğini 1-2 kısa cümleyle söyle "
            "('distance_m' varsa yuvarlayarak). 'accepted' false ise 'reason' nedenini kısaca söyle (too_far: o kadar uzağa "
            "gitmiyorsun, yolun o tarafa değil; already_there: zaten yakınlardasınız; unknown_destination: orayı bilmiyorsun; "
            "no_waypoint: haritada işaret yok) ve başka bir yer sor. 'arrived' true ise vardığınızı söyle ve iyi günler dile. "
            "Bilgi dışında rakam uydurma."
        ),
        "transit": (
            "Toplu taşıma navigasyonu sonucu aşağıda (GERÇEK). 'found' true ise 1-2 kısa cümleyle söyle: 'board_walk_m' metre yürüyüp "
            "'board_stop' durağından 'line' hattına ('line_type': otobus, metro ya da bolgesel_otobus = şehirler arası yolcu otobüsü) bin, 'stops_to_ride' durak sonra 'alight_stop' durağında in, 'final_walk_m' "
            "metre yürü; yaklaşık 'est_minutes' dakika, ücret 'fare' dolar; biniş durağı haritaya işaretlendi. 'found' false ise 'reason': "
            "walk (yürümek daha kısa, 'walk_minutes' dakika), no_stop_nearby (yakında durak yok), no_service (hat yok), unknown_destination "
            "(orayı bilmiyorsun). Bilgi dışında hat, durak veya süre uydurma."
        ),
        "invite": (
            "Oyuncu seni restorana/kafeye ya da evine davet etti. 'accepted' true ve 'meeting_location' varsa "
            "davetini kabul ettiğini, aracının yanına gelip 'meeting_location' yerine birlikte gideceğinizi söyle. 'accepted' false "
            "ise 'reason' nedenini kibarca söyle (busy_now: şu an müsait değilsin; "
            "player_not_in_car: 'Arabanla gelirsen iyi olur' gibi nazikçe söyle, kesinlikle 'arabana binmen gerekiyor' deme; "
            "no_free_seat: ön koltuk dolu; wanted_level: polis peşindeyken olmaz; "
            "home_invite_locked: ilişkiniz ve oyuncunun itibarı henüz ev daveti için yeterli değil; "
            "no_home: oyuncunun davet edebileceği evi yok; no_destination: uygun restoran/kafe bulunamadı; "
            "cooldown: biraz önce görüştünüz, birazdan; busy: zaten yoldasın; diğerleri: şu an olmuyor)."
        ),
        "favor": (
            "Oyuncu sana bir iyilik yaptı veya yapıyor (sonuç aşağıda, GERÇEK). 'kind' money: 'amount' dolar verdi, 'accepted' false "
            "ise 'reason' (insufficient_funds: parası yetmedi) kibarca söyle; kabulde içten, karakterine uygun, kısa teşekkür et "
            "(küçük miktarda mütevazı, büyük miktarda şaşkın ve minnettar). 'kind' ride: 'accepted' true ise kabul edip binmeyi, "
            "'destination' yerine gideceğini söyle; false ise 'reason' (no_vehicle: araçta değil, no_free_seat: yer yok, "
            "unknown_destination: yeri bilmiyorsun, busy: şu an olmaz) nazikçe açıkla. 'arrived' true ise yerine vardığını, "
            "teşekkür ettiğini söyle."
        ),
        "vendor_tip": (
            "Oyuncu seyyar/kaçak satıcıyı sordu. Sonuç GERÇEK. 'known' true ise nerede olduğunu ÜSTÜ KAPALI söyle: 'street' sokağını, "
            "'direction' yönünü, 'distance_band' uzaklığını ima et ('şu tarafta, ... civarında'); kesin adres/bina/isim verme, "
            "konum haritaya işlendi ('marked_on_map'), bunu \"biraz kolaya kaçtı, bir bakarsın\" gibi dolaylı söyleyebilirsin. "
            "'known' false ise 'reason': does_not_know (bilmiyorsun, bu işlerle alakan yok, şüpheyle bak), asked_recently (az önce sordun, "
            "ısrar etme), nothing_new (zaten hepsini biliyorsun/anlatacak yeni bir şey yok). 1-2 kısa cümle."
        ),
        "police_stop": (
            "Oyuncuyu DURDURDUN ve konuşmayı İLK sen başlatıyorsun. 'reason' weapon_report ise: halka açık yerde silahla görüldüğüne dair ihbar aldın, "
            "üstünü arayacaksın. random_check ise: rutin çevirme. 1-2 kısa, net cümleyle durumu söyle, üstünü arayacağını belirt ve iş birliği iste "
            "(kıpırdama, elleri göster). Sonucu bilmediğini unutma, sonuç uydurma."
        ),
        "favor_ask": (
            "Sen oyuncuya KENDİN yaklaştın ve konuşmayı İLK sen başlatıyorsun (oyuncu henüz bir şey demedi). Bilgi GERÇEK: 'kind' money ise "
            "'amount' dolar rica et (kısa, inandırıcı sebep: otobüs parası, telefon şarjı, yemek...). 'kind' ride ise oyuncunun arabasına işaret "
            "ederek 'destination' yerine bırakıp bırakamayacağını nazikçe sor. 1-2 kısa, doğal cümle; ısrarcı veya tehditkâr olma. Oyuncu reddederse "
            "bir dahaki turda kibarca bırakırsın."
        ),
        "offense": (
            "Oyuncunun söylediğine tepkin sonuçlandı (GERÇEK). 'offense' türü, 'consequence' sonucu: leaves (konuşmayı bitirip "
            "uzaklaşıyorsun), police (polisi arıyorsun), fight (tepki göstereceksin), warning (uyarıyorsun, sınır koyuyorsun). Sonuca "
            "uygun, karakterine yakışan 1 kısa cümle söyle; küfürlü karşılık verme."
        ),
        "phone": (
            "Hizmet çağrısının sonucu aşağıda (GERÇEK). 'accepted' true ise hizmetin yola çıktığını ve 'eta_seconds' "
            "kadar sürede (saniyeyi dakikaya çevirip yuvarla) geleceğini söyle; taksi/helikopter ise 'fare_info' ile "
            "tarifeyi (açılış ve km ücreti) söyle. 'accepted' false ise 'reason' nedenini açıkla (already_on_the_way: "
            "zaten yolda; cooldown: kısa süre önce çağrıldı, biraz bekle; wanted_level: arandığın için hizmet "
            "verilemiyor; no_road_nearby: yakında uygun yol yok; busy_in_ride: şu an yolculuktasın; no_vehicle: aracın yok/son aracı "
            "bulunamadı; vehicle_too_far: araç çok uzakta; insufficient_funds: parası yetmiyor; already_in_it: zaten o aracın içinde). "
            "Tamirci/vale kabulünde 'estimated_fee' / 'fee' ücretini ve 'eta_seconds' süresini söyle. Otopark görevlisi "
            "('service' parking): 'action' valet_transport ise vale'nin taksiyle geleceğini, aracını alıp en yakın garaja götüreceğini ve senin bulunduğun yerde kalacağını söyle; taksiye bineceğini söyleme. "
            "henüz garaja ulaştığını iddia etme. 'accepted' true ise 'vehicle' aracının garaja götürülmek üzere yola çıktığını, 'fee' ücretini ve varsa 'eta_seconds' süresini söyle; false ise "
            "vehicle_moving: araç hareket ediyor; vehicle_too_far: araç yanında değilsin, yakınına git; "
            "no_road_nearby: yakında uygun yol yok; no_garage: uygun garaj bulunamadı; no_garage_road: garajın yakınına araçla ulaşılabilecek yol yok; "
            "crew_unavailable veya vehicle_unavailable: vale/taksi şu an ayarlanamadı; already_in_garage: araç zaten garajda; "
            "already_on_the_way: vale zaten yolda; wanted_level: aranırken vale hizmeti verilemiyor; insufficient_funds: 50 doların yetmiyor; "
            "no_vehicle: mod garajına kayıtlı aracın yok. 'locate_vehicle' sonucunda: 'in_garage' true ise araç garajda; değilse "
            "'street' (varsa), 'direction' (dogu/bati/kuzey/guney) ve 'distance_m' ile nerede olduğunu söyle ve haritaya "
            "işaret koyduğunu belirt; 'found' false ise aracı yok."
        ),
        "contact_call": (
            "Haviland'in rehberden arama isteğinin sonucu (GERÇEK). 'accepted' true ise seçilen kişiyi aramaya "
            "bağladığını kısa söyle. false ise 'not_found' nedeniyle kişiyi rehberde bulamadığını ve adını tekrar "
            "sormak istediğini belirt; hizmet çağırmayı veya hizmetler menüsünü önermeyi sürdürme."
        ),
        "wanted_lawyer": (
            "Yakalama dosyasını silme denemesinin sonucu aşağıda (GERÇEK). 'cleared' true ise aynen şu anlamda "
            "kısa ve samimi söyle: 'Dosya silindi. İyi ki sana güveniyordum zaten.' 'cleared' false ise dosyanın "
            "silinemediğini belirt ve state.case.offense/facts içindeki suçun türüne dayanarak oyuncuya kısa, "
            "hafif alaycı bir laf söyle; başka suç, mağdur veya ayrıntı uydurma. 'next_bar_chance' varsa yalnızca "
            "yeni olasılığı belirt. Başarısızken dosya silindiğini veya aranmanın bittiğini asla söyleme."
        ),
    }

    async def followup(self, ws, session: Session, purpose: str, facts: dict) -> None:
        """Modun hesapladığı GERÇEK bilgiyle (yön, ücret, süre...) NPC'ye ikinci cümleyi söyletir; oyuncu görmez."""
        rule = self.FOLLOWUP.get(purpose, self.FOLLOWUP["directions"])
        instruction = (
            "[SİSTEM - oyuncu görmez] " + rule + " intent 'chat' olsun, cevap 1-2 kısa cümle olsun. Bilgi: "
            + json.dumps(facts, ensure_ascii=False)
        )
        async with self.turn_lock:
            await self._reply(ws, session, instruction, time.perf_counter(), silent=True)

    async def _reply(self, ws, session: Session, text: str, t0: float, silent: bool = False) -> None:
        try:
            loop = asyncio.get_running_loop()
            if not silent:
                await ws.send(json.dumps({"type": "transcript", "text": text}))

            cloud = build_messages(session.context, session.npc, session.state, session.history, text,
                                   self.redact, scrub=True)
            local = build_messages(session.context, session.npc, session.state, session.history, text,
                                   self.redact, scrub=self.cfg["privacy"]["scrub_local_too"])
            raw = ""
            await ws.send(json.dumps({"type": "thinking", "state": "start"}))  # mod "düşünüyor..." yazar (sessiz, bildirimsiz)
            try:
                async for chunk in self.router.stream(local, cloud_messages=cloud):
                    raw += chunk
            except AllProvidersFailed as e:
                log.error("LLM yok: %s", e)
                await ws.send(json.dumps({"type": "thinking", "state": "end"}))
                await ws.send(json.dumps({"type": "error", "message": "LLM servisi yanıt vermedi"}))
                return
            await ws.send(json.dumps({"type": "thinking", "state": "end"}))
            relationship = (session.state.get("mission") or {}).get("relationship", "")
            reply = parse_reply(raw, session.context, relationship)
            # "Pardon, anlamadım?" yerine düşün: geçerli cevap çıkana kadar en çok 2 kez, JSON uyarısıyla yeniden sor
            nudge = {"role": "user", "content": "Önceki cevabın geçerli bir JSON nesnesi değildi. Düşün ve SADECE şemaya uygun TEK bir "
                                                 "JSON nesnesi döndür ('say' ilk alan; Türkçe, kısa)."}
            for attempt in range(2):
                if not is_fallback(reply):
                    break
                log.warning("geçersiz cevap, yeniden deneniyor (%d)", attempt + 1)
                raw = ""
                try:
                    async for chunk in self.router.stream(local + [nudge], cloud_messages=cloud + [nudge]):
                        raw += chunk
                except AllProvidersFailed:
                    break
                reply = parse_reply(raw, session.context, relationship)
            if is_fallback(reply) and (plain := salvage_text(raw)):  # son çare: modelin düz metin cevabı "anlamadım"dan iyidir
                log.warning("JSON alınamadı, düz metin kullanıldı: %r", plain[:80])
                reply = {**reply, "say": guard_explicit(plain, relationship)[0]}
            log.info("LLM (%s) %.2fs: %s", self.router.last_provider, time.perf_counter() - t0, reply["say"])
            await ws.send(json.dumps({"type": "npc_reply", **reply}, ensure_ascii=False))

            if not silent:  # sistem talimatı geçmişe girmez, sadece NPC'nin cümlesi girer
                session.history.append({"role": "user", "content": text})
            session.history.append({"role": "assistant", "content": reply["say"]})
            session.history = session.history[-2 * self.history_turns:]

            # Parantez içi sahne notları (örn. "(Özel ilişki)") ekranda görünür ama sesli okunmaz.
            spoken = re.sub(r"\([^)]*\)", "", reply["say"]).strip()
            if self.tts and spoken:
                voice = session.npc.get("voice", {})
                wav = await loop.run_in_executor(None, self.tts.synthesize, spoken, voice)
                await ws.send(json.dumps({"type": "speaking", "state": "start"}))
                from audio.playback import play
                await loop.run_in_executor(None, play, wav, self.tts.sample_rate, self.cfg["tts"]["output_device"])
                await ws.send(json.dumps({"type": "speaking", "state": "end"}))
        except Exception:  # bir turun hatası sunucuyu veya sonraki turları etkilemesin
            log.exception("tur başarısız")
            try:
                await ws.send(json.dumps({"type": "speaking", "state": "end"}))  # mod konuşuyor sanıp takılmasın
            except Exception:
                pass


async def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s: %(message)s")
    threading.Thread(target=automenu.run, daemon=True).start()  # en son çare: oyun menüsüne Geri tuşu (HIKAYE_AUTOBACK=1 ise)
    cfg = yaml.safe_load((ROOT / "config.yaml").read_text(encoding="utf-8"))
    companion = Companion(cfg)
    browser_host = BrowserHostManager()
    from webv2_backend.engine import create_engine
    from webv2_backend.http_server import WebV2HTTPServer

    data_root = pathlib.Path(os.environ.get("HIKAYE_WEBV2_DATA", ROOT.parent / "data")).resolve()
    webv2 = create_engine(
        root_path=ROOT.parent,
        data_root=data_root,
        plugin_dir=ROOT / "webv2_backend" / "plugins",
    )
    loaded_plugins = webv2.load_plugins()
    http_server = WebV2HTTPServer(webv2, data_root / "webv2").start()
    log.info("WebV2 HTTP: http://127.0.0.1:%d/ (%d eklenti)", http_server.port, len(loaded_plugins))
    browser_host.start()
    host, port = cfg["server"]["host"], cfg["server"]["port"]
    try:
        async with websockets.serve(companion.handle, host, port):
            log.info("hazır: ws://%s:%d  (oyunu açabilirsin)", host, port)
            await asyncio.Future()
    finally:
        http_server.close()
        browser_host.stop()


if __name__ == "__main__":
    asyncio.run(main())
