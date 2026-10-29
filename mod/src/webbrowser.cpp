#include "webbrowser.h"

#include <windows.h>

#include <cctype>
#include <fstream>
#include <sstream>

#include "common.h"
#include "overlay.h"
#include "textinput.h"
#include "addons.h"
#include "transit.h"
#include "phoneos.h"
#include "pcos.h"

#include <algorithm>
#include <filesystem>

namespace hikaye {

namespace {
// href -> "site/sayfa" (küçük harf, sadece güvenli karakterler, ".." yok). Boş dönerse geçersiz.
std::string Normalize(std::string href) {
    for (auto& c : href) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (href.rfind("hml://", 0) == 0) href = href.substr(6);
    if (href == "home" || href.empty()) return "home";
    for (char c : href) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '/' || c == '-' || c == '_')) return "";
    }
    if (href.find("..") != std::string::npos || href[0] == '/') return "";
    if (href.find('/') == std::string::npos) href += "/index";
    if (href.back() == '/') href += "index";
    return href;
}

std::string Lower(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// Türkçe harfleri sadeleştirir (arama büyük/küçük ve ç/ğ/ı/ö/ş/ü duyarsız olsun). UTF-8 iki baytlı karşılıklar.
std::string Fold(const std::string& in) {
    static const char* const map[][2] = {{"ç", "c"}, {"Ç", "c"}, {"ğ", "g"}, {"Ğ", "g"}, {"ı", "i"}, {"İ", "i"},
                                         {"ö", "o"}, {"Ö", "o"}, {"ş", "s"}, {"Ş", "s"}, {"ü", "u"}, {"Ü", "u"}};
    std::string out;
    for (size_t i = 0; i < in.size();) {
        bool hit = false;
        for (const auto& m : map) {
            const size_t n = std::char_traits<char>::length(m[0]);
            if (in.compare(i, n, m[0]) == 0) {
                out += m[1];
                i += n;
                hit = true;
                break;
            }
        }
        if (!hit) out += static_cast<char>(std::tolower(static_cast<unsigned char>(in[i++])));
    }
    return out;
}

std::string Esc(const std::string& s) {
    std::string o;
    for (char c : s) o += c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '&' ? "&amp;" : c == '"' ? "&quot;" : std::string(1, c);
    return o;
}

std::string ReadFile(const std::string& path);

// data/web/<rel> önce, yoksa eklentilerin web/<rel> dosyası
std::string ReadWeb(const std::string& rel) {
    std::string s = ReadFile(std::string(kBaseDir) + "/data/web/" + rel);
    if (!s.empty()) return s;
    for (const auto& d : addons::Dirs()) {
        s = ReadFile(d + "/web/" + rel);
        if (!s.empty()) return s;
    }
    return "";
}

// Sayfa kaynağı: önce dosya, yoksa durum/oyun verisinden üretilen sayfa (LST/Dashound durak ve hat sayfaları)
std::string LoadPage(const std::string& url) {
    std::string s = ReadWeb(url + ".hml");
    if (s.empty()) s = transit::BuildPage(url);
    if (s.empty()) s = phoneos::BuildPage(url);  // telefon ana ekranı ve uygulamaları
    if (s.empty()) s = pcos::BuildPage(url);     // bilgisayar masaüstü ve uygulamaları
    return s;
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
}  // namespace

void WebBrowser::Open(const std::string& root) {
    active_ = true;
    history_.clear();
    url_.clear();
    sel_ = -1;
    typing_ = false;
    BuildIndex();
    ask_cmd_.clear();
    hml::Context empty;
    Navigate(root, true, empty);
}

void WebBrowser::ApplyAction(const std::string& cmd, const hml::Context& ctx) {
    std::string r = ctx.action ? ctx.action(cmd) : std::string();
    if (r.rfind("@goto:", 0) == 0) {  // eylem sayfa değiştirir: "@goto:url|bildirim"
        const size_t bar = r.find('|');
        const std::string url = r.substr(6, bar == std::string::npos ? std::string::npos : bar - 6);
        r = bar == std::string::npos ? std::string() : r.substr(bar + 1);
        Navigate(url, true, ctx);
    } else {
        RefreshPhonePage(ctx);
    }
    notice_ = r;
    notice_until_ = GetTickCount64() + 5000;
    Rebuild(ctx);
}

// Telefon sayfaları canlıdır (pil, konum, kişi listesi): kaynak yeniden üretilir
void WebBrowser::RefreshPhonePage(const hml::Context& ctx) {
    (void)ctx;
    // Operatör siteleri dinamik (canlı kota/sinyal gösterimi); phone/ ve pc/ sayfaları da canlıdır.
    const bool is_op = url_ == "whiz.com" || url_ == "grahamtelecom.com" || url_ == "modatel.com" ||
                       url_.rfind("whiz.com/", 0) == 0 || url_.rfind("grahamtelecom.com/", 0) == 0 || url_.rfind("modatel.com/", 0) == 0;
    if (url_.rfind("phone/", 0) != 0 && url_.rfind("pc/", 0) != 0 && !is_op) return;
    const std::string src = LoadPage(url_);
    hml::Node root;
    std::string err;
    if (!src.empty() && hml::Parse(src, root, err)) root_ = std::move(root);
    phone_src_at_ = GetTickCount64();
}

void WebBrowser::Activate(const hml::Context& ctx) {
    if (sel_ < 0 || sel_ >= static_cast<int>(page_.items.size())) return;
    const std::string href = page_.items[static_cast<size_t>(sel_)].href;
    if (href.rfind("act:", 0) == 0) {  // oyun eylemi (satın al, sat, işaretle...): sayfa değişmez, sonuç bildirilir
        ApplyAction(href.substr(4), ctx);
    } else if (href.rfind("askact:", 0) == 0) {  // yazı iste (numara, kişi, yer), sonra eylemi çalıştır
        ask_cmd_ = href.substr(7);
        typing_ = true;
        textinput::Begin();
        notice_ = "Yazıp Enter'a bas (iptal: Esc)";
        notice_until_ = GetTickCount64() + 6000;
    } else {
        Navigate(href, true, ctx);
    }
}

// Fare imleci: oyunun imleci (kontrol 239/240) ya da yedek olarak işletim sistemi imleci
void WebBrowser::ReadCursor() {
    UI::_SHOW_CURSOR_THIS_FRAME();
    float x = CONTROLS::GET_DISABLED_CONTROL_NORMAL(0, 239), y = CONTROLS::GET_DISABLED_CONTROL_NORMAL(0, 240);
    if (x == 0.f && y == 0.f) {
        POINT pt;
        RECT rc;
        const HWND hw = GetForegroundWindow();
        if (hw && GetCursorPos(&pt) && ScreenToClient(hw, &pt) && GetClientRect(hw, &rc) && rc.right > 0 && rc.bottom > 0) {
            x = static_cast<float>(pt.x) / static_cast<float>(rc.right);
            y = static_cast<float>(pt.y) / static_cast<float>(rc.bottom);
        }
    }
    cur_x_ = std::max(0.f, std::min(1.f, x));
    cur_y_ = std::max(0.f, std::min(1.f, y));
}

void WebBrowser::OpenAt(const std::string& href, const hml::Context& ctx) {
    Open();
    Navigate(href, true, ctx);
}

int WebBrowser::NextSelectable(int from, int dir) const {
    const int n = static_cast<int>(page_.items.size());
    for (int i = from + dir; i >= 0 && i < n; i += dir) {
        if (page_.items[static_cast<size_t>(i)].selectable) return i;
    }
    return -1;
}

void WebBrowser::Navigate(const std::string& href, bool push, const hml::Context& ctx) {
    if (href.rfind("ara:", 0) == 0) {  // Eyefind sonuç sayfası (sentetik)
        Search(href.substr(4));
        if (push) history_.push_back("ara:" + query_);
        sel_ = -1;
        Rebuild(ctx);
        return;
    }
    const std::string url = Normalize(href);
    // Şebeke kotası: telefon/bilgisayar uygulama sayfaları hariç her internet sayfası 1 kota düşürür.
    // Operator siteleri intranet: ConsumeInternetPage içinde muaf tutulur.
    const bool is_app = !url.empty() && (url.rfind("phone/", 0) == 0 || url.rfind("pc/", 0) == 0 || url == "home");
    if (!is_app && !url.empty()) phoneos::ConsumeInternetPage(url);
    std::string src = url.empty() ? "" : LoadPage(url);
    std::string err;
    hml::Node root;
    if (src.empty() || !hml::Parse(src, root, err)) {
        root = hml::Node{};
        std::string fallback = "<page title=\"Hata\"><h1>Sayfa bulunamadi</h1><p>" + (url.empty() ? href : url) +
                               "</p><link href=\"home\">Ana sayfa</link></page>";
        hml::Parse(fallback, root, err);
        if (!url.empty()) Log("web: sayfa yok/bozuk: " + url + (err.empty() ? "" : " (" + err + ")"));
    }
    root_ = std::move(root);
    url_ = url.empty() ? "hata" : url;
    if (push) history_.push_back(url_);
    sel_ = -1;
    Rebuild(ctx);
}

void WebBrowser::BuildIndex() {
    index_.clear();
    try {
        std::vector<std::filesystem::path> bases{std::filesystem::path(kBaseDir) / "data" / "web"};
        for (const auto& d : addons::Dirs()) bases.push_back(std::filesystem::path(d) / "web");  // eklenti siteleri de aranabilir
        for (const auto& base : bases) {
        std::error_code ec;
        if (!std::filesystem::exists(base, ec)) continue;
        for (const auto& e : std::filesystem::recursive_directory_iterator(base)) {
            if (!e.is_regular_file() || e.path().extension() != ".hml") continue;
            std::string rel = std::filesystem::relative(e.path(), base).generic_string();
            rel.resize(rel.size() - 4);
            if (rel == "home" || rel.rfind("eyefind.com/", 0) == 0) continue;
            const std::string src = ReadFile(e.path().string());
            hml::Node root;
            std::string err;
            if (!hml::Parse(src, root, err)) continue;
            Doc d;
            d.url = rel;
            for (const auto& k : root.kids) {
                if (k.tag != "page") continue;
                const auto t = k.attrs.find("title");
                d.title = t == k.attrs.end() ? rel : t->second;
                const auto kw = k.attrs.find("keywords");
                if (kw != k.attrs.end()) d.text += kw->second + " ";
            }
            bool in_tag = false;  // gövde: etiketsiz düz metin (koşullar yok sayılır; her şey aranabilir)
            for (char ch : src) {
                if (ch == '<') in_tag = true;
                else if (ch == '>') { in_tag = false; d.text += ' '; }
                else if (!in_tag) d.text += ch;
            }
            d.text = Fold(d.title + " " + rel + " " + d.text);
            index_.push_back(std::move(d));
        }
        }
    } catch (...) {
    }
    Log("web: " + std::to_string(index_.size()) + " sayfa dizinlendi");
}

void WebBrowser::Search(const std::string& query) {
    query_ = query;
    const std::string q = Fold(query);
    std::vector<std::string> words;
    std::string w;
    for (char ch : q + " ") {
        if (ch == ' ') {
            if (w.size() >= 2) words.push_back(w);
            w.clear();
        } else {
            w += ch;
        }
    }
    std::vector<std::pair<int, const Doc*>> hits;
    for (const auto& d : index_) {
        int score = 0;
        for (const auto& word : words) {
            if (d.text.find(word) != std::string::npos) score += 1 + (Fold(d.title).find(word) != std::string::npos ? 3 : 0);
        }
        if (score > 0) hits.push_back({score, &d});
    }
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::string src = "<page title=\"Eyefind\"><h1>Eyefind</h1><p>\"" + Esc(query) + "\" için " + std::to_string(hits.size()) + " sonuç</p><hr/>";
    for (size_t i = 0; i < hits.size() && i < 12; ++i) {
        const Doc& d = *hits[i].second;
        std::string shown = d.url;
        if (shown.size() > 6 && shown.compare(shown.size() - 6, 6, "/index") == 0) shown.resize(shown.size() - 6);
        src += "<card title=\"" + Esc(d.title) + "\" href=\"" + Esc(d.url) + "\">" + Esc(shown) + "</card>";
    }
    if (hits.empty()) src += "<p>Hiçbir sayfa bulunamadı. Başka bir kelime dene.</p>";
    src += "<hr/><link href=\"home\">Ana sayfa</link></page>";
    std::string err;
    hml::Node root;
    hml::Parse(src, root, err);
    root_ = std::move(root);
    url_ = "eyefind.com/ara";
}

void WebBrowser::Rebuild(const hml::Context& ctx_in) {
    hml::Context ctx = ctx_in;
    const std::string site = url_.substr(0, url_.find('/'));
    ctx.css = [site](const std::string& name) {  // stil dosyası: sitenin klasöründen (güvenli ad)
        for (char ch : name) {
            if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-' || ch == '_')) return std::string();
        }
        if (name.find("..") != std::string::npos) return std::string();
        std::string s = ReadWeb(site + "/" + name);
        if (s.empty()) s = ReadWeb(name);  // ortak stil
        return s;
    };
    page_ = hml::Render(root_, ctx);
    rebuilt_at_ = GetTickCount64();
    const int n = static_cast<int>(page_.items.size());
    if (sel_ >= n || sel_ < 0 || !page_.items[static_cast<size_t>(sel_)].selectable) {
        int pick = sel_ >= 0 && sel_ < n ? NextSelectable(sel_ - 1, +1) : -1;
        if (pick < 0) pick = NextSelectable(-1, +1);
        sel_ = pick;
    }
}

bool WebBrowser::Update(const hml::Context& ctx, bool p_pressed) {
    if (!active_) return false;
    if (typing_) {  // adres/arama kutusu: Enter gönder, Esc iptal
        textinput::Pump();
        overlay::SetInput(textinput::Current(), true);
        std::string text;
        if (textinput::ConsumeSubmitted(text)) {
            typing_ = false;
            overlay::SetInput("", false);
            while (!text.empty() && text.back() == ' ') text.pop_back();
            if (!ask_cmd_.empty()) {  // telefon uygulaması girdisi: "{}" yerine yazılan metin konur
                std::string cmd = ask_cmd_;
                std::string clean;
                for (char ch : text) {
                    if (static_cast<unsigned char>(ch) >= 32) clean += ch;
                }
                const size_t slot = cmd.find("{}");
                if (slot != std::string::npos) cmd.replace(slot, 2, clean);
                ask_cmd_.clear();
                ApplyAction(cmd, ctx);
            } else if (!text.empty()) {
                const std::string low = Lower(text);
                const bool looks_url = low.find(' ') == std::string::npos && (low.find('.') != std::string::npos || low.find('/') != std::string::npos);
                const std::string norm = looks_url ? Normalize(low) : "";
                if (!norm.empty() && !LoadPage(norm).empty()) Navigate(low, true, ctx);
                else Navigate("ara:" + text, true, ctx);
            }
        } else if (textinput::ConsumeCancelled() || !textinput::Active()) {
            typing_ = false;
            ask_cmd_.clear();
            overlay::SetInput("", false);
        }
        return false;
    }
    if (keys_.Pressed('U')) {
        typing_ = true;
        textinput::Begin();
        return false;
    }
    if (p_pressed) {
        active_ = false;
        return true;
    }
    if (GetTickCount64() - phone_src_at_ > 1500ULL) RefreshPhonePage(ctx);  // telefon sayfaları canlı
    if (GetTickCount64() - rebuilt_at_ > 700ULL) Rebuild(ctx);  // görev durumu değişmiş olabilir
    const int nitems = static_cast<int>(page_.items.size());
    const auto is_tile = [&](int i) { return i >= 0 && i < nitems && page_.items[static_cast<size_t>(i)].kind == hml::Kind::Tile; };
    int first_tile = sel_, last_tile = sel_;
    if (is_tile(sel_)) {  // kutucuk grubu: 3'lü ızgarada gezinme
        while (is_tile(first_tile - 1)) --first_tile;
        while (is_tile(last_tile + 1)) ++last_tile;
    }
    if (arrows_) {
        if (is_tile(sel_)) {
            if (keys_.Repeat(VK_RIGHT) && sel_ < last_tile) ++sel_;
            if (keys_.Repeat(VK_LEFT) && sel_ > first_tile) --sel_;
        }
        if (keys_.Repeat(VK_DOWN)) {
            if (is_tile(sel_) && sel_ - (sel_ - first_tile) % 3 + 3 <= last_tile) {
                sel_ = std::min(sel_ + 3, last_tile);
            } else {
                const int n = NextSelectable(is_tile(sel_) ? last_tile : sel_, +1);
                if (n >= 0) sel_ = n;
            }
        }
        if (keys_.Repeat(VK_UP)) {
            if (is_tile(sel_) && sel_ - 3 >= first_tile) {
                sel_ -= 3;
            } else {
                const int n = NextSelectable(is_tile(sel_) ? first_tile : sel_, -1);
                if (n >= 0) sel_ = n;
            }
        }
    }
    bool activate = keys_.Pressed(VK_RETURN) && sel_ >= 0;
    bool want_back = false, want_home = false;
    if (mouse_) {  // fare: imleç altındaki öğeyi seç, sol tık aç, sağ tık geri
        ReadCursor();
        const int hv = overlay::HoverItem();
        if (hv >= 0 && hv < nitems && page_.items[static_cast<size_t>(hv)].selectable) sel_ = hv;
        const bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (lmb && !lmb_was_) ++click_seq_;
        lmb_was_ = lmb;
        const int ck = overlay::TakeWebClick();
        if (ck == -3) want_back = true;   // pencere kapatma düğmesi
        if (ck == -4) want_home = true;   // Facade düğmesi: masaüstü
        if (ck >= 0 && ck < nitems && page_.items[static_cast<size_t>(ck)].selectable) {
            sel_ = ck;
            activate = true;
        }
    }
    if (activate) Activate(ctx);
    if (want_home && !history_.empty()) {
        const std::string root = history_.front();
        history_.clear();
        Navigate(root, true, ctx);
        return false;
    }
    if (want_back || keys_.Pressed(VK_BACK) || (mouse_ && keys_.Pressed(VK_RBUTTON))) {
        if (history_.size() > 1) {
            history_.pop_back();
            const std::string prev = history_.back();
            Navigate(prev, false, ctx);
        } else {
            active_ = false;  // telefon ana ekranından geri: telefon kapanır
            return true;
        }
    }
    return false;
}

void WebBrowser::Publish(const std::string& clock) const {
    if (!active_) return;
    overlay::PhoneView v;
    v.open = true;
    v.web = true;
    v.clock = clock;
    std::string shown = url_;
    if (shown.size() > 6 && shown.compare(shown.size() - 6, 6, "/index") == 0) shown.resize(shown.size() - 6);
    if (shown.rfind("phone/", 0) == 0) shown = "telefon://" + shown.substr(6);
    if (shown.rfind("pc/", 0) == 0) shown = "bilgisayar://" + shown.substr(3);
    v.pc = pcos::Active();
    v.cursor = mouse_;
    v.cursor_x = cur_x_;
    v.cursor_y = cur_y_;
    v.click_seq = click_seq_;
    v.web_url = shown == "home" ? "internet://ana-sayfa" : shown;
    if (!pcos::Active()) {  // bilgisayarda telefon pili gösterilmez
        v.battery = phoneos::BatteryText();
        v.battery_low = phoneos::Battery() <= 15 && !phoneos::Charging();
    }
    v.web_title = page_.title;
    for (const auto& it : page_.items) {
        overlay::WebItem w;
        w.kind = static_cast<int>(it.kind) == 0 ? 0 : static_cast<int>(it.kind);  // hml::Kind sırası = overlay kind
        w.text = it.text;
        w.sub = it.sub;
        w.meta = it.meta;
        w.selectable = it.selectable;
        w.color = it.style.color;
        w.bg = it.style.bg;
        w.border = it.style.border;
        w.size = it.style.size;
        w.pad = it.style.pad;
        w.radius = it.style.radius;
        w.align = it.style.align;
        w.gw = it.gw;
        w.gh = it.gh;
        w.gbg = it.gbg;
        for (const auto& s : it.shapes) {
            overlay::WebShape o;
            o.type = static_cast<int>(s.type);
            o.x = s.x; o.y = s.y; o.w = s.w; o.h = s.h; o.r = s.r; o.sw = s.sw; o.size = s.size;
            o.fill = s.fill; o.stroke = s.stroke; o.align = s.align;
            o.pts = s.pts;
            o.text = s.text;
            w.shapes.push_back(std::move(o));
        }
        v.web_items.push_back(std::move(w));
    }
    v.web_selected = sel_;
    v.web_bg = page_.bg;
    v.web_color = page_.color;
    v.web_accent = page_.accent;
    if (GetTickCount64() < notice_until_) v.notice = notice_;
    v.hint = typing_ ? "Adres veya aranacak kelime yaz   Enter: git   Esc: iptal"
                     : "↑↓ seç   Enter aç   U: adres/ara   Backspace geri   P kapat";
    overlay::SetPhone(v);
}

}  // namespace hikaye
