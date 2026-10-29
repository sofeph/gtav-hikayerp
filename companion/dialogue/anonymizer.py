"""Uzak LLM'e giden her şeyin geçtiği filtre.

İki katman:
1. İzin listesi (prompt_builder): LLM'e sadece açıkça seçilmiş alanlar gider. Oyuncu profili,
   gerçek para miktarı, karakter adı, dosya yolu vb. hiç prompt'a girmez.
2. Serbest metin temizliği (burası): oyuncunun mikrofonla söylediği ve STT'nin yazdığı metinde
   gerçek kişisel bilgi (telefon, e-posta, TC no, IBAN, kart no, URL, IP) olabilir.
"""
import re

_PATTERNS: list[tuple[re.Pattern, str]] = [
    (re.compile(r"[\w.+-]+@[\w-]+(?:\.[\w-]+)+"), "[e-posta]"),
    (re.compile(r"https?://\S+|www\.\S+"), "[bağlantı]"),
    (re.compile(r"\bTR\d{2}(?:[ ]?\d{4}){5}[ ]?\d{2}\b", re.I), "[iban]"),
    (re.compile(r"\b(?:\d[ -]?){13,19}\b"), "[kart]"),
    (re.compile(r"\b\d{11}\b"), "[kimlik-no]"),
    (re.compile(r"(?:\+?90[ ]?)?\(?0?5\d{2}\)?[ -]?\d{3}[ -]?\d{2}[ -]?\d{2}\b"), "[telefon]"),
    (re.compile(r"\b(?:\d{1,3}\.){3}\d{1,3}\b"), "[ip]"),
]


def scrub_text(text: str, redact_terms: list[str] | None = None) -> str:
    for pattern, token in _PATTERNS:
        text = pattern.sub(token, text)
    for term in redact_terms or []:
        if term.strip():
            text = re.sub(re.escape(term.strip()), "[isim]", text, flags=re.I)
    return text


def rapport_tier(score: int) -> str:
    """Ham skor yerine kaba kademe gider."""
    if score <= -50:
        return "düşman"
    if score >= 50:
        return "dost"
    return "tanıdık" if score >= 10 else "yabancı"
