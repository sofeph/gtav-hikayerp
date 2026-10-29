#pragma once
#include <functional>
#include <utility>
#include <map>
#include <string>
#include <vector>

// HML: oyun içi web sayfaları için küçük bir işaretleme dili (telefon tarayıcısı). Oyun/ImGui'den bağımsızdır (test edilebilir).
// Sayfa = data/web/<site>/<sayfa>.hml, kökü <page title="...">. Etiketler:
//   <h1> <h2> <p> <b> <i> <br/> <hr/>            başlık, paragraf, satır içi vurgu (düz metin), satır sonu, çizgi
//   <link href="site/sayfa">etiket</link>         seçilebilir bağlantı (href "bleeter.com" = o sitenin index sayfası)
//   <card title=".." price=".." href="..">gövde</card>   ilan kartı (href varsa seçilebilir)
//   <post user=".." handle=".." time="..">metin</post>   Bleeter gönderisi
//   <list><item>..</item></list>                  madde işaretli liste
//   <if koşullar>..<else/>..</if>                 koşullu içerik (görev durumuna göre sayfa değişir)
//   <svg w h spin="true">...</svg>                vektör grafik; spin=true grafiği döndürür
// Koşullar (hepsi birlikte sağlanmalı): done="görev" notdone="görev" active="görev" available="görev"
//   flag="anahtar" eq="değer" ne="değer" (eq/ne yoksa: bayrak dolu ve "0" değil)  hour_ge="N" hour_lt="N"  cash_ge="N"
// Metin içinde değişkenler: {cash} {hour} {flag:anahtar}. Kaçışlar: &lt; &gt; &amp; &quot;
namespace hikaye::hml {

struct Node {
    std::string tag;  // "#text" metin düğümü
    std::map<std::string, std::string> attrs;
    std::string text;
    std::vector<Node> kids;
};



enum class Kind { Text, H1, H2, Rule, Link, Card, Post, Bullet, Graphic, Tile };  // Tile: uygulama kutucuğu (<tile icon="T" color="#hex" href="..">Ad</tile>), 3'lü ızgara

// Vektör çizim parçası (SVG alt kümesi). Koordinatlar çizim alanının kendi birimindedir (svg w/h).
struct Shape {
    enum class Type { Rect, Circle, Line, Polyline, Polygon, Text } type = Type::Rect;
    float x = 0, y = 0, w = 0, h = 0, r = 0;  // rect: x y w h (r: köşe), circle: x y r, line: x y -> w h (bitiş), text: x y
    std::vector<std::pair<float, float>> pts;
    int fill = -1, stroke = -1;               // 0xRRGGBB, -1: yok
    float sw = 1.f, size = 14.f;
    int align = 0;                            // text: 0 sol, 1 orta, 2 sağ
    std::string text;
};

// Biçem (stil): CSS benzeri .hss dosyalarından gelir. Renkler 0xRRGGBB (-1: varsayılan).
struct Style {
    int color = -1, bg = -1, border = -1;
    float size = 0.f;     // yazı boyutu (1080p pikseli); 0: tür varsayılanı
    int align = 0;        // 0 sol, 1 orta, 2 sağ
    float pad = -1.f;     // kart/gönderi iç boşluğu (1080p px); -1 varsayılan
    float radius = -1.f;  // köşe yuvarlaklığı; -1 varsayılan
};

struct Item {
    Kind kind = Kind::Text;
    std::string text, sub, meta, href;
    bool selectable = false;
    Style style;
    float gw = 0, gh = 0;  // Graphic: çizim alanı
    int gbg = -1;
    std::vector<Shape> shapes;
    bool spin = false;
};

// <source name="ad"/> etiketi: oyun kodundan dinamik öğe listesi (ilanlar, teklifler...). Context::source ile sağlanır.
// Seçilebilir öğenin href'i "act:komut" ise tarayıcı sayfayı değiştirmez, Context::action(komut) çağırıp sonucu bildirir.
struct Context {
    std::function<std::string(const std::string&)> flag;           // bayrak değeri ("" yoksa)
    std::function<std::string(const std::string&)> mission_state;  // "done" | "active" | "available" | "locked"
    std::function<std::vector<Item>(const std::string&)> source;  // <source name="..."/>
    std::function<std::string(const std::string&)> action;        // "act:komut" -> bildirim metni
    std::function<std::string(const std::string&)> css;           // <page style="x.hss"> -> stil dosyası metni ("" yoksa)
    int cash = 0;
    int hour = 12;
};

struct Page {
    std::string title;
    std::vector<Item> items;
    int bg = -1, color = -1, accent = -1;  // sayfa teması: arka plan, varsayılan yazı, vurgu (page { bg: #112233; })
};

// Koşul özniteliklerini (done/notdone/active/available/flag/eq/ne/hour_ge/hour_lt/cash_ge) bağlama göre değerlendirir.
bool Matches(const std::map<std::string, std::string>& conds, const Context& ctx);

// Ayrıştırır. Hata durumunda false ve err dolu; hoşgörülüdür (kapanmamış etiketleri kapatır).
bool Parse(const std::string& src, Node& root, std::string& err);
// Bağlama göre düz öğe listesi üretir (koşullar ve değişkenler çözülür).
Page Render(const Node& root, const Context& ctx);

}  // namespace hikaye::hml
