#pragma once

#include <string>
#include <vector>
#include <map>

namespace hikaye::gtml {

// GTML Display List types (matching the Python compiler output)
struct GTMLRect {
    float x = 0, y = 0, w = 0, h = 0;
    float r = 0;           // corner radius
    int fill = -1;         // 0xRRGGBB
    int stroke = -1;       // 0xRRGGBB
    float sw = 1;          // stroke width
};

struct GTMLText {
    float x = 0, y = 0;
    std::string text;
    float size = 14;
    int color = -1;        // 0xRRGGBB
    int align = 0;         // 0=left, 1=center, 2=right
    float max_width = 0;   // 0 = no wrap
};

struct GTMLImage {
    float x = 0, y = 0, w = 0, h = 0;
    std::string src;       // texture identifier or URL
    int tint = -1;         // 0xRRGGBB
};

struct GTMLNode {
    enum Type { Box, Text, Button, Image, Input, Scroll } type = Box;
    
    // Common
    std::string id;
    float x = 0, y = 0, w = 0, h = 0;
    bool visible = true;
    bool interactive = false;
    
    // Box
    float r = 0;
    int bg = -1;
    int border = -1;
    float border_width = 1;
    int elevation = 0;     // Material elevation level
    
    // Text
    std::string text;
    float font_size = 14;
    int color = -1;
    int text_align = 0;
        float max_width = 0;   // 0 = no wrap
    
        // Button
    std::string action;    // gta.trigger event name
    bool primary = false;
    bool disabled = false;
    
    // Image
    std::string src;
    int tint = -1;
    
    // Input
    std::string placeholder;
    std::string value;
    bool password = false;
    
    // Scroll
    float content_height = 0;
    float scroll_y = 0;
    
    // Children
    std::vector<GTMLNode> children;
};

struct GTMLDisplayList {
    float width = 0, height = 0;
    int bg_color = -1;
    std::vector<GTMLNode> nodes;
    std::string version = "1.0";
};

// Parse Display List from JSON
bool ParseDisplayList(const std::string& json_str, GTMLDisplayList& out, std::string& error);

// Render Display List using ImGui DrawList
void RenderDisplayList(const GTMLDisplayList& dl, float scale, float offset_x, float offset_y);

// Hit test - returns node id at screen position (normalized 0..1)
std::string HitTest(const GTMLDisplayList& dl, float norm_x, float norm_y, float scale, float offset_x, float offset_y);

}  // namespace hikaye::gtml