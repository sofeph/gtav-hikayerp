#pragma once
#include <functional>
#include <vector>

#include "common.h"

namespace hikaye::vehctl {

// Araç kontrolleri (sadece mod aktifken): kapı kilidi, sinyaller, dörtlü, kontak. Tuşlar data/controls.json'da.
//  K: kilitle/aç (araçtaysan o araç; değilsen 25 m içindeki SENİN aracın). Sahip olduğun araç, sen inip uzaklaşınca kendiliğinden kilitlenir.
//  Sol/Sağ sinyal: dönüşten sonra (~45°) kendiliğinden söner. Dörtlü: açıp kapatırsın. Kontak: motoru aç/kapat (yakıt yoksa çalışmaz).
// owned: bu araç senin mi?; owned_list: sahip olunan, dünyadaki araçlar (kilit için).
void Update(bool input_blocked, const std::function<bool(Vehicle)>& owned, const std::function<std::vector<Vehicle>()>& owned_list);
// Araçta yazı yazarken (T) Franklin'in özel yeteneği gibi: zaman yavaşlar, araç otomatik sürülür (şerit takibi, trafikte kalır);
// yazı bitince (gönder/iptal) kontrol oyuncuya döner. typing: ekran klavyesi açık mı. Sadece sürücü koltuğunda ve hareket halinde çalışır.
void AutoDrive(bool typing);
void Reset();  // moddan çıkarken

}  // namespace hikaye::vehctl
