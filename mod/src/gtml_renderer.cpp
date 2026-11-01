#include "gtml_renderer.h"

#include <imgui.h>
#include "json.hpp"
#include <algorithm>
#include <cmath>

namespace hikaye::gtml {

using json = nlohmann::json;

static ImU32 Col(int rgb, int alpha = 255) {
    if (rgb < 0) return IM_COL32(0, 0, 0, 0);
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
}

static ImU32 ElevationShadow(int level, int alpha = 255) {
    // Material Design 3 elevation shadows
    static const struct { float y, blur, spread; int alpha; } shadows[] = {
        {0, 0, 0, 0},           // 0
        {1, 3, 0, 30},          // 1
        {3, 6, 0, 40},          // 2
        {6, 12, 0, 50},         // 3
        {10, 20, 0, 60},        // 4
        {15, 30, 0, 70},        // 5
    };
    if (level < 0) level = 0;
    if (level > 5) level = 5;
    return IM_COL32(0, 0, 0, shadows[level].alpha);
}

bool ParseDisplayList(const std::string& json_str, GTMLDisplayList& out, std::string& error) {
    try {
        json j = json::parse(json_str);
        
        out.width = j.value("width", 0.f);
        out.height = j.value("height", 0.f);
        out.bg_color = j.value("bg_color", -1);
        out.version = j.value("version", "1.0");
        
        auto parseNode = [&](auto&& self, const json& node_json) -> GTMLNode {
            GTMLNode node;
            node.type = static_cast<GTMLNode::Type>(node_json.value("type", 0));
            node.id = node_json.value("id", "");
            node.x = node_json.value("x", 0.f);
            node.y = node_json.value("y", 0.f);
            node.w = node_json.value("w", 0.f);
            node.h = node_json.value("h", 0.f);
            node.visible = node_json.value("visible", true);
            node.interactive = node_json.value("interactive", false);
            node.r = node_json.value("r", 0.f);
            node.bg = node_json.value("bg", -1);
            node.border = node_json.value("border", -1);
            node.border_width = node_json.value("border_width", 1.f);
            node.elevation = node_json.value("elevation", 0);
            node.text = node_json.value("text", "");
            node.font_size = node_json.value("font_size", 14.f);
            node.color = node_json.value("color", -1);
            node.text_align = node_json.value("text_align", 0);
            node.action = node_json.value("action", "");
            node.primary = node_json.value("primary", false);
            node.disabled = node_json.value("disabled", false);
            node.src = node_json.value("src", "");
            node.tint = node_json.value("tint", -1);
            node.placeholder = node_json.value("placeholder", "");
            node.value = node_json.value("value", "");
            node.password = node_json.value("password", false);
            node.content_height = node_json.value("content_height", 0.f);
            node.scroll_y = node_json.value("scroll_y", 0.f);
            
            if (node_json.contains("children") && node_json["children"].is_array()) {
                for (const auto& child : node_json["children"]) {
                    node.children.push_back(self(self, child));
                }
            }
            return node;
        };
        
        if (j.contains("nodes") && j["nodes"].is_array()) {
            for (const auto& node_json : j["nodes"]) {
                out.nodes.push_back(parseNode(parseNode, node_json));
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

static void RenderNode(ImDrawList* dl, const GTMLNode& node, float scale, float offset_x, float offset_y, ImFont* font) {
    if (!node.visible) return;
    
    const float x = offset_x + node.x * scale;
    const float y = offset_y + node.y * scale;
    const float w = node.w * scale;
    const float h = node.h * scale;
    const float r = node.r * scale;
    
    // Draw elevation shadow
    if (node.elevation > 0) {
        const float shadow_offset = node.elevation * 2.f * scale;
        const float shadow_blur = node.elevation * 4.f * scale;
        dl->AddRectFilled(
            ImVec2(x + shadow_offset, y + shadow_offset),
            ImVec2(x + w + shadow_offset, y + h + shadow_offset),
            ElevationShadow(node.elevation),
            r
        );
    }
    
    // Draw background
    if (node.bg >= 0) {
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), Col(node.bg), r);
    }
    
    // Draw border
    if (node.border >= 0 && node.border_width > 0) {
        dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), Col(node.border), r, 0, node.border_width * scale);
    }
    
    // Draw text
    if (!node.text.empty() && node.font_size > 0) {
        const float fs = node.font_size * scale;
        const ImU32 text_col = node.color >= 0 ? Col(node.color) : IM_COL32(255, 255, 255, 255);
        const float max_w = node.max_width > 0 ? node.max_width * scale : w;
        
        float tx = x;
        if (node.text_align == 1) { // center
            const float tw = font->CalcTextSizeA(fs, FLT_MAX, max_w, node.text.c_str()).x;
            tx = x + (w - tw) * 0.5f;
        } else if (node.text_align == 2) { // right
            const float tw = font->CalcTextSizeA(fs, FLT_MAX, max_w, node.text.c_str()).x;
            tx = x + w - tw;
        }
        
        dl->AddText(font, fs, ImVec2(tx, y), text_col, node.text.c_str(), nullptr, max_w);
    }
    
    // Draw image
    if (!node.src.empty()) {
        // For now, draw a placeholder rect
        // In production, this would look up a texture ID from a texture atlas
        ImU32 img_col = node.tint >= 0 ? Col(node.tint) : IM_COL32(255, 255, 255, 255);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), img_col, r);
    }
    
    // Draw children
    for (const auto& child : node.children) {
        RenderNode(dl, child, scale, offset_x, offset_y, font);
    }
}

void RenderDisplayList(const GTMLDisplayList& dl, float scale, float offset_x, float offset_y) {
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    
    // Draw background
    if (dl.bg_color >= 0 && dl.width > 0 && dl.height > 0) {
        draw_list->AddRectFilled(
            ImVec2(offset_x, offset_y),
            ImVec2(offset_x + dl.width * scale, offset_y + dl.height * scale),
            Col(dl.bg_color)
        );
    }
    
    // Render all nodes
    for (const auto& node : dl.nodes) {
        RenderNode(draw_list, node, scale, offset_x, offset_y, font);
    }
}

static bool PointInRect(float px, float py, const GTMLNode& node, float scale, float offset_x, float offset_y) {
    const float x = offset_x + node.x * scale;
    const float y = offset_y + node.y * scale;
    const float w = node.w * scale;
    const float h = node.h * scale;
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

static std::string HitTestRecursive(const GTMLNode& node, float norm_x, float norm_y, float scale, float offset_x, float offset_y) {
    if (!node.visible) return "";
    
    // Check children first (top-down, so last child wins)
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
        std::string hit = HitTestRecursive(*it, norm_x, norm_y, scale, offset_x, offset_y);
        if (!hit.empty()) return hit;
    }
    
    // Check self
    if (node.interactive && PointInRect(norm_x, norm_y, node, scale, offset_x, offset_y)) {
        return node.id;
    }
    
    return "";
}

std::string HitTest(const GTMLDisplayList& dl, float norm_x, float norm_y, float scale, float offset_x, float offset_y) {
    // Check nodes in reverse order (top-most first)
    for (auto it = dl.nodes.rbegin(); it != dl.nodes.rend(); ++it) {
        std::string hit = HitTestRecursive(*it, norm_x, norm_y, scale, offset_x, offset_y);
        if (!hit.empty()) return hit;
    }
    return "";
}

}  // namespace hikaye::gtml