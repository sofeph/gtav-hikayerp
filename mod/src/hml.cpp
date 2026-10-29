#include "hml.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace hikaye::hml {

namespace {

bool IsVoid(const std::string& t) { return t == "br" || t == "hr" || t == "else"; }

std::string Lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '&') {
            struct E { const char* name; char ch; };
            static const E es[] = {{"&lt;", '<'}, {"&gt;", '>'}, {"&amp;", '&'}, {"&quot;", '"'}, {"&apos;", '\''}};
            bool hit = false;
            for (const auto& e : es) {
                const size_t n = std::char_traits<char>::length(e.name);
                if (s.compare(i, n, e.name) == 0) {
                    out += e.ch;
                    i += n;
                    hit = true;
                    break;
                }
            }
            if (hit) continue;
        }
        out += s[i++];
    }
    return out;
}

// Ardışık boşlukları teke indirir; satır sonları boşluk olur (satır sonu için <br/>).
std::string Collapse(const std::string& s) {
    std::string out;
    bool space = false;
    for (unsigned char c : s) {
        if (std::isspace(c)) {
            space = true;
            continue;
        }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += static_cast<char>(c);
    }
    if (space && !out.empty()) out += ' ';
    return out;
}

std::string Trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

std::string FormatMoney(int v) {
    std::string d = std::to_string(v < 0 ? -v : v), g;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) g += '.';
        g += d[i];
    }
    return (v < 0 ? "-$" : "$") + g;
}

std::string Subst(const std::string& s, const Context& ctx) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '{') {
            const size_t e = s.find('}', i);
            if (e != std::string::npos && e - i < 64) {
                const std::string key = s.substr(i + 1, e - i - 1);
                if (key == "cash") {
                    out += FormatMoney(ctx.cash);
                    i = e + 1;
                    continue;
                }
                if (key == "hour") {
                    out += std::to_string(ctx.hour);
                    i = e + 1;
                    continue;
                }
                if (key.rfind("flag:", 0) == 0) {
                    if (ctx.flag) out += ctx.flag(key.substr(5));
                    i = e + 1;
                    continue;
                }
            }
        }
        out += s[i++];
    }
    return out;
}

std::string Attr(const Node& n, const char* k) {
    const auto it = n.attrs.find(k);
    return it == n.attrs.end() ? std::string() : it->second;
}


// ---- Stil (HSS): "seçici { özellik: değer; }" -- seçiciler: etiket, .sınıf, etiket.sınıf; özellikler: color bg border accent size align pad radius ----
struct Rule {
    std::string tag, cls;
    Style style;
    int accent = -1;
    bool has_color = false, has_bg = false, has_border = false, has_size = false, has_align = false, has_pad = false, has_radius = false;
};
struct Sheet {
    std::vector<Rule> rules;
};
const Sheet* g_sheet = nullptr;

int ParseColor(std::string v) {
    v = Trim(v);
    if (v.empty() || v[0] != '#') return -1;
    v = v.substr(1);
    if (v.size() == 3) v = std::string{v[0], v[0], v[1], v[1], v[2], v[2]};
    if (v.size() != 6) return -1;
    char* end = nullptr;
    const long x = std::strtol(v.c_str(), &end, 16);
    return (end && *end == 0) ? static_cast<int>(x) : -1;
}

void ApplyDecl(Rule& r, const std::string& prop, const std::string& val) {
    if (prop == "color") { r.style.color = ParseColor(val); r.has_color = true; }
    else if (prop == "bg" || prop == "background") { r.style.bg = ParseColor(val); r.has_bg = true; }
    else if (prop == "border") { r.style.border = ParseColor(val); r.has_border = true; }
    else if (prop == "accent") { r.accent = ParseColor(val); }
    else if (prop == "size") { r.style.size = static_cast<float>(std::atof(val.c_str())); r.has_size = true; }
    else if (prop == "pad" || prop == "padding") { r.style.pad = static_cast<float>(std::atof(val.c_str())); r.has_pad = true; }
    else if (prop == "radius") { r.style.radius = static_cast<float>(std::atof(val.c_str())); r.has_radius = true; }
    else if (prop == "align") { r.style.align = Trim(val) == "center" ? 1 : Trim(val) == "right" ? 2 : 0; r.has_align = true; }
}

void ParseDecls(Rule& r, const std::string& body) {
    size_t i = 0;
    while (i < body.size()) {
        const size_t semi = body.find(';', i);
        const std::string decl = body.substr(i, semi == std::string::npos ? std::string::npos : semi - i);
        const size_t colon = decl.find(':');
        if (colon != std::string::npos) ApplyDecl(r, Lower(Trim(decl.substr(0, colon))), Trim(decl.substr(colon + 1)));
        if (semi == std::string::npos) break;
        i = semi + 1;
    }
}

Sheet ParseSheet(std::string css) {
    Sheet s;
    for (size_t a; (a = css.find("/*")) != std::string::npos;) {  // yorumlar
        const size_t b = css.find("*/", a + 2);
        css.erase(a, b == std::string::npos ? std::string::npos : b + 2 - a);
    }
    size_t i = 0;
    while (i < css.size()) {
        const size_t open = css.find('{', i);
        if (open == std::string::npos) break;
        const size_t close = css.find('}', open);
        if (close == std::string::npos) break;
        const std::string sel_list = css.substr(i, open - i), body = css.substr(open + 1, close - open - 1);
        i = close + 1;
        size_t p = 0;
        while (p <= sel_list.size()) {  // "h1, .x" virgüllü seçiciler
            const size_t comma = sel_list.find(',', p);
            std::string sel = Lower(Trim(sel_list.substr(p, comma == std::string::npos ? std::string::npos : comma - p)));
            if (!sel.empty()) {
                Rule r;
                const size_t dot = sel.find('.');
                r.tag = dot == std::string::npos ? sel : sel.substr(0, dot);
                r.cls = dot == std::string::npos ? "" : sel.substr(dot + 1);
                ParseDecls(r, body);
                s.rules.push_back(std::move(r));
            }
            if (comma == std::string::npos) break;
            p = comma + 1;
        }
    }
    return s;
}

void Overlay(Style& st, const Rule& r) {
    if (r.has_color) st.color = r.style.color;
    if (r.has_bg) st.bg = r.style.bg;
    if (r.has_border) st.border = r.style.border;
    if (r.has_size) st.size = r.style.size;
    if (r.has_align) st.align = r.style.align;
    if (r.has_pad) st.pad = r.style.pad;
    if (r.has_radius) st.radius = r.style.radius;
}

// Etiket + sınıf + satır içi style="" için bileşik biçem (kademe: etiket < .sınıf < etiket.sınıf < satır içi).
Style StyleFor(const Node& n) {
    Style st;
    if (!g_sheet) {
        // satır içi style yine uygulanır
    } else {
        const std::string cls = Lower(Attr(n, "class"));
        for (int pass = 0; pass < 3; ++pass) {
            for (const auto& r : g_sheet->rules) {
                const bool tag_ok = r.tag.empty() || r.tag == n.tag;
                const bool cls_ok = r.cls.empty() || (!cls.empty() && (" " + cls + " ").find(" " + r.cls + " ") != std::string::npos);
                if (!tag_ok || !cls_ok) continue;
                const int level = (!r.cls.empty() ? (r.tag.empty() ? 1 : 2) : 0);
                if (level == pass) Overlay(st, r);
            }
        }
    }
    const std::string inl = Attr(n, "style");
    if (!inl.empty()) {
        Rule r;
        ParseDecls(r, inl);
        Overlay(st, r);
    }
    return st;
}

bool Cond(const Node& n, const Context& ctx) {
    auto state = [&](const std::string& id) { return ctx.mission_state ? ctx.mission_state(id) : std::string("locked"); };
    for (const auto& [k, v] : n.attrs) {
        if (k == "done" && state(v) != "done") return false;
        if (k == "notdone" && state(v) == "done") return false;
        if (k == "active" && state(v) != "active") return false;
        if (k == "available" && state(v) != "available") return false;
        if (k == "hour_ge" && ctx.hour < std::atoi(v.c_str())) return false;
        if (k == "hour_lt" && ctx.hour >= std::atoi(v.c_str())) return false;
        if (k == "cash_ge" && ctx.cash < std::atoi(v.c_str())) return false;
        if (k == "flag") {
            const std::string val = ctx.flag ? ctx.flag(v) : std::string();
            const auto eq = n.attrs.find("eq");
            const auto ne = n.attrs.find("ne");
            if (eq != n.attrs.end() && val != eq->second) return false;
            if (ne != n.attrs.end() && val == ne->second) return false;
            if (eq == n.attrs.end() && ne == n.attrs.end() && (val.empty() || val == "0")) return false;
        }
    }
    return true;
}

// <if> için seçilen dal: [begin, end) çocuk aralığı
void Branch(const Node& n, const Context& ctx, size_t& b, size_t& e) {
    size_t split = n.kids.size();
    for (size_t i = 0; i < n.kids.size(); ++i) {
        if (n.kids[i].tag == "else") {
            split = i;
            break;
        }
    }
    if (Cond(n, ctx)) {
        b = 0;
        e = split;
    } else {
        b = split < n.kids.size() ? split + 1 : n.kids.size();
        e = n.kids.size();
    }
}

std::string Flatten(const Node& n, const Context& ctx) {
    if (n.tag == "#text") return Subst(n.text, ctx);
    if (n.tag == "br") return "\n";
    if (n.tag == "if") {
        size_t b, e;
        Branch(n, ctx, b, e);
        std::string s;
        for (size_t i = b; i < e; ++i) s += Flatten(n.kids[i], ctx);
        return s;
    }
    std::string s;
    for (const auto& k : n.kids) s += Flatten(k, ctx);
    return s;
}

std::string Body(const Node& n, const Context& ctx) {
    std::string s = Trim(Flatten(n, ctx));
    // "\n" yalnızca <br/> kaynaklıdır (metin düğümleri zaten boşluğa indirildi)
    return s;
}


// ---- Çizim: <svg w h bg> içinde rect/circle/line/polyline/polygon/text; <chart type=bar|line|pie data="Etiket:değer,..."> ----
float Num(const Node& n, const char* k, const Context& ctx, float def = 0.f) {
    const std::string v = Attr(n, k);
    return v.empty() ? def : static_cast<float>(std::atof(Subst(v, ctx).c_str()));
}

int Paint(const Node& n, const char* k, const Context& ctx) {
    const std::string v = Subst(Attr(n, k), ctx);
    return v.empty() || v == "none" ? -1 : ParseColor(v);
}

std::vector<std::pair<float, float>> Points(const std::string& s) {
    std::vector<std::pair<float, float>> out;
    std::string cur;
    std::vector<float> nums;
    for (char c : s + " ") {
        if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) nums.push_back(static_cast<float>(std::atof(cur.c_str())));
            cur.clear();
        } else {
            cur += c;
        }
    }
    for (size_t i = 0; i + 1 < nums.size(); i += 2) out.push_back({nums[i], nums[i + 1]});
    return out;
}

Shape MakeShape(const Node& n, const Context& ctx) {
    Shape s;
    const std::string& t = n.tag;
    s.fill = Paint(n, "fill", ctx);
    s.stroke = Paint(n, "stroke", ctx);
    s.sw = Num(n, "sw", ctx, 1.f);
    if (t == "rect") {
        s.type = Shape::Type::Rect;
        s.x = Num(n, "x", ctx); s.y = Num(n, "y", ctx); s.w = Num(n, "w", ctx); s.h = Num(n, "h", ctx); s.r = Num(n, "rx", ctx);
    } else if (t == "circle") {
        s.type = Shape::Type::Circle;
        s.x = Num(n, "cx", ctx); s.y = Num(n, "cy", ctx); s.r = Num(n, "r", ctx);
    } else if (t == "line") {
        s.type = Shape::Type::Line;
        s.x = Num(n, "x1", ctx); s.y = Num(n, "y1", ctx); s.w = Num(n, "x2", ctx); s.h = Num(n, "y2", ctx);
        if (s.stroke < 0) s.stroke = s.fill;
    } else if (t == "polyline" || t == "polygon") {
        s.type = t == "polyline" ? Shape::Type::Polyline : Shape::Type::Polygon;
        s.pts = Points(Subst(Attr(n, "points"), ctx));
    } else if (t == "text") {
        s.type = Shape::Type::Text;
        s.x = Num(n, "x", ctx); s.y = Num(n, "y", ctx); s.size = Num(n, "size", ctx, 14.f);
        const std::string a = Attr(n, "anchor");
        s.align = a == "middle" ? 1 : a == "end" ? 2 : 0;
        s.text = Trim(Flatten(n, ctx));
        if (s.fill < 0) s.fill = 0xFFFFFF;
    }
    return s;
}

void Pie(std::vector<Shape>& out, float cx, float cy, float r, const std::vector<float>& vals, const std::vector<int>& cols) {
    float total = 0.f;
    for (float v : vals) total += std::max(0.f, v);
    if (total <= 0.f) return;
    float a0 = -1.5707963f;
    for (size_t i = 0; i < vals.size(); ++i) {
        const float sweep = 6.2831853f * std::max(0.f, vals[i]) / total;
        Shape s;
        s.type = Shape::Type::Polygon;
        s.fill = cols[i % cols.size()];
        s.pts.push_back({cx, cy});
        const int steps = std::max(2, static_cast<int>(sweep * 8));
        for (int k = 0; k <= steps; ++k) {
            const float a = a0 + sweep * static_cast<float>(k) / static_cast<float>(steps);
            s.pts.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
        }
        out.push_back(std::move(s));
        a0 += sweep;
    }
}

// <chart type="bar|line|pie" data="A:10,B:20" w="400" h="160" colors="#f00,#0f0" title="..."> : değerler dinamik olabilir ({flag:x})
Item MakeChart(const Node& n, const Context& ctx) {
    Item it;
    it.kind = Kind::Graphic;
    it.gw = Num(n, "w", ctx, 400.f);
    it.gh = Num(n, "h", ctx, 170.f);
    const std::string type = Lower(Attr(n, "type"));
    std::vector<std::string> labels;
    std::vector<float> vals;
    {
        const std::string d = Subst(Attr(n, "data"), ctx);
        size_t i = 0;
        while (i < d.size()) {
            const size_t comma = d.find(',', i);
            const std::string part = d.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
            const size_t colon = part.rfind(':');
            if (colon != std::string::npos) {
                labels.push_back(Trim(part.substr(0, colon)));
                vals.push_back(static_cast<float>(std::atof(part.substr(colon + 1).c_str())));
            }
            if (comma == std::string::npos) break;
            i = comma + 1;
        }
    }
    std::vector<int> cols;
    {
        const std::string cs = Attr(n, "colors");
        size_t i = 0;
        while (i < cs.size()) {
            const size_t comma = cs.find(',', i);
            cols.push_back(ParseColor(cs.substr(i, comma == std::string::npos ? std::string::npos : comma - i)));
            if (comma == std::string::npos) break;
            i = comma + 1;
        }
    }
    if (cols.empty()) cols = {0x4C8DFF, 0x4CC38A, 0xFFB84C, 0xFF6B6B, 0xA78BFA, 0x4CD7E6};
    for (auto& c : cols) if (c < 0) c = 0x888888;
    const int bg = Paint(n, "bg", ctx);
    it.gbg = bg;
    const float W = it.gw, H = it.gh;
    float vmax = 0.f;
    for (float v : vals) vmax = std::max(vmax, v);
    if (vmax <= 0.f) vmax = 1.f;
    const float left = 8.f, right = W - 8.f, top = 22.f, bottom = H - 22.f;
    const std::string title = Subst(Attr(n, "title"), ctx);
    if (!title.empty()) {
        Shape t;
        t.type = Shape::Type::Text;
        t.x = left; t.y = 4.f; t.size = 14.f; t.fill = 0xDDE3F0; t.text = title;
        it.shapes.push_back(std::move(t));
    }
    auto label_at = [&](float x, float y, const std::string& s, int align, float size = 11.f, int col = 0xAAB2C8) {
        Shape t;
        t.type = Shape::Type::Text;
        t.x = x; t.y = y; t.size = size; t.fill = col; t.align = align; t.text = s;
        it.shapes.push_back(std::move(t));
    };
    const size_t cnt = vals.size();
    if (type == "pie") {
        const float r = std::min(H - 34.f, W * 0.4f) * 0.5f;
        Pie(it.shapes, left + r + 6.f, H * 0.5f + 6.f, r, vals, cols);
        for (size_t i = 0; i < cnt; ++i) {  // gösterge
            Shape sw;
            sw.type = Shape::Type::Rect; sw.x = W * 0.55f; sw.y = 28.f + 20.f * static_cast<float>(i); sw.w = 12.f; sw.h = 12.f; sw.fill = cols[i % cols.size()];
            it.shapes.push_back(std::move(sw));
            label_at(W * 0.55f + 18.f, 27.f + 20.f * static_cast<float>(i), labels[i] + " " + std::to_string(static_cast<int>(vals[i])), 0, 12.f, 0xDDE3F0);
        }
    } else if (cnt > 0 && type == "line") {
        Shape axis;
        axis.type = Shape::Type::Line; axis.x = left; axis.y = bottom; axis.w = right; axis.h = bottom; axis.stroke = 0x555D75;
        it.shapes.push_back(std::move(axis));
        Shape ln;
        ln.type = Shape::Type::Polyline; ln.stroke = cols[0]; ln.sw = 2.f;
        for (size_t i = 0; i < cnt; ++i) {
            const float x = cnt == 1 ? (left + right) * 0.5f : left + (right - left) * static_cast<float>(i) / static_cast<float>(cnt - 1);
            const float y = bottom - (bottom - top) * vals[i] / vmax;
            ln.pts.push_back({x, y});
        }
        it.shapes.push_back(ln);
        for (size_t i = 0; i < cnt; ++i) {
            Shape dot;
            dot.type = Shape::Type::Circle; dot.x = ln.pts[i].first; dot.y = ln.pts[i].second; dot.r = 3.f; dot.fill = cols[0];
            it.shapes.push_back(std::move(dot));
            label_at(ln.pts[i].first, bottom + 4.f, labels[i], 1);
        }
    } else if (cnt > 0) {  // bar
        const float slot = (right - left) / static_cast<float>(cnt);
        for (size_t i = 0; i < cnt; ++i) {
            Shape b;
            b.type = Shape::Type::Rect;
            const float bh = (bottom - top) * vals[i] / vmax;
            b.x = left + slot * static_cast<float>(i) + slot * 0.18f; b.w = slot * 0.64f; b.y = bottom - bh; b.h = bh; b.r = 3.f;
            b.fill = cols[i % cols.size()];
            it.shapes.push_back(b);
            label_at(b.x + b.w * 0.5f, b.y - 14.f, std::to_string(static_cast<int>(vals[i])), 1, 11.f, 0xDDE3F0);
            label_at(b.x + b.w * 0.5f, bottom + 4.f, labels[i], 1);
        }
    }
    return it;
}

void Emit(const Node& n, const Context& ctx, Page& page);

void EmitKids(const Node& n, const Context& ctx, Page& page, size_t b, size_t e) {
    std::string loose;  // blok olmayan açık metin
    auto flush = [&]() {
        const std::string t = Trim(loose);
        if (!t.empty()) page.items.push_back({Kind::Text, t, "", "", "", false});
        loose.clear();
    };
    static const char* blocks[] = {"h1", "h2", "p", "link", "card", "post", "hr", "list", "item", "if", "page", "div", "source", "svg", "chart", "tile"};
    for (size_t i = b; i < e; ++i) {
        const Node& k = n.kids[i];
        bool is_block = false;
        for (const char* t : blocks) is_block |= k.tag == t;
        if (is_block) {
            flush();
            Emit(k, ctx, page);
        } else {
            loose += Flatten(k, ctx);
        }
    }
    flush();
}

void Emit(const Node& n, const Context& ctx, Page& page) {
    const std::string& t = n.tag;
    const size_t before = page.items.size();
    struct Tag { Page& p; size_t b; const Node& n; ~Tag() { if (n.tag == "if" || n.tag == "page" || n.tag == "div" || n.tag == "list" || n.tag == "source") return; const Style s = StyleFor(n); for (size_t i = b; i < p.items.size(); ++i) p.items[i].style = s; } } tag{page, before, n};
    if (t == "if") {
        size_t b, e;
        Branch(n, ctx, b, e);
        EmitKids(n, ctx, page, b, e);
    } else if (t == "chart") {
        page.items.push_back(MakeChart(n, ctx));
    } else if (t == "svg") {
        Item it;
        it.kind = Kind::Graphic;
        it.gw = Num(n, "w", ctx, 400.f);
        it.gh = Num(n, "h", ctx, 160.f);
        it.gbg = Paint(n, "bg", ctx);
        it.spin = Attr(n, "spin") == "true";
        std::function<void(const Node&)> walk = [&](const Node& c) {
            for (const auto& k : c.kids) {
                if (k.tag == "if") {
                    size_t b, e;
                    Branch(k, ctx, b, e);
                    Node tmp;
                    tmp.kids.assign(k.kids.begin() + static_cast<long>(b), k.kids.begin() + static_cast<long>(e));
                    walk(tmp);
                } else if (k.tag == "rect" || k.tag == "circle" || k.tag == "line" || k.tag == "polyline" || k.tag == "polygon" || k.tag == "text") {
                    it.shapes.push_back(MakeShape(k, ctx));
                } else if (k.tag == "g") {
                    walk(k);
                }
            }
        };
        walk(n);
        page.items.push_back(std::move(it));
    } else if (t == "source") {
        if (ctx.source) {
            for (auto& it : ctx.source(Attr(n, "name"))) page.items.push_back(std::move(it));
        }
    } else if (t == "page" || t == "div" || t == "list") {
        if (t == "page") page.title = Subst(Attr(n, "title"), ctx);
        EmitKids(n, ctx, page, 0, n.kids.size());
    } else if (t == "h1") {
        page.items.push_back({Kind::H1, Body(n, ctx), "", "", "", false});
    } else if (t == "h2") {
        page.items.push_back({Kind::H2, Body(n, ctx), "", "", "", false});
    } else if (t == "p") {
        const std::string s = Body(n, ctx);
        if (!s.empty()) page.items.push_back({Kind::Text, s, "", "", "", false});
    } else if (t == "hr") {
        page.items.push_back({Kind::Rule, "", "", "", "", false});
    } else if (t == "item") {
        page.items.push_back({Kind::Bullet, Body(n, ctx), "", "", "", false});
    } else if (t == "link") {
        const std::string href = Attr(n, "href");
        page.items.push_back({Kind::Link, Body(n, ctx), "", "", href, !href.empty()});
    } else if (t == "tile") {
        const std::string href = Attr(n, "href");
        page.items.push_back({Kind::Tile, Body(n, ctx), Subst(Attr(n, "icon"), ctx), "", href, !href.empty()});
        const int col = Paint(n, "color", ctx);
        if (col >= 0) page.items.back().style.bg = col;
    } else if (t == "card") {
        const std::string href = Attr(n, "href");
        page.items.push_back({Kind::Card, Subst(Attr(n, "title"), ctx), Body(n, ctx),
                              Subst(Attr(n, "price"), ctx), href, !href.empty()});
    } else if (t == "post") {
        std::string handle = Attr(n, "handle");
        const std::string time = Attr(n, "time");
        std::string sub = (handle.empty() ? "" : "@" + handle) + (time.empty() ? "" : "  ·  " + time);
        page.items.push_back({Kind::Post, Body(n, ctx), Subst(sub, ctx), Subst(Attr(n, "user"), ctx), Attr(n, "href"),
                              !Attr(n, "href").empty()});
    }
}

}  // namespace

bool Parse(const std::string& src, Node& root, std::string& err) {
    root = Node{};
    root.tag = "#root";
    std::vector<Node*> stack{&root};
    std::string text;
    auto flush_text = [&]() {
        if (text.empty()) return;
        const std::string c = Collapse(Decode(text));
        text.clear();
        if (c.empty()) return;
        Node n;
        n.tag = "#text";
        n.text = c;
        stack.back()->kids.push_back(std::move(n));
    };
    size_t i = 0;
    while (i < src.size()) {
        if (src[i] != '<') {
            text += src[i++];
            continue;
        }
        if (src.compare(i, 4, "<!--") == 0) {
            const size_t e = src.find("-->", i + 4);
            if (e == std::string::npos) {
                err = "kapanmamis yorum";
                return false;
            }
            i = e + 3;
            continue;
        }
        const size_t gt = src.find('>', i);
        if (gt == std::string::npos) {
            err = "kapanmamis etiket";
            return false;
        }
        flush_text();
        std::string inner = src.substr(i + 1, gt - i - 1);
        i = gt + 1;
        if (!inner.empty() && inner[0] == '/') {  // kapanış
            const std::string name = Lower(Trim(inner.substr(1)));
            size_t depth = stack.size();
            while (depth > 1 && stack[depth - 1]->tag != name) --depth;
            if (depth > 1) stack.resize(depth - 1);  // eşleşen yoksa yok say
            continue;
        }
        bool self_close = false;
        if (!inner.empty() && inner.back() == '/') {
            self_close = true;
            inner.pop_back();
        }
        size_t p = 0;
        while (p < inner.size() && !std::isspace(static_cast<unsigned char>(inner[p]))) ++p;
        Node n;
        n.tag = Lower(inner.substr(0, p));
        while (p < inner.size()) {  // nitelikler: ad="değer" veya ad='değer' veya ad
            while (p < inner.size() && std::isspace(static_cast<unsigned char>(inner[p]))) ++p;
            size_t s = p;
            while (p < inner.size() && inner[p] != '=' && !std::isspace(static_cast<unsigned char>(inner[p]))) ++p;
            const std::string name = Lower(inner.substr(s, p - s));
            if (name.empty()) break;
            std::string val;
            while (p < inner.size() && std::isspace(static_cast<unsigned char>(inner[p]))) ++p;
            if (p < inner.size() && inner[p] == '=') {
                ++p;
                while (p < inner.size() && std::isspace(static_cast<unsigned char>(inner[p]))) ++p;
                if (p < inner.size() && (inner[p] == '"' || inner[p] == '\'')) {
                    const char q = inner[p++];
                    const size_t e = inner.find(q, p);
                    val = inner.substr(p, e == std::string::npos ? std::string::npos : e - p);
                    p = e == std::string::npos ? inner.size() : e + 1;
                } else {
                    s = p;
                    while (p < inner.size() && !std::isspace(static_cast<unsigned char>(inner[p]))) ++p;
                    val = inner.substr(s, p - s);
                }
            }
            n.attrs[name] = Decode(val);
        }
        Node& slot = (stack.back()->kids.push_back(std::move(n)), stack.back()->kids.back());
        if (!self_close && !IsVoid(slot.tag)) stack.push_back(&slot);
    }
    flush_text();
    return true;
}

bool Matches(const std::map<std::string, std::string>& conds, const Context& ctx) {
    Node n;
    n.tag = "if";
    n.attrs = conds;
    return Cond(n, ctx);
}

Page Render(const Node& root, const Context& ctx) {
    Page page;
    Sheet sheet;
    g_sheet = nullptr;
    for (const auto& k : root.kids) {
        if (k.tag != "page") continue;
        const std::string name = Attr(k, "style");
        if (!name.empty() && ctx.css) {
            const std::string css = ctx.css(name);
            if (!css.empty()) {
                sheet = ParseSheet(css);
                g_sheet = &sheet;
                for (const auto& r : sheet.rules) {  // page { bg, color, accent }
                    if (r.tag == "page" && r.cls.empty()) {
                        if (r.has_bg) page.bg = r.style.bg;
                        if (r.has_color) page.color = r.style.color;
                        if (r.accent >= 0) page.accent = r.accent;
                    }
                }
            }
        }
        break;
    }
    EmitKids(root, ctx, page, 0, root.kids.size());
    g_sheet = nullptr;
    return page;
}

}  // namespace hikaye::hml
