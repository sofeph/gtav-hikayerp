#pragma once
#include <string>
#include <vector>

#include "common.h"

namespace hikaye::inventory {

// FiveM tarzı envanter: karaktere özel (saves/inventory_<slot>.json), ağırlık ve yuva sınırlı. Eşyalar data/items.json'da tanımlıdır.
// Açma tuşu: data/controls.json -> "inventory" (varsayılan I). Menüden: Kullan (iyileş, sigara iç, içki/esrar efekti, silahı kuşan), At.
// Dükkanlar eşyayı envantere verir (shop effect "give_item"). Yasadışı eşyalar (çakı, molotof, kaçak içki, esrar) polis aramasında
// bulunursa müsadere edilir ve yakalama kararı çıkar (weaponwatch). OTORİTE KODDA: kapasite, kullanım etkisi ve müsadere burada.
void Load();                 // data/items.json
void LoadSave(int slot);     // karakter değişince
void Shutdown();
// Her frame (mod aktifken). blocked: konuşma/telefon/tarayıcı/hapis gibi durumlarda menü açılmaz.
void Update(bool blocked);
bool Add(const std::string& id, int qty = 1);   // kapasite yetmezse false (hiçbiri eklenmez)
bool Remove(const std::string& id, int qty = 1);
int Count(const std::string& id);
float Weight();
bool HasIllegal();                              // envanterde yasadışı eşya var mı
std::vector<std::string> ConfiscateIllegal();   // yasadışı eşyaları siler, adlarını döner
bool MenuOpen();

}  // namespace hikaye::inventory
