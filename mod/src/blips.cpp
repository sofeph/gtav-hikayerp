#include "blips.h"

namespace hikaye::blips {

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
    if (!name.empty()) {
        const std::string n = Ascii(name);
        UI::_SET_TEXT_ENTRY(const_cast<char*>("STRING"));
        UI::_ADD_TEXT_COMPONENT_STRING(const_cast<char*>(n.c_str()));
        UI::END_TEXT_COMMAND_SET_BLIP_NAME(b);
    }
    return b;
}

void Remove(Blip& b) {
    if (b != 0 && UI::DOES_BLIP_EXIST(b)) UI::REMOVE_BLIP(&b);
    b = 0;
}

}  // namespace hikaye::blips
