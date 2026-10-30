#pragma once
#include <string>

#include "common.h"

namespace hikaye::blips {

// Harita işareti (işletme ikonları) yardımcıları. Oyunun harita yazı tipi Türkçe harfleri çizemediği için ad ASCII'ye çevrilir.
Blip Add(const Vector3& pos, int sprite, int colour, const std::string& name, float scale, bool short_range);
void SetName(Blip blip, const std::string& name);
void Remove(Blip& b);
std::string Ascii(const std::string& s);
// Periodically categorizes active game blips; blips explicitly named by this mod are left unchanged.
void Update();

}  // namespace hikaye::blips
