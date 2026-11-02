#include "svg_icon.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "common.h"

namespace hikaye::svg_icon {

namespace {

constexpr int kSuperSample = 4;    // piksel başına 4x4 örnek: yumuşak kenarlar
constexpr int kQuadSteps = 8;      // eğri başına düz parça sayısı
constexpr int kCubicSteps = 12;
constexpr float kPaddingRatio = 0.04f;

struct Pt {
    float x = 0.f, y = 0.f;
};
using Contour = std::vector<Pt>;

// Yerel ayardan bağımsız sayı okuyucu (strtof Türkçe yerel ayarda virgül bekleyebilir)
class Reader {
public:
    explicit Reader(const std::string& s) : s_(s) {}

    void SkipSeparators() {
        while (i_ < s_.size() && (std::isspace(static_cast<unsigned char>(s_[i_])) || s_[i_] == ',')) ++i_;
    }
    bool AtEnd() {
        SkipSeparators();
        return i_ >= s_.size();
    }
    bool AtCommand() {
        SkipSeparators();
        return i_ < s_.size() && std::isalpha(static_cast<unsigned char>(s_[i_])) && s_[i_] != 'e' && s_[i_] != 'E';
    }
    char TakeCommand() { return s_[i_++]; }

    bool Number(float& out) {
        SkipSeparators();
        size_t j = i_;
        double sign = 1.0, value = 0.0;
        if (j < s_.size() && (s_[j] == '+' || s_[j] == '-')) sign = s_[j++] == '-' ? -1.0 : 1.0;
        bool digits = false;
        while (j < s_.size() && std::isdigit(static_cast<unsigned char>(s_[j]))) {
            value = value * 10.0 + (s_[j++] - '0');
            digits = true;
        }
        if (j < s_.size() && s_[j] == '.') {
            ++j;
            double scale = 0.1;
            while (j < s_.size() && std::isdigit(static_cast<unsigned char>(s_[j]))) {
                value += (s_[j++] - '0') * scale;
                scale *= 0.1;
                digits = true;
            }
        }
        if (!digits) return false;
        if (j < s_.size() && (s_[j] == 'e' || s_[j] == 'E')) {
            size_t k = j + 1;
            int esign = 1, exp = 0;
            if (k < s_.size() && (s_[k] == '+' || s_[k] == '-')) esign = s_[k++] == '-' ? -1 : 1;
            if (k < s_.size() && std::isdigit(static_cast<unsigned char>(s_[k]))) {
                while (k < s_.size() && std::isdigit(static_cast<unsigned char>(s_[k]))) exp = exp * 10 + (s_[k++] - '0');
                value *= std::pow(10.0, esign * exp);
                j = k;
            }
        }
        out = static_cast<float>(sign * value);
        i_ = j;
        return true;
    }
    bool Point(Pt& p) { return Number(p.x) && Number(p.y); }

private:
    const std::string& s_;
    size_t i_ = 0;
};

void Quad(Contour& c, Pt p0, Pt p1, Pt p2) {
    for (int k = 1; k <= kQuadSteps; ++k) {
        const float t = static_cast<float>(k) / kQuadSteps, u = 1.f - t;
        c.push_back({u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x, u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y});
    }
}

void Cubic(Contour& c, Pt p0, Pt p1, Pt p2, Pt p3) {
    for (int k = 1; k <= kCubicSteps; ++k) {
        const float t = static_cast<float>(k) / kCubicSteps, u = 1.f - t;
        const float a = u * u * u, b = 3 * u * u * t, cc = 3 * u * t * t, d = t * t * t;
        c.push_back({a * p0.x + b * p1.x + cc * p2.x + d * p3.x, a * p0.y + b * p1.y + cc * p2.y + d * p3.y});
    }
}

// SVG path verisini kapalı çokgenlere çevirir. Desteklenen: M L H V Q T C S Z (büyük/küçük harf). Yay (A) yok.
bool ParsePath(const std::string& d, std::vector<Contour>& out) {
    Reader r(d);
    Pt cur, start, ctrl;
    char cmd = 0, prev = 0;
    Contour c;
    auto flush = [&]() {
        if (c.size() >= 3) out.push_back(std::move(c));
        c.clear();
    };
    while (!r.AtEnd()) {
        if (r.AtCommand()) cmd = r.TakeCommand();
        else if (!cmd) return false;  // komutsuz sayı
        const bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
        const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));
        const Pt base = rel ? cur : Pt{};
        auto abs = [&](Pt p) { return Pt{p.x + base.x, p.y + base.y}; };
        Pt p1, p2, p3;
        switch (up) {
            case 'Z':
                flush();
                cur = start;
                cmd = 0;  // Z'den sonra yeni komut gelmeli
                break;
            case 'M':
                if (!r.Point(p1)) return false;
                flush();
                cur = start = abs(p1);
                c.push_back(cur);
                cmd = rel ? 'l' : 'L';  // devam eden çiftler çizgi sayılır
                break;
            case 'L':
                if (!r.Point(p1)) return false;
                cur = abs(p1);
                c.push_back(cur);
                break;
            case 'H':
                if (!r.Number(p1.x)) return false;
                cur.x = rel ? cur.x + p1.x : p1.x;
                c.push_back(cur);
                break;
            case 'V':
                if (!r.Number(p1.y)) return false;
                cur.y = rel ? cur.y + p1.y : p1.y;
                c.push_back(cur);
                break;
            case 'Q':
                if (!r.Point(p1) || !r.Point(p2)) return false;
                ctrl = abs(p1);
                Quad(c, cur, ctrl, abs(p2));
                cur = abs(p2);
                break;
            case 'T':
                if (!r.Point(p2)) return false;
                ctrl = (prev == 'Q' || prev == 'T') ? Pt{2 * cur.x - ctrl.x, 2 * cur.y - ctrl.y} : cur;
                Quad(c, cur, ctrl, abs(p2));
                cur = abs(p2);
                break;
            case 'C':
                if (!r.Point(p1) || !r.Point(p2) || !r.Point(p3)) return false;
                ctrl = abs(p2);
                Cubic(c, cur, abs(p1), ctrl, abs(p3));
                cur = abs(p3);
                break;
            case 'S': {
                if (!r.Point(p2) || !r.Point(p3)) return false;
                const Pt first = (prev == 'C' || prev == 'S') ? Pt{2 * cur.x - ctrl.x, 2 * cur.y - ctrl.y} : cur;
                ctrl = abs(p2);
                Cubic(c, cur, first, ctrl, abs(p3));
                cur = abs(p3);
                break;
            }
            default:
                return false;
        }
        prev = up;
    }
    flush();
    return !out.empty();
}

std::string ReadPathData(const std::string& name) {
    // İkon adları HML sayfalarından da gelir: yalnızca güvenli karakterler (yol dışına çıkılamaz)
    if (name.empty() || name.size() > 64) return {};
    for (char ch : name) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-')) return {};
    }
    std::ifstream in(std::filesystem::u8path(std::string(kBaseDir) + "/data/icons/" + name + ".svg"), std::ios::binary);
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string svg = ss.str();

    std::string d;
    for (size_t tag = svg.find("<path"); tag != std::string::npos; tag = svg.find("<path", tag + 5)) {
        const size_t end = svg.find('>', tag);
        const size_t attr = svg.find(" d=\"", tag);
        if (attr == std::string::npos || attr > end) continue;
        const size_t a = attr + 4, b = svg.find('"', a);
        if (b == std::string::npos) break;
        d += svg.substr(a, b - a);
        d += ' ';
    }
    return d;
}

}  // namespace

bool RasterizePath(const std::string& d, int size, Mask& out) {
    if (size <= 0 || size > 1024) return false;
    std::vector<Contour> contours;
    if (!ParsePath(d, contours)) return false;

    float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
    for (const auto& c : contours) {
        for (const auto& p : c) {
            minx = std::min(minx, p.x);
            miny = std::min(miny, p.y);
            maxx = std::max(maxx, p.x);
            maxy = std::max(maxy, p.y);
        }
    }
    const float bw = maxx - minx, bh = maxy - miny;
    if (bw <= 0.f || bh <= 0.f) return false;

    // Kareye sığdır ve ortala (ikonların genişlikleri farklı: 448, 512, 576 ...)
    const float pad = size * kPaddingRatio, inner = size - 2.f * pad;
    const float scale = inner / std::max(bw, bh);
    const float ox = pad + (inner - bw * scale) * 0.5f - minx * scale;
    const float oy = pad + (inner - bh * scale) * 0.5f - miny * scale;

    struct Edge {
        float x0, y0, x1, y1;
        int dir;
    };
    std::vector<Edge> edges;
    for (const auto& c : contours) {
        for (size_t i = 0; i < c.size(); ++i) {
            const Pt a = c[i], b = c[(i + 1) % c.size()];
            const float ay = a.y * scale + oy, by = b.y * scale + oy;
            if (ay == by) continue;
            edges.push_back({a.x * scale + ox, ay, b.x * scale + ox, by, by > ay ? 1 : -1});
        }
    }

    // Tarama çizgisi + nonzero sarım kuralı: iç içe konturlar (dişlinin ortası gibi) boş kalır
    std::vector<uint16_t> acc(static_cast<size_t>(size) * size, 0);
    struct Crossing {
        float x;
        int dir;
    };
    std::vector<Crossing> xs;
    const int sub_w = size * kSuperSample;
    for (int sy = 0; sy < size * kSuperSample; ++sy) {
        const float y = (sy + 0.5f) / kSuperSample;
        xs.clear();
        for (const auto& e : edges) {
            const float lo = std::min(e.y0, e.y1), hi = std::max(e.y0, e.y1);
            if (y < lo || y >= hi) continue;
            xs.push_back({e.x0 + (y - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0), e.dir});
        }
        std::sort(xs.begin(), xs.end(), [](const Crossing& a, const Crossing& b) { return a.x < b.x; });
        uint16_t* row = &acc[static_cast<size_t>(sy / kSuperSample) * size];
        int winding = 0;
        float span_start = 0.f;
        for (const auto& cr : xs) {
            const int before = winding;
            winding += cr.dir;
            if (before == 0 && winding != 0) {
                span_start = cr.x;
            } else if (before != 0 && winding == 0) {
                const int sx0 = std::max(0, static_cast<int>(std::ceil(span_start * kSuperSample - 0.5f)));
                const int sx1 = std::min(sub_w, static_cast<int>(std::ceil(cr.x * kSuperSample - 0.5f)));
                for (int sx = sx0; sx < sx1; ++sx) ++row[sx / kSuperSample];
            }
        }
    }

    out.size = size;
    out.alpha.resize(acc.size());
    constexpr int kSamples = kSuperSample * kSuperSample;
    for (size_t i = 0; i < acc.size(); ++i) out.alpha[i] = static_cast<uint8_t>(std::min(255, acc[i] * 255 / kSamples));
    return true;
}

bool Rasterize(const std::string& name, int size, Mask& out) {
    const std::string d = ReadPathData(name);
    return !d.empty() && RasterizePath(d, size, out);
}

}  // namespace hikaye::svg_icon
