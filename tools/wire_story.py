"""Bir kerelik: gen_missions.py'yi dinamik görevsiz, senaryolu hâle getirir."""
import pathlib

T = pathlib.Path(__file__).parent
g = (T / "gen_missions.py").read_text(encoding="utf-8")
if "# ---- 50 dinamik görev" in g:
    g = g[: g.index("# ---- 50 dinamik görev")]
    g += (
        'exec(compile((pathlib.Path(__file__).parent / "story_block2.py").read_text(encoding="utf-8"), "story_block2.py", "exec"))\n\n'
        'print("hikaye gorevi:", len(list(OUT.glob("*.json"))))\n'
    )
    (T / "gen_missions.py").write_text(g, encoding="utf-8")
s = (T / "story_block.py").read_text(encoding="utf-8")
s = s.replace(
    'for old in OUT.glob("*.json"):\n    if not old.name.startswith("dyn_"):\n        old.unlink()',
    'for old in OUT.glob("*.json"):\n    old.unlink()  # dinamik görevler kaldırıldı: eski dosyalar silinir, hikaye yeniden üretilir',
    1,
)
(T / "story_block.py").write_text(s, encoding="utf-8")
print("ok")
