#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "common/options.h"
#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "unlimiter/config.h"

namespace game {

void SetOwnString(uint32_t hash, const std::string& text);

namespace {

struct Added {
    uint32_t width, height, format;
    const char* root;  // the folder of the list and its addtex
};

// the cars' added textures (the Mods Loader's build) and the Unlimiter's own (UnlimiterImport: the new menus' icons,
// the tires' and the neon's), a car's first
void ReadAddedList(const char* root, std::map<uint32_t, Added>& out) {
    std::vector<uint8_t> file;
    if (!kernel::ReadGameFile(std::string(root) + "addtex.txt", file)) return;
    std::string text(file.begin(), file.end());
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        uint32_t hash, w, h, f;
        if (std::sscanf(text.substr(at, end - at).c_str(), "%x %u %u %u", &hash, &w, &h, &f) == 4) out.emplace(hash, Added{w, h, f, root});
        at = end + 1;
    }
}

const std::map<uint32_t, Added>& AddedList() {
    static const std::map<uint32_t, Added> list = [] {
        std::map<uint32_t, Added> out;
        ReadAddedList("D:\\", out);
        ReadAddedList("D:\\unlimiter\\", out);
        LOGR("unlimiter: %zu added textures", out.size());
        return out;
    }();
    return list;
}

const char* StringsLanguage(const std::string& code) {
    if (code == "fra") return "french";
    if (code == "deu") return "german";
    if (code == "ita") return "italian";
    if (code == "esp") return "spanish";
    return "english";
}

void LoadStrings(const char* language) {
    std::vector<uint8_t> file;
    if (!kernel::ReadGameFile(std::string("D:\\strings\\") + language + ".txt", file)) return;
    std::string text(file.begin(), file.end());
    size_t count = 0;
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        uint32_t hash;
        int skip = 0;
        if (std::sscanf(line.c_str(), "%x %n", &hash, &skip) < 1 || !skip) continue;
        std::string value = line.substr(size_t(skip));
        while (!value.empty() && value.back() == '\r') value.pop_back();
        SetOwnString(hash, value);
        count++;
    }
    LOGR("unlimiter: %zu strings in %s", count, language);
}

}  // namespace

uint32_t AddedTexture(X86Context& ctx, uint32_t hash) {
    const auto& list = AddedList();
    auto it = list.find(hash);
    if (it == list.end()) return 0;
    static std::map<uint32_t, uint32_t> made;
    auto done = made.find(hash);
    if (done != made.end()) return done->second;
    std::vector<uint8_t> pixels;
    char file[64];
    std::snprintf(file, sizeof file, "%saddtex\\%08x.dxt", it->second.root, hash);
    uint32_t info = 0;
    if (kernel::ReadGameFile(file, pixels))
        info = MakeGuestTexture(ctx, hash, it->second.width, it->second.height, it->second.format, pixels);
    if (!info) LOGR("unlimiter: added texture %08x could not be made", hash);
    made[hash] = info;
    return info;
}

void UnlimiterFrame(X86Context& ctx) {
    static bool strings = false;
    if (!strings) {
        strings = true;
        const char* language = StringsLanguage(BootLanguage());
        LoadStrings(language);
        if (std::string(language) != "english") LoadStrings("english");
    }
    unlimiter::Frame(ctx);
    unlimiter::SnapshotPresets();
}

}  // namespace game
