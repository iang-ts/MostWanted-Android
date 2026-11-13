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

void AddParts(X86Context& ctx, uint32_t screen) {
    uint32_t type = CurrentCarType(ctx);
    if (type == 0xFFFFFFFF) return;
    Reset(type);
    const CarConfig& config = Config(type);
    for (const Category& c : PartsCategories())
        if (Wanted(config, c.slot)) AddCategory(ctx, screen, type, c);
    static const char* const kAttachment[kLanguageCount] = {"ATTACHMENT %d", "ACCESSOIRE %d", "ANBAUTEIL %d", "ACCESSORIO %d",
                                                            "ACCESORIO %d", "Acess\xF3rio %d", "AKSESORI %d"};
    for (int i = 0; i < config.parts.attachments; i++) {
        uint32_t slot = kSlotAttachment0 + uint32_t(i);
        if (!HasParts(ctx, type, slot)) continue;
        char key[32], name[32], text[48];
        std::snprintf(key, sizeof key, "PartsAttachment%d", i);
        std::snprintf(name, sizeof name, "CO_ATTACHMENT_%d", i + 1);
        std::snprintf(text, sizeof text, kAttachment[LanguageIndex(ctx)], i + 1);
        uint32_t icon = IconOf(ctx, type, key, BinHash("VISUAL_PART_ATTACHMENT"));
        if (!TextureExists(ctx, icon)) icon = kFallbackIcon;
        uint32_t title = CarTextHash(type, "Names", key, name);
        Label(ctx, title, text, title == BinHash(name));
        Add(ctx, screen, 0x10D + uint32_t(i), slot, icon, title);
    }
}

void AddVinylGroups(X86Context& ctx, uint32_t screen) {
    uint32_t type = CurrentCarType(ctx);
    if (type == 0xFFFFFFFF || !Config(type).visual.vinyls_custom) return;
    bool found = HasVinylGroup(ctx, type, kVinylCustomGroup);
    LOGR("unlimiter: custom vinyls of %s %s", CarTypeName(type).c_str(), found ? "found" : "not found");
    if (!found) return;
    const Ini& groups = Data("_VinylGroups.ini");
    uint32_t icon = IconOf(ctx, type, "VisualVinylsCustom", groups.Hash("Group0", "Texture", kVinylIcon));
    if (!TextureExists(ctx, icon)) icon = kVinylIcon;
    uint32_t title = groups.Hash("Group0", "String", BinHash("CO_VINYL_CUSTOM"));
    if (title == 0xFFFFFFFF) title = BinHash("CO_VINYL_CUSTOM");
    title = NameOf(ctx, type, "VisualVinylsCustom", title);
    static const char* const kText[kLanguageCount] = {"CUSTOM",         "PERSONNALISES",  "EIGENE", "PERSONALIZZATI",
                                                      "PERSONALIZADOS", "Personalizados", "KUSTOM"};
    Label(ctx, title, kText[LanguageIndex(ctx)]);
    Add(ctx, screen, kVinylCustomMenu, kSlotVinylLayer0, icon, title);
    g.by_menu[kVinylCustomMenu - kOwnMenuFirst].group = int(kVinylCustomGroup);
}

void AddVisual(X86Context& ctx, uint32_t screen) {
    uint32_t type = CurrentCarType(ctx);
    if (type == 0xFFFFFFFF) return;
    Reset(type);
    const CarConfig& config = Config(type);
    for (const Category& c : VisualCategories())
        if (Wanted(config, c.slot)) AddCategory(ctx, screen, type, c);
}

struct Stock {
    bool shown;
    const char* key;
};

Stock StockMenu(const CarConfig& c, uint32_t menu, uint32_t icon) {
    if (OwnRimBrands() && menu >= kMenuRimBrandFirst && menu <= kMenuRimBrandLast) return {false, nullptr};
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
    if (active) unlimiter::BeginOptions(screen);
    __imp__sub_002ABC20(ctx, base);
    if (!active) return;
    unlimiter::EndOptions(screen);
    uint32_t previous = kernel::R32(screen + unlimiter::kScreenPreviousMenu);
    bool stock = previous >= unlimiter::kMenuVinylGroupFirst && previous <= unlimiter::kMenuVinylGroupLast;
    if (stock || previous == unlimiter::kVinylCustomMenu)
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
