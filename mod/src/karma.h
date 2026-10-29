#pragma once
#include <string>

#include "common.h"

namespace hikaye::karma {

// RDR2 tarzı onur (karma) sistemi. Değer -100..+100, karaktere özel (saves/karma_<slot>.json).
// Kazandırır: iyilikler (para vermek, birini bir yere götürmek). Düşürür: hakaret, taciz, zorbalık, tehdit, fiziksel saldırı.
// OTORİTE KODDADIR: LLM sadece olayı (niyet) bildirir; değişimi, sınırları ve sonuçları kod belirler.
// Seviyeler: outlaw (<= -60), bad (<= -20), neutral, good (>= 20), hero (>= 60). NPC'lere 'player_honor' olarak gider,
// telefon sayfalarında (Bleeter/Weazel News) {flag:karma} ve {flag:karma_level} olarak görünür.
void Load(int slot);
void Save();
void Shutdown();  // moddan çıkarken kaydet

// Onur değişimi (reason: ekranda kısa Türkçe sebep). Sayaç: "hakaret", "taciz", "zorbalik", "tehdit", "siddet", "iyilik_para", "iyilik_yolculuk".
void Toast(const std::string& text, int seconds = 4);  // kısa ekran mesajı
void Add(int delta, const std::string& reason, const std::string& counter = "");
int Value();
std::string Level();       // outlaw | bad | neutral | good | hero
std::string LevelLabel();  // Türkçe etiket
int Counter(const std::string& key);
std::string Lookup(const std::string& key);  // tarayıcı: "karma", "karma_level", "karma_label", "count_<sayaç>"

// Her frame (mod aktifken): fiziksel saldırı algılama (yakındaki yayaya vurma/öldürme) ve ekran bildirimi.
void Update();

}  // namespace hikaye::karma
