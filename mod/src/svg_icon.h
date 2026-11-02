#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Telefon ikonları: data/icons/<ad>.svg (tools/fa_to_svg.py ile Font Awesome'dan üretilir) okunur ve
// kendi rasterleştiricimizle alfa maskesine çizilir. Font gerekmez; maske overlay'de dokuya yüklenir.
namespace hikaye::svg_icon {

struct Mask {
    int size = 0;                 // size x size piksel
    std::vector<uint8_t> alpha;   // 0..255 kaplama
};

// İkonu size x size kareye ortalayıp sığdırarak çizer (çift kenarlı/nonzero dolgu: delikler doğru çıkar).
// Dosya yoksa ya da path okunamazsa false.
bool Rasterize(const std::string& name, int size, Mask& out);

// SVG path "d" verisini maskeye çizer (dosyadan bağımsız; test ve diğer kaynaklar için).
bool RasterizePath(const std::string& d, int size, Mask& out);

}  // namespace hikaye::svg_icon
