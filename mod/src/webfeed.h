#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "hml.h"
#include "json.hpp"

namespace hikaye {

// Telefon tarayıcısı için YAPAY ZEKA ÜRETİMLİ içerik (statik değil): Bleeter gönderileri ve şikayetler, Weazel News haberleri,
// GeneralSale ilanları. Companion (LLM) üretir; mod sadece ister, önbelleğe alır (saves/webfeed.json) ve gösterir.
// Şehirde olanlar (data/web/feed_topics.json: görev/onur durumuna bağlı olaylar) LLM'e konu olarak verilir; içerikler bunlara dolaylı değinir.
// HML'de <source name="ai_complaints|ai_bleets|ai_news|ai_ads"/> ile kullanılır.
class WebFeed {
public:
    void Load();
    void SetExtraTopics(std::function<std::vector<std::string>()> f) { extra_ = std::move(f); }  // ör. son suç haberleri
    void SetSender(std::function<void(const std::string&)> send) { send_ = std::move(send); }
    void OnPosts(const nlohmann::json& msg);  // companion cevabı {"type":"posts","kind","posts":[...]}
    std::vector<hml::Item> Source(const std::string& source_name, const hml::Context& ctx);

private:
    struct Entry {
        nlohmann::json posts = nlohmann::json::array();
        unsigned long long fetched_at = 0, requested_at = 0;
        bool pending = false;
    };
    std::vector<std::string> Topics(const hml::Context& ctx) const;
    void Save() const;
    std::function<void(const std::string&)> send_;
    std::function<std::vector<std::string>()> extra_;
    std::map<std::string, Entry> feeds_;
    nlohmann::json topics_ = nlohmann::json::array();  // [{"when":{...},"topic":"..."}]
};

}  // namespace hikaye
