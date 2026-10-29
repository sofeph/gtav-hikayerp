#include "creator.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "overlay.h"
#include "textinput.h"

using nlohmann::json;

namespace hikaye {

namespace {
const char* const kMotherNames[kMotherCount] = {"Hannah", "Aubrey", "Jasmine", "Gisele", "Amelia", "Isabella", "Zoe", "Ava",
                                                "Camila", "Violet", "Sophia", "Evelyn", "Nicole", "Ashley", "Gracie", "Brianna",
                                                "Natalie", "Olivia", "Elizabeth", "Charlotte", "Emma", "Misty"};
const char* const kFatherNames[kFatherCount] = {"Benjamin", "Daniel", "Joshua", "Noah", "Andrew", "Juan", "Alex", "Isaac",
                                                "Evan", "Ethan", "Vincent", "Angel", "Diego", "Adrian", "Gabriel", "Michael",
                                                "Santiago", "Kevin", "Louis", "Samuel", "Anthony", "Claude", "Niko", "John"};
const char* const kFaceNames[20] = {"Burun genişliği", "Burun yüksekliği", "Burun uzunluğu", "Burun kemiği", "Burun ucu",
                                    "Burun eğriliği", "Kaş yüksekliği", "Kaş derinliği", "Elmacık yüksekliği",
                                    "Elmacık genişliği", "Yanak genişliği", "Göz açıklığı", "Dudak kalınlığı",
                                    "Çene kemiği genişliği", "Çene kemiği uzunluğu", "Çene ucu yüksekliği",
                                    "Çene ucu uzunluğu", "Çene ucu genişliği", "Çene çukuru", "Boyun kalınlığı"};

int Wrap(int v, int dir, int lo, int hi) {
    if (hi < lo) return lo;
    v += dir;
    if (v > hi) v = lo;
    if (v < lo) v = hi;
    return v;
}
float ClampF(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }

std::string Pct(float v) {
    char b[16];
    std::snprintf(b, sizeof(b), "%d%%", static_cast<int>(std::lround(v * 100.f)));
    return b;
}
std::string Num(int v) { return std::to_string(v); }

// Seçenek listesinde (boş = "-") dolaşır.
void CycleString(std::string& cur, const std::vector<std::string>& opts, int dir) {
    if (opts.empty()) return;
    int idx = -1;
    for (size_t i = 0; i < opts.size(); ++i) {
        if (opts[i] == cur) idx = static_cast<int>(i);
    }
    idx = Wrap(idx, dir, 0, static_cast<int>(opts.size()) - 1);
    cur = opts[static_cast<size_t>(idx)];
}
std::string Dash(const std::string& s) { return s.empty() ? "-" : s; }

std::vector<std::string> StringList(const json* pool, const char* key) {
    std::vector<std::string> out{""};
    if (pool && pool->contains(key)) {
        for (const auto& v : (*pool)[key]) out.push_back(v.get<std::string>());
    }
    return out;
}
}  // namespace

void Creator::Begin(CharacterProfile* profile, Ped ped, const json* pool, SwapModelFn swap) {
    p_ = profile;
    ped_ = ped;
    pool_ = pool;
    swap_ = std::move(swap);
    tabs_ = {"Kimlik", "Atalar", "Yüz", "Saç", "Giyim", "Geçmiş", "Bitir"};
    tab_ = 0;
    selected_ = 0;
    typing_ = false;
    result_ = Result::Continue;
    BuildRows();
    Move(0);

    ENTITY::FREEZE_ENTITY_POSITION(ped_, true);
    ENTITY::SET_ENTITY_INVINCIBLE(ped_, true);
    cam_ = CAM::CREATE_CAM(const_cast<char*>("DEFAULT_SCRIPTED_CAMERA"), true);
    CAM::SET_CAM_ACTIVE(cam_, true);
    CAM::RENDER_SCRIPT_CAMS(true, true, 600, true, false);
    Reapply();
}

void Creator::End() {
    if (cam_ != 0) {
        CAM::RENDER_SCRIPT_CAMS(false, true, 600, true, false);
        CAM::SET_CAM_ACTIVE(cam_, false);
        CAM::DESTROY_CAM(cam_, false);
        cam_ = 0;
    }
    if (ENTITY::DOES_ENTITY_EXIST(ped_)) {
        ENTITY::FREEZE_ENTITY_POSITION(ped_, false);
        ENTITY::SET_ENTITY_INVINCIBLE(ped_, false);
    }
    overlay::MenuView off;
    overlay::SetMenu(off);
    overlay::SetInput("", false);
    textinput::Cancel();
}

void Creator::Reapply() {
    if (!ENTITY::DOES_ENTITY_EXIST(ped_)) return;
    ApplyToPed(ped_, *p_);
}

void Creator::AddOverlay(int tab, int id, const std::string& name, bool color) {
    auto count = [id] { return PED::_GET_NUM_HEAD_OVERLAY_VALUES(id); };
    Row style;
    style.tab = tab;
    style.label = name;
    style.value = [this, id] { return p_->overlays[id].index == 255 ? std::string("Yok") : Num(p_->overlays[id].index + 1); };
    style.change = [this, id, count](int d) {
        int cur = p_->overlays[id].index == 255 ? -1 : p_->overlays[id].index;  // -1 = yok
        cur = Wrap(cur, d, -1, count() - 1);
        p_->overlays[id].index = cur < 0 ? 255 : cur;
        Reapply();
    };
    rows_.push_back(style);

    Row op;
    op.tab = tab;
    op.label = name + " yoğunluğu";
    op.value = [this, id] { return Pct(p_->overlays[id].opacity); };
    op.change = [this, id](int d) {
        p_->overlays[id].opacity = ClampF(p_->overlays[id].opacity + 0.1f * static_cast<float>(d), 0.f, 1.f);
        Reapply();
    };
    rows_.push_back(op);

    if (color) {
        Row c;
        c.tab = tab;
        c.label = name + " rengi";
        c.value = [this, id] { return Num(p_->overlays[id].color + 1); };
        c.change = [this, id](int d) {
            p_->overlays[id].color = Wrap(p_->overlays[id].color, d, 0, 63);
            Reapply();
        };
        rows_.push_back(c);
    }
}

void Creator::AddComp(int tab, const std::string& name, int comp) {
    Row r;
    r.tab = tab;
    r.label = name;
    r.value = [this, comp] { return Num(p_->comps[comp].drawable); };
    r.change = [this, comp](int d) {
        const int n = PED::GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS(ped_, comp);
        p_->comps[comp].drawable = Wrap(p_->comps[comp].drawable, d, 0, n - 1);
        p_->comps[comp].texture = 0;
        Reapply();
    };
    rows_.push_back(r);
    Row t;
    t.tab = tab;
    t.label = name + " rengi/deseni";
    t.value = [this, comp] { return Num(p_->comps[comp].texture); };
    t.change = [this, comp](int d) {
        const int n = PED::GET_NUMBER_OF_PED_TEXTURE_VARIATIONS(ped_, comp, p_->comps[comp].drawable);
        p_->comps[comp].texture = Wrap(p_->comps[comp].texture, d, 0, n - 1);
        Reapply();
    };
    rows_.push_back(t);
}

void Creator::AddProp(int tab, const std::string& name, int prop) {
    Row r;
    r.tab = tab;
    r.label = name;
    r.value = [this, prop] { return p_->props[prop].drawable < 0 ? std::string("Yok") : Num(p_->props[prop].drawable); };
    r.change = [this, prop](int d) {
        const int n = PED::GET_NUMBER_OF_PED_PROP_DRAWABLE_VARIATIONS(ped_, prop);
        p_->props[prop].drawable = Wrap(p_->props[prop].drawable, d, -1, n - 1);
        p_->props[prop].texture = 0;
        Reapply();
    };
    rows_.push_back(r);
    Row t;
    t.tab = tab;
    t.label = name + " rengi";
    t.value = [this, prop] { return Num(p_->props[prop].texture); };
    t.change = [this, prop](int d) {
        if (p_->props[prop].drawable < 0) return;
        const int n = PED::GET_NUMBER_OF_PED_PROP_TEXTURE_VARIATIONS(ped_, prop, p_->props[prop].drawable);
        p_->props[prop].texture = Wrap(p_->props[prop].texture, d, 0, n - 1);
        Reapply();
    };
    rows_.push_back(t);
}

void Creator::BuildRows() {
    rows_.clear();

    // --- Kimlik ---
    {
        Row name;
        name.tab = 0;
        name.label = "Ad Soyad";
        name.value = [this] { return Dash(p_->name); };
        name.enter = [this] {
            if (overlay::Ready()) {
                textinput::Begin();
                typing_ = true;
            }
        };
        rows_.push_back(name);
        Row sex;
        sex.tab = 0;
        sex.label = "Cinsiyet";
        sex.value = [this] { return std::string(p_->female ? "Kadın" : "Erkek") + (lock_gender_ ? " (kilitli)" : ""); };
        sex.change = [this](int) {
            if (lock_gender_) return;
            p_->female = !p_->female;
            p_->ResetOutfit();
            if (swap_) ped_ = swap_(p_->female);  // model değişir
            ENTITY::FREEZE_ENTITY_POSITION(ped_, true);
            ENTITY::SET_ENTITY_INVINCIBLE(ped_, true);
            Reapply();
        };
        rows_.push_back(sex);
    }

    // --- Atalar ---
    {
        Row mom;
        mom.tab = 1;
        mom.label = "Anne";
        mom.value = [this] { return std::string(kMotherNames[p_->mother]); };
        mom.change = [this](int d) {
            p_->mother = Wrap(p_->mother, d, 0, kMotherCount - 1);
            Reapply();
        };
        rows_.push_back(mom);
        Row dad;
        dad.tab = 1;
        dad.label = "Baba";
        dad.value = [this] { return std::string(kFatherNames[p_->father]); };
        dad.change = [this](int d) {
            p_->father = Wrap(p_->father, d, 0, kFatherCount - 1);
            Reapply();
        };
        rows_.push_back(dad);
        Row shape;
        shape.tab = 1;
        shape.label = "Yüz benzerliği (anne ↔ baba)";
        shape.value = [this] { return Pct(p_->shape_mix); };
        shape.change = [this](int d) {
            p_->shape_mix = ClampF(p_->shape_mix + 0.05f * static_cast<float>(d), 0.f, 1.f);
            Reapply();
        };
        rows_.push_back(shape);
        Row skin;
        skin.tab = 1;
        skin.label = "Ten benzerliği (anne ↔ baba)";
        skin.value = [this] { return Pct(p_->skin_mix); };
        skin.change = [this](int d) {
            p_->skin_mix = ClampF(p_->skin_mix + 0.05f * static_cast<float>(d), 0.f, 1.f);
            Reapply();
        };
        rows_.push_back(skin);
    }

    // --- Yüz ---
    for (int i = 0; i < 20; ++i) {
        Row r;
        r.tab = 2;
        r.label = kFaceNames[i];
        r.value = [this, i] { return Pct(p_->face[i]); };
        r.change = [this, i](int d) {
            p_->face[i] = ClampF(p_->face[i] + 0.1f * static_cast<float>(d), -1.f, 1.f);
            Reapply();
        };
        rows_.push_back(r);
    }

    // --- Saç ve yüz detayları ---
    {
        Row hair;
        hair.tab = 3;
        hair.label = "Saç stili";
        hair.value = [this] { return Num(p_->hair); };
        hair.change = [this](int d) {
            p_->hair = Wrap(p_->hair, d, 0, PED::GET_NUMBER_OF_PED_DRAWABLE_VARIATIONS(ped_, 2) - 1);
            Reapply();
        };
        rows_.push_back(hair);
        Row hc;
        hc.tab = 3;
        hc.label = "Saç rengi";
        hc.value = [this] { return Num(p_->hair_color + 1); };
        hc.change = [this](int d) {
            p_->hair_color = Wrap(p_->hair_color, d, 0, 63);
            Reapply();
        };
        rows_.push_back(hc);
        Row hh;
        hh.tab = 3;
        hh.label = "Vurgu rengi";
        hh.value = [this] { return Num(p_->hair_highlight + 1); };
        hh.change = [this](int d) {
            p_->hair_highlight = Wrap(p_->hair_highlight, d, 0, 63);
            Reapply();
        };
        rows_.push_back(hh);
        Row eye;
        eye.tab = 3;
        eye.label = "Göz rengi";
        eye.value = [this] { return Num(p_->eye_color + 1); };
        eye.change = [this](int d) {
            p_->eye_color = Wrap(p_->eye_color, d, 0, 31);
            Reapply();
        };
        rows_.push_back(eye);
    }
    AddOverlay(3, 2, "Kaş", false);
    AddOverlay(3, 1, "Sakal", false);
    AddOverlay(3, 10, "Göğüs kılı", false);
    AddOverlay(3, 0, "Leke / çil", false);
    AddOverlay(3, 3, "Yaşlanma", false);
    AddOverlay(3, 6, "Ten pürüzü", false);
    AddOverlay(3, 7, "Güneş hasarı", false);
    AddOverlay(3, 9, "Ben", false);
    AddOverlay(3, 4, "Makyaj", true);
    AddOverlay(3, 5, "Allık", true);
    AddOverlay(3, 8, "Ruj", true);

    // --- Giyim ---
    AddComp(4, "Üst", 11);
    AddComp(4, "Atlet / iç", 8);
    AddComp(4, "Kollar", 3);
    AddComp(4, "Alt", 4);
    AddComp(4, "Ayakkabı", 6);
    AddComp(4, "Maske", 1);
    AddComp(4, "Aksesuar", 7);
    AddComp(4, "Çanta", 5);
    AddProp(4, "Şapka", 0);
    AddProp(4, "Gözlük", 1);
    AddProp(4, "Küpe", 2);
    AddProp(4, "Saat", 6);
    AddProp(4, "Bileklik", 7);

    // --- Geçmiş (ortak nokta sistemi: hemşehrilik, askerlik, meslek, takım) ---
    {
        std::vector<std::string> cities{""};
        if (pool_ && pool_->contains("hometowns")) {
            for (const auto& h : (*pool_)["hometowns"]) cities.push_back(h.value("city", ""));
        }
        Row town;
        town.tab = 5;
        town.label = "Memleket";
        town.value = [this] { return Dash(p_->hometown); };
        town.change = [this, cities](int d) {
            CycleString(p_->hometown, cities, d);
            p_->district.clear();
        };
        rows_.push_back(town);
        Row dist;
        dist.tab = 5;
        dist.label = "İlçe";
        dist.value = [this] { return Dash(p_->district); };
        dist.change = [this](int d) {
            std::vector<std::string> opts{""};
            if (pool_ && pool_->contains("hometowns")) {
                for (const auto& h : (*pool_)["hometowns"]) {
                    if (h.value("city", "") == p_->hometown) {
                        for (const auto& s : h["districts"]) opts.push_back(s.get<std::string>());
                    }
                }
            }
            CycleString(p_->district, opts, d);
        };
        rows_.push_back(dist);
        Row mil;
        mil.tab = 5;
        mil.label = "Askerlik yeri";
        mil.value = [this] { return p_->female ? std::string("-") : Dash(p_->military_unit); };
        mil.change = [this](int d) {
            if (!p_->female) CycleString(p_->military_unit, StringList(pool_, "military_units"), d);
        };
        rows_.push_back(mil);
        Row job;
        job.tab = 5;
        job.label = "Eski meslek";
        job.value = [this] { return Dash(p_->past_job); };
        job.change = [this](int d) { CycleString(p_->past_job, StringList(pool_, "past_jobs"), d); };
        rows_.push_back(job);
        Row team;
        team.tab = 5;
        team.label = "Tuttuğu takım";
        team.value = [this] { return Dash(p_->team); };
        team.change = [this](int d) { CycleString(p_->team, StringList(pool_, "teams"), d); };
        rows_.push_back(team);
    }

    // --- Bitir ---
    {
        Row save;
        save.tab = 6;
        save.label = "Kaydet ve Hikaye RP'ye başla";
        save.enter = [this] { result_ = Result::Done; };
        rows_.push_back(save);
        Row cancel;
        cancel.tab = 6;
        cancel.label = "Vazgeç (hikaye moduna dön)";
        cancel.enter = [this] { result_ = Result::Cancelled; };
        rows_.push_back(cancel);
    }
}

int Creator::RowsInTab(int tab) const {
    int n = 0;
    for (const auto& r : rows_) n += r.tab == tab ? 1 : 0;
    return n;
}

void Creator::Move(int dir) {
    // selected_ her zaman geçerli sekmedeki bir satırı göstermeli; dir=0: sekmenin ilk satırına git.
    std::vector<int> idx;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].tab == tab_ && !rows_[i].header) idx.push_back(static_cast<int>(i));
    }
    if (idx.empty()) return;
    if (dir == 0) {
        selected_ = idx.front();
        return;
    }
    size_t pos = 0;
    for (size_t i = 0; i < idx.size(); ++i) {
        if (idx[i] == selected_) pos = i;
    }
    pos = static_cast<size_t>((static_cast<int>(pos) + dir + static_cast<int>(idx.size())) % static_cast<int>(idx.size()));
    selected_ = idx[pos];
}

void Creator::UpdateCamera() {
    if (cam_ == 0 || !ENTITY::DOES_ENTITY_EXIST(ped_)) return;
    const bool body = tab_ >= 4;  // giyim ve sonrası: tüm vücut
    const float dist = body ? 3.2f : 1.05f;
    const float up = body ? 0.25f : 0.62f;
    const Vector3 c = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(ped_, 0.f, dist, up);
    const Vector3 look = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(ped_, 0.f, 0.f, body ? 0.0f : 0.62f);
    CAM::SET_CAM_COORD(cam_, c.x, c.y, c.z);
    CAM::POINT_CAM_AT_COORD(cam_, look.x, look.y, look.z);
    CAM::SET_CAM_FOV(cam_, body ? 42.f : 30.f);
}

void Creator::Publish() {
    overlay::MenuView v;
    v.open = true;
    v.title = "Karakter Oluştur";
    v.subtitle = p_->name.empty() ? "Adın henüz yok: Kimlik sekmesinde Enter ile yaz" : p_->name;
    v.tabs = tabs_;
    v.tab = tab_;
    v.hint = "PgUp/PgDn sekme   ↑↓ satır   ←→ değiştir   Enter onay";
    int first = -1;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].tab != tab_) continue;
        if (first < 0) first = static_cast<int>(i);
        overlay::MenuRow r;
        r.label = rows_[i].label;
        r.header = rows_[i].header;
        if (rows_[i].value) r.value = rows_[i].value();
        if (static_cast<int>(i) == selected_) v.selected = static_cast<int>(v.rows.size());
        v.rows.push_back(r);
    }
    overlay::SetMenu(v);
}

Creator::Result Creator::Update() {
    if (typing_) {  // ad yazılıyor (Türkçe karakterler desteklenir)
        textinput::Pump();
        CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
        overlay::SetInput(textinput::Current(), true);
        std::string text;
        if (textinput::ConsumeSubmitted(text)) {
            typing_ = false;
            overlay::SetInput("", false);
            if (!text.empty()) p_->name = text;
        } else if (textinput::ConsumeCancelled() || !textinput::Active()) {
            typing_ = false;
            overlay::SetInput("", false);
        }
        Publish();
        return Result::Continue;
    }

    CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);
    if (keys_.Pressed(VK_NEXT)) {  // PageDown
        tab_ = (tab_ + 1) % static_cast<int>(tabs_.size());
        Move(0);
    }
    if (keys_.Pressed(VK_PRIOR)) {  // PageUp
        tab_ = (tab_ + static_cast<int>(tabs_.size()) - 1) % static_cast<int>(tabs_.size());
        Move(0);
    }
    if (keys_.Repeat(VK_UP)) Move(-1);
    if (keys_.Repeat(VK_DOWN)) Move(1);
    const bool left = keys_.Repeat(VK_LEFT), right = keys_.Repeat(VK_RIGHT);
    if ((left || right) && selected_ >= 0 && selected_ < static_cast<int>(rows_.size()) && rows_[static_cast<size_t>(selected_)].change) {
        rows_[static_cast<size_t>(selected_)].change(right ? 1 : -1);
    }
    if (keys_.Pressed(VK_RETURN) && selected_ >= 0 && selected_ < static_cast<int>(rows_.size()) && rows_[static_cast<size_t>(selected_)].enter) {
        rows_[static_cast<size_t>(selected_)].enter();
    }
    UpdateCamera();
    Publish();
    return result_;
}

}  // namespace hikaye
