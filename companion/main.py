import os
import sys

_ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
if _ROOT_DIR not in sys.path:
    sys.path.insert(0, _ROOT_DIR)

"""Faz 0 prototipi: terminalden yazarak NPC ile konuş (STT/TTS henüz yok).

Çalıştırma (companion/ klasöründen):  python main.py
"""
import asyncio
import json
import logging
import pathlib
import time

import yaml

from dialogue.output_parser import parse_reply
from dialogue.prompt_builder import build_messages
from llm.router import AllProvidersFailed, LLMRouter

ROOT = pathlib.Path(__file__).resolve().parent

NPC = {
    "voice": {"speaker": "Damien Black"},   # XTTS yerleşik ses; shop JSON'daki npc.voice karşılığı
    "personality": "Ağzı laf yapan, samimi ama çakal bir galerici. Kısa ve esprili konuşur.",
    "profile": {"name": "Ramazan", "hometown": "Trabzon", "hometown_district": "Of",
                "military_unit": "Kars", "past_jobs": ["taksici"], "team": "Trabzonspor"},
}
# Gerçekte bu state moddan gelir (otorite mod). Burada sadece deneme için sabit.
STATE = {
    "items": [{"id": "sultan", "name": "Sultan", "price": 12000}, {"id": "blista", "name": "Blista", "price": 8000}],
    "floor_price": {"sultan": 10200, "blista": 6800},
    "patience_left": 3, "last_offers": [], "unlocked_common_ground": [],
    "player_cash": 9000, "cheapest_price": 8000, "rapport": 0,
}


def load_tts(cfg: dict):
    tcfg = cfg["tts"]
    for engine in (tcfg["engine"], tcfg.get("fallback")):
        try:
            if engine == "xtts":
                from tts.xtts_tts import XTTS
                return XTTS()
            if engine == "piper":
                from tts.piper_tts import PiperTTS
                return PiperTTS(ROOT / tcfg["piper_dir"])
        except Exception as e:  # yüklenemeyen motor yerine yedeğe geç
            print(f"[uyarı] TTS '{engine}' yüklenemedi: {e}")
    return None


async def main(voice_mode: bool) -> None:
    logging.basicConfig(level=logging.INFO, format="%(name)s: %(message)s")
    cfg = yaml.safe_load((ROOT / "config.yaml").read_text(encoding="utf-8"))
    router = LLMRouter(cfg["llm"])
    redact = cfg["privacy"]["redact_terms"]
    history: list[dict] = []
    n = cfg["dialogue"]["history_turns"]
    stt = tts = recorder = None
    if voice_mode:
        from audio.playback import play
        from audio.record import PushToTalkRecorder
        from stt.whisper_stt import WhisperSTT
        stt = WhisperSTT(**{k: cfg["stt"][k] for k in ("model", "device", "compute_type")})
        recorder = PushToTalkRecorder(cfg["stt"]["mic_device"])
        tts = load_tts(cfg)
    print("Galerici Ramazan ile konuş. Çıkmak için 'q'." + (" Enter: kayıt başlat/bitir." if voice_mode else ""))
    while True:
        if voice_mode:
            if input("[Enter: konuşmaya başla] ").strip().lower() == "q":
                break
            recorder.start()
            input("[kayıt... Enter: bitir] ")
            t0 = time.perf_counter()
            text = stt.transcribe(recorder.stop())
            print(f"sen> {text}   (STT {time.perf_counter() - t0:.2f}s)")
            if not text:
                continue
        else:
            text = input("sen> ").strip()
            if not text or text == "q":
                break
        # Uzak provider'lar scrub'lı, local provider ham metni alır (scrub_local_too=false iken).
        cloud = build_messages("shop", NPC, STATE, history, text, redact, scrub=True)
        local = build_messages("shop", NPC, STATE, history, text, redact, scrub=cfg["privacy"]["scrub_local_too"])
        t0, first, raw = time.perf_counter(), None, ""
        try:
            async for chunk in router.stream(local, cloud_messages=cloud):
                first = first or time.perf_counter() - t0
                raw += chunk
        except AllProvidersFailed as e:
            print(f"[hata] hiçbir provider cevap vermedi: {e}")
            continue
        reply = parse_reply(raw, "shop")
        print(f"{NPC['profile']['name']}> {reply['say']}   ({router.last_provider}, ilk token {first:.2f}s, toplam {time.perf_counter() - t0:.2f}s)")
        print("   ", json.dumps({k: v for k, v in reply.items() if k != "say"}, ensure_ascii=False))
        history += [{"role": "user", "content": text}, {"role": "assistant", "content": reply["say"]}]
        history = history[-2 * n:]
        if tts:
            t1 = time.perf_counter()
            audio = tts.synthesize(reply["say"], NPC["voice"])
            print(f"    (TTS {time.perf_counter() - t1:.2f}s)")
            play(audio, tts.sample_rate, cfg["tts"]["output_device"])


if __name__ == "__main__":
    import sys
    asyncio.run(main("--voice" in sys.argv))
