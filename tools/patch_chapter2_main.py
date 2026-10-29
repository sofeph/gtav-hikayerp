"""Bir kerelik yama: main.cpp'ye Bölüm 2 ve karakter değiştirme bağlantıları."""
import pathlib

p = pathlib.Path(__file__).resolve().parents[1] / "mod/src/main.cpp"
c = p.read_text(encoding="utf-8")

a1 = "    PauseMenu pause_probe;"
n1 = (
    "    missions.SetAlterSpawner([&mode](float x, float y, float z, float h, std::string& name) {\n"
    "        return mode.SpawnAlter(x, y, z, h, name);\n"
    "    });\n"
    "    PauseMenu pause_probe;"
)
assert a1 in c
c = c.replace(a1, n1, 1)

a2 = "        mode.Update(shops.pool());\n"
n2 = (
    "        mode.Update(shops.pool());\n"
    "        // Bölüm 1 bitti (final sahnesinden sonra): kadın karakter oluşturucu açılır; serbest dolaşımda karakter değiştirme tuşu çalışır.\n"
    '        if (mode.active() && missions.TakeFlag("bolum2_bekliyor")) mode.BeginChapter2(shops.pool());\n'
    '        mode.SetSwitchAllowed(missions.FlagIs("serbest_dolasim", "1"));\n'
)
assert a2 in c
c = c.replace(a2, n2, 1)
p.write_text(c, encoding="utf-8")
print("ok")
