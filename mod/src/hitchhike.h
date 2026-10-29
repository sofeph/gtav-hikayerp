#pragma once
#include <string>

#include "common.h"
#include "json.hpp"

namespace hikaye::hitchhike {

// Otostop (data/hitchhike.json: accept_chance varsayılan 0.40). Yaya iken tuşa (varsayılan H) basınca yakındaki seyir halindeki
// bir sivil araç için zar atılır: %40 durur, oyuncu ön yolcu koltuğuna biner. Haritada işaret varsa sürücü oraya götürür, yoksa
// dolaşır; E ile ya da varışta iner. Yolcuyken sürüş suçları oyuncuya yazılmaz (karma/crimes sadece sürücüyü izler).
void Load();
void Update(const Vector3& player_pos, bool e_pressed, bool input_blocked);
void Reset();  // moddan çıkarken sürücü ve aracı serbest bırak

// Sohbet (conversation 'hitch' bağlamı): binince sürücüyle konuşulur; LLM sadece 'nereye' metnini çıkarır, gidilip gidilmeyeceği,
// mesafe ve sürüş KODDA. Haritada işaret yoksa sürücü hedef söylenene kadar durur.
bool Riding();         // oyuncu otostop arabasında
Ped Driver();
bool ConsumeBoarded();  // yeni bindi: sohbet başlatılsın
nlohmann::json SetDestination(const Vector3& pos, const std::string& name);
nlohmann::json UseWaypoint();
bool ConsumeArrival(nlohmann::json& facts);

}  // namespace hikaye::hitchhike
