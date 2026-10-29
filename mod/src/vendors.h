#pragma once
#include <vector>

#include "common.h"
#include "hml.h"
#include "json.hpp"
#include "shop_manager.h"

namespace hikaye::vendors {

// Gizli seyyar satıcılar: haritada ikon YOK. Konumu üç yoldan öğrenilir:
//  1) Bleeter'daki üstü kapalı (kodlu) ilanlar ("vendor_hints" kaynağı): sokak adı dolaylı verilir;
//  2) keşif: satıcıya ~18 m yaklaşınca "bulundu" sayılır;
//  3) birine sorarak: sokak yayasına ya da satıcının kendisine sorarsın (niyet 'ask_vendor'); bilip bilmediğini ve ne kadar
//     açık söyleyeceğini KOD belirler (ününe göre şans), bilirse haritaya yaklaşık bir nokta işaretlenir.
// Keşif karaktere özeldir (saves/vendors_<slot>.json). Gizli dükkan: shop JSON'unda "hidden": true.
void LoadSave(int slot);
void Shutdown();
void Update(const std::vector<Shop>& shops, const Vector3& player_pos);                       // keşif
std::vector<hml::Item> HintItems(const std::vector<Shop>& shops);                             // Bleeter kodlu ilanlar
nlohmann::json AskAround(const std::vector<Shop>& shops, const Vector3& player_pos);          // LLM'e gerçek bilgi
bool Discovered(const std::string& id);

}  // namespace hikaye::vendors
