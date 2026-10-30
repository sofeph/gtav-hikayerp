#include "blips.h"

#include <windows.h>

#include <unordered_map>
#include <unordered_set>

namespace hikaye::blips {

namespace {

constexpr unsigned long long kScanIntervalMs = 2000;
constexpr unsigned long long kReapplyIntervalMs = 5000;
constexpr int kMaxBlipSprite = 900;

std::unordered_set<Blip> g_named_blips;
std::unordered_map<Blip, unsigned long long> g_last_applied;
unsigned long long g_last_scan = 0;

const char* CategoryForSprite(int sprite) {
    switch (sprite) {
        case 1: return "Harita Isareti";
        case 2: return "Buyuk Harita Isareti";
        case 3:
        case 41:
        case 42:
        case 43:
        case 56:
        case 58:
        case 60: return "Polis";
        case 4: return "Polis Bolgesi";
        case 6:
        case 27:
        case 28:
        case 163:
        case 164:
        case 425: return "Oyuncu";
        case 8: return "Isaretli Hedef";
        case 15:
        case 16:
        case 64:
        case 90:
        case 307:
        case 401:
        case 422:
        case 423: return "Hava Araci";
        case 38:
        case 94:
        case 126:
        case 127:
        case 309:
        case 314:
        case 315:
        case 316:
        case 376:
        case 379: return "Yaris";
        case 40:
        case 350:
        case 417:
        case 418: return "Ev";
        case 50:
        case 357:
        case 369: return "Garaj";
        case 51:
        case 140: return "Uyusturucu";
        case 52: return "Dukkan";
        case 61: return "Hastane";
        case 65: return "Gorev";
        case 66: return "Zirhli Arac";
        case 68:
        case 402: return "Arac Servisi";
        case 71: return "Berber";
        case 72: return "Arac Modifiye";
        case 73:
        case 362: return "Giyim Magazasi";
        case 75: return "Dovme Salonu";
        case 76:
        case 77:
        case 78:
        case 79:
        case 86:
        case 88:
        case 89:
        case 112:
        case 120:
        case 149:
        case 352:
        case 355:
        case 381:
        case 382:
        case 383:
        case 384:
        case 385:
        case 387:
        case 388:
        case 389:
        case 400: return "Gorev Kisisi";
        case 84: return "Rampage Gorevi";
        case 85: return "Turistik Tur";
        case 93:
        case 121: return "Eglence Mekani";
        case 100: return "Arac Yikama";
        case 102: return "Komedi Kulubu";
        case 103: return "Dart";
        case 106: return "FIB";
        case 108:
        case 431:
        case 434: return "Para";
        case 109:
        case 358: return "Golf";
        case 110:
        case 119:
        case 313: return "Silah Magazasi";
        case 122: return "Tenis";
        case 134: return "Anahtar";
        case 135: return "Sinema";
        case 136: return "Muzik";
        case 141: return "Avcilik";
        case 147: return "Silah Ticareti";
        case 251: return "Silah Ticareti";
        case 150:
        case 151:
        case 152:
        case 154:
        case 155:
        case 156:
        case 157:
        case 158:
        case 159:
        case 160:
        case 173:
        case 174: return "Silah";
        case 153:
        case 175: return "Saglik ve Zirh";
        case 162: return "Onemli Nokta";
        case 176: return "Yapi";
        case 184: return "Kamera";
        case 188: return "Kelepce";
        case 197: return "Yoga";
        case 198: return "Taksi";
        case 205: return "Kuculen Isaret";
        case 206: return "Epsilon Gorevi";
        case 225:
        case 226:
        case 426: return "Kisisel Arac";
        case 237: return "Hapishane";
        case 266: return "Lunapark";
        case 267:
        case 374:
        case 375: return "Isletme";
        case 269: return "Altruist Kampi";
        case 270:
        case 353: return "Dusman";
        case 273: return "Chop";
        case 274:
        case 364: return "Etkisiz";
        case 279: return "Fahise";
        case 280: return "Dost";
        case 303: return "Odul Hedefi";
        case 304:
        case 363:
        case 428: return "Gorev";
        case 305: return "Hayatta Kalma Gorevi";
        case 306:
        case 351:
        case 365:
        case 408: return "Paket";
        case 308: return "Denizalti";
        case 310:
        case 378:
        case 380:
        case 409: return "Olum Maci";
        case 311: return "Bilek Guresi";
        case 318: return "Cop Kamyonu";
        case 356:
        case 371: return "Marina";
        case 360:
        case 370: return "Helipad";
        case 359:
        case 372: return "Hangar";
        case 361:
        case 415: return "Benzin Bidonu";
        case 367: return "Tamamlandi";
        case 368: return "Roket";
        case 377: return "Parasutu";
        case 398:
        case 399: return "Olusturucu";
        case 404:
        case 410:
        case 427: return "Tekne";
        case 416: return "RP";
        case 421: return "Tank";
        case 430: return "Kronometre";
        default: return nullptr;
    }
}

void ApplyName(Blip blip, const std::string& name) {
    const std::string n = Ascii(name);
    UI::_SET_TEXT_ENTRY(const_cast<char*>("STRING"));
    UI::_ADD_TEXT_COMPONENT_STRING(const_cast<char*>(n.c_str()));
    UI::END_TEXT_COMMAND_SET_BLIP_NAME(blip);
}

}  // namespace

std::string Ascii(const std::string& s) {
    static const char* const map[][2] = {{"ç", "c"}, {"Ç", "C"}, {"ğ", "g"}, {"Ğ", "G"}, {"ı", "i"}, {"İ", "I"},
                                         {"ö", "o"}, {"Ö", "O"}, {"ş", "s"}, {"Ş", "S"}, {"ü", "u"}, {"Ü", "U"}};
    std::string out;
    for (size_t i = 0; i < s.size();) {
        bool hit = false;
        for (const auto& m : map) {
            const size_t n = std::char_traits<char>::length(m[0]);
            if (s.compare(i, n, m[0]) == 0) {
                out += m[1];
                i += n;
                hit = true;
                break;
            }
        }
        if (!hit) out += s[i++];
    }
    return out;
}

Blip Add(const Vector3& pos, int sprite, int colour, const std::string& name, float scale, bool short_range) {
    const Blip b = UI::ADD_BLIP_FOR_COORD(pos.x, pos.y, pos.z);
    UI::SET_BLIP_SPRITE(b, sprite);
    UI::SET_BLIP_COLOUR(b, colour);
    UI::SET_BLIP_SCALE(b, scale);
    UI::SET_BLIP_AS_SHORT_RANGE(b, short_range);
    SetName(b, name);
    return b;
}

void SetName(Blip blip, const std::string& name) {
    if (blip == 0 || name.empty() || !UI::DOES_BLIP_EXIST(blip)) return;
    g_named_blips.insert(blip);
    ApplyName(blip, name);
}

void Remove(Blip& b) {
    if (b != 0) {
        g_named_blips.erase(b);
        g_last_applied.erase(b);
        if (UI::DOES_BLIP_EXIST(b)) UI::REMOVE_BLIP(&b);
    }
    b = 0;
}

void Update() {
    const unsigned long long now = GetTickCount64();
    if (now - g_last_scan < kScanIntervalMs) return;
    g_last_scan = now;

    for (auto it = g_named_blips.begin(); it != g_named_blips.end();) {
        if (!UI::DOES_BLIP_EXIST(*it)) it = g_named_blips.erase(it);
        else ++it;
    }
    for (auto it = g_last_applied.begin(); it != g_last_applied.end();) {
        if (!UI::DOES_BLIP_EXIST(it->first)) it = g_last_applied.erase(it);
        else ++it;
    }

    for (int sprite = 1; sprite <= kMaxBlipSprite; ++sprite) {
        Blip blip = UI::GET_FIRST_BLIP_INFO_ID(sprite);
        int guard = 0;
        while (blip != 0 && UI::DOES_BLIP_EXIST(blip) && guard++ < 300) {
            if (g_named_blips.find(blip) == g_named_blips.end()) {
                const auto last = g_last_applied.find(blip);
                if (last == g_last_applied.end() || now - last->second >= kReapplyIntervalMs) {
                    const char* category = CategoryForSprite(UI::GET_BLIP_SPRITE(blip));
                    const std::string name = category ? category : "Harita Isareti " + std::to_string(sprite);
                    ApplyName(blip, name);
                    g_last_applied[blip] = now;
                }
            }
            blip = UI::GET_NEXT_BLIP_INFO_ID(sprite);
        }
    }
}

}  // namespace hikaye::blips
