#include "common/presets.h"

#include <cctype>
#include <fstream>
#include <mutex>

#include "core/log.h"

namespace presets {

namespace {

std::mutex g_lock;
std::filesystem::path g_dir, g_saves;
std::filesystem::file_time_type g_seen{};
bool g_baseline = false;

std::filesystem::path File(const std::string& profile) {
    std::string name;
    for (char c : profile) name += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? c : '_';
    if (name.empty()) name = "presets";
    return g_dir / (name + ".bin");
}

std::filesystem::file_time_type Newest() {
    std::error_code ec;
    std::filesystem::file_time_type newest{};
    for (auto it = std::filesystem::recursive_directory_iterator(g_saves, ec); !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        auto t = it->last_write_time(ec);
        if (!ec && t > newest) newest = t;
    }
    return newest;
}

}  // namespace

void SetDir(const std::filesystem::path& dir) {
    std::lock_guard lock(g_lock);
    g_dir = dir;
}

void SetSaves(const std::filesystem::path& dir) {
    std::lock_guard lock(g_lock);
    g_saves = dir;
    g_baseline = false;
}

bool SavesWritten() {
    std::lock_guard lock(g_lock);
    if (g_saves.empty()) return false;
    auto newest = Newest();
    if (!g_baseline) {
        g_baseline = true;
        g_seen = newest;
        return false;
    }
    if (newest <= g_seen) return false;
    g_seen = newest;
    return true;
}

bool Read(const std::string& profile, std::vector<uint8_t>& out) {
    std::lock_guard lock(g_lock);
    if (g_dir.empty()) return false;
    std::ifstream in(File(profile), std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

void Write(const std::string& profile, const std::vector<uint8_t>& data) {
    std::lock_guard lock(g_lock);
    if (g_dir.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
    std::filesystem::path file = File(profile), temp = file;
    temp += ".new";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!out) return;
    }
    std::filesystem::rename(temp, file, ec);
    if (ec) LOGE("presets: cannot write %s", file.c_str());
}

}  // namespace presets
