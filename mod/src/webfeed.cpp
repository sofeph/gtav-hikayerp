#include "webfeed.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>

#include "common.h"

namespace hikaye {

using nlohmann::json;

namespace {
constexpr unsigned long long kRefreshMs = 8ULL * 60ULL * 1000ULL;  // içerik 8 dakikada bir yenilenir
constexpr unsigned long long kPendingMs = 90ULL * 1000ULL;         // cevap gelmezse tekrar dene
std::string SavePath() { return std::string(kBaseDir) + "/saves/webfeed.json"; }
}  // namespace

void WebFeed::Load() {
    try {
        std::ifstream t(std::string(kBaseDir) + "/data/web/feed_topics.json");
        if (t) topics_ = json::parse(t).value("topics", json::array());
    } catch (const std::exception& e) {
        Log(std::string("feed_topics HATA: ") + e.what());
    }
    try {  // önceki oturumun içerikleri: telefon açılır açılmaz boş görünmesin
        std::ifstream in(SavePath());
        if (in) {
            const json j = json::parse(in);
            for (const auto& [k, v] : j.items()) feeds_[k].posts = v;
        }
        // Previously generated classified ads could contain prices invented by the model.
        for (auto& post : feeds_["ads"].posts) {
            if (post.is_object()) post["price"] = "";
        }
    } catch (...) {
    }
}

void WebFeed::Save() const {
    try {
        std::filesystem::create_directories(std::string(kBaseDir) + "/saves");
        json j = json::object();
        for (const auto& [k, e] : feeds_) j[k] = e.posts;
        std::ofstream out(SavePath());
        out << j.dump(1);
    } catch (...) {
    }
}

std::vector<std::string> WebFeed::Topics(const hml::Context& ctx) const {
    std::vector<std::string> out;
    for (const auto& t : topics_) {
        std::map<std::string, std::string> conds;
        const json when = t.value("when", json::object());
        for (const auto& [k, v] : when.items()) conds[k] = v.get<std::string>();
        if (hml::Matches(conds, ctx)) out.push_back(t.value("topic", ""));
    }
    if (extra_) {
        for (auto& s : extra_()) out.push_back(std::move(s));
    }
    // en güncel olaylar önde olsun, çok uzamasın: sondan 8 + rastgele 3 eski
    if (out.size() > 11) {
        std::vector<std::string> keep(out.end() - 8, out.end());
        std::mt19937 rng(static_cast<unsigned>(GetTickCount64()));
        std::vector<std::string> old(out.begin(), out.end() - 8);
        std::shuffle(old.begin(), old.end(), rng);
        for (size_t i = 0; i < 3 && i < old.size(); ++i) keep.push_back(old[i]);
        out = std::move(keep);
    }
    return out;
}

void WebFeed::OnPosts(const json& msg) {
    const std::string kind = msg.value("kind", "");
    Entry& e = feeds_[kind];
    e.pending = false;
    if (msg.value("error", false) || !msg.contains("posts") || !msg["posts"].is_array() || msg["posts"].empty()) return;  // eski içerik kalır
    e.posts = msg["posts"];
    e.fetched_at = GetTickCount64();
    Save();
    Log("webfeed: " + kind + " " + std::to_string(e.posts.size()) + " oge");
}

std::vector<hml::Item> WebFeed::Source(const std::string& name, const hml::Context& ctx) {
    std::vector<hml::Item> out;
    const std::string kind = name.rfind("ai_", 0) == 0 ? name.substr(3) : name;
    Entry& e = feeds_[kind];
    const unsigned long long now = GetTickCount64();
    const bool stale = e.fetched_at == 0 || now - e.fetched_at > kRefreshMs;
    if (stale && send_ && (!e.pending || now - e.requested_at > kPendingMs)) {
        e.pending = true;
        e.requested_at = now;
        json topics = json::array();
        for (const auto& t : Topics(ctx)) topics.push_back(t);
        send_(json{{"type", "gen_posts"}, {"kind", kind}, {"count", 6}, {"topics", topics}}.dump());
    }
    if (e.posts.empty()) {
        hml::Item it;
        it.text = e.pending ? "İçerik yükleniyor..." : "Şu an içerik yok. Biraz sonra tekrar dene.";
        out.push_back(it);
        return out;
    }
    static const char* const kAgo[] = {"az önce", "5 dk önce", "12 dk önce", "35 dk önce", "1 sa önce", "2 sa önce", "4 sa önce", "dün"};
    int i = 0;
    for (const auto& p : e.posts) {
        hml::Item it;
        if (kind == "news" || kind == "ads") {
            it.kind = hml::Kind::Card;
            it.text = p.value("title", "");
            it.sub = p.value("body", "");
            if (kind == "ads" && p.contains("seller") && !p.value("seller", "").empty()) it.sub += " (Satıcı: " + p.value("seller", "") + ")";
            it.meta = kind == "news" ? p.value("tag", "") : p.value("price", "");
        } else {
            it.kind = hml::Kind::Post;
            it.text = p.value("text", "");
            it.meta = p.value("user", "");
            it.sub = "@" + p.value("handle", "") + "  ·  " + kAgo[std::min<int>(i, 7)];
        }
        if (!it.text.empty()) out.push_back(std::move(it));
        ++i;
    }
    return out;
}

}  // namespace hikaye
