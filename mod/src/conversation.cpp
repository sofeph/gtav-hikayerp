#include "conversation.h"
#include "hitchhike.h"
#include "phoneos.h"
#include "transit.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

#include "controls.h"
#include "economy.h"
#include "inventory.h"
#include "karma.h"
#include "weaponwatch.h"
#include "overlay.h"
#include "profile_gen.h"
#include "textinput.h"

namespace hikaye {

namespace {
constexpr float kEndDistanceFactor = 3.f;

using nlohmann::json;

// GTA fontları ş ı ğ gibi harfleri çizemez; altyazıyı ASCII'ye çeviriyoruz.
std::string FoldTr(const std::string& s) {
    static const char* const map[][2] = {
        {"ç", "c"}, {"Ç", "C"}, {"ğ", "g"}, {"Ğ", "G"}, {"ı", "i"}, {"İ", "I"},
        {"ö", "o"}, {"Ö", "O"}, {"ş", "s"}, {"Ş", "S"}, {"ü", "u"}, {"Ü", "U"},
    };
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

}  // namespace

void DrawLine(const std::string& text, float y, int r, int g, int b) {
    // ImGui katmanı hazırsa Türkçe harflerle (ş ı ğ) ona çizdir; değilse eski native yönteme (ASCII) dön.
    if (overlay::Ready()) {
        overlay::Text(overlay::HasTurkishFont() ? text : FoldTr(text), y, r, g, b);
        return;
    }
    const std::string t = FoldTr(text);
    UI::SET_TEXT_FONT(0);
    UI::SET_TEXT_SCALE(0.f, 0.45f);
    UI::SET_TEXT_COLOUR(r, g, b, 255);
    UI::SET_TEXT_CENTRE(true);
    UI::SET_TEXT_OUTLINE();
    UI::SET_TEXT_WRAP(0.15f, 0.85f);
    UI::_SET_TEXT_ENTRY(const_cast<char*>("STRING"));
    // Her metin bileşeni en fazla 99 bayt alır; uzun satırı parçalara böl.
    for (size_t i = 0; i < t.size(); i += 90) {
        const std::string part = t.substr(i, 90);
        UI::_ADD_TEXT_COMPONENT_STRING(const_cast<char*>(part.c_str()));
    }
    UI::_DRAW_TEXT(0.5f, y);
}

namespace {
float HeadingTo(const Vector3& from, const Vector3& to) {
    // GTA heading: 0 = +Y, saat yönünün tersine artar.
    return std::atan2(-(to.x - from.x), to.y - from.y) * 57.29578f;
}

float Dist2D(const Vector3& a, const Vector3& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}
}  // namespace

void Conversation::Notify(const std::string& text) {
    notice_ = text;
    notice_until_ = GetTickCount64() + 3000;
}

void Conversation::Start(Shop& shop, const Vector3& player_pos) {
    if (!ws_->connected()) {
        Notify("Servis kapali: companion calismiyor");
        return;
    }
    shop_ = &shop;
    context_ = (&shop == &street_) ? (virtual_context_.empty() ? "street" : virtual_context_) : "shop";
    npc_name_ = shop.npc_json.value("profile", json::object()).value("name", "");
    ptt_down_ = speaking_ = false;
    player_line_.clear();
    npc_line_.clear();
    thinking_since_ = 0;
    gave_money_ = false;

    last_result_.clear();
    fresh_transcript_ = false;
    const json& npc = shop.npc_json;
    json msg = {
        {"type", "session_start"},
        {"context", context_},
        {"id", shop.id},
        {"npc", {{"personality", npc.value("personality", "")},
                 {"profile", npc.value("profile", json::object())},
                 {"voice", npc.value("voice", json::object())},
                 {"flirty", npc.value("flirty", false)}}},
        {"state", BuildState()},
    };
    ws_->Send(msg.dump());

    if (ENTITY::DOES_ENTITY_EXIST(shop.ped)) {
        if (context_ == "street") {
            // Yürüyen yaya durur, kaçmaz; oturum bitince serbest bırakılır (End).
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(shop.ped, true, true);
            PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(shop.ped, true);
            AI::TASK_STAND_STILL(shop.ped, 600000);
        }
        // NPC oyuncuya dönsün (dükkan NPC'leri dondurulmuş; yönü doğrudan ayarlıyoruz). Araçtaki şoför/pilotun
        // yönü değiştirilmez (aracı döndürürdü).
        if (context_ == "shop" || context_ == "street" || context_ == "quest") {
            ENTITY::SET_ENTITY_HEADING(shop.ped, HeadingTo(shop.pos, player_pos));
        }
    }
    Log("oturum basladi: " + shop.id + " (" + context_ + ", " + npc_name_ + ")");
}

void Conversation::StartLawyer(const json& pool) {
    if (active() || !jail_ || !jail_->in_cell()) return;
    virtual_context_ = "lawyer";
    street_ = Shop{};
    street_.id = "avukat_" + std::to_string(jail_->arrests());
    street_.inventory = json::array();
    const json def = {{"role", "avukat"}, {"gender", "random"}, {"profile", "random"}};
    street_.npc_json = profile_gen::Resolve(def, street_.id, pool);
    street_.npc_gender = street_.npc_json.value("gender", "male");
    street_.npc_name = street_.npc_json.value("profile", json::object()).value("name", "");
    Start(street_, ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true));
}

void Conversation::StartWantedLawyer(const json& npc, const json& pool, bool bar_association) {
    if (active() || !jail_ || !jail_->CanUsePhone() || !jail_->HasWantedCase()) return;
    virtual_context_ = bar_association ? "wanted_bar" : "wanted_sule";
    person_call_ = false;
    quest_call_ = false;
    street_ = Shop{};
    street_.id = bar_association ? "ls_bar_lawyer" : "sule_wanted_lawyer";
    street_.inventory = json::array();
    if (bar_association) {
        const json def = {{"role", "Los Santos Barosu avukatı"}, {"gender", "random"}, {"profile", "random"}};
        street_.npc_json = profile_gen::Resolve(def, street_.id, pool);
        street_.npc_json["personality"] = "Los Santos Barosu'nda çalışan, ciddi ve soğukkanlı bir ceza avukatısın. "
                                          "Telefonda müvekkile kısa ve profesyonel konuşursun.";
    } else {
        street_.npc_json = npc;
    }
    street_.npc_gender = street_.npc_json.value("gender", "female");
    street_.npc_name = street_.npc_json.value("profile", json::object()).value(
        "name", std::string(bar_association ? "Baro avukatı" : "Şule Tatlılıoğlu"));
    Start(street_, ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true));
}

void Conversation::SendFollowup(const char* purpose, const json& facts) {
    // NPC'nin ilk cümlesi ("tamam, bir bakayım") bitince companion bu gerçek bilgiyle ikinci cümleyi söyler.
    ws_->Send(json{{"type", "followup"}, {"purpose", purpose}, {"facts", facts}}.dump());
}

void Conversation::StartPhone(const json& pool, const std::string& role, const std::string& line_id) {
    if (active() || !services_ || (jail_ && !jail_->CanUsePhone())) return;
    virtual_context_ = "phone";
    street_ = Shop{};
    street_.id = line_id;
    street_.inventory = json::array();
    const json def = {{"role", role}, {"gender", "random"}, {"profile", "random"}};
    street_.npc_json = profile_gen::Resolve(def, street_.id, pool);
    if (line_id == "haviland") {  // telefonun yapay zekâ asistanı: insan değil, sabit ad ve kişilik
        street_.npc_json["personality"] = "Haviland: telefonun yapay zekâ asistanısın. İnsan DEĞİLSİN; kendini gerektiğinde yapay zekâ asistanı olarak tanıt. "
                                           "Kısa, nazik, yardımsever ve sakin konuşursun; kişisel geçmiş, memleket, aile gibi insana ait şeyler uydurmazsın.";
        json prof = street_.npc_json.value("profile", json::object());
        prof = json{{"name", "Haviland"}, {"title", "Telefon yapay zekâ asistanı"}, {"gender", street_.npc_json.value("gender", "female")}};
        street_.npc_json["profile"] = prof;
        street_.npc_json["voice"] = json{{"speaker", phoneos::HavilandVoice()}};  // Haviland > Ses seç
        street_.npc_json["gender"] = phoneos::HavilandVoiceFemale() ? "female" : "male";
    }
    street_.npc_gender = street_.npc_json.value("gender", "male");
    street_.npc_name = street_.npc_json.value("profile", json::object()).value("name", "");
    Start(street_, ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true));
}

void Conversation::StartRide(const json& pool) {
    if (active() || !services_ || !services_->InRide()) return;
    const bool heli = services_->ride_kind() == RideKind::Heli;
    const Ped driver = services_->ride_driver();
    virtual_context_ = heli ? "heli" : "taxi";
    street_ = Shop{};
    street_.id = "yolculuk_" + std::to_string(driver);
    street_.ped = driver;
    street_.inventory = json::array();
    const json def = {{"role", heli ? "helikopter pilotu" : "taksi şoförü"},
                      {"gender", PED::IS_PED_MALE(driver) ? "male" : "female"},
                      {"profile", "random"}};
    street_.npc_json = profile_gen::Resolve(def, street_.id, pool);
    street_.npc_gender = street_.npc_json.value("gender", "male");
    street_.npc_name = street_.npc_json.value("profile", json::object()).value("name", "");
    Start(street_, ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true));
}

void Conversation::StartHitch(const json& pool) {
    if (active() || !hitchhike::Riding()) return;
    const Ped driver = hitchhike::Driver();
    virtual_context_ = "hitch";
    street_ = Shop{};
    street_.id = "otostop_" + std::to_string(driver);
    street_.ped = driver;
    street_.inventory = json::array();
    const json def = {{"role", "otostop çekeni arabasına alan sürücü"},
                      {"gender", PED::IS_PED_MALE(driver) ? "male" : "female"},
                      {"profile", "random"}};
    street_.npc_json = profile_gen::Resolve(def, street_.id, pool);
    street_.npc_gender = street_.npc_json.value("gender", "male");
    street_.npc_name = street_.npc_json.value("profile", json::object()).value("name", "");
    Start(street_, ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true));
}

void Conversation::StartPersonCall(const json& npc) {
    if (active() || !services_ || (jail_ && !jail_->CanUsePhone())) return;
    virtual_context_ = "person";
    person_call_ = true;
    quest_call_ = false;
    street_ = Shop{};
    street_.id = "arama_kisi_" + npc.value("profile", json::object()).value("name", "tanidik");
    street_.ped = 0;
    street_.inventory = json::array();
    street_.npc_json = npc;
    street_.npc_gender = npc.value("gender", "male");
    street_.npc_name = npc.value("profile", json::object()).value("name", "");
    Start(street_, ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true));
}

void Conversation::StartHangout(const Missions::Talk& talk, const Vector3& player_pos) {
    if (active() || !ENTITY::DOES_ENTITY_EXIST(talk.ped)) return;
    virtual_context_ = "hangout";
    street_ = Shop{};
    street_.id = "hangout_" + talk.npc_id;
    street_.ped = talk.ped;
    street_.pos = talk.pos;
    street_.interact_radius = 4.f;
    street_.inventory = json::array();
    street_.npc_json = talk.npc_json;
    street_.npc_gender = talk.npc_json.value("gender", "male");
    street_.npc_name = talk.name;
    Start(street_, player_pos);
}

void Conversation::StartMissionCall(const Missions::Talk& talk) {
    if (active() || !missions_ || (jail_ && jail_->active())) return;
    virtual_context_ = "quest";
    quest_call_ = true;
    quest_npc_id_ = talk.npc_id;
    street_ = Shop{};
    street_.id = "arama_" + talk.npc_id;
    street_.ped = 0;
    street_.inventory = json::array();
    street_.npc_json = talk.npc_json;
    street_.npc_gender = talk.npc_json.value("gender", "male");
    street_.npc_name = talk.name;
    Start(street_, ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true));
}

void Conversation::StartMission(const Missions::Talk& talk) {
    if (active() || !missions_ || !ENTITY::DOES_ENTITY_EXIST(talk.ped)) return;
    virtual_context_ = "quest";
    quest_call_ = false;
    quest_npc_id_ = talk.npc_id;
    street_ = Shop{};
    street_.id = "gorev_" + talk.npc_id;
    street_.ped = talk.ped;
    street_.pos = talk.pos;
    street_.interact_radius = 2.5f;
    street_.inventory = json::array();
    street_.npc_json = talk.npc_json;
    street_.npc_gender = talk.npc_json.value("gender", "male");
    street_.npc_name = talk.name;
    Start(street_, ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true));
}

void Conversation::StartStreet(Ped ped, const Vector3& player_pos, const json& pool) {
    if (active() || !ENTITY::DOES_ENTITY_EXIST(ped)) return;
    virtual_context_.clear();
    if (jail_) jail_->WatchForMurder(ped);  // konuştuğun kişiyi öldürürsen 5 yıldız
    street_ = Shop{};
    street_.id = "ped_" + std::to_string(ped);
    street_.ped = ped;
    street_.pos = ENTITY::GET_ENTITY_COORDS(ped, true);
    street_.interact_radius = 2.5f;
    street_.inventory = json::array();
    // Tüm NPC'lerde aynı dinamik (CLAUDE.md §7.2): cinsiyet ped'den, gerisi handle'dan seed'lenen havuzdan.
    const json def = {{"role", "sokaktaki vatandas"},
                      {"gender", PED::IS_PED_MALE(ped) ? "male" : "female"},
                      {"profile", "random"}};
    street_.npc_json = profile_gen::Resolve(def, street_.id, pool);
    street_.npc_gender = street_.npc_json.value("gender", "male");
    street_.npc_name = street_.npc_json.value("profile", json::object()).value("name", "");
    Start(street_, player_pos);
}

bool Conversation::StartPoliceStop(Ped cop, const Vector3& player_pos, const json& pool, const json& facts) {
    if (active() || !ENTITY::DOES_ENTITY_EXIST(cop)) return false;
    virtual_context_ = "police";
    street_ = Shop{};
    street_.id = "polis_" + std::to_string(cop);
    street_.ped = cop;
    street_.pos = ENTITY::GET_ENTITY_COORDS(cop, true);
    street_.interact_radius = 6.f;
    street_.inventory = json::array();
    const json def = {{"role", "polis memuru"}, {"gender", PED::IS_PED_MALE(cop) ? "male" : "female"}, {"profile", "random"}};
    street_.npc_json = profile_gen::Resolve(def, street_.id, pool);
    street_.npc_gender = street_.npc_json.value("gender", "male");
    street_.npc_name = street_.npc_json.value("profile", json::object()).value("name", "");
    Start(street_, player_pos);
    if (!active()) return false;
    SendFollowup("police_stop", facts);
    return true;
}

bool Conversation::StartProactive(Ped ped, const Vector3& player_pos, const json& pool, const json& facts) {
    if (active() || !ENTITY::DOES_ENTITY_EXIST(ped)) return false;
    StartStreet(ped, player_pos, pool);
    if (!active()) return false;
    SendFollowup("favor_ask", facts);  // ilk cümleyi NPC söyler
    return true;
}

json Conversation::BuildState() const {
    json s = BuildStateCore();
    if (!s.is_object()) s = json::object();
    s["player_honor"] = karma::Level();  // NPC'nin oyuncuya tavrı: outlaw/bad/neutral/good/hero
    return s;
}

json Conversation::BuildStateCore() const {
    if (context_ == "quest") {
        // Görev durumu: aşama hedefi ve 'advance' koşulu. Para/konum yok; ilerlemeyi kod belirler.
        return missions_ ? missions_->QuestState(quest_npc_id_, quest_call_) : json::object();
    }
    if (context_ == "lawyer") {
        // Dava özeti: LLM yalnızca bu gerekçeleri anlatır; bakiye sadece "yeter mi" olarak çıkarılır (companion).
        json state = {{"case", jail_->CaseState()}, {"player_cash", economy::GetCash()},
                      {"cheapest_price", jail_->LawyerFee()}};
        if (!last_result_.empty()) state["last_result"] = last_result_;
        return state;
    }
    if (context_ != "shop") return json::object();  // sokak NPC'sinin ticari durumu yok
    json items = json::array();
    int cheapest = 0;
    for (const auto& it : shop_->inventory) {
        if (it.value("stock", -1) == 0) continue;  // stokta olmayanı NPC'ye hiç gösterme
        const int price = it.value("price", 0);
        cheapest = cheapest == 0 ? price : std::min(cheapest, price);
        items.push_back({{"id", it.value("id", "")}, {"name", it.value("name", "")}, {"price", price}});
    }
    json state = {{"items", items}, {"player_cash", economy::GetCash()}, {"cheapest_price", cheapest}};
    if (!last_result_.empty()) state["last_result"] = last_result_;
    return state;
}

void Conversation::SendState() {
    if (shop_) ws_->Send(json{{"type", "state"}, {"state", BuildState()}}.dump());
}

void Conversation::TryPurchase(const std::string& item_id, int qty) {
    json* item = nullptr;
    for (auto& it : shop_->inventory) {
        if (it.value("id", "") == item_id) item = &it;
    }
    if (!item) {
        last_result_ = "unknown_item";
        return SendState();
    }
    const std::string name = item->value("name", item_id);
    const json effect = item->value("effect", json::object());
    // Araç gibi tekil ürünlerde adet 1'e sabitlenir.
    if (effect.value("type", "") == "spawn_vehicle") qty = 1;
    qty = std::max(1, std::min(qty, 20));

    // Ürün için envanter ön koşulu (silah ruhsatı vb.)
    {
        const std::string req = item->value("requires_item", "");
        if (!req.empty() && inventory::Count(req) < 1) {
            last_result_ = "requires_item:" + req;
            Notify("Bunun icin once \"" + req + "\" gerekli");
            return SendState();
        }
    }
    const int stock = item->value("stock", -1);  // -1: sınırsız
    if (stock == 0 || (stock > 0 && stock < qty)) {
        last_result_ = "out_of_stock:" + name;
        Notify("Stokta yok: " + name);
        return SendState();
    }
    const int total = item->value("price", 0) * qty;
    if (!economy::Spend(total)) {
        last_result_ = "insufficient_funds:" + name;
        Notify("Yetersiz bakiye: " + name + " $" + std::to_string(total));
        return SendState();
    }

    const Ped player = PLAYER::PLAYER_PED_ID();
    Vector3 at = shop_->vehicle_spawn;
    float heading = shop_->vehicle_heading;
    if (!shop_->has_vehicle_spawn) {  // tanımlı nokta yoksa oyuncunun önünde doğar
        at = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(player, 0.f, 6.f, 0.f);
        heading = ENTITY::GET_ENTITY_HEADING(player);
    }
    bool delivered = true;
    for (int i = 0; i < qty && delivered; ++i) delivered = economy::ApplyEffect(effect, at, heading);
    if (!delivered) {
        // Teslimat başarısızsa parayı iade et (para bir kere düştü).
        economy::Earn(total);
        last_result_ = "delivery_failed:" + name;
        Notify("Teslimat basarisiz, para iade edildi");
        return SendState();
    }
    if (stock > 0) (*item)["stock"] = stock - qty;
    last_result_ = "sale_done:" + name;
    Notify("Satin alindi: " + name + "  -$" + std::to_string(total));
    Log("satis: " + name + " x" + std::to_string(qty) + " $" + std::to_string(total));
    SendState();
}

void Conversation::Directions(const std::string& destination) {
    if (!places_) return;
    const Ped player = PLAYER::PLAYER_PED_ID();
    const Vector3 from = ENTITY::GET_ENTITY_COORDS(player, true);
    Place place;
    json facts;
    if (places_->Resolve(destination, from, place)) {
        UI::SET_NEW_WAYPOINT(place.pos.x, place.pos.y);
        const int dist = static_cast<int>(Places::Dist2D(from, place.pos));
        facts = {{"destination", place.name},
                 {"distance_m", dist},
                 {"direction", Places::Compass(from, place.pos)},
                 {"zone", places_->ZoneNameAt(place.pos)},
                 {"waypoint_set", true},
                 {"very_close", dist < 40}};
        Notify("Haritaya isaretlendi: " + place.name);
        Log("yon tarifi: " + place.name + " " + std::to_string(dist) + "m");
    } else {
        facts = {{"error", "unknown_destination"}, {"asked", destination}};
        Log("yon tarifi: bilinmeyen yer: " + destination);
    }
    // NPC'nin ilk cümlesi ("bir bakayım") bitince companion bu gerçek bilgiyle ikinci cümleyi söyler.
    SendFollowup("directions", facts);
}

void Conversation::RunPendingEvent() {
    const bool finished_speaking = speaking_seen_ && !speaking_;
    if (!finished_speaking && GetTickCount64() < pending_deadline_) return;
    const Pending ev = pending_;
    Ped ped = shop_->ped;
    End("olay", true);  // oturumu kapat, ama ped'in görevlerini koru
    if (ev == Pending::LawyerRelease) {
        if (jail_) jail_->ReleaseWithLawyer();
        return;
    }
    if (ev == Pending::HangUp || ev == Pending::Leave) {  // telefon kapandı / NPC küsüp uzaklaşıyor
        if (ev == Pending::Leave && ENTITY::DOES_ENTITY_EXIST(ped) && !PED::IS_PED_IN_ANY_VEHICLE(ped, false)) {
            AI::CLEAR_PED_TASKS(ped);
            AI::TASK_SMART_FLEE_PED(ped, PLAYER::PLAYER_PED_ID(), 60.0f, 8000, false, false);
        }
        return;
    }
    if (ev == Pending::CallPolice) {
        const bool can = jail_ && !jail_->active() && !call_.active();
        if (can) call_.Start(ped, [this](bool player_armed) {
            CaseFacts facts;
            facts.player_armed = player_armed;
            jail_->OnPoliceCalled(facts);
        });
        return;
    }
    if (ev == Pending::Fight) {
        // Gerginlik sonucu KOD belirler: telefonla polisi arama, silahlı saldırı veya yumruk dövüşü.
        const bool can_call = jail_ && !jail_->active() && !call_.active();
        if (can_call && street_events::Chance(jail_->PoliceCallChance())) {
            call_.Start(ped, [this](bool player_armed) {
                CaseFacts facts;
                facts.player_armed = player_armed;
                jail_->OnPoliceCalled(facts);
            });
        } else {
            street_events::Confront(ped, jail_ ? jail_->ArmedAttackChance() : 0.5);
            if (jail_) jail_->WatchForMurder(ped);
        }
        return;
    }
    if (!ride_.Start(ped)) {  // araç uygun değil (yürüyor, yolcu koltuğu dolu, araçta değilsin...)
        Notify("Arabaya binemedi");
        AI::CLEAR_PED_TASKS(ped);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, false);
        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&ped);
    }
}

void Conversation::End(const char* reason, bool keep_ped) {
    if (!shop_) return;
    pending_ = Pending::None;
    if (ptt_down_) ws_->Send(R"({"type":"ptt_up"})");
    ws_->Send(R"({"type":"session_end"})");
    Log(std::string("oturum bitti: ") + shop_->id + " (" + reason + ")");
    if (context_ == "street" && !keep_ped && ENTITY::DOES_ENTITY_EXIST(shop_->ped)) {
        Ped ped = shop_->ped;
        AI::CLEAR_PED_TASKS(ped);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, false);
        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&ped);  // oyun yayayı tekrar kendisi yönetsin
    }
    shop_ = nullptr;
    person_call_ = false;
    ptt_down_ = speaking_ = false;
}

void Conversation::HandleMessage(const std::string& raw) {
    try {
        const json m = json::parse(raw);
        const std::string type = m.value("type", "");
        const unsigned long long now = GetTickCount64();
        if (type == "transcript") {
            player_line_ = m.value("text", "");
            player_until_ = now + 4000;
            fresh_transcript_ = true;
            last_result_.clear();  // eski sonuç yeni turda geçerli değil
        } else if (type == "posts") {
            if (on_posts_) on_posts_(m);
        } else if (type == "thinking") {
            thinking_since_ = m.value("state", "") == "start" ? now : 0;
        } else if (type == "npc_reply") {
            thinking_since_ = 0;
            npc_line_ = m.value("say", "");
            npc_until_ = now + 4000 + npc_line_.size() * 60;
            const std::string intent = m.value("intent", "chat");
            Log("NPC: " + npc_line_ + " [" + intent + "]");
            if (shop_ && missions_ && context_ == "quest") {  // görev sırrını (örn. ürün anahtarı) söyledi: telefon notlarına otomatik yazılır
                std::string label, value;
                if (missions_->SecretMentioned(quest_npc_id_, npc_line_, label, value) && phoneos::AddNote(label + ": " + value)) Notify("Telefon notlarına eklendi: " + label);
            }
            // Sokak olayları: LLM sadece niyet önerir. Flörtöz değilse araba teklifi yok sayılır;
            // silahlı/silahsız ayrımını zar belirler (street_events). NPC cümlesini bitirince tetiklenir.
            if (shop_ && context_ == "street" && !person_call_ && pending_ == Pending::None) {
                if (intent == "confront") pending_ = Pending::Fight;
                else if (intent == "accept_ride" && shop_->npc_json.value("flirty", false)) pending_ = Pending::Ride;
                if (pending_ != Pending::None) {
                    speaking_seen_ = false;
                    pending_deadline_ = now + 12000;  // TTS yoksa da olay takılı kalmasın
                }
            }
            // Polis çevirmesi/arama: LLM niyet önerir (comply/flee/insult/plead); sonucu weaponwatch (kod) belirler.
            if (shop_ && context_ == "police" && fresh_transcript_ && weaponwatch::InDialogue()) {
                fresh_transcript_ = false;
                weaponwatch::OnPoliceIntent(intent);
                if ((intent == "comply" || intent == "flee") && pending_ == Pending::None) {
                    pending_ = Pending::HangUp;  // polis cümlesini bitirince konuşma kapanır, arama/takip sürer
                    speaking_seen_ = false;
                    pending_deadline_ = now + 9000;
                }
            }
            // Seyyar satıcıyı sormak: bilip bilmediğini ve konumun işlenmesini KOD belirler (ününe göre şans); LLM sadece söyler.
            if (shop_ && fresh_transcript_ && intent == "ask_vendor" && vendor_ask_ && (context_ == "street" || context_ == "shop")) {
                fresh_transcript_ = false;
                SendFollowup("vendor_tip", vendor_ask_());
            }
            // Hakaret / taciz / zorbalık / tehdit: LLM sadece bildirir; onur kaybını ve sonucu (küsme, polis, kavga) KOD belirler.
            if (shop_ && fresh_transcript_ && intent == "report_offense" && (context_ == "street" || context_ == "shop" || context_ == "quest")) {
                fresh_transcript_ = false;
                const std::string kind = m.contains("item") && m["item"].is_string() ? m["item"].get<std::string>() : "insult";
                const bool street = context_ == "street";
                int delta = -3;
                std::string counter = "hakaret", why = "Hakaret ettin";
                std::string consequence = "leaves";
                if (kind == "harassment") {
                    delta = -7; counter = "taciz"; why = "Tacizde bulundun";
                    consequence = street && street_events::Chance(0.45) ? "police" : "leaves";
                } else if (kind == "bullying") {
                    delta = -6; counter = "zorbalik"; why = "Birini aşağıladın";
                    consequence = street && street_events::Chance(0.30) ? "fight" : "leaves";
                } else if (kind == "threat") {
                    delta = -8; counter = "tehdit"; why = "Tehdit ettin";
                    consequence = street ? (street_events::Chance(0.5) ? "police" : "fight") : "leaves";
                } else if (!street || street_events::Chance(0.5)) {
                    consequence = "leaves";
                } else {
                    consequence = "warning";
                }
                karma::Add(delta, why, counter);
                if (pending_ == Pending::None) {
                    if (consequence == "police") pending_ = Pending::CallPolice;
                    else if (consequence == "fight") pending_ = Pending::Fight;
                    else if (consequence == "leaves") pending_ = street ? Pending::Leave : Pending::HangUp;
                    if (pending_ != Pending::None) {
                        speaking_seen_ = false;
                        pending_deadline_ = now + 12000;
                    }
                }
                SendFollowup("offense", {{"offense", kind}, {"consequence", consequence}});
            }
            // Para verme iyiliği: miktarı LLM yazar, düşme ve onur kazancı kodda (yetersiz bakiye -> reddedilir).
            if (shop_ && fresh_transcript_ && intent == "give_money" && (context_ == "street" || context_ == "shop" || context_ == "quest")) {
                fresh_transcript_ = false;
                const int amount = std::max(1, m.value("qty", 1));
                json facts = {{"kind", "money"}, {"amount", amount}, {"accepted", false}};
                if (gave_money_) {
                    facts["reason"] = "already_gave";
                } else if (!economy::Spend(amount)) {
                    facts["reason"] = "insufficient_funds";
                } else {
                    gave_money_ = true;
                    facts["accepted"] = true;
                    karma::Add(amount >= 500 ? 6 : amount >= 100 ? 4 : amount >= 20 ? 2 : 1, "Birine yardım ettin", "iyilik_para");
                }
                SendFollowup("favor", facts);
            }
            // Yolculuk iyiliği (sokak yayası): hedefi kod çözer, binmeyi ve varışı RideFavor yönetir.
            if (shop_ && context_ == "street" && places_ && fresh_transcript_ && intent == "give_ride" &&
                m.contains("destination") && m["destination"].is_string()) {
                fresh_transcript_ = false;
                const Vector3 from = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true);
                Place pl;
                json facts = {{"kind", "ride"}, {"accepted", false}};
                if (!places_->Resolve(m["destination"].get<std::string>(), from, pl)) {
                    facts["reason"] = "unknown_destination";
                } else {
                    std::string why;
                    if (ride_favor_.Start(shop_->ped, pl.pos, pl.name, why)) {
                        facts["accepted"] = true;
                        facts["destination"] = pl.name;
                        shop_->ped = 0;  // yaya artık RideFavor'un; oturum kapanınca serbest bırakılmasın
                    } else {
                        facts["reason"] = why;
                    }
                }
                SendFollowup("favor", facts);
            }
            // Görev: LLM 'advance' önerir; sıra bu NPC'deyse KOD aşamayı ilerletir, ödülü verir.
            if (shop_ && context_ == "quest" && missions_ && fresh_transcript_ && intent == "advance") {
                fresh_transcript_ = false;
                const json r = missions_->TryAdvance(quest_npc_id_, quest_call_, m.contains("item") && m["item"].is_string() ? m["item"].get<std::string>() : std::string());
                if (r.value("advanced", false)) {
                    Notify(r.value("completed", false) ? "Gorev tamamlandi! +$" + std::to_string(r.value("reward", 0))
                                                        : "Gorev guncellendi");
                    SendState();  // sonraki turda LLM yeni aşamayı bilsin
                }
            }
            // Kod kararlı görev aşaması (auto_advance): oyuncu NPC ile gerçekten konuştu, LLM'e sorulmadan kod ilerletir.
            if (shop_ && context_ == "quest" && missions_ && fresh_transcript_) {
                const bool is_farewell = (intent == "leave" || intent == "hang_up");
                const json r = missions_->TryAutoAdvance(quest_npc_id_, quest_call_, is_farewell);
                if (r.value("advanced", false)) {
                    Notify(r.value("completed", false) ? "Gorev tamamlandi!" : "Gorev guncellendi");
                    SendState();
                } else if (!is_farewell && r.contains("need")) {
                    Notify("Gorev icin sohbet: " + std::to_string(r.value("turns", 0)) + "/" + std::to_string(r.value("need", 5)) +
                           " (veya vedalasarak tamamlayabilirsin)");
                    SendState();  // LLM'in 'note' alanındaki sayaç güncellensin
                }
            }
            // Hangout daveti: LLM sadece niyeti önerir; kabul şansı ilişkiye göre, koşullar (araç, koltuk, yıldız) kodda.
            if (shop_ && context_ == "quest" && quest_call_ && hangout_ && missions_ && fresh_transcript_ && intent == "invite") {
                fresh_transcript_ = false;
                json facts = {{"kind", "invite"}, {"accepted", false}};
                Missions::Talk who;
                const std::string rel = missions_->Relationship(quest_npc_id_);
                const double chance = rel == "partner" ? 1.0 : rel == "close_friend" ? 0.95 : rel == "friend" ? 0.85
                                      : rel == "acquaintance" ? 0.5 : 0.0;
                if (!missions_->PersonTalk(quest_npc_id_, who)) {
                    facts["reason"] = "unknown_contact";
                } else if (!street_events::Chance(chance)) {
                    facts["reason"] = "busy_now";
                } else {
                    const std::string r = hangout_->Request(who);
                    facts["accepted"] = r == "ok";
                    facts["reason"] = r;
                }
                if (facts.value("accepted", false)) Notify("Arkadas davet edildi");
                SendFollowup("invite", facts);
            }
            // Telefonla tanışılan NPC buluşmayı kabul ederse, mevcut araç buluşması akışına katılır.
            if (shop_ && person_call_ && hangout_ && fresh_transcript_ && intent == "invite") {
                fresh_transcript_ = false;
                Missions::Talk who;
                who.npc_id = street_.id;
                who.name = npc_name_;
                who.npc_json = street_.npc_json;
                who.model = street_.npc_json.value("model", "");
                who.fallback_models = street_.npc_json.value("fallback_models", std::vector<std::string>());
                json facts = {{"kind", "invite"}, {"accepted", false}};
                const std::string result = hangout_->Request(who);
                facts["accepted"] = result == "ok";
                if (result != "ok") facts["reason"] = result;
                if (result == "ok") Notify("Buluşma kabul edildi; arabanın yanında bekliyor");
                SendFollowup("invite", facts);
            }
            // Adres sorma: LLM sadece "nereye" metnini çıkarır, gerisini kod çözer.
            if (shop_ && context_ == "street" && fresh_transcript_ && intent == "ask_directions" &&
                m.contains("destination") && m["destination"].is_string()) {
                fresh_transcript_ = false;
                Directions(m["destination"].get<std::string>());
            }
            // Hız ricası: LLM sadece niyeti önerir; kabul/ret (taksi %80, helikopter hiç) KODDA belirlenir.
            if (shop_ && (context_ == "taxi" || context_ == "heli") && services_ && fresh_transcript_ &&
                (intent == "drive_faster" || intent == "drive_slower")) {
                fresh_transcript_ = false;
                const bool faster = intent == "drive_faster";
                const json facts = services_->RequestSpeed(faster ? 1 : -1);
                Notify(facts.value("granted", false) ? (faster ? "Sofor hizlandi" : "Sofor yavasladi") : "Ricani kabul etmedi");
                SendFollowup("ride", facts);
            }
            // Yolculuk: LLM sadece "nereye" metnini çıkarır; yer, mesafe, ücret ve sürüş kodda.
            if (shop_ && (context_ == "taxi" || context_ == "heli") && services_ && places_ && fresh_transcript_ &&
                intent == "set_destination" && m.contains("destination") && m["destination"].is_string()) {
                fresh_transcript_ = false;
                const json facts = services_->SetDestination(m["destination"].get<std::string>(), *places_);
                if (facts.value("accepted", false)) Notify("Yolculuk basladi: $" + std::to_string(facts.value("fare", 0)));
                SendFollowup("ride", facts);
            }
            // Numara isteme (sokak): verip vermeyeceğini kod belirler; verirse rehbere kaydedilir (numara NPC'ye göre sabit)
            if (shop_ && context_ == "street" && !person_call_ && fresh_transcript_ && intent == "give_number") {
                fresh_transcript_ = false;
                int kv = 0;
                try { kv = std::stoi(karma::Lookup("karma")); } catch (...) {}
                const json facts = phoneos::OfferNumber(shop_->npc_json, kv);
                if (facts.value("accepted", false) && !facts.value("already_saved", false)) Notify("Numara rehbere kaydedildi");
                SendFollowup("number", facts);
            }
            // Otostop: LLM sadece "nereye" metnini çıkarır; gidilip gidilmeyeceği, mesafe ve sürüş kodda.
            if (shop_ && context_ == "hitch" && places_ && fresh_transcript_ && intent == "set_destination" && m.contains("destination") &&
                m["destination"].is_string()) {
                fresh_transcript_ = false;
                const std::string asked = m["destination"].get<std::string>();
                const std::string folded = Places::Fold(asked);
                json facts;
                if (folded.find("isaret") != std::string::npos || folded.find("waypoint") != std::string::npos || folded.find("harita") != std::string::npos) {
                    facts = hitchhike::UseWaypoint();
                } else {
                    Place pl;
                    if (places_->Resolve(asked, ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true), pl)) facts = hitchhike::SetDestination(pl.pos, pl.name);
                    else facts = {{"kind", "hitch"}, {"accepted", false}, {"reason", "unknown_destination"}, {"asked", asked}};
                }
                if (facts.value("accepted", false)) Notify("Sürücü yola çıktı");
                SendFollowup("hitch", facts);
            }
            // Telefon: LLM hangi hizmeti istediğini önerir; gelip gelmeyeceğini, süreyi ve ücreti kod belirler.
            if (shop_ && context_ == "phone" && places_ && fresh_transcript_ && intent == "transit_route" &&
                m.contains("destination") && m["destination"].is_string()) {
                // Toplu taşıma navigasyonu: LLM sadece 'nereye' metnini çıkarır; hat, durak, süre ve ücret kodda.
                fresh_transcript_ = false;
                const Vector3 from = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true);
                Place pl;
                json facts;
                const std::string asked = m["destination"].get<std::string>();
                if (places_->Resolve(asked, from, pl)) facts = transit::PlanRoute(from, pl.pos, pl.name);
                else facts = {{"kind", "transit_route"}, {"found", false}, {"reason", "unknown_destination"}, {"asked", asked}};
                if (facts.value("found", false)) Notify("Biniş durağı haritaya işaretlendi");
                SendFollowup("transit", facts);
            }
            if (shop_ && context_ == "phone" && services_ && fresh_transcript_) {
                struct Map { const char* intent; ServiceKind kind; };
                static const Map kMap[] = {{"call_taxi", ServiceKind::Taxi},
                                           {"call_heli_taxi", ServiceKind::HeliTaxi},
                                           {"call_police", ServiceKind::Police},
                                           {"call_ambulance", ServiceKind::Ambulance},
                                           {"call_fire", ServiceKind::Fire},
                                           {"call_mechanic", ServiceKind::Mechanic}};
                for (const auto& e : kMap) {
                    if (intent == e.intent) {
                        fresh_transcript_ = false;
                        const json facts = services_->Dispatch(e.kind);
                        if (facts.value("accepted", false)) Notify("Hizmet yolda: " + std::to_string(facts.value("eta_seconds", 0)) + " sn");
                        SendFollowup("phone", facts);
                    }
                }
                if (intent == "call_my_vehicle" && garage_) {  // sahip olunan aracı çağır (vale)
                    fresh_transcript_ = false;
                    const std::string which = m.contains("vehicle") && m["vehicle"].is_string() ? m["vehicle"].get<std::string>() : "";
                    const json facts = garage_->Summon(which);
                    if (facts.value("accepted", false)) Notify("Aracin yolda: " + std::to_string(facts.value("eta_seconds", 0)) + " sn");
                    SendFollowup("phone", facts);
                }
                if (intent == "locate_vehicle" && garage_) {  // aracım nerede
                    fresh_transcript_ = false;
                    const std::string which = m.contains("vehicle") && m["vehicle"].is_string() ? m["vehicle"].get<std::string>() : "";
                    const json facts = garage_->Locate(which);
                    if (facts.value("waypoint_set", false)) Notify("Arac haritada isaretlendi");
                    SendFollowup("phone", facts);
                }
                if (intent == "park_vehicle" && garage_) {  // otopark görevlisi: dışarıdaki aracı garaja aldır
                    fresh_transcript_ = false;
                    const std::string which = m.contains("vehicle") && m["vehicle"].is_string() ? m["vehicle"].get<std::string>() : "";
                    const json facts = garage_->ParkVehicle(which);
                    if (facts.value("accepted", false)) Notify("Arac garaja alindi");
                    SendFollowup("phone", facts);
                }
                if (intent == "hang_up" && pending_ == Pending::None) {
                    pending_ = Pending::HangUp;  // veda cümlesi bitince kapanır
                    speaking_seen_ = false;
                    pending_deadline_ = now + 8000;
                }
            }
            if (shop_ && jail_ && fresh_transcript_ &&
                (context_ == "wanted_sule" || context_ == "wanted_bar") && intent == "clear_wanted") {
                fresh_transcript_ = false;
                const json facts = jail_->AttemptWantedClear(context_ == "wanted_bar");
                if (facts.value("cleared", false)) Notify("Aranma kaydi silindi");
                SendFollowup("wanted_lawyer", facts);
            }
            // NPC vedalaştı / ayrılmak istedi (iyi günler vb.): cümle bitince diyalog sonlanır
            if (shop_ && pending_ == Pending::None && (intent == "leave" || intent == "hang_up")) {
                const bool is_call = (context_ == "phone" || context_ == "wanted_sule" || context_ == "wanted_bar" ||
                                      quest_call_ || person_call_);
                pending_ = is_call ? Pending::HangUp : Pending::Leave;
                speaking_seen_ = false;
                pending_deadline_ = now + 8000;
            }
            // Avukat: LLM sadece "tutuyorum" niyetini önerir; ücret, indirim ve tahliye kodda hesaplanır.
            if (shop_ && context_ == "lawyer" && jail_ && fresh_transcript_ && intent == "hire_lawyer") {
                fresh_transcript_ = false;
                last_result_ = jail_->HireLawyer();
                if (last_result_ == "lawyer_hired") {
                    pending_ = Pending::LawyerRelease;  // avukat cümlesini bitirince tahliye süreci başlar
                    speaking_seen_ = false;
                    pending_deadline_ = now + 12000;
                    Notify("Avukat tutuldu");
                } else if (last_result_ == "insufficient_funds") {
                    Notify("Avukat ücreti için bakiye yetersiz");
                }
                SendState();
            }
            // Satın alma niyeti: her oyuncu cümlesi için en fazla bir satış.
            if (shop_ && context_ == "shop" && fresh_transcript_ && (intent == "accept" || intent == "add_to_cart") &&
                m.contains("item") && m["item"].is_string()) {
                fresh_transcript_ = false;
                TryPurchase(m["item"].get<std::string>(), m.value("qty", 1));
            }
        } else if (type == "speaking") {
            speaking_ = m.value("state", "") == "start";
            if (speaking_) speaking_seen_ = true;
        } else if (type == "error") {
            thinking_since_ = 0;
            Notify("Hata: " + m.value("message", "bilinmiyor"));
        }
    } catch (const std::exception& e) {
        Log(std::string("bozuk mesaj: ") + e.what());
    }
}

void Conversation::Update(const Vector3& player_pos, bool toggle_pressed) {
    ride_favor_.Update();  // iyilik yolculuğu konuşmadan bağımsız sürer
    std::string raw;
    while (ws_->Poll(raw)) HandleMessage(raw);
    if (!shop_) {
        if (typing_) {  // oturum yazarken kapandı: yazı kutusunu da kapat
            textinput::Cancel();
            overlay::SetInput("", false);
        }
        typing_ = false;
        return;
    }
    if (typing_) toggle_pressed = false;  // klavyede yazılan 'e' oturumu kapatmasın

    if (context_ == "street" || (context_ == "quest" && !quest_call_)) {
        if (ENTITY::DOES_ENTITY_EXIST(shop_->ped)) shop_->pos = ENTITY::GET_ENTITY_COORDS(shop_->ped, true);
        if (ENTITY::IS_ENTITY_DEAD(shop_->ped)) return End("npc oldu");
    }
    if (!ws_->connected()) return End("baglanti koptu");
    if (toggle_pressed) return End("oyuncu kapatti");
    if (context_ == "lawyer") {
        // Avukatın fiziksel NPC'si yok: görüşme, hücreden çıkınca (ceza çekilince) biter.
        if (!jail_ || !jail_->in_cell()) return End("hucreden cikti");
    } else if (context_ == "taxi" || context_ == "heli") {
        // Şoför/pilot aracın içinde: görüşme yolculukla biter (indin, araç gitti...). Varışta ücret sözlü anlatılır.
        if (!services_ || !services_->InRide()) return End("yolculuk bitti");
        json arrival;
        if (services_->ConsumeArrival(arrival)) SendFollowup("ride", arrival);
    } else if (context_ == "hitch") {
        // Sürücü aracın içinde: görüşme yolculukla biter (indin, araç gitti...). Varış sözlü anlatılır.
        if (!hitchhike::Riding()) return End("yolculuk bitti");
        json arrival;
        if (hitchhike::ConsumeArrival(arrival)) SendFollowup("hitch", arrival);
    } else if (person_call_) {
        if (jail_ && !jail_->CanUsePhone()) return End("telefon kapandi");  // kayıtlı kişiyle telefon görüşmesi: mesafe yok
    } else if (context_ == "wanted_sule" || context_ == "wanted_bar") {
        if (!jail_ || !jail_->CanUsePhone()) return End("telefon kapandi");
    } else if (context_ == "phone" || (context_ == "quest" && quest_call_)) {
        // Telefonun mesafesi yok: gözaltı/taser başlayınca kapanır, kovalamacada açık kalır.
        if (jail_ && !jail_->CanUsePhone()) return End("telefon kapandi");
    } else {
        if (Dist2D(player_pos, shop_->pos) > shop_->interact_radius * kEndDistanceFactor) return End("uzaklasti");
        if (!ENTITY::DOES_ENTITY_EXIST(shop_->ped)) return End("npc yok");
    }
    if (pending_ != Pending::None) RunPendingEvent();
    if (!shop_) return;  // olay oturumu kapatmış olabilir

    // T: Online tarzı yazarak konuş. Sesli konuşmayla aynı hattan geçer (companion STT'yi atlar).
    if (typing_) {
        std::string text;
        bool finished = false, submitted = false;
        if (native_kb_) {
            const int status = GAMEPLAY::UPDATE_ONSCREEN_KEYBOARD();  // 0: yazılıyor, 1: tamam, 2: iptal, 3: kullanılamaz
            if (status == 1) {
                const char* result = GAMEPLAY::GET_ONSCREEN_KEYBOARD_RESULT();
                text = result ? result : "";
                finished = submitted = true;
            } else if (status != 0) {
                finished = true;
            }
        } else {
            // Kendi yazı kutumuz: Türkçe karakterler (ş ı ğ İ ç ö ü) desteklenir.
            textinput::Pump();
            CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);  // yazarken oyun komutları çalışmasın
            overlay::SetInput(textinput::Current(), true);
            if (textinput::ConsumeSubmitted(text)) finished = submitted = true;
            else if (textinput::ConsumeCancelled() || !textinput::Active()) finished = true;
        }
        if (finished) {
            typing_ = false;
            overlay::SetInput("", false);
            if (submitted && !text.empty()) {
                SendState();
                ws_->Send(json{{"type", "text_input"}, {"text", text}}.dump());
            }
        }
        return;  // yazarken PTT dahil başka tuş işlenmez
    }
    const bool t_down = (GetAsyncKeyState('T') & 0x8000) != 0;
    if (t_down && !t_was_down_ && !speaking_ && !ptt_down_) {
        if (overlay::Ready()) {
            textinput::Begin();
            native_kb_ = false;
        } else {
            // ImGui yoksa oyunun ekran klavyesine dön (Türkçe harf desteklemeyebilir; LLM aksansız metni de anlar).
            GAMEPLAY::DISPLAY_ONSCREEN_KEYBOARD(true, const_cast<char*>("FMMC_KEY_TIP8"), const_cast<char*>(""),
                                                const_cast<char*>(""), const_cast<char*>(""), const_cast<char*>(""),
                                                const_cast<char*>(""), 120);
            native_kb_ = true;
        }
        typing_ = true;
    }
    t_was_down_ = t_down;

    const bool down = (GetAsyncKeyState(controls::TalkKey()) & 0x8000) != 0;
    if (down && !ptt_down_ && !speaking_) {  // NPC konuşurken konuşulmaz
        ptt_down_ = true;
        SendState();  // CLAUDE.md §10: her turdan önce güncel durum (para, stok)
        ws_->Send(R"({"type":"ptt_down"})");
    } else if (!down && ptt_down_) {
        ptt_down_ = false;
        ws_->Send(R"({"type":"ptt_up"})");
    }
}

Phone::CallView Conversation::GetPhoneView() const {
    Phone::CallView v;
    if (!shop_ || !(context_ == "phone" || context_ == "wanted_sule" || context_ == "wanted_bar" ||
                    person_call_ || (context_ == "quest" && quest_call_))) return v;
    const unsigned long long now = GetTickCount64();
    v.in_call = true;
    v.caller = npc_name_ == "Haviland" ? "Haviland" : (npc_name_.empty() ? "Santral" : "Santral: " + npc_name_);
    if (npc_until_ > now) v.caller_line = npc_line_;
    else if (thinking_since_ != 0 && now - thinking_since_ < 60000ULL) v.thinking = true;  // yazı telefon arayüzünde tek balon olarak çizilir
    if (player_until_ > now) v.player_line = player_line_;
    v.assistant = npc_name_ == "Haviland";
    v.listening = ptt_down_;
    v.speaking = speaking_;
    v.typing = typing_;
    return v;
}

std::string Conversation::Thinking(unsigned long long now) const {
    return std::string("düşünüyor") + std::string(1 + (now / 400) % 3, '.');  // sessiz yazı: TTS ve bildirim yok
}

void Conversation::Draw() {
    const unsigned long long now = GetTickCount64();
    // Telefon açıkken santral görüşmesi telefonun içinde gösterilir; altyazıyı tekrar çizme.
    if (phone_visible_ && shop_ && (context_ == "phone" || context_ == "wanted_sule" || context_ == "wanted_bar" ||
                                   person_call_ || (context_ == "quest" && quest_call_))) return;
    if (npc_until_ > now) DrawLine(npc_name_ + ": " + npc_line_, 0.86f, 255, 220, 120);
    else if (thinking_since_ != 0 && now - thinking_since_ < 60000ULL) DrawLine(npc_name_ + ": " + Thinking(now), 0.86f, 200, 200, 200);
    if (player_until_ > now) DrawLine("Sen: " + player_line_, 0.82f, 200, 230, 255);
    if (notice_until_ > now) {
        DrawLine(notice_, 0.78f, 255, 255, 255);
    } else if (shop_) {
        if (ptt_down_) DrawLine("Dinleniyor...", 0.78f, 255, 120, 120);
        else if (!speaking_) {
            DrawLine(controls::KeyName(controls::TalkKey()) + ": basili tut ve konus    T: yaz    E: bitir", 0.78f, 255, 255, 255);
        }
    }
}

}  // namespace hikaye
