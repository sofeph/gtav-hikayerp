import json
import logging
import re

from .prompt_builder import INTENTS

from .currency import normalize_currency  # noqa: F401  (testler buradan da içe aktarır)

log = logging.getLogger("parser")

# Cinsel/erotik içerik güvencesi: prompt'a ek olarak kodda da kontrol. Model yine de açık ifade üretirse cevap, ima edilen
# "(Özel ilişki)" notu + iltifatla (sevgili ise) veya nazik bir sınırla değiştirilir.
_EXPLICIT_STEMS = ("seks", "seviş", "çıplak", "soyun", "yatağa", "cinsel")  # kökler: çekimleri de yakalar
_PARTNER_FALLBACK = "(Özel ilişki) Seninle olmak bana çok iyi geliyor, her halin çok hoşuma gidiyor."
_BOUNDARY_FALLBACK = "Bu konuya girmeyelim, ama sana değer veriyorum."

# Vedalaşma / Bağlam sonlandırma ifadeleri: AI bu kalıplarla veda ettiğinde bağlam biter
_FAREWELL_REGEX = re.compile(
    r"\b(iyi\s+günler|iyi\s+akşamlar|iyi\s+geceler|görüşmek\s+üzere|görüşürüz|hoşça\s*kal|hoşçakal|"
    r"kendine\s+iyi\s+bak|kolay\s+gelsin|eyvallah|hadi\s+eyvallah|sağlıcakla\s+kal|selametle|bay\s+bay|güle\s+güle)\b",
    re.IGNORECASE,
)



def guard_explicit(say: str, relationship: str = "") -> tuple[str, bool]:
    low = say.lower()
    if not any(stem in low for stem in _EXPLICIT_STEMS):
        return say, False
    return (_PARTNER_FALLBACK if relationship == "partner" else _BOUNDARY_FALLBACK), True

MOODS = {"neutral", "happy", "annoyed", "suspicious"}
CLAIM_TYPES = {"hometown", "hometown_district", "military_unit", "past_job", "posting", "team"}
FALLBACK = {"say": "Pardon, anlamadım?", "intent": "chat", "item": None, "qty": 1, "player_offer": None,
            "npc_offer": None, "destination": None, "departure": None, "ticket_class": None,
            "claims": [], "mood": "neutral"}


def _extract_json_object(text: str) -> str | None:
    start = text.find("{")
    if start < 0:
        return None
    depth, in_str, esc = 0, False, False
    for i in range(start, len(text)):
        ch = text[i]
        if in_str:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
        elif ch == '"':
            in_str = True
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return None


def _try_load(text: str) -> dict | None:
    candidate = _extract_json_object(text)
    if candidate is None:
        return None
    for attempt in (candidate, re.sub(r",\s*([}\]])", r"\1", candidate)):  # sondaki virgül onarımı
        try:
            obj = json.loads(attempt)
            return obj if isinstance(obj, dict) else None
        except json.JSONDecodeError:
            continue
    return None


def is_fallback(reply: dict) -> bool:
    """Cevap gerçek bir NPC cümlesi mi, yoksa 'anlamadım' yedeği mi?"""
    return reply.get("say") == FALLBACK["say"]


def salvage_text(raw: str, allow_plain: bool = True) -> str:
    """JSON bozuk/kesik geldiyse cümleyi kurtar: 'say' alanını (kesilmiş de olsa) ya da düz metni al."""
    t = re.sub(r"<think>.*?(</think>|$)", "", raw, flags=re.S).strip()
    m = re.search(r'"say"\s*:\s*"((?:[^"\\]|\\.)*)', t)
    if m:
        try:
            s = json.loads('"' + m.group(1) + '"')
        except json.JSONDecodeError:
            s = m.group(1).replace('\\"', '"').replace("\\n", " ")
        s = s.strip()
        if len(s) >= 4:
            return s[:300]
    if allow_plain and "{" not in t and "}" not in t:
        t = re.sub(r"```\w*", "", t).strip()
        if len(t) >= 6:
            return t[:300]
    return ""


def _num_or_none(v):
    return int(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else None


def parse_reply(raw: str, context: str, relationship: str = "") -> dict:
    """LLM çıktısı sadece öneridir: her alan doğrulanır, bilinmeyen intent `chat` olur."""
    obj = _try_load(raw)
    if obj is None or not isinstance(obj.get("say"), str) or not obj["say"].strip():
        saved = salvage_text(raw, allow_plain=False)  # düz metin burada kullanılmaz: önce yeniden sorulur (server)
        if saved:  # "anlamadım" yerine kurtarılan cümle (niyet: chat); durum işlemleri bu turda yapılmaz
            log.warning("bozuk JSON'dan cümle kurtarıldı: %r", saved[:80])
            fixed, _ = normalize_currency(saved)
            fixed, _ = guard_explicit(fixed, relationship)
            return {**FALLBACK, "say": fixed}
        return dict(FALLBACK)
    intent = obj.get("intent")
    claims = [
        {"type": c["type"], "value": str(c.get("value", ""))}
        for c in obj.get("claims") or []
        if isinstance(c, dict) and c.get("type") in CLAIM_TYPES and c.get("value")
    ]
    say, fixed = normalize_currency(obj["say"].strip())
    if fixed:
        log.warning("para birimi dolara düzeltildi: %r -> %r", obj["say"].strip(), say)
    say, blocked = guard_explicit(say, relationship)
    if blocked:
        log.warning("açık ifade engellendi (ilişki=%s)", relationship or "-")
    final_intent = intent if intent in INTENTS[context] else "chat"
    # AI kendisi vedalaştıysa (iyi günler, hoşça kal vb.) bağlam biter
    if final_intent == "chat" and _FAREWELL_REGEX.search(say):
        if context == "phone" and "hang_up" in INTENTS.get(context, []):
            final_intent = "hang_up"
            log.info("AI telefonda vedalaştı (%r), bağlam bitiriliyor (intent=hang_up)", say)
        elif "leave" in INTENTS.get(context, []):
            final_intent = "leave"
            log.info("AI vedalaştı (%r), bağlam bitiriliyor (intent=leave)", say)

    return {
        "say": say,
        "intent": final_intent,
        "item": obj.get("item") if isinstance(obj.get("item"), str) else None,
        "qty": max(1, _num_or_none(obj.get("qty")) or 1),
        "player_offer": _num_or_none(obj.get("player_offer")),
        "npc_offer": _num_or_none(obj.get("npc_offer")),
        "destination": obj.get("destination") if isinstance(obj.get("destination"), str) else None,
        "departure": obj.get("departure") if isinstance(obj.get("departure"), str) else None,
        "ticket_class": obj.get("ticket_class") if isinstance(obj.get("ticket_class"), str) else None,
        "claims": claims,
        "mood": obj.get("mood") if obj.get("mood") in MOODS else "neutral",
    }
