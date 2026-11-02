#pragma once

#include <functional>
#include <string>

// Runs a GTML page's <script> blocks inside the ASI (QuickJS) against a small DOM shim.
// Everything except QueueClick must be called from the game script thread.
namespace hikaye::gtml_js {

// Feed the latest compiler layout each frame (empty = no GTML page). A changed layout restarts the page's JS.
void Tick(const std::string& source_layout_json);

// Layout with script-made changes applied; empty when no page is loaded.
std::string CurrentJson();

// Thread-safe: deliver a click on a node id (from the render thread's hit test).
void QueueClick(const std::string& node_id);

// gta.trigger(event, data) from page scripts; payload is JSON.
void SetTriggerHandler(std::function<void(const std::string& event, const std::string& payload_json)> handler);

void Shutdown();

}  // namespace hikaye::gtml_js
