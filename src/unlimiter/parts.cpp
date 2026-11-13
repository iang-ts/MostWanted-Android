#include "unlimiter/parts.h"

#include <cstring>
#include <vector>

#include "common/options.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"

namespace game {
void SetOwnString(uint32_t hash, const std::string& text);
}

namespace unlimiter {

namespace {

constexpr uint32_t kCustomizeManager = 0x4DEC68, kManagerRecord = 0x04;
constexpr uint32_t kNoAttributes = 0xFFFF;

}  // namespace

uint32_t HashFrom(const char* text, uint32_t start) {
    uint32_t h = start;
    for (const char* p = text; *p; p++) h = h * 33 + uint8_t(*p);
    return h;
}

uint32_t PartAt(uint32_t ride, uint32_t slot) { return kernel::R32(ride + kRideParts + slot * 4); }

void PutPart(uint32_t ride, uint32_t slot, uint32_t part) { kernel::W32(ride + kRideParts + slot * 4, part); }

uint32_t FindPart(X86Context& ctx, uint32_t car_type, uint32_t slot, uint32_t hash) {
    X86Context saved = ctx;
    ctx.ecx = kCarPartDatabase;
    uint32_t part = kernel::CallGuest(ctx, kNewGetCarPart, {car_type, slot, hash, 0, 0xFFFFFFFF});
    ctx = saved;
    return part;
}

bool HasParts(X86Context& ctx, uint32_t car_type, uint32_t slot) { return FindPart(ctx, car_type, slot, 0) != 0; }

bool PartAttribute(uint32_t part, uint32_t key, uint32_t& value) {
    uint32_t pack = kernel::R32(kCarPartPack);
    if (!part || !pack) return false;
    uint32_t list = kernel::R16(part + kPartAttributeList);
    if (list == kNoAttributes) return false;
    uint32_t lists = kernel::R32(pack + kPackAttributeLists), attributes = kernel::R32(pack + kPackAttributes);
    uint32_t at = lists + list * 2;
    int count = int16_t(kernel::R16(at));
    for (int i = 0; i < count; i++) {
        int index = int16_t(kernel::R16(at + 2 + uint32_t(i) * 2));
        uint32_t attribute = attributes + uint32_t(index) * 8;
        if (kernel::R32(attribute) == key) {
            value = kernel::R32(attribute + 4);
            return true;
        }
    }
    return false;
}

float PartFloat(uint32_t part, uint32_t key, float fallback) {
    uint32_t raw;
    if (!PartAttribute(part, key, raw)) return fallback;
    float v;
    std::memcpy(&v, &raw, 4);
    return v;
}

int PartInt(uint32_t part, uint32_t key, int fallback) {
    uint32_t raw;
    return PartAttribute(part, key, raw) ? int(raw) : fallback;
}

uint32_t CurrentCarType(X86Context& ctx) {
    X86Context saved = ctx;
    ctx.ecx = kernel::R32(kCustomizeManager + kManagerRecord);
    uint32_t type = ctx.ecx ? kernel::CallGuest(ctx, kCarRecordGetType, {}) : 0xFFFFFFFF;
    ctx = saved;
    return type;
}

bool StringExists(X86Context& ctx, uint32_t hash) {
    X86Context saved = ctx;
    bool found = (kernel::CallGuest(ctx, kDoesStringExist, {hash}) & 0xFF) != 0;
    ctx = saved;
    return found;
}

bool TextureExists(X86Context& ctx, uint32_t hash) {
    X86Context saved = ctx;
    uint32_t info = kernel::CallGuest(ctx, kGetTextureInfo, {hash, 0, 0});
    ctx = saved;
    return info != 0;
}

uint32_t Label(X86Context& ctx, uint32_t hash, const std::string& own_text, bool replace) {
    if (replace || !StringExists(ctx, hash)) game::SetOwnString(hash, own_text);
    return hash;
}

namespace {

constexpr uint32_t kReturnToStock = 0x60A662F5;
constexpr uint32_t kOptionMenu = 0x50;

std::string GameText(X86Context& ctx, uint32_t hash) {
    X86Context saved = ctx;
    ctx.edx = hash;
    uint32_t text = kernel::CallGuest(ctx, kSearchForString, {});
    ctx = saved;
    std::string out;
    if (!text || !mem::IsCommitted(text)) return out;
    for (uint32_t i = 0; i < 64; i++) {
        char c = *static_cast<char*>(kernel::HostPtr(text + i));
        if (!c) break;
        out += c;
    }
    return out;
}

bool Portuguese(X86Context& ctx) {
    static int portuguese = -1;
    if (portuguese < 0) {
        std::string stock = Upper(GameText(ctx, kReturnToStock));
        if (!stock.empty()) portuguese = stock.find("BRICA") != std::string::npos ? 1 : 0;
    }
    return portuguese == 1;
}

struct ScreenOptions {
    uint32_t screen = 0;
    std::vector<uint32_t> menus;
    uint32_t replaced = 0;
    bool placed = false;
};

ScreenOptions g_options;

}  // namespace

int LanguageIndex(X86Context& ctx) {
    const std::string& code = game::BootLanguage();
    if (code == "fra") return 1;
    if (code == "deu") return 2;
    if (code == "ita") return 3;
    if (code == "esp") return 4;
    if (code == "ind") return 6;
    return Portuguese(ctx) ? 5 : 0;
}

bool IsOwnMenu(uint32_t menu) {
    return menu - kOwnMenuFirst < kOwnMenus || menu == kMenuRimBrandOwn ||
           menu - kMenuRimBrandExtra < kMaxRimBrands - kStockRimBrands ||
           menu - kMenuVinylExtra < kMaxVinylGroups - kMenuVinylStockCount ||
           (menu >= kMenuDecalSlotFirst + 6 && menu < kMenuDecalSlotFirst + kMenuDecalSlots);
}

uint32_t OptionCount() { return g_options.screen ? uint32_t(g_options.menus.size()) : 0; }

void BeginOptions(uint32_t screen) {
    g_options = ScreenOptions{};
    g_options.screen = screen;
}

void NoteOption(uint32_t screen, uint32_t option) {
    if (!g_options.screen || screen != g_options.screen) return;
    g_options.menus.push_back(option ? kernel::R16(option + kOptionMenu) : 0);
}

void PlaceInitial(uint32_t screen) {
    if (screen != g_options.screen) return;
    uint32_t previous = kernel::R32(screen + kScreenPreviousMenu);
    for (size_t i = 0; i < g_options.menus.size(); i++) {
        if (!previous || g_options.menus[i] != previous) continue;
        uint32_t position = uint32_t(i + 1);
        if ((previous & 0xFFFF00FF) == position) return;
        g_options.replaced = previous;
        g_options.placed = true;
        kernel::W32(screen + kScreenPreviousMenu, position);
        return;
    }
}

void EndOptions(uint32_t screen) {
    if (screen == g_options.screen && g_options.placed) kernel::W32(screen + kScreenPreviousMenu, g_options.replaced);
    g_options = ScreenOptions{};
}

}  // namespace unlimiter
