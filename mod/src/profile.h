#pragma once
#include <string>
#include <utility>
#include <vector>

#include "common.h"
#include "json.hpp"

namespace hikaye {

// Hikaye RP modunun kendi karakteri (GTA Online tarzı). Hikaye karakterlerinden (Michael/Franklin/Trevor) ve onların
// parasından tamamen ayrıdır; kayıt dosyası: HikayeTurkRP/saves/character.json
struct CharacterProfile {
    std::string name;  // boş bırakılabilir: karakter oluşturucuda oyuncu kendi seçer (Yusuf Cansever bir görev NPC'sidir)
    bool female = false;

    // Atalar (kimlik listelerindeki sıra numaraları, bkz. profile.cpp)
    int mother = 0, father = 0;
    float shape_mix = 0.5f, skin_mix = 0.5f;

    float face[20] = {};  // yüz özellikleri, -1..1

    int hair = 0, hair_color = 0, hair_highlight = 0, eye_color = 0;

    struct Overlay {
        int index = 255;  // 255 = yok
        float opacity = 1.f;
        int color = 0;
    };
    Overlay overlays[13];  // 0 leke, 1 sakal, 2 kaş, 3 yaşlanma, 4 makyaj, 5 allık, 6 ten, 7 güneş, 8 ruj, 9 ben, 10 göğüs kılı...

    struct Comp {
        int drawable = 0, texture = 0;
    };
    Comp comps[12];  // ped bileşenleri (1 maske, 3 kollar, 4 alt, 5 çanta, 6 ayakkabı, 7 aksesuar, 8 atlet, 9 zırh, 10 amblem, 11 üst)

    struct Prop {
        int drawable = -1, texture = 0;  // -1 = yok
    };
    Prop props[10];  // 0 şapka, 1 gözlük, 2 küpe, 6 saat, 7 bileklik

    // Geçmiş (ortak nokta sistemi: hemşehrilik, askerlik, meslek, takım)
    std::string hometown, district, military_unit, past_job, team;

    int cash = 5000;  // modun kendi cüzdanı (hikaye parasına dokunulmaz)
    std::vector<std::pair<std::string, int>> weapons;  // RP silahları (WEAPON_ öneki olmadan, mermi); hikaye silahlarından ayrı

    bool has_pos = false;  // son konum
    float x = 0, y = 0, z = 0, heading = 0;

    CharacterProfile();  // cinsiyete uygun varsayılan kıyafet
    void ResetOutfit();
    nlohmann::json ToJson() const;
    static CharacterProfile FromJson(const nlohmann::json& j);
};

constexpr int kMotherCount = 22, kFatherCount = 24;

bool LoadProfile(CharacterProfile& out, int slot = 0);  // slot 0: character.json (Bölüm 1, erkek), 1: character_2.json (Bölüm 2, kadın)
bool SaveProfile(const CharacterProfile& p, int slot = 0);
// Profili freemode ped'e uygular (kalıtım, yüz, saç, göz, makyaj, kıyafet, aksesuar).
void ApplyToPed(Ped ped, const CharacterProfile& p);

}  // namespace hikaye
