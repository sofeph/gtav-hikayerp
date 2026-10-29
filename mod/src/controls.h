#pragma once
#include <string>

namespace hikaye::controls {

// data/controls.json'dan tuş atamalarını okur. Dosya yoksa/bozuksa varsayılanlar kalır (hata modu düşürmez).
void Load();

int InventoryKey();  // envanter menüsü (varsayılan I)
int HitchhikeKey();  // otostop (varsayılan H)
int QrKey();         // durakta telefonla QR okut (varsayılan J)
int SwitchKey();  // serbest dolaşımda karakter değiştirme (varsayılan F10)
int LockKey();         // araç kilidi (varsayılan K)
int SignalLeftKey();   // sol sinyal (varsayılan NUM4)
int SignalRightKey();  // sağ sinyal (varsayılan NUM6)
int HazardKey();       // dörtlü (varsayılan NUM5)
int EngineKey();       // kontak (varsayılan NUM8)

int ModeKey();   // Hikaye RP modu etkinleştirme tuşu (varsayılan F9)
int PhoneKey();  // sanal tuş kodu (VK_*)
int TalkKey();   // bas-konuş
std::string KeyName(int vk);  // ekranda gösterilecek ad: "O", "F5", "↑"...

// Oyunun telefonundaki (↑) rehbere "Süper Rehber" kişisi eklensin mi ve rehberdeki sıra numarası.
bool NativeContactEnabled();
int NativeContactIndex();

}  // namespace hikaye::controls
