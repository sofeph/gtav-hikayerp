#include "nativephone.h"

#include <windows.h>

#include "controls.h"

namespace hikaye {

namespace {
constexpr int kPhoneSelectControl = 176;  // INPUT_PHONE_SELECT
constexpr int kContactsView = 2;          // SET_DATA_SLOT: rehber ekranı
constexpr const char* kContactName = "SÜPER REHBER";  // oyunun kişi adı büyük harf gösterir
constexpr const char* kContactIcon = "CHAR_DEFAULT";

void StartScript(const char* name) {
    SCRIPT::REQUEST_SCRIPT(const_cast<char*>(name));
    for (int i = 0; i < 200 && !SCRIPT::HAS_SCRIPT_LOADED(const_cast<char*>(name)); ++i) {
        SCRIPT::REQUEST_SCRIPT(const_cast<char*>(name));
        WAIT(0);
    }
    SYSTEM::START_NEW_SCRIPT(const_cast<char*>(name), 1424);
    SCRIPT::SET_SCRIPT_AS_NO_LONGER_NEEDED(const_cast<char*>(name));
}

void PushString(const char* label, const char* text) {
    GRAPHICS::_BEGIN_TEXT_COMPONENT(const_cast<char*>(label));
    if (text) UI::_ADD_TEXT_COMPONENT_STRING(const_cast<char*>(text));
    GRAPHICS::_END_TEXT_COMPONENT();
}
}  // namespace

void NativePhone::Load() {
    enabled_ = controls::NativeContactEnabled();
    index_ = controls::NativeContactIndex();
    hand_hash_ = HashKey("cellphone_flashhand");
    contacts_hash_ = HashKey("appcontacts");
}

bool NativePhone::ConsumeOpenRequest() {
    const bool r = request_;
    request_ = false;
    return r;
}

int NativePhone::Scaleform() {
    // Her karakterin telefonu ayrı bir scaleform: Michael iFruit, Franklin Badger, Trevor Facade.
    const Hash model = ENTITY::GET_ENTITY_MODEL(PLAYER::PLAYER_PED_ID());
    const char* name = "cellphone_ifruit";
    if (model == HashKey("player_one")) name = "cellphone_badger";
    else if (model == HashKey("player_two")) name = "cellphone_facade";
    const int handle = GRAPHICS::REQUEST_SCALEFORM_MOVIE(const_cast<char*>(name));
    return GRAPHICS::HAS_SCALEFORM_MOVIE_LOADED(handle) ? handle : 0;
}

void NativePhone::DrawContact(int handle) {
    GRAPHICS::_PUSH_SCALEFORM_MOVIE_FUNCTION(handle, const_cast<char*>("SET_DATA_SLOT"));
    GRAPHICS::_PUSH_SCALEFORM_MOVIE_FUNCTION_PARAMETER_INT(kContactsView);
    GRAPHICS::_PUSH_SCALEFORM_MOVIE_FUNCTION_PARAMETER_INT(index_);
    GRAPHICS::_PUSH_SCALEFORM_MOVIE_FUNCTION_PARAMETER_INT(0);
    PushString("STRING", kContactName);
    PushString("CELL_999", nullptr);
    PushString("CELL_2000", kContactIcon);
    GRAPHICS::_POP_SCALEFORM_MOVIE_FUNCTION_VOID();
}

int NativePhone::SelectedIndex(int handle) {
    GRAPHICS::_PUSH_SCALEFORM_MOVIE_FUNCTION(handle, const_cast<char*>("GET_CURRENT_SELECTION"));
    const auto ret = GRAPHICS::_POP_SCALEFORM_MOVIE_FUNCTION();
    for (int i = 0; i < 30 && !GRAPHICS::_0x768FF8961BA904D6(ret); ++i) WAIT(0);  // dönüş değeri hazır olana kadar
    return GRAPHICS::_0x2DE7EFA66B906036(ret);
}

void NativePhone::DismissNotification() {
    // Native telefon tanımadığı kişiyi aramaya çalışınca "kişi artık mevcut değil" bildirimi çıkar: sil.
    UI::_SET_NOTIFICATION_TEXT_ENTRY(const_cast<char*>("STRING"));
    UI::_ADD_TEXT_COMPONENT_STRING(const_cast<char*>(""));
    const int id = UI::_DRAW_NOTIFICATION(false, false);
    UI::_REMOVE_NOTIFICATION(id);
    UI::_REMOVE_NOTIFICATION(id - 1);
}

void NativePhone::ClosePhone(int handle) {
    // Telefonu temiz kapatmanın yolu: ekranı kapat, scriptleri öldürüp yeniden başlat.
    GRAPHICS::_PUSH_SCALEFORM_MOVIE_FUNCTION(handle, const_cast<char*>("SHUTDOWN_MOVIE"));
    GRAPHICS::_POP_SCALEFORM_MOVIE_FUNCTION_VOID();
    WAIT(0);
    MOBILE::DESTROY_MOBILE_PHONE();
    GAMEPLAY::TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME(const_cast<char*>("cellphone_flashhand"));
    GAMEPLAY::TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME(const_cast<char*>("cellphone_controller"));
    WAIT(0);
    StartScript("cellphone_flashhand");
    StartScript("cellphone_controller");
    AI::TASK_USE_MOBILE_PHONE(PLAYER::PLAYER_PED_ID(), 0);  // telefonu cebe koy
}

void NativePhone::Update() {
    if (!enabled_) return;
    if (SCRIPT::_GET_NUMBER_OF_INSTANCES_OF_STREAMED_SCRIPT(hand_hash_) <= 0) return;  // telefon ekranda değil
    if (SCRIPT::_GET_NUMBER_OF_INSTANCES_OF_STREAMED_SCRIPT(contacts_hash_) <= 0) return;  // rehber ekranında değil
    const int handle = Scaleform();
    if (handle == 0) return;

    DrawContact(handle);  // rehber açıkken her karede yazılmalı

    if (CONTROLS::IS_CONTROL_PRESSED(0, kPhoneSelectControl)) {
        // Sadece seçim anında okunur (bekleme içerir).
        if (SelectedIndex(handle) == index_) {
            GAMEPLAY::TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME(const_cast<char*>("appcontacts"));  // gerçek aramayı engelle
            WAIT(10);
            DismissNotification();
            ClosePhone(handle);
            request_ = true;
            Log("Super Rehber secildi: telefon aciliyor");
        }
    }
}

}  // namespace hikaye
