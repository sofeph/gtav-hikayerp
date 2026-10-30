#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hikaye::cef {

struct Frame {
    std::shared_ptr<const std::vector<unsigned char>> pixels;
    int width = 0;
    int height = 0;
    std::uint64_t revision = 0;
};

bool Initialize(std::string& error);
bool Open(const std::string& url, int width, int height, std::string& error);
void LoadUrl(const std::string& url);
void GoBack();
void ExecuteJavaScript(const std::string& script);
void Close();
bool IsOpen();
void Resize(int width, int height);
Frame LatestFrame();

void MouseMove(int x, int y, bool left_down, bool right_down);
void MouseClick(int x, int y, bool right_button, bool mouse_up);
void UpdateKeyboard();

std::string LocalFileUrl(const std::string& relative_path);
std::string UrlEncode(const std::string& value);

}  // namespace hikaye::cef
