#include "installer.h"

#include <algorithm>
#include <cctype>

#include <windows.h>

#include "common.h"
#include "overlay.h"
#include "phoneos.h"
#include "textinput.h"

namespace hikaye {

namespace {
std::string NormKey(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c)) o += static_cast<char>(std::toupper(c));
    }
    return o;
}
}  // namespace

void Installer::Begin(const Missions::InstallSpec& spec) {
    spec_ = spec;
    active_ = true;
    step_ = Step::Welcome;
    sel_ = 0;
    mistakes_ = 0;
    custom_ = false;
    chosen_.clear();
    notice_.clear();
    typed_key_.clear();
}

void Installer::Fail(const std::string& generic, const std::string& hint) {
    ++mistakes_;
    notice_ = spec_.hints ? hint : generic;
}

bool Installer::Update(bool& success, int& mistakes) {
    if (!active_) return false;
    CONTROLS::DISABLE_ALL_CONTROL_ACTIONS(0);

    if (step_ == Step::Key) {  // ürün anahtarı yazımı: Enter onay, Esc geri
        textinput::Pump();
        overlay::SetInput(textinput::Current(), true);
        std::string text;
        if (textinput::ConsumeSubmitted(text)) {
            if (NormKey(text) == NormKey(spec_.key)) {
                notice_.clear();
                step_ = Step::Progress;
                progress_start_ = GetTickCount64();
            } else {
                Fail("Ürün anahtarı geçersiz.", "Ürün anahtarı geçersiz. Anahtarı Burak biliyor; ona sor.");
                textinput::Begin();
            }
        } else if (textinput::ConsumeCancelled() || !textinput::Active()) {
            step_ = Step::Disk;
            overlay::SetInput("", false);
        }
        Draw();
        return false;
    }

    std::vector<std::string> rows;  // seçilebilir satırlar (Draw ile aynı sırada)
    switch (step_) {
        case Step::Welcome: rows = {"İleri", "İptal"}; break;
        case Step::License: rows = {"Kabul ediyorum", "Kabul etmiyorum"}; break;
        case Step::Type: rows = {"Hızlı kurulum (önerilen)", "Özel kurulum"}; break;
        case Step::Components:
            for (const auto& c : spec_.components) rows.push_back(c.second);
            rows.push_back("İleri");
            break;
        case Step::Disk: rows = {"C: sürücüsü (2 GB boş)", "D: sürücüsü (180 GB boş)"}; break;
        case Step::Done: rows = {"Bitir"}; break;
        default: break;
    }
    if (!rows.empty()) sel_ = std::clamp(sel_, 0, static_cast<int>(rows.size()) - 1);
    // Fare: üzerine gelinen satır seçilir, sol tık onaylar; sağ tık / pencerenin x düğmesi vazgeçer. Yön tuşları yok.
    ReadCursor();
    const bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (lmb && !lmb_was_) ++click_seq_;
    lmb_was_ = lmb;
    const int hv = overlay::HoverItem();
    if (hv >= 0) {
        for (size_t r = 0; r < row_items_.size(); ++r) {
            if (row_items_[r] == hv) sel_ = static_cast<int>(r);
        }
    }
    const int ck = overlay::TakeWebClick();
    bool clicked = false;
    if (ck >= 0) {
        for (size_t r = 0; r < row_items_.size(); ++r) {
            if (row_items_[r] == ck) {
                sel_ = static_cast<int>(r);
                clicked = true;
            }
        }
    }
    const bool enter = clicked || keys_.Pressed(VK_RETURN);
    const bool back = keys_.Pressed(VK_BACK) || keys_.Pressed(VK_RBUTTON) || ck == -3;
    if (back && step_ != Step::Progress) {  // Backspace: kurulumdan vazgeç
        active_ = false;
        success = false;
        mistakes = mistakes_;
        overlay::SetMenu({});
        return true;
    }

    if (enter) {
        const Step prev = step_;
        notice_.clear();
        switch (step_) {
            case Step::Welcome:
                if (sel_ == 0) step_ = Step::License;
                else { active_ = false; success = false; mistakes = mistakes_; overlay::SetMenu({}); return true; }
                break;
            case Step::License:
                if (sel_ == 0) step_ = Step::Type;
                else Fail("Lisansı kabul etmeden kurulum olmaz.", "Lisansı kabul etmeden kurulum yapılamaz.");
                break;
            case Step::Type:
                if (sel_ == 0) {  // tuzak: hızlı kurulum istenmeyen yazılımları da ekler
                    custom_ = false;
                    for (const auto& a : spec_.avoid) chosen_.insert(a);
                    Fail("Hızlı kurulum istenmeyen programlar ekledi.",
                         "Hızlı kurulum istenmeyen eklentileri de kuruyor. Özel kurulum seçmek daha doğru.");
                }
                custom_ = true;
                step_ = Step::Components;
                sel_ = 0;
                break;
            case Step::Components:
                if (sel_ < static_cast<int>(spec_.components.size())) {
                    const std::string& id = spec_.components[sel_].first;
                    if (!chosen_.erase(id)) chosen_.insert(id);
                } else {
                    std::string missing, extra;
                    for (const auto& r : spec_.required) {
                        if (!chosen_.count(r)) missing += (missing.empty() ? "" : ", ") + r;
                    }
                    for (const auto& a : spec_.avoid) {
                        if (chosen_.count(a)) extra += (extra.empty() ? "" : ", ") + a;
                    }
                    if (!missing.empty()) Fail("Gerekli bileşenler eksik.", "Eksik gerekli bileşen var: " + missing);
                    else if (!extra.empty()) Fail("İstenmeyen bileşenler seçili.", "Gereksiz/istenmeyen bileşen seçili: " + extra);
                    else { step_ = Step::Disk; sel_ = 0; }
                }
                break;
            case Step::Disk:
                if (sel_ == 0) {
                    Fail("Diskte yeterli yer yok.", "C: sürücüsünde yer yetmiyor. D: sürücüsünü seç.");
                } else if (spec_.key.empty()) {
                    step_ = Step::Progress;
                    progress_start_ = GetTickCount64();
                } else {
                    step_ = Step::Key;
                    textinput::Begin();
                }
                break;
            case Step::Done:
                active_ = false;
                success = true;
                mistakes = mistakes_;
                overlay::SetMenu({});
                return true;
            default: break;
        }
        if (step_ != prev) sel_ = 0;
    }

    if (step_ == Step::Progress && GetTickCount64() - progress_start_ > 5000) {
        step_ = Step::Done;
        sel_ = 0;
    }
    Draw();
    return false;
}

void Installer::ReadCursor() {
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

// Sihirbaz Facade masaüstünde bir PENCERE olarak çizilir (başlık çubuğu, x düğmesi, görev çubuğu): PhoneView pc=true + web öğeleri
void Installer::Draw() const {
    overlay::PhoneView v;
    v.open = true;
    v.web = true;
    v.pc = true;
    v.cursor = true;
    v.cursor_x = cur_x_;
    v.cursor_y = cur_y_;
    v.click_seq = click_seq_;
    v.web_url = "bilgisayar://kurulum";
    v.web_title = spec_.title;
    char clk[8];
    std::snprintf(clk, sizeof(clk), "%02d:%02d", TIME::GET_CLOCK_HOURS(), TIME::GET_CLOCK_MINUTES());
    v.clock = clk;
    v.signal = phoneos::SignalText();
    v.hint = "";
    row_items_.clear();
    int sel_item = -1, row_count = 0;
    const auto add = [&](int kind, const std::string& text, bool selectable, const std::string& sub = "") {
        overlay::WebItem w;
        w.kind = kind;
        w.text = text;
        w.sub = sub;
        w.selectable = selectable;
        v.web_items.push_back(std::move(w));
        if (selectable) {
            row_items_.push_back(static_cast<int>(v.web_items.size()) - 1);
            if (row_count == sel_) sel_item = static_cast<int>(v.web_items.size()) - 1;
            ++row_count;
        }
    };
    const auto head = [&](const std::string& l) { add(0, l, false); };
    const auto row = [&](const std::string& l, const std::string& val = "") { add(4, val.empty() ? l : val + "  " + l, true); };
    add(1, spec_.title, false);
    switch (step_) {
        case Step::Welcome:
            add(2, "Kurulum sihirbazına hoş geldiniz", false);
            head("Bu sihirbaz programı bilgisayara kuracak.");
            row("İleri");
            row("İptal");
            break;
        case Step::License:
            add(2, "Lisans sözleşmesi", false);
            head("Yazılımı kişisel ve kurumsal kullanım için kullanabilirsiniz.");
            row("Kabul ediyorum");
            row("Kabul etmiyorum");
            break;
        case Step::Type:
            add(2, "Kurulum türü", false);
            row("Hızlı kurulum (önerilen)");
            row("Özel kurulum");
            break;
        case Step::Components:
            add(2, "Bileşenleri seç (tıkla: işaretle)", false);
            for (const auto& c : spec_.components) row(c.second, chosen_.count(c.first) ? "[x]" : "[ ]");
            row("İleri");
            break;
        case Step::Disk:
            add(2, "Kurulum yeri", false);
            row("C: sürücüsü (2 GB boş)");
            row("D: sürücüsü (180 GB boş)");
            break;
        case Step::Key:
            add(2, "Ürün anahtarı", false);
            head("Anahtarı yaz, Enter ile onayla (Esc: geri)");
            break;
        case Step::Progress: {
            const int pct = static_cast<int>(std::min<unsigned long long>(100, (GetTickCount64() - progress_start_) / 50));
            add(2, "Kuruluyor...", false);
            overlay::WebItem bar;  // ilerleme çubuğu (vektör çizim)
            bar.kind = 8;
            bar.gw = 380.f;
            bar.gh = 30.f;
            bar.gbg = 0x1b2236;
            overlay::WebShape fill;
            fill.type = 0;
            fill.x = 4.f;
            fill.y = 4.f;
            fill.w = 372.f * static_cast<float>(pct) / 100.f;
            fill.h = 22.f;
            fill.r = 4.f;
            fill.fill = 0x4c8dff;
            bar.shapes.push_back(fill);
            overlay::WebShape lbl;
            lbl.type = 5;
            lbl.x = 190.f;
            lbl.y = 7.f;
            lbl.size = 15.f;
            lbl.fill = 0xffffff;
            lbl.align = 1;
            lbl.text = "%" + std::to_string(pct);
            bar.shapes.push_back(lbl);
            v.web_items.push_back(std::move(bar));
            break;
        }
        case Step::Done:
            add(2, "Kurulum tamamlandı", false);
            head("Program başarıyla kuruldu.");
            row("Bitir");
            break;
    }
    if (!notice_.empty()) head("! " + notice_);
    v.web_selected = sel_item;
    overlay::SetPhone(v);
}

}  // namespace hikaye
