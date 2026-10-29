"""Para birimi kontrolü: oyun dolarla çalışır, metinde TL/lira/₺ geçerse dolara çevrilir.

İki yerde kullanılır:
- LLM'e giden oyuncu cümlesi (model hiç TL görmesin),
- LLM'den gelen cevap (model yine de TL derse oyuncuya gitmeden düzelt).
"""
import re

_NUM = r"\d[\d\.,]*"
_FIXES = [
    (re.compile(rf"₺\s*({_NUM})"), r"\1 dolar"),                # ₺500      -> 500 dolar
    (re.compile(rf"\$\s*({_NUM})"), r"\1 dolar"),               # $12.500   -> 12.500 dolar (TTS "$"yi okumaz)
    (re.compile(r"\b(?:TL|tl|Tl|TRY)\b(?:'\w+)?"), "dolar"),     # 500 TL, TL'ye -> dolar
    (re.compile(r"\b[Ll]ira\w*"), "dolar"),                      # lira, liralık -> dolar
    (re.compile(r"₺"), "dolar"),
]


def normalize_currency(text: str) -> tuple[str, bool]:
    """Para birimini dolara çevirir. Dönüş: (düzeltilmiş metin, değişti mi)."""
    out = text
    for pattern, repl in _FIXES:
        out = pattern.sub(repl, out)
    return out, out != text
