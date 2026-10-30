#pragma once
#include <string>
#include <vector>

#include "hml.h"
#include "input.h"

namespace hikaye {

// Telefondaki tarayıcı. Sayfalar data/web/<site>/<sayfa>.hml (HML işaretleme dili, bkz. hml.h); "home" ana sayfadır.
// Sayfa içeriği görev durumuna göre canlı yeniden hesaplanır (Bleeter/Weazel News/GeneralSale görev ilerledikçe değişir).
// Tuşlar: ↑↓ bağlantı seç, Enter aç, U adres yaz veya Eyefind'da ara (adres bulunamazsa/boşluk varsa arama yapılır),
// Backspace geri (ana sayfada telefona dön), P telefonu kapat.
class WebBrowser {
public:
    bool active() const { return active_; }
    bool typing() const { return typing_; }  // adres/arama/uygulama yazısı yazılıyor
    void Open(const std::string& root = "phone/home");  // root: geri tuşuyla kapanan ilk sayfa (telefon: phone/home, bilgisayar: pc/home)
    void Close();
    // Fare imleci (tıkla, sağ tık geri) ve yön tuşlarının açık olup olmadığı. Bilgisayarda sadece fare (yön tuşu yok).
    void SetMouse(bool on, bool arrows) { mouse_ = on; arrows_ = arrows; }
    void OpenAt(const std::string& href, const hml::Context& ctx);  // tarayıcıyı açar ve adrese gider (durak QR'ı)
    // Her frame (sadece active iken). Dönüş: true = telefon tamamen kapatılsın (P).
    bool Update(const hml::Context& ctx, bool p_pressed);
    void Publish(const std::string& clock) const;

private:
    void Navigate(const std::string& href, bool push, const hml::Context& ctx, bool skip_delay = false);
    void NavigateDelayed(const std::string& href, bool push, const hml::Context& ctx, unsigned long long delay_ms);  // yavaş sinyalde beklemeli yükleme
    void Search(const std::string& query);  // Eyefind: data/web altındaki tüm sayfalarda arar
    void BuildIndex();
    struct Doc { std::string url, title, text; };
    std::vector<Doc> index_;
    bool typing_ = false;
    bool mouse_ = false, arrows_ = true, lmb_was_ = false, rmb_was_ = false;
    float cur_x_ = 0.5f, cur_y_ = 0.5f;
    int click_seq_ = 0;
    void ReadCursor();
    void Activate(const hml::Context& ctx);
    std::string ask_cmd_;  // askact: yazı girdisi bekleyen eylem şablonu ("ph:dial:{}")
    unsigned long long phone_src_at_ = 0;
    void ApplyAction(const std::string& cmd, const hml::Context& ctx);
    void RefreshPhonePage(const hml::Context& ctx);
    std::string query_;  // sonuç sayfası sorgusu ("ara:..." adresi)
    size_t last_search_src_bytes_ = 0;  // son arama sonuç sayfasının üretilen kaynak baytı (kota için)
    // Yavaş bağlantı (2 çubuk) beklemesi: bekleyen sayfa yüklemesi
    bool pending_nav_ = false;
    std::string pending_href_;
    bool pending_push_ = false;
    unsigned long long pending_at_ = 0;  // bu saatte yükle
    void Rebuild(const hml::Context& ctx_in);
    int NextSelectable(int from, int dir) const;

    bool active_ = false;
    bool external_mode_ = false;
    std::string url_;  // normalleştirilmiş: "bleeter.com/index"
    std::vector<std::string> history_;
    hml::Node root_;
    hml::Page page_;
    int sel_ = -1;
    std::string notice_;
    unsigned long long notice_until_ = 0;
    unsigned long long rebuilt_at_ = 0;
    Keys keys_;
};

}  // namespace hikaye
