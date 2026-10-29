#pragma once
#include <array>
#include <string>
#include <vector>

#include "common.h"

namespace hikaye {

struct Place {
    std::string name;  // oyuncuya/LLM'e söylenecek ad
    Vector3 pos{};
};

// Yer/adres altyapısı (NPC'ye yol sorma; ileride taksi ve telefon hizmetleri). Veri: data/places/*.json
// (bkz. data/places/SOURCES.md). OTORİTE KODDA: LLM sadece "nereye" metnini çıkarır; konumu, mesafeyi ve yönü
// burası hesaplar, LLM bunları uyduramaz.
class Places {
public:
    void Load();
    // Dükkan gibi adı olan ek yerler (ShopManager yüklendikten sonra eklenir).
    void AddNamed(const std::string& name, const Vector3& pos);

    // "havaalanı", "en yakın benzinlik", "Davis", "hastane", "Davis Oto Galeri"... metnini çözer.
    // `from`: "en yakın X" için oyuncunun konumu. Bulamazsa false.
    bool Resolve(const std::string& query, const Vector3& from, Place& out) const;

    // Küçük harf + Türkçe harfleri ASCII'ye çevirir ("İşaret" -> "isaret"); metin eşleştirme için.
    static std::string Fold(const std::string& s);
    std::string ZoneNameAt(const Vector3& p) const;                        // "Davis" (yoksa boş)
    static std::string Compass(const Vector3& from, const Vector3& to);   // "kuzeydoğu"
    static float Dist2D(const Vector3& a, const Vector3& b);

private:
    struct Zone {
        std::string id, name;
        std::vector<std::array<float, 4>> bounds;  // minx, miny, maxx, maxy
    };
    struct Group {  // aynı türden birçok nokta: ATM'ler, hastaneler... (en yakını seçilir)
        std::string label;
        std::vector<Place> points;
    };
    struct Entry {
        enum class Kind { Zone, Group, Named } kind;
        std::string key;  // katlanmış (küçük harf, aksansız) arama anahtarı
        size_t index;     // zones_ / groups_ / named_ içinde
    };

    void AddKey(const std::string& raw, Entry::Kind kind, size_t index);
    bool ZoneCenter(const Zone& z, Place& out) const;

    std::vector<Zone> zones_;
    std::vector<Group> groups_;
    std::vector<Place> named_;
    std::vector<Entry> entries_;
};

}  // namespace hikaye
