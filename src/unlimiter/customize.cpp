#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"
#include "unlimiter/parts.h"

namespace unlimiter {

namespace {

constexpr uint32_t kPartsPackage = 0x46C4E0;
constexpr uint32_t kGenericTopPackage = 0x46C4DC;  // CustomizeGenericTop.fng, the attachments submenu
constexpr uint32_t kPartsTitle = 0x158;
constexpr uint32_t kFallbackIcon = 0x28C24F6;
constexpr uint32_t kOptionSkipped = 0xFFFFFFFF;

bool g_filtering = false;

struct Category {
    uint32_t menu, slot;
    const char* key;
    const char* default_icon;
    const char* default_name;
    const char* text[kLanguageCount];
};

bool Wanted(const CarConfig& c, uint32_t slot) {
    switch (slot) {
        case kSlotInterior: return c.parts.interior;
        case kSlotBase: return c.parts.roof;
        case kSlotFrontBrake: return c.parts.brakes;
        case kSlotLeftHeadlight: return c.parts.headlights;
        case kSlotLeftBrakelight: return c.parts.taillights;
        case kSlotLeftMirror: return c.parts.mirrors;
        case kSlotDriver: return c.visual.driver;
        case kSlotLicensePlate: return c.visual.license_plate;
        case kSlotTire: return c.visual.tires;
        case kSlotNeon: return c.visual.neon && !Global().disable_neon;
        default: return false;
    }
}

const std::vector<Category>& PartsCategories() {
    static const std::vector<Category> list = {
        {0x106, kSlotInterior, "PartsInterior", "VISUAL_PART_INTERIOR", "CO_INTERIOR",
         {"INTERIOR", "INTERIEUR", "INNENRAUM", "INTERNI", "INTERIOR", "Interior", "INTERIOR"}},
        {0x107, kSlotBase, "PartsRoof", "VISUAL_PART_ROOF", "CO_ROOF",
         {"ROOF", "TOIT", "DACH", "TETTO", "TECHO", "Teto", "ATAP"}},
        {0x108, kSlotFrontBrake, "PartsBrakes", "VISUAL_PART_BRAKE", "CO_BRAKES",
         {"BRAKES", "FREINS", "BREMSEN", "FRENI", "FRENOS", "Freios", "REM"}},
        {0x109, kSlotLeftHeadlight, "PartsHeadlights", "VISUAL_PART_HEAD_LIGHTS", "CO_HEADLIGHTS",
         {"HEADLIGHTS", "PHARES", "SCHEINWERFER", "FARI", "FAROS", "Far\xF3is", "LAMPU DEPAN"}},
        {0x10A, kSlotLeftBrakelight, "PartsTaillights", "VISUAL_PART_TAIL_LIGHTS", "CO_TAILLIGHTS",
         {"TAILLIGHTS", "FEUX ARRIERE", "RUECKLICHTER", "FANALI", "PILOTOS", "Lanternas", "LAMPU BELAKANG"}},
        {0x10B, kSlotLeftMirror, "PartsMirrors", "VISUAL_PART_SIDE_MIRROR", "CO_SIDE_MIRROR",
         {"MIRRORS", "RETROVISEURS", "SPIEGEL", "SPECCHIETTI", "ESPEJOS", "Retrovisores", "SPION"}},
    };
    return list;
}

const std::vector<Category>& VisualCategories() {
    static const std::vector<Category> list = {
        {0x117, kSlotDriver, "VisualDriver", "VISUAL_PART_DRIVER", "CO_DRIVER",
         {"DRIVER", "PILOTE", "FAHRER", "PILOTA", "PILOTO", "Piloto", "PENGEMUDI"}},
        {0x118, kSlotLicensePlate, "VisualLicensePlate", "VISUAL_PART_LICENSE_PLATE", "CO_LICENSE_PLATE",
         {"LICENSE PLATE", "PLAQUE", "KENNZEICHEN", "TARGA", "MATRICULA", "Placa", "PELAT NOMOR"}},
        {0x119, kSlotTire, "VisualTires", "VISUAL_PART_TIRE", "CO_TIRES",
         {"TIRES", "PNEUS", "REIFEN", "PNEUMATICI", "NEUMATICOS", "Pneus", "BAN"}},
        {0x11A, kSlotNeon, "VisualNeon", "VISUAL_PART_NEON", "CO_NEON",
         {"NEON", "NEON", "NEON", "NEON", "NEON", "Neon", "NEON"}},
    };
    return list;
}

struct Chosen {
    uint32_t slot, icon, title;
    int group = -1;
};

constexpr uint32_t kVinylCustomMenu = kMenuVinylCustom, kVinylCustomGroup = 31, kVinylIcon = 0x55778E5A;
constexpr uint32_t kGroupLocal = 0x14, kVinylFlagLocal = 0x0D;

struct VinylGroup {
    int index = 0;
    uint32_t title = 0, icon = 0;
    bool alt_camera = true;
};

struct VinylShown {
    uint32_t menu = 0, icon = 0, title = 0;
    int index = 0;
    int position = 0;
};

const VinylGroup* kVinylDefaults() {
    static const VinylGroup list[] = {
        {31, 0, 0, true}, {0, 0, 0, false}, {1, 0, 0, true}, {2, 0, 0, true}, {3, 0, 0, false},
        {4, 0, 0, true},  {5, 0, 0, true},  {6, 0, 0, true}, {7, 0, 0, true}, {8, 0, 0, true},
    };
    return list;
}

const char* const kVinylStrings[] = {"CO_VINYL_CUSTOM", "CO_VINYL_FLAME",     "CO_VINYL_TRIBAL", "CO_VINYL_STRIPE",
                                     "CO_VINYL_RACE_FLAG", "CO_VINYL_NATIONAL_FLAG", "CO_VINYL_BODY", "CO_VINYL_UNIQUE",
                                     "CO_VINYL_CONTEST", "CO_VINYL_SPECIAL"};
const char* const kVinylTextures[] = {"VINYL_GROUP_CUSTOM", "VINYL_GROUP_FLAMES", "VINYL_GROUP_TRIBAL",
                                      "VINYL_GROUP_STRIPES", "VINYL_GROUP_RACING_FLAG", "VINYL_GROUP_NATIONAL_FLAG",
                                      "VINYL_GROUP_BODY", "VINYL_GROUP_UNIQUE", "VINYL_GROUP_CONTEST",
                                      "VINYL_GROUP_SPECIAL"};

VinylGroup DefaultVinyl(int i) {
    VinylGroup g = kVinylDefaults()[i];
    g.title = BinHash(kVinylStrings[i]);
    g.icon = BinHash(kVinylTextures[i]);
    return g;
}

int DefaultVinylSlot(int index) {
    for (int i = 0; i <= 9; i++)
        if (kVinylDefaults()[i].index == index) return i;
    return 1;
}

void UpsertVinyl(std::vector<VinylGroup>& out, const VinylGroup& g) {
    for (VinylGroup& have : out)
        if (have.index == g.index) {
            have = g;
            return;
        }
    if (out.size() < kMaxVinylGroups) out.push_back(g);
}

VinylGroup ReadVinyl(const Ini& ini, const std::string& section, const VinylGroup& fallback, bool folder) {
    VinylGroup g = fallback;
    if (ini.Has(section, "Index")) g.index = ini.Int(section, "Index", g.index);
    if (g.index < 0 || g.index > 31) g.index = fallback.index;
    if (ini.Has(section, "String")) g.title = ini.Hash(section, "String", g.title);
    if (ini.Has(section, "Texture")) g.icon = ini.Hash(section, "Texture", g.icon);
    int alt = folder ? 1 : (fallback.alt_camera ? 1 : 0);
    g.alt_camera = ini.Int(section, "UseAltCamera", alt) != 0;
    if (!g.title) g.title = fallback.title;
    if (!g.icon) g.icon = fallback.icon;
    return g;
}

const std::vector<VinylGroup>& VinylGroups() {
    static const std::vector<VinylGroup> groups = [] {
        std::vector<VinylGroup> out;
        const Ini& ini = Data("_VinylGroups.ini");
        int count = ini.Int("VinylGroups", "NumberOfVinylGroups", 9);
        if (count < 0) count = 0;
        if (count > int(kMaxVinylGroups) - 1) count = int(kMaxVinylGroups) - 1;
        char section[16];
        for (int i = 0; i <= count; i++) {
            std::snprintf(section, sizeof section, "Group%d", i);
            VinylGroup fallback = i <= 9 ? DefaultVinyl(i) : DefaultVinyl(1);
            UpsertVinyl(out, ini.Has(section, "Index") || ini.Has(section, "String") ? ReadVinyl(ini, section, fallback, false)
                                                                                      : fallback);
        }
        for (const Ini& file : Folder("VinylGroups")) {
            if (!file.Has("Group", "Index")) continue;
            int index = file.Int("Group", "Index", -1);
            if (index < 0 || index > 31) continue;
            VinylGroup fallback = DefaultVinyl(DefaultVinylSlot(index));
            fallback.index = index;
            UpsertVinyl(out, ReadVinyl(file, "Group", fallback, true));
        }
        LOGR("unlimiter: %zu vinyl groups", out.size());
        return out;
    }();
    return groups;
}

std::vector<VinylShown> g_shown;
bool g_vinyl_setup = false;

uint32_t MenuOfVinyl(int index) {
    if (index == int(kVinylCustomGroup)) return kVinylCustomMenu;
    if (index >= 0 && index < int(kMenuVinylStockCount)) return kMenuVinylStockFirst + uint32_t(index);
    return kMenuVinylExtra + uint32_t(std::max(0, index - int(kMenuVinylStockCount)));
}

const VinylShown* ShownVinyl(uint32_t menu) {
    for (const VinylShown& s : g_shown)
        if (s.menu == menu) return &s;
    return nullptr;
}

bool HasVinylGroup(X86Context& ctx, uint32_t type, uint32_t group) {
    uint32_t prev = 0;
    for (int guard = 0; guard < 4096; guard++) {
        X86Context saved = ctx;
        ctx.ecx = kCarPartDatabase;
        uint32_t part = kernel::CallGuest(ctx, kNewGetCarPart, {type, kSlotVinylLayer0, 0, prev, 0xFFFFFFFF});
        ctx = saved;
        if (!part) return false;
        if ((*static_cast<uint8_t*>(kernel::HostPtr(part + kPartUpgrade)) & 0x1F) == group) return true;
        prev = part;
    }
    return false;
}

struct MenuState {
    Chosen by_menu[kOwnMenus] = {};
    bool set[kOwnMenus] = {};
    uint32_t slot_title[kSlotCount] = {};
    uint32_t car = 0xFFFFFFFF;
};

MenuState g;

void Reset(uint32_t car) {
    if (g.car == car) return;
    g = MenuState{};
    g.car = car;
}

uint32_t IconOf(X86Context& ctx, uint32_t type, const char* key, uint32_t fallback) {
    uint32_t hash = CarHash(type, "Icons", key, fallback);
    if (hash == 0xFFFFFFFF || hash == fallback) return fallback;
    return TextureExists(ctx, hash) ? hash : fallback;
}

uint32_t NameOf(X86Context& ctx, uint32_t type, const char* key, uint32_t fallback) {
    uint32_t hash = CarHash(type, "Names", key, fallback);
    if (hash == 0xFFFFFFFF || hash == fallback) return fallback;
    return StringExists(ctx, hash) ? hash : fallback;
}

void Add(X86Context& ctx, uint32_t screen, uint32_t menu, uint32_t slot, uint32_t icon, uint32_t title) {
    uint32_t index = menu - kOwnMenuFirst;
    if (index < kOwnMenus) {
        g.by_menu[index] = {slot, icon, title};
        g.set[index] = true;
    }
    if (slot < kSlotCount) g.slot_title[slot] = title;
    AddOwnOption(ctx, screen, kernel::R32(kPartsPackage), icon, title, menu);
}

void AddCategory(X86Context& ctx, uint32_t screen, uint32_t type, const Category& c) {
    if (!HasParts(ctx, type, c.slot)) return;
    uint32_t fallback_icon = BinHash(c.default_icon);
    uint32_t icon = IconOf(ctx, type, c.key, fallback_icon);
    if (!TextureExists(ctx, icon)) icon = kFallbackIcon;
    uint32_t title = CarTextHash(type, "Names", c.key, c.default_name);
    Label(ctx, title, c.text[LanguageIndex(ctx)]);
    Add(ctx, screen, c.menu, c.slot, icon, title);
}

void AddOneAttachment(X86Context& ctx, uint32_t screen, uint32_t type, int i) {
    uint32_t slot = kSlotAttachment0 + uint32_t(i);
    char key[32], name[32], text[48];
    std::snprintf(key, sizeof key, "PartsAttachment%d", i);
    std::snprintf(name, sizeof name, "CO_ATTACHMENT_%d", i + 1);
    std::snprintf(text, sizeof text, "Acess\xF3rio %d", i + 1);
    uint32_t icon = IconOf(ctx, type, key, BinHash("VISUAL_PART_ATTACHMENT"));
    if (!TextureExists(ctx, icon)) icon = kFallbackIcon;
    uint32_t title = CarTextHash(type, "Names", key, name);
    Label(ctx, title, text, title == BinHash(name));
    Add(ctx, screen, kMenuAttachmentFirst + uint32_t(i), slot, icon, title);
}

bool AnyAttachment(X86Context& ctx, uint32_t type, int count) {
    for (int i = 0; i < count; i++)
        if (HasParts(ctx, type, kSlotAttachment0 + uint32_t(i))) return true;
    return false;
}

void AddAttachmentSlots(X86Context& ctx, uint32_t screen, uint32_t type, int count) {
    for (int i = 0; i < count; i++) {
        if (!HasParts(ctx, type, kSlotAttachment0 + uint32_t(i))) continue;
        AddOneAttachment(ctx, screen, type, i);
    }
}

// the disc's CO_ATTACHMENTS is the French "Pièces jointes". The other part names use the Brazilian form
uint32_t AttachmentTitle(X86Context& ctx, uint32_t type) {
    uint32_t title = CarTextHash(type, "Names", "PartsAttachments", "CO_ATTACHMENTS");
    return Label(ctx, title, "Acess\xF3rios", title == BinHash("CO_ATTACHMENTS"));
}

void AddParts(X86Context& ctx, uint32_t screen) {
    uint32_t type = CurrentCarType(ctx);
    if (type == 0xFFFFFFFF) return;
    Reset(type);
    const CarConfig& config = Config(type);
    for (const Category& c : PartsCategories())
        if (Wanted(config, c.slot)) AddCategory(ctx, screen, type, c);
    if (config.parts.attachments <= 0 || !AnyAttachment(ctx, type, config.parts.attachments)) return;
    // the parts list itself, unless the car asks for one Acessórios entry that opens them
    if (config.main.attachments_under_parts) {
        AddAttachmentSlots(ctx, screen, type, config.parts.attachments);
        return;
    }
    uint32_t icon = IconOf(ctx, type, "PartsAttachments", BinHash("VISUAL_PART_ATTACHMENT"));
    if (!TextureExists(ctx, icon)) icon = kFallbackIcon;
    uint32_t title = AttachmentTitle(ctx, type);
    AddOwnOption(ctx, screen, kernel::R32(kGenericTopPackage), icon, title, kMenuAttachments);
}

// menu 0x10C is a category, not a part slot. The dispatcher would otherwise open the parts browser
// the parts list ends by telling the scroller which row to stand on. Refresh reads that row, and a menu that
// never asked crashes on the empty one
void SelectRow(X86Context& ctx, uint32_t screen) {
    uint32_t position = kernel::R32(screen + kScreenPreviousMenu) & 0xFFFF00FF;
    uint32_t count = OptionCount();
    if (position == 0 || position > count) position = 1;
    if (!count) return;
    if (kernel::G<uint8_t>(screen + 0x125)) {
        kernel::G<uint8_t>(screen + 0x118) = 0;
        kernel::G<uint8_t>(screen + 0x115) = 1;
        kernel::G<uint8_t>(screen + 0x116) = 0;
        kernel::W32(screen + 0x10C, 0);
    }
    uint32_t scroller = screen + 0x2C, vtbl = kernel::R32(scroller);
    if (!vtbl) return;
    X86Context saved = ctx;
    ctx.ecx = scroller;
    kernel::CallGuest(ctx, kernel::R32(vtbl + 0x3C), {position});
    ctx = saved;
}

void FillAttachmentMenu(X86Context& ctx, uint32_t screen) {
    uint32_t type = CurrentCarType(ctx);
    if (type == 0xFFFFFFFF) return;
    kernel::W32(screen + 0x1AC, AttachmentTitle(ctx, type));
    BeginOptions(screen);
    AddAttachmentSlots(ctx, screen, type, Config(type).parts.attachments);
    PlaceInitial(screen);
    SelectRow(ctx, screen);
    EndOptions(screen);
}

void AddVinylGroups(X86Context& ctx, uint32_t screen) {
    uint32_t type = CurrentCarType(ctx);
    g_shown.clear();
    if (type == 0xFFFFFFFF) return;
    const CarConfig& config = Config(type);
    bool custom = config.visual.vinyls_custom && HasVinylGroup(ctx, type, kVinylCustomGroup);
    LOGR("unlimiter: custom vinyls of %s %s", CarTypeName(type).c_str(), custom ? "found" : "not found");
    static const char* const kText[kLanguageCount] = {"CUSTOM",         "PERSONNALISES",  "EIGENE", "PERSONALIZZATI",
                                                      "PERSONALIZADOS", "Personalizados", "KUSTOM"};
    for (const VinylGroup& group : VinylGroups()) {
        if (group.index == int(kVinylCustomGroup) && !custom) continue;
        uint32_t icon = group.icon, title = group.title;
        if (group.index == int(kVinylCustomGroup)) {
            icon = IconOf(ctx, type, "VisualVinylsCustom", icon ? icon : kVinylIcon);
            title = NameOf(ctx, type, "VisualVinylsCustom", title ? title : BinHash("CO_VINYL_CUSTOM"));
            Label(ctx, title, kText[LanguageIndex(ctx)]);
        }
        if (!icon || !TextureExists(ctx, icon)) icon = kVinylIcon;
        if (!title) title = BinHash("CO_VINYL_CUSTOM");
        uint32_t menu = MenuOfVinyl(group.index);
        AddOwnOption(ctx, screen, kernel::R32(kPartsPackage), icon, title, menu);
        if (menu == kVinylCustomMenu) {
            g.by_menu[kVinylCustomMenu - kOwnMenuFirst] = {kSlotVinylLayer0, icon, title, group.index};
            g.set[kVinylCustomMenu - kOwnMenuFirst] = true;
        }
        g_shown.push_back({menu, icon, title, group.index, int(OptionCount())});
    }
}

void AddVisual(X86Context& ctx, uint32_t screen) {
    uint32_t type = CurrentCarType(ctx);
    if (type == 0xFFFFFFFF) return;
    Reset(type);
    const CarConfig& config = Config(type);
    for (const Category& c : VisualCategories())
        if (Wanted(config, c.slot)) AddCategory(ctx, screen, type, c);
}

int g_decal_place = -1, g_decal_count = -1;
bool g_decal_slots = false;

int NumDecals(X86Context& ctx, int place) {
    int fallback = place == 2 || place == 3 ? 6 : 1;
    X86Context saved = ctx;
    ctx.ecx = 0x4DEC68;
    uint32_t part = kernel::CallGuest(ctx, 0x296480, {kSlotDecalFrontWindow + uint32_t(place)});
    ctx = saved;
    int count = part ? PartInt(part, BinHash("NUM_DECALS"), fallback) : fallback;
    if (count < 0 || count > int(kMenuDecalSlots)) count = fallback;
    // a stock door part says 5, and the disc still lists six slots. Only a higher count adds 7 and 8
    if (count < fallback) count = fallback;
    return count;
}

struct Stock {
    bool shown;
    const char* key;
};

Stock StockMenu(const CarConfig& c, uint32_t menu, uint32_t icon) {
    if (OwnRimBrands() && menu >= kMenuRimBrandFirst && menu <= kMenuRimBrandLast) return {false, nullptr};
    // the game adds 0x402 to 0x409 itself; the ini's list replaces them. 0x401 is the return-to-stock option
    if (!VinylGroups().empty() && menu >= kMenuVinylStockFirst && menu < kMenuVinylStockFirst + kMenuVinylStockCount)
        return {false, nullptr};
    // the doors list six slots on their own. NUM_DECALS drops the ones past the part's count
    if (g_decal_slots && menu >= kMenuDecalSlotFirst && menu < kMenuDecalSlotFirst + 6 &&
        int(menu - kMenuDecalSlotFirst) >= g_decal_count)
        return {false, nullptr};
    switch (menu) {
        case kMenuCategoryParts: return {c.category.parts, "Parts"};
        case kMenuCategoryPerformance: return {c.category.performance, "Performance"};
        case kMenuCategoryVisual: return {c.category.visual, "Visual"};
        case kMenuBodyKits: return {c.parts.body_kits, "PartsBodyKits"};
        case kMenuSpoilers: return {c.parts.spoilers, "PartsSpoilers"};
        case kMenuRims: return {c.parts.rims, "PartsRims"};
        case kMenuHoods: return {c.parts.hoods, "PartsHoods"};
        case kMenuRoofScoops: return {c.parts.roof_scoops, "PartsRoofScoops"};
        case kMenuEngine: return {c.performance.engine, "PerformanceEngine"};
        case kMenuTransmission: return {c.performance.transmission, "PerformanceTransmission"};
        case kMenuChassis: return {c.performance.chassis, "PerformanceChassis"};
        case kMenuNitrous: return {c.performance.nitrous, "PerformanceNitrous"};
        case kMenuTires: return {c.performance.tires, "PerformanceTires"};
        case kMenuBrakes: return {c.performance.brakes, "PerformanceBrakes"};
        case kMenuInduction:
            return {c.performance.induction, icon == kIconSupercharger ? "PerformanceSupercharger" : "PerformanceTurbo"};
        case kMenuPaint: return {c.visual.paint, "VisualPaint"};
        case kMenuVinyls: return {c.visual.vinyls, "VisualVinyls"};
        case kMenuRimPaint: return {c.visual.rim_paint, "VisualRimPaint"};
        case kMenuWindowTint: return {c.visual.window_tint, "VisualWindowTint"};
        case kMenuDecals: return {c.visual.decals, "VisualDecals"};
        case kMenuNumbers: return {c.visual.numbers, "VisualNumbers"};
        case kMenuCustomGauges: return {c.visual.custom_gauges, "VisualCustomGauges"};
        default: break;
    }
    static const char* const kPlaces[6] = {"VisualDecalsWindshield", "VisualDecalsRearWindow", "VisualDecalsLeftDoor",
                                           "VisualDecalsRightDoor", "VisualDecalsLeftQuarter", "VisualDecalsRightQuarter"};
    if (menu >= kMenuDecalPlaceFirst && menu <= kMenuDecalPlaceLast)
        return {c.visual.decal_places[menu - kMenuDecalPlaceFirst], kPlaces[menu - kMenuDecalPlaceFirst]};
    return {true, nullptr};
}

}  // namespace

// the generic-top package does not know an attachment slot, so its own click and back leave the icons on the next screen
bool AttachmentClick(X86Context& ctx, uint32_t screen, uint32_t message) {
    if (!Active() || !Ready()) return false;
    uint32_t type = CurrentCarType(ctx);
    if (type >= CarTypeCount() || Config(type).main.attachments_under_parts) return false;
    if (kernel::R32(screen + 0x14C) != kMenuAttachments) return false;
    constexpr uint32_t kAccept = 0xC407210, kBack = 0x911AB364;
    constexpr uint32_t kSwitch = 0xEB160, kFEng = 0x48E94C, kCategory = 0x46C4D8;
    uint32_t package = 0, packed = 0;
    if (message == kBack) {
        package = kernel::R32(kCategory);
        packed = kMenuCategoryParts | (kMenuAttachments << 16);
    } else if (message == kAccept) {
        uint32_t option = kernel::R32(screen + 0x38);
        uint32_t picked = option ? kernel::R32(option + 0x50) & 0xFFFF : 0;
        if (picked < kMenuAttachmentFirst || picked >= kMenuAttachmentFirst + kSlotAttachments) return false;
        package = kernel::R32(option + 0x4C);
        packed = picked | (kMenuAttachments << 16);
    } else {
        return false;
    }
    if (!package) return false;
    X86Context saved = ctx;
    ctx.ecx = kernel::R32(kFEng);
    kernel::CallGuest(ctx, kSwitch, {package, packed, 0, 0});
    ctx = saved;
    return true;
}

// the part screen's own back opens the category package, which is the bar at the bottom. The list the player
// came from is the top bar
bool AttachmentPartBack(X86Context& ctx, uint32_t screen) {
    uint32_t slot = kernel::R32(screen + 0x144);
    if (slot < kMenuAttachmentFirst || slot >= kMenuAttachmentFirst + kSlotAttachments) return false;
    uint32_t type = CurrentCarType(ctx);
    if (!Active() || !Ready() || type >= CarTypeCount() || Config(type).main.attachments_under_parts) return false;
    constexpr uint32_t kSwitch = 0xEB160, kFEng = 0x48E94C, kGenericTop = 0x46C4DC;
    uint32_t top = kernel::R32(kGenericTop);
    if (!top) return false;
    LOGR("unlimiter: attachment %x back to the top list", slot);
    X86Context saved = ctx;
    ctx.ecx = kernel::R32(kFEng);
    kernel::CallGuest(ctx, kSwitch, {top, (slot << 16) | kMenuAttachments, 0, 0});
    ctx = saved;
    return true;
}

int DecalPlace() { return g_decal_place; }

bool SetupDecalSlots(X86Context& ctx, uint32_t screen) {
    uint32_t place_menu = kernel::R32(screen + 0x14C);
    if (place_menu < kMenuDecalPlaceFirst || place_menu > kMenuDecalPlaceLast) return false;
    g_decal_place = int(place_menu - kMenuDecalPlaceFirst);
    g_decal_count = NumDecals(ctx, g_decal_place);
    g_decal_slots = true;
    kernel::W32(0x46C508, place_menu);
    return true;
}

void AddExtraDecalSlots(X86Context& ctx, uint32_t screen) {
    g_decal_slots = false;
    if (g_decal_place < 0 || g_decal_count < 0) return;
    int from = g_decal_place == 2 || g_decal_place == 3 ? 6 : 0;
    uint32_t package = kernel::R32(0x46C4E8);
    for (int i = from; i < g_decal_count && i < int(kMenuDecalSlots); i++) {
        uint32_t icon = (g_decal_place == 2 ? 0xFE957F48u : 0x2E40EADBu) + uint32_t(i);
        uint32_t title = 0x7D212CFAu + uint32_t(i);
        Label(ctx, title, "SLOT " + std::to_string(i + 1));
        AddOwnOption(ctx, screen, package, icon, title, kMenuDecalSlotFirst + uint32_t(i));
    }
    g_decal_count = -1;
}

uint32_t DecalTextureSlot(uint32_t menu) {
    if (g_decal_place < 0 || menu < kMenuDecalSlotFirst + 6 || menu >= kMenuDecalSlotFirst + kMenuDecalSlots) return 0;
    return kSlotDecalTextureFirst + uint32_t(g_decal_place) * kSlotDecalTexturesPerPlace + (menu - kMenuDecalSlotFirst);
}

bool VinylGroupMenu(uint32_t menu) { return ShownVinyl(menu) != nullptr; }

int VinylGroupIndex(uint32_t menu) {
    const VinylShown* s = ShownVinyl(menu);
    return s ? s->index : -1;
}

bool VinylAltCamera(uint32_t menu) {
    int index = VinylGroupIndex(menu);
    if (index < 0 && menu >= kMenuVinylStockFirst && menu < kMenuVinylStockFirst + kMenuVinylStockCount)
        index = int(menu - kMenuVinylStockFirst);
    if (index < 0) return false;
    for (const VinylGroup& g : VinylGroups())
        if (g.index == index) return g.alt_camera;
    return DefaultVinyl(DefaultVinylSlot(index)).alt_camera;
}

int VinylOption(int group) {
    if (!g_vinyl_setup) return 0;
    for (const VinylShown& s : g_shown)
        if (s.index == group) return s.position;
    return 1;
}

uint32_t AddOwnOption(X86Context& ctx, uint32_t screen, uint32_t package, uint32_t icon, uint32_t title, uint32_t menu) {
    bool filtering = g_filtering;
    g_filtering = true;
    X86Context saved = ctx;
    ctx.ecx = screen;
    uint32_t option = kernel::CallGuest(ctx, kAddCustomOption, {package, icon, title, menu});
    ctx = saved;
    g_filtering = filtering;
    return option;
}

}  // namespace unlimiter

X86_EXTERN_FUNC(__imp__sub_002AABD0);
X86_FUNC(sub_002AABD0) {
    if (!unlimiter::g_filtering && unlimiter::Active() && unlimiter::Ready()) {
        unlimiter::g_filtering = true;
        uint32_t icon = kernel::R32(ctx.esp + 8), title = kernel::R32(ctx.esp + 12), menu = kernel::R32(ctx.esp + 16);
        uint32_t type = unlimiter::CurrentCarType(ctx);
        if (type != 0xFFFFFFFF) {
            unlimiter::Stock s = unlimiter::StockMenu(unlimiter::Config(type), menu, icon);
            if (!s.shown) {
                unlimiter::g_filtering = false;
                ctx.eax = unlimiter::kOptionSkipped;
                ctx.esp += 4 + 16;
                return;
            }
            if (s.key) {
                kernel::W32(ctx.esp + 8, unlimiter::IconOf(ctx, type, s.key, icon));
                kernel::W32(ctx.esp + 12, unlimiter::NameOf(ctx, type, s.key, title));
            }
        }
        unlimiter::g_filtering = false;
    }
    __imp__sub_002AABD0(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_002AA3B0);
X86_FUNC(sub_002AA3B0) {
    uint32_t menu = kernel::R32(ctx.esp + 4);
    if (unlimiter::Active() && unlimiter::IsOwnMenu(menu)) {
        ctx.eax &= ~0xFFu;
        ctx.esp += 4 + 8;
        return;
    }
    __imp__sub_002AA3B0(ctx, base);
}

void UnlimiterAddParts(X86Context& ctx, uint8_t*) {
    if (!unlimiter::Active() || !unlimiter::Ready()) return;
    unlimiter::AddParts(ctx, ctx.esi);
    unlimiter::PlaceInitial(ctx.esi);
}

void UnlimiterAddVisual(X86Context& ctx, uint8_t*) {
    if (!unlimiter::Active() || !unlimiter::Ready()) return;
    unlimiter::AddVisual(ctx, ctx.esi);
    unlimiter::PlaceInitial(ctx.esi);
}

void UnlimiterAddVinylGroups(X86Context& ctx, uint8_t*) {
    if (!unlimiter::Active() || !unlimiter::Ready()) return;
    unlimiter::AddVinylGroups(ctx, ctx.esi);
    unlimiter::PlaceInitial(ctx.esi);
}

// CustomizeSub::Setup. The open menu is at +0x14C (kScreenMenu is the rims screen). 0x10C is filled here
X86_EXTERN_FUNC(__imp__sub_002AF090);
X86_FUNC(sub_002AF090) {
    uint32_t screen = ctx.ecx;
    uint32_t menu = kernel::R32(screen + 0x14C);
    bool on = unlimiter::Active() && unlimiter::Ready();
    uint32_t type = on ? unlimiter::CurrentCarType(ctx) : 0xFFFFFFFF;
    bool submenu = on && type < unlimiter::CarTypeCount() && !unlimiter::Config(type).main.attachments_under_parts;
    if (submenu && menu == unlimiter::kMenuAttachments) {
        unlimiter::FillAttachmentMenu(ctx, screen);
        uint32_t finish = kernel::R32(kernel::R32(screen) + 0x0C);
        X86Context saved = ctx;
        ctx.ecx = screen;
        kernel::CallGuest(ctx, finish, {});
        ctx = saved;
        ctx.esp += 4;
        return;
    }
    __imp__sub_002AF090(ctx, base);
    bool slot = menu >= unlimiter::kMenuAttachmentFirst && menu < unlimiter::kMenuAttachmentFirst + unlimiter::kSlotAttachments;
    if (submenu && slot) kernel::W32(screen + unlimiter::kScreenPreviousMenu, unlimiter::kMenuAttachments);
}

X86_EXTERN_FUNC(__imp__sub_002AB330);
X86_FUNC(sub_002AB330) {
    uint32_t screen = ctx.ecx;
    bool active = unlimiter::Active() && unlimiter::Ready();
    if (active) unlimiter::BeginOptions(screen);
    __imp__sub_002AB330(ctx, base);
    if (active) unlimiter::EndOptions(screen);
}

X86_EXTERN_FUNC(__imp__sub_002AB700);
X86_FUNC(sub_002AB700) {
    uint32_t screen = ctx.ecx;
    bool active = unlimiter::Active() && unlimiter::Ready();
    if (active) unlimiter::BeginOptions(screen);
    __imp__sub_002AB700(ctx, base);
    if (active) unlimiter::EndOptions(screen);
}

X86_EXTERN_FUNC(__imp__sub_002ABC20);
X86_FUNC(sub_002ABC20) {
    uint32_t screen = ctx.ecx;
    bool active = unlimiter::Active() && unlimiter::Ready();
    unlimiter::g_vinyl_setup = active;
    if (active) unlimiter::BeginOptions(screen);
    __imp__sub_002ABC20(ctx, base);
    unlimiter::g_vinyl_setup = false;
    if (!active) return;
    unlimiter::EndOptions(screen);
    uint32_t previous = kernel::R32(screen + unlimiter::kScreenPreviousMenu);
    bool stock = previous >= unlimiter::kMenuVinylGroupFirst && previous <= unlimiter::kMenuVinylGroupLast;
    bool extra = previous >= unlimiter::kMenuVinylExtra && previous < unlimiter::kMenuVinylExtra + unlimiter::kMaxVinylGroups;
    if (stock || extra || previous == unlimiter::kVinylCustomMenu)
        kernel::W32(screen + unlimiter::kScreenPreviousMenu, unlimiter::kMenuCategoryVisual);
}

X86_EXTERN_FUNC(__imp__sub_00245070);
X86_FUNC(sub_00245070) {
    uint32_t slot = kernel::R32(ctx.esp + 4), type = kernel::R32(ctx.esp + 8);
    __imp__sub_00245070(ctx, base);
    if (slot != unlimiter::kSlotVinylLayer0 || ctx.eax != unlimiter::kPartNamePair || !unlimiter::Active() ||
        !unlimiter::Ready() || type >= unlimiter::CarTypeCount() || !unlimiter::Config(type).visual.vinyls_custom)
        return;
    uint32_t own = unlimiter::BinHash(unlimiter::CarTypeName(type));
    if (kernel::R32(ctx.eax) == own || kernel::R32(ctx.eax + 4) == own) return;
    if (!kernel::R32(ctx.eax + 4)) kernel::W32(ctx.eax + 4, own);
}

X86_EXTERN_FUNC(__imp__sub_00297690);
X86_FUNC(sub_00297690) {
    uint32_t slot = kernel::R32(ctx.esp + 4);
    __imp__sub_00297690(ctx, base);
    if (!unlimiter::Active() || slot >= unlimiter::kSlotCount) return;
    if (uint32_t wheels = unlimiter::WheelCartTitle(slot)) ctx.eax = wheels;
    if (ctx.eax) return;
    if (uint32_t title = unlimiter::g.slot_title[slot]) ctx.eax = title;
}

void UnlimiterPartsSlot(X86Context& ctx, uint8_t*) {
    if (!unlimiter::Active()) return;
    uint32_t menu = kernel::R32(ctx.edi + unlimiter::kScreenMenu);
    if (uint32_t decal = unlimiter::DecalTextureSlot(menu)) {
        ctx.esi = decal;
        return;
    }
    if (const unlimiter::VinylShown* vinyl = unlimiter::ShownVinyl(menu)) {
        if (menu == unlimiter::kVinylCustomMenu || menu >= unlimiter::kMenuVinylExtra) {
            ctx.esi = unlimiter::kSlotVinylLayer0;
            ctx.ebx = vinyl->icon;
            kernel::W32(ctx.edi + unlimiter::kPartsTitle, vinyl->title);
            kernel::W32(ctx.ebp - unlimiter::kGroupLocal, uint32_t(vinyl->index));
            *static_cast<uint8_t*>(kernel::HostPtr(ctx.ebp - unlimiter::kVinylFlagLocal)) = 1;
        }
        return;
    }
    uint32_t index = menu - unlimiter::kOwnMenuFirst;
    if (index >= unlimiter::kOwnMenus || !unlimiter::g.set[index]) return;
    const unlimiter::Chosen& c = unlimiter::g.by_menu[index];
    ctx.esi = c.slot;
    ctx.ebx = c.icon;
    kernel::W32(ctx.edi + unlimiter::kPartsTitle, c.title);
    if (c.group >= 0) {
        kernel::W32(ctx.ebp - unlimiter::kGroupLocal, uint32_t(c.group));
        *static_cast<uint8_t*>(kernel::HostPtr(ctx.ebp - unlimiter::kVinylFlagLocal)) = 1;
    }
}

X86_EXTERN_FUNC(__imp__sub_00297C60);
X86_FUNC(sub_00297C60) {
    if (unlimiter::g_vinyl_setup) {
        int position = unlimiter::VinylOption(int(kernel::R32(ctx.esp + 4)));
        if (position > 0) {
            ctx.eax = uint32_t(position);
            ctx.esp += 8;
            return;
        }
    }
    __imp__sub_00297C60(ctx, base);
}

// the decal slot list. Doors add six slots themselves; NUM_DECALS on the place's part keeps only that many, and
// slots 7 and 8 are ours. A window or a quarter with more than one gets its list here too
X86_EXTERN_FUNC(__imp__sub_002AC0A0);
X86_FUNC(sub_002AC0A0) {
    uint32_t screen = ctx.ecx;
    bool slots = unlimiter::Active() && unlimiter::Ready() && unlimiter::SetupDecalSlots(ctx, screen);
    __imp__sub_002AC0A0(ctx, base);
    if (slots) unlimiter::AddExtraDecalSlots(ctx, screen);
}

// a new decal screen is built like slot 6, so its back target is the place, then its own menu is put back
X86_EXTERN_FUNC(__imp__sub_002AC380);
X86_FUNC(sub_002AC380) {
    uint32_t arg = kernel::R32(ctx.esp + 4);
    uint32_t menu = arg ? kernel::R16(arg + 8) : 0;
    bool extra = menu >= unlimiter::kMenuDecalSlotFirst + 6 && menu < unlimiter::kMenuDecalSlotFirst + unlimiter::kMenuDecalSlots;
    if (extra) kernel::W16(arg + 8, uint16_t(unlimiter::kMenuDecalSlotFirst + 5));
    __imp__sub_002AC380(ctx, base);
    if (!extra) return;
    kernel::W16(arg + 8, uint16_t(menu));
    if (ctx.eax) kernel::W32(ctx.eax + unlimiter::kScreenMenu, menu);
}

// the decal slot index. Slots 1 to 6 return 0x6B to 0x70; 7 and 8 follow them
X86_EXTERN_FUNC(__imp__sub_002985B0);
X86_FUNC(sub_002985B0) {
    uint32_t screen = ctx.ecx;
    __imp__sub_002985B0(ctx, base);
    if (!unlimiter::Active()) return;
    uint32_t menu = kernel::R32(screen + unlimiter::kScreenMenu);
    if (menu == unlimiter::kMenuDecalSlotFirst + 6) ctx.eax = 0x71;
    if (menu == unlimiter::kMenuDecalSlotFirst + 7) ctx.eax = 0x72;
}
