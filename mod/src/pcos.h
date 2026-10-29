#pragma once
#include <string>
#include <vector>

#include "common.h"
#include "missions.h"
#include "overlay.h"

namespace hikaye::pcos {

// Bilgisayar işletim sistemi. İki kullanım:
//  İş bilgisayarı (Burak'ın PC'si, Facade Office kurulumu): masaüstünde sadece İnternet ve Kurulum vardır. Ayakta, bilgisayarın başında E.
//  Kendi dizüstün (dükkandan alınır, envanterde): Oyun, Ofis (notlar, hesap makinesi), İnternet, Mail. GÜÇ KAYNAĞINDA ÇALIŞIR (pili yok)
//  ve AYAKTA KULLANILMAZ: evde (oturarak), araçta (kontak açık: çakmaklık, sürücü isen duruyor olmalı) ya da otobüs/metroda. Güç ya da oturma
//  durumu bozulunca (araçtan inme, eve uzaklaşma, kontağı kapatma) dizüstü kapanır.
// Ekranlar HML sayfası (pc/<sayfa>) olarak telefon tarayıcısında gösterilir; eylemler "act:pc:<komut>". Oyunlar minigames modülündedir.
enum class Mode { None, Work, Laptop };

void LoadSave(int slot);
void Shutdown();

void OpenWork(const Missions::InstallSpec& spec);  // iş bilgisayarı masaüstü
void RequestLaptop();                              // envanterden "Kullan"
bool ConsumeLaptopRequest();
// Dizüstünü açmayı dener: güç + oturma koşulu. Olmazsa why'a Türkçe neden yazılır.
bool TryOpenLaptop(const std::vector<Vector3>& homes, std::string& why);

bool Active();
Mode CurrentMode();
void Close();
void Update(const Vector3& pos, const std::vector<Vector3>& homes);  // her frame: güç/oturma denetimi

bool ConsumeInstall(Missions::InstallSpec& out);  // "Kurulum" uygulaması seçildi

// Oyun ekranı (tarayıcı yerine)
bool GameActive();
bool UpdateGame();  // true: oyundan çıkıldı
overlay::PhoneView GameView(const std::string& clock);

std::string BuildPage(const std::string& url);  // "pc/home" ... bilinmiyorsa ""
std::string Action(const std::string& cmd);
void PushMail(const std::string& from, const std::string& subject, const std::string& body);

}  // namespace hikaye::pcos
