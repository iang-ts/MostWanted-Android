#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"
#include "unlimiter/parts.h"

// Paint groups, as the PC's CustomizePaint: the filter L1/R1 walks is _PaintGroups.ini (and PaintGroups/*.ini),
// Group0 the car's own paints when PaintCustom is on. A vinyl with NUMREMAPCOLOURS already opens its colours; a
// group the game's menus 0x402-0x409 do not cover takes the same path.

namespace unlimiter {

namespace {

constexpr uint32_t kEmptyBrand = 0xFFFFFFFF;
constexpr uint32_t kBrandName = 0xEBB03E66, kNumRemap = 0x6212682B;
constexpr uint32_t kPaintIndex = 0x1B8, kSwatchSaved = 0x1BC, kSwatchList = 0x22C;
constexpr uint32_t kBuildSwatch = 0x2A7840, kListCount = 0x4E8D0;
constexpr uint32_t kSetLanguage = 0xEAB30, kFilterObject = 0x78008599;
constexpr uint32_t kQueueMessage = 0xE1CB0, kFEng = 0x48E94C, kShowArrows = 0x1A7240F3;
constexpr uint32_t kManager = 0x4DEC68, kMalloc = 0x40110, kCopyPart = 0x299F80, kSetTempPart = 0x2963A0;
constexpr uint32_t kSwitchPackage = 0xEB160, kPaintPackage = 0x46C4EC, kPlayLocked = 0x29A240;
constexpr uint32_t kAccept = 0x406415E3, kSelectedPart = 0x0C, kSelectedFlags = 0x20, kPreviousMenu = 0x148;
constexpr int kDefaultCount = 8;

struct PaintGroup {
    uint32_t brand = kEmptyBrand, title = 0;
    std::string key;
    bool body = true, rims = false, vinyl = false;
};

struct Defaults {
    const char* name;
    const char* title;
    bool body, rims, vinyl;
};

const Defaults kDefaults[kDefaultCount] = {
    {"", "CP_FILTER_CUSTOM", true, false, false},       {"GLOSS", "CP_FILTER_GLOSS", true, false, false},
    {"METAL", "CP_FILTER_METALLIC", true, false, false}, {"PEARL", "CP_FILTER_PEARL", true, false, false},
    {"TRAFFIC", "CP_FILTER_TRAFFIC", true, false, false}, {"COP", "CP_FILTER_COP", true, false, false},
    {"RIM", "CP_FILTER_RIM", false, true, false},         {"VINYL", "CP_FILTER_VINYL", false, false, true},
};

bool g_unify = false;
uint32_t g_light = 0;
bool g_index_ours = false;
int g_group = 0;

// the screen only keeps three saved swatch positions (0x1BC, and the two after it). The group index stays here,
// and the build is shown group 0's slot so a rim or a traffic paint does not read past those three
int& Saved(int i) {
    static int saved[kMaxPaintGroups];
    static bool ready = false;
    if (!ready) {
        for (int n = 0; n < int(kMaxPaintGroups); n++) saved[n] = -1;
        ready = true;
    }
    if (i < 0 || i >= int(kMaxPaintGroups)) i = 0;
    return saved[i];
}

void ResetSaved() {
    for (int i = 0; i < int(kMaxPaintGroups); i++) Saved(i) = -1;
}

uint32_t Call(X86Context& ctx, uint32_t fn, uint32_t self, std::initializer_list<uint32_t> args) {
    X86Context saved = ctx;
    ctx.ecx = self;
    uint32_t result = kernel::CallGuest(ctx, fn, args);
    ctx = saved;
    return result;
}

PaintGroup FromDefault(int i) {
    const Defaults& d = kDefaults[i];
    PaintGroup g;
    g.brand = d.name[0] ? BinHash(d.name) : kEmptyBrand;
    g.key = d.title;
    g.title = BinHash(d.title);
    g.body = d.body;
    g.rims = d.rims;
    g.vinyl = d.vinyl;
    return g;
}

int DefaultOf(uint32_t brand) {
    for (int i = 0; i < kDefaultCount; i++)
        if (FromDefault(i).brand == brand) return i;
    return 1;
}

PaintGroup ReadGroup(const Ini& ini, const std::string& section, const PaintGroup& fallback, bool folder) {
    PaintGroup g = fallback;
    if (ini.Has(section, "BrandName")) {
        std::string name = ini.Text(section, "BrandName");
        g.brand = name.empty() ? kEmptyBrand : BinHash(name);
    }
    if (ini.Has(section, "String")) {
        g.key = ini.Text(section, "String");
        g.title = ini.Hash(section, "String", g.title);
    }
    int body = folder ? 1 : (fallback.body ? 1 : 0);
    int rims = folder ? 0 : (fallback.rims ? 1 : 0);
    int vinyl = folder ? 0 : (fallback.vinyl ? 1 : 0);
    g.body = ini.Int(section, "AvailableForBody", body) != 0;
    g.rims = ini.Int(section, "AvailableForRims", rims) != 0;
    g.vinyl = ini.Int(section, "AvailableForVinyl", vinyl) != 0;
    if (!g.title) g.title = fallback.title;
    return g;
}

void Push(std::vector<PaintGroup>& out, const PaintGroup& g) {
    if (out.size() < kMaxPaintGroups) out.push_back(g);
}

// the stock _PaintGroups.ini writes COP, RIM and VINYL all as [Group5]. One section per name would keep only VINYL
// and the body filter would lose COP. Each repeated header is the next group.
struct PaintBlock {
    int index = -1;
    Ini ini;
};

std::vector<PaintBlock> PaintBlocks() {
    std::vector<uint8_t> bytes;
    std::vector<PaintBlock> out;
    if (!kernel::ReadGameFile("D:\\unlimiter\\_PaintGroups.ini", bytes)) return out;
    std::string text(bytes.begin(), bytes.end());
    std::string chunk;
    int index = -1;
    auto flush = [&] {
        if (index < 0) return;
        PaintBlock block;
        block.index = index;
        block.ini.Parse("[Group]\n" + chunk);
        out.push_back(std::move(block));
        chunk.clear();
        index = -1;
    };
    size_t at = 0;
    if (text.size() >= 3 && uint8_t(text[0]) == 0xEF && uint8_t(text[1]) == 0xBB && uint8_t(text[2]) == 0xBF) at = 3;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        size_t head = 0;
        while (head < line.size() && std::isspace(static_cast<unsigned char>(line[head]))) head++;
        if (head < line.size() && line[head] == '[') {
            flush();
            size_t close = line.find(']', head);
            std::string name = Upper(line.substr(head + 1, close == std::string::npos ? std::string::npos : close - head - 1));
            if (name.rfind("GROUP", 0) == 0 && name.size() > 5) index = std::atoi(name.c_str() + 5);
            continue;
        }
        if (index >= 0) chunk += line + '\n';
    }
    flush();
    return out;
}

PaintGroup BlockGroup(const Ini& ini, int i) {
    PaintGroup fallback = FromDefault(i < kDefaultCount ? i : 0);
    if (ini.Has("Group", "BrandName")) {
        std::string name = ini.Text("Group", "BrandName");
        fallback = FromDefault(DefaultOf(name.empty() ? kEmptyBrand : BinHash(name)));
    }
    bool present = ini.Has("Group", "BrandName") || ini.Has("Group", "String");
    return present ? ReadGroup(ini, "Group", fallback, false) : fallback;
}

// the folder replaces the first group of the same brand after Group0, which is the car's own paints
void Overlay(std::vector<PaintGroup>& out, const PaintGroup& g) {
    for (size_t i = 1; i < out.size(); i++)
        if (out[i].brand == g.brand) {
            out[i] = g;
            return;
        }
    Push(out, g);
}

const std::vector<PaintGroup>& Groups() {
    static const std::vector<PaintGroup> groups = [] {
        std::vector<PaintGroup> out;
        const Ini& ini = Data("_PaintGroups.ini");
        g_unify = ini.Int("PaintGroups", "UnifyPaints", 0) != 0;
        std::string light = ini.Text("PaintGroups", "DefaultLightMaterial", "MAGGUNMETAL");
        g_light = light.empty() ? BinHash("MAGGUNMETAL") : BinHash(light);
        int count = ini.Int("PaintGroups", "NumberOfPaintGroups", 7);
        if (count < 0) count = 0;
        if (count > int(kMaxPaintGroups) - 1) count = int(kMaxPaintGroups) - 1;
        std::vector<PaintBlock> blocks = PaintBlocks();
        std::vector<char> used(blocks.size(), 0), filled(size_t(count + 1), 0);
        for (int i = 0; i <= count; i++) {
            int found = -1;
            for (size_t b = 0; b < blocks.size(); b++)
                if (!used[b] && blocks[b].index == i) {
                    found = int(b);
                    break;
                }
            if (found >= 0) {
                used[size_t(found)] = 1;
                Push(out, BlockGroup(blocks[size_t(found)].ini, i));
            } else {
                filled[size_t(i)] = 1;
                Push(out, FromDefault(i < kDefaultCount ? i : 0));
            }
        }
        // a repeated [Group5] fills the missing Group6 and Group7, in the order COP, RIM, VINYL
        for (size_t b = 0; b < blocks.size(); b++) {
            if (used[b]) continue;
            bool placed = false;
            for (int i = blocks[b].index; i <= count && size_t(i) < filled.size(); i++) {
                if (!filled[size_t(i)]) continue;
                out[size_t(i)] = BlockGroup(blocks[b].ini, i);
                filled[size_t(i)] = 0;
                placed = true;
                break;
            }
            if (!placed) Push(out, BlockGroup(blocks[b].ini, count + 1));
        }
        for (const Ini& file : Folder("PaintGroups")) {
            if (!file.Has("Group", "BrandName")) continue;
            std::string name = file.Text("Group", "BrandName");
            uint32_t brand = name.empty() ? kEmptyBrand : BinHash(name);
            Overlay(out, ReadGroup(file, "Group", FromDefault(DefaultOf(brand)), true));
        }
        for (size_t i = 0; i < out.size(); i++)
            LOGR("unlimiter: paint %zu %s body=%d rims=%d vinyl=%d", i, out[i].key.c_str(), out[i].body, out[i].rims,
                 out[i].vinyl);
        return out;
    }();
    return groups;
}

void Arm(uint32_t screen, int group) {
    if (group < 0 || size_t(group) >= Groups().size()) group = 0;
    g_group = group;
    kernel::W32(screen + kPaintIndex, 0);
    kernel::W32(screen + kSwatchSaved, uint32_t(Saved(group)));
}

bool On() { return Active() && Ready() && !Groups().empty(); }

bool Custom(X86Context& ctx) {
    uint32_t type = CurrentCarType(ctx);
    return type != 0xFFFFFFFF && (Config(type).visual.paint_custom || g_unify);
}

bool Shown(int i, uint32_t menu, bool custom) {
    if (i < 0 || size_t(i) >= Groups().size()) return false;
    if (i == 0 && !custom && !g_unify) return false;
    const PaintGroup& g = Groups()[size_t(i)];
    if (g_unify) return true;
    if (menu == kMenuPaint) return g.body;
    if (menu == kMenuRimPaint) return g.rims;
    return g.vinyl;
}

bool SameBrand(uint32_t group, uint32_t part) {
    if (group == kEmptyBrand) return part == 0 || part == kEmptyBrand;
    return group == part;
}

int GroupFor(X86Context& ctx, uint32_t part, uint32_t menu) {
    bool custom = Custom(ctx);
    uint32_t brand = 0;
    if (part) PartAttribute(part, kBrandName, brand);
    int first = -1;
    for (int i = 0; size_t(i) < Groups().size(); i++) {
        if (!Shown(i, menu, custom)) continue;
        if (first < 0) first = i;
        if (SameBrand(Groups()[size_t(i)].brand, brand)) return i;
    }
    return first < 0 ? 0 : first;
}

int Next(X86Context& ctx, int cur, int dir, uint32_t menu) {
    bool custom = Custom(ctx);
    std::vector<int> avail;
    for (int i = 0; size_t(i) < Groups().size(); i++)
        if (Shown(i, menu, custom)) avail.push_back(i);
    if (avail.empty()) return cur;
    int at = 0;
    for (int i = 0; size_t(i) < avail.size(); i++)
        if (avail[size_t(i)] == cur) at = i;
    int step = dir < 0 ? -1 : 1;
    at = (at + step + int(avail.size())) % int(avail.size());
    return avail[size_t(at)];
}

uint32_t Title(X86Context& ctx, int i) {
    if (i < 0 || size_t(i) >= Groups().size()) return 0;
    if (i == 0) {
        uint32_t type = CurrentCarType(ctx);
        if (type != 0xFFFFFFFF) {
            uint32_t own = CarHash(type, "Names", "VisualPaintCustom", kEmptyBrand);
            if (own && own != kEmptyBrand && StringExists(ctx, own)) return own;
        }
    }
    uint32_t hash = Groups()[size_t(i)].title;
    // the disc has Brilhante, Metálico, Perolado and Tráfego. Polícia is not among them, so the filter kept the
    // previous name and looked like it was not there
    if (hash && !StringExists(ctx, hash)) {
        const std::string& key = Groups()[size_t(i)].key;
        const char* word = nullptr;
        if (key == "CP_FILTER_COP") word = "Pol\xED""cia";
        else if (key == "CP_FILTER_RIM") word = "Aro";
        else if (key == "CP_FILTER_VINYL") word = "Vinil";
        else if (key == "CP_FILTER_CUSTOM") word = "Personalizada";
        else if (!key.empty()) word = key.c_str();
        if (word) Label(ctx, hash, word);
    }
    return hash;
}

bool PaintMenu(uint32_t menu) { return menu == kMenuPaint || menu == kMenuRimPaint; }

bool ExtraVinyl(uint32_t menu) {
    return menu == kMenuVinylCustom || (menu >= kMenuVinylExtra && menu < kMenuVinylExtra + kMaxVinylGroups);
}

bool VinylColourMenu(uint32_t menu) {
    return ExtraVinyl(menu) || (menu >= kMenuVinylStockFirst && menu < kMenuVinylStockFirst + kMenuVinylStockCount);
}

// the vinyl colour list is the VINYL group's brand, so PaintGroups/VINYL.ini renames it. The layer index stays the game's
uint32_t VinylBrand() {
    for (const PaintGroup& g : Groups())
        if (g.vinyl) return g.brand == kEmptyBrand ? 0 : g.brand;
    return BinHash("VINYL");
}

bool Joining() { return g_unify && On(); }

bool VinylColourSlot(uint8_t slot) { return slot >= kSlotVinylColour0 && slot < kSlotVinylColour0 + 4; }

uint8_t SlotByte(uint32_t part) { return part ? *static_cast<uint8_t*>(kernel::HostPtr(part + kPartSlot)) : 0; }

bool PaintSlot(uint8_t slot) {
    return slot == kSlotBasePaint || slot == kSlotPaintRim ||
           (slot >= kSlotVinylColour0 && slot < kSlotVinylColour0 + 4);
}

}  // namespace

// the swatch build compares BRAND_NAME (return 0x2A7980). UnifyPaints shows every vinyl colour, whatever its brand
bool ForceVinylColourBrand(uint32_t ret, uint32_t part, uint32_t key) {
    return ret == 0x2A7980 && key == kBrandName && Joining() && VinylColourSlot(SlotByte(part));
}

// the cart names a paint from four brands and leaves every other one as metallic. The group's own string is the name
uint32_t PaintCartFilter(X86Context& ctx, uint32_t brand) {
    if (!On()) return 0;
    for (int i = 0; size_t(i) < Groups().size(); i++) {
        if (Groups()[size_t(i)].brand != brand) continue;
        return Title(ctx, i);
    }
    return 0;
}

}  // namespace unlimiter

X86_EXTERN_FUNC(__imp__sub_00298540);
X86_FUNC(sub_00298540) {
    uint32_t screen = ctx.ecx, part = kernel::R32(ctx.esp + 4);
    uint32_t menu = kernel::R32(screen + unlimiter::kScreenMenu);
    if (!unlimiter::On()) return __imp__sub_00298540(ctx, base);
    if (!unlimiter::PaintMenu(menu)) {
        if (!unlimiter::VinylColourMenu(menu)) return __imp__sub_00298540(ctx, base);
        ctx.eax = unlimiter::VinylBrand();
        ctx.esp += 8;
        return;
    }
    int index = unlimiter::g_index_ours ? unlimiter::g_group : unlimiter::GroupFor(ctx, part, menu);
    if (!unlimiter::Shown(index, menu, unlimiter::Custom(ctx))) index = unlimiter::GroupFor(ctx, part, menu);
    unlimiter::Arm(screen, index);
    uint32_t brand = unlimiter::Groups()[size_t(index)].brand;
    ctx.eax = brand == unlimiter::kEmptyBrand ? 0 : brand;
    ctx.esp += 8;
}

X86_EXTERN_FUNC(__imp__sub_002AD160);
X86_FUNC(sub_002AD160) {
    uint32_t screen = ctx.ecx;
    uint32_t menu = kernel::R32(screen + unlimiter::kScreenMenu);
    if (!unlimiter::On() || !unlimiter::PaintMenu(menu)) return __imp__sub_002AD160(ctx, base);
    int dir = int(kernel::R32(ctx.esp + 4));
    int cur = unlimiter::g_group;
    int next = unlimiter::Next(ctx, cur, dir, menu);
    if (next == cur) {
        ctx.esp += 8;
        return;
    }
    uint32_t node = kernel::R32(screen + 0x234);
    if (node) node += 4;
    uint32_t count = unlimiter::Call(ctx, unlimiter::kListCount, screen + unlimiter::kSwatchList, {node});
    unlimiter::Saved(cur) = int(count) - 1;
    unlimiter::g_group = next;
    unlimiter::g_index_ours = true;
    uint32_t slot = menu == unlimiter::kMenuPaint ? unlimiter::kSlotBasePaint : unlimiter::kSlotPaintRim;
    unlimiter::Call(ctx, unlimiter::kBuildSwatch, screen, {slot});
    uint32_t refresh = kernel::R32(kernel::R32(screen) + 0x0C);
    unlimiter::Call(ctx, refresh, screen, {});
    ctx.esp += 8;
}

X86_EXTERN_FUNC(__imp__sub_002A2580);
X86_FUNC(sub_002A2580) {
    uint32_t screen = ctx.ecx;
    __imp__sub_002A2580(ctx, base);
    if (!unlimiter::On()) return;
    uint32_t menu = kernel::R32(screen + unlimiter::kScreenMenu);
    if (!unlimiter::PaintMenu(menu)) return;
    uint32_t title = unlimiter::Title(ctx, unlimiter::g_group);
    if (!title) return;
    kernel::CallGuest(ctx, unlimiter::kSetLanguage, {kernel::R32(screen + 0x10), unlimiter::kFilterObject, title});
}

X86_EXTERN_FUNC(__imp__sub_002AFE80);
X86_FUNC(sub_002AFE80) {
    uint32_t screen = ctx.ecx;
    unlimiter::g_index_ours = false;
    if (unlimiter::On()) unlimiter::ResetSaved();
    uint32_t menu = kernel::R32(screen + unlimiter::kScreenMenu);
    // a vinyl colour's brand must not be read back as a body filter, or the layer index becomes 1 or 2
    if (unlimiter::On() && unlimiter::VinylColourMenu(menu) &&
        kernel::R32(screen + unlimiter::kPaintIndex) == 0xFFFFFFFF)
        kernel::W32(screen + unlimiter::kPaintIndex, 0);
    bool extra = unlimiter::On() && unlimiter::ExtraVinyl(menu);
    if (extra) kernel::W32(screen + unlimiter::kScreenMenu, unlimiter::kMenuVinylStockFirst);
    __imp__sub_002AFE80(ctx, base);
    if (extra) kernel::W32(screen + unlimiter::kScreenMenu, menu);
    if (!unlimiter::On() || menu != unlimiter::kMenuRimPaint) return;
    int n = 0;
    for (int i = 0; size_t(i) < unlimiter::Groups().size(); i++)
        if (unlimiter::Shown(i, menu, unlimiter::Custom(ctx))) n++;
    // the game hides the rim paint's arrows: more than one rim group needs them
    if (n > 1)
        unlimiter::Call(ctx, unlimiter::kQueueMessage, kernel::R32(unlimiter::kFEng),
                        {unlimiter::kShowArrows, kernel::R32(screen + 0x10), 0});
}

X86_EXTERN_FUNC(__imp__sub_00254390);
X86_FUNC(sub_00254390) {
    uint32_t part = ctx.ecx;
    __imp__sub_00254390(ctx, base);
    if (!unlimiter::On() || ctx.eax || !unlimiter::PaintSlot(unlimiter::SlotByte(part))) return;
    ctx.eax = unlimiter::g_light ? unlimiter::g_light : unlimiter::BinHash("MAGGUNMETAL");
}

X86_EXTERN_FUNC(__imp__sub_002A6C70);
X86_FUNC(sub_002A6C70) {
    uint32_t screen = ctx.ecx, message = kernel::R32(ctx.esp + 4);
    if (message == 0x911AB364 && unlimiter::AttachmentPartBack(ctx, screen)) {
        ctx.esp += 20;
        return;
    }
    uint32_t menu = kernel::R32(screen + unlimiter::kScreenMenu);
    if (!unlimiter::On() || message != unlimiter::kAccept || !unlimiter::ExtraVinyl(menu))
        return __imp__sub_002A6C70(ctx, base);
    uint32_t selected = unlimiter::Call(ctx, kernel::R32(kernel::R32(screen) + 0x14), screen, {});
    if (!selected) return __imp__sub_002A6C70(ctx, base);
    if ((kernel::R32(selected + unlimiter::kSelectedFlags) & 0xF) == 2) {
        unlimiter::Call(ctx, unlimiter::kPlayLocked, screen, {});
        ctx.esp += 20;
        return;
    }
    uint32_t part = kernel::R32(selected + unlimiter::kSelectedPart);
    if (!part || !unlimiter::PartInt(part, unlimiter::kNumRemap, 0)) return __imp__sub_002A6C70(ctx, base);
    X86Context saved = ctx;
    ctx.ecx = 0;
    uint32_t copy = kernel::CallGuest(ctx, unlimiter::kMalloc, {0x2C});
    ctx = saved;
    if (copy) unlimiter::Call(ctx, unlimiter::kCopyPart, copy, {selected});
    unlimiter::Call(ctx, unlimiter::kSetTempPart, unlimiter::kManager, {copy});
    uint32_t key = (kernel::R32(screen + unlimiter::kPreviousMenu) << 16) | menu;
    unlimiter::Call(ctx, unlimiter::kSwitchPackage, kernel::R32(unlimiter::kFEng),
                    {kernel::R32(unlimiter::kPaintPackage), key, 0, 0});
    ctx.esp += 20;
}

// GetCarPartList, from the swatch build (return 0x2A7951). UnifyPaints also takes the rim, the vinyl colour and the
// gauge backing, the same four slots the PC walks
X86_EXTERN_FUNC(__imp__sub_0029F350);
X86_FUNC(sub_0029F350) {
    uint32_t ret = kernel::R32(ctx.esp), self = ctx.ecx;
    uint32_t slot = kernel::R32(ctx.esp + 4), list = kernel::R32(ctx.esp + 8), flag = kernel::R32(ctx.esp + 12);
    __imp__sub_0029F350(ctx, base);
    if (ret != 0x2A7951 || !unlimiter::Joining()) return;
    X86Context saved = ctx;
    static const uint32_t kSlots[] = {unlimiter::kSlotBasePaint, unlimiter::kSlotPaintRim, unlimiter::kSlotVinylColour0,
                                      unlimiter::kSlotHudBackingColour};
    for (uint32_t extra : kSlots) {
        if (extra == slot) continue;
        ctx = saved;
        ctx.ecx = self;
        kernel::CallGuest(ctx, 0x29F350, {extra, list, flag});
    }
    ctx = saved;
}

// GetLocalizedString, at the cart's paint line (0x2A5CE7, and 0x2A62DA for French). The hash on the stack is the
// filter the disc picked. A brand the disc does not list takes the group's name instead
X86_EXTERN_FUNC(__imp__sub_00124C40);
X86_FUNC(sub_00124C40) {
    uint32_t ret = kernel::R32(ctx.esp);
    if (ret == 0x144121) {
        if (uint32_t phrase = unlimiter::CopPhrase()) kernel::W32(ctx.esp + 4, phrase);
    }
    if (unlimiter::On() && (ret == 0x2A5CE7 || ret == 0x2A62DA)) {
        uint32_t part = kernel::R32(ctx.ebp + (ret == 0x2A5CE7 ? 0xFFFFFFB8u : 0xFFFFFFB4u));
        uint32_t brand = 0;
        if (part && unlimiter::PartAttribute(part, unlimiter::kBrandName, brand)) {
            if (uint32_t filter = unlimiter::PaintCartFilter(ctx, brand)) kernel::W32(ctx.esp + 4, filter);
        }
    }
    __imp__sub_00124C40(ctx, base);
}
