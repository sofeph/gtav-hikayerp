#pragma once
#include "common.h"

namespace hikaye {

// Oyunun kendi telefonunun (↑) rehberine "Süper Rehber" kişisini ekler; seçilince native telefon kapanır ve bizim
// telefonumuz açılır (open request).
//
// Teknik: iFruitAddon2 (Bob74, MIT lisansı, https://github.com/Bob74/iFruitAddon2) fikrinin .NET'siz C++ karşılığı.
// Rehber ekranı açıkken telefonun scaleform'una "SET_DATA_SLOT" ile kişiyi yazar, seçimi "GET_CURRENT_SELECTION" ile
// okur; oyunun "arama" akışı (appcontacts script'i) durdurulur ve telefon scriptleri yeniden başlatılarak kapatılır.
// Telefonun iç yapısı oyun sürümüne bağlıdır: bir şey ters giderse özellik sessizce çalışmaz, oyun etkilenmez.
class NativePhone {
public:
    void Load();
    void Update();  // her frame, script thread
    // "Süper Rehber" seçildiyse true (bir kez): bizim telefonu açmanın zamanı.
    bool ConsumeOpenRequest();

private:
    int Scaleform();
    int SelectedIndex(int handle);
    void DrawContact(int handle);
    void DismissNotification();
    void ClosePhone(int handle);

    bool enabled_ = true;
    int index_ = 43;  // iFruitAddon2'nin varsayılan başlangıcı 40; başka modlarla çakışmasın diye farklı
    bool request_ = false;
    Hash hand_hash_ = 0, contacts_hash_ = 0;
};

}  // namespace hikaye
