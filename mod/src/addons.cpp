#include "addons.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "common.h"
#include "json.hpp"

namespace hikaye::addons {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {
std::vector<std::string> g_dirs;
}

void Scan() {
    g_dirs.clear();
    std::error_code ec;
    const fs::path root = fs::path(kBaseDir) / "addons";
    if (!fs::exists(root, ec)) return;
    std::vector<fs::path> dirs;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (e.is_directory()) dirs.push_back(e.path());
    }
    std::sort(dirs.begin(), dirs.end());
    for (const auto& d : dirs) {
        if (d.filename().string().rfind("_", 0) == 0) continue;  // "_ornek..." gibi alt çizgiyle başlayanlar şablondur, yüklenmez
        bool enabled = true;
        std::string name = d.filename().string();
        try {
            std::ifstream in(d / "addon.json");
            if (in) {
                const json j = json::parse(in);
                enabled = j.value("enabled", true);
                name = j.value("name", name);
            }
        } catch (const std::exception& e) {
            Log("addon.json okunamadi (" + d.string() + "): " + e.what());
            continue;
        }
        if (!enabled) continue;
        g_dirs.push_back(d.generic_string());
        Log("eklenti yuklendi: " + name);
    }
}

const std::vector<std::string>& Dirs() { return g_dirs; }

std::vector<std::string> Files(const std::string& sub, const std::string& ext) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& d : g_dirs) {
        const fs::path dir = fs::path(d) / sub;
        if (!fs::exists(dir, ec)) continue;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (e.is_regular_file() && e.path().extension() == ext) out.push_back(e.path().generic_string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace hikaye::addons
