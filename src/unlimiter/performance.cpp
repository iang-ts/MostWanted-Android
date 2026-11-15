#include <atomic>
#include <cstdio>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"
#include "unlimiter/parts.h"

// [Names] and [Icons] for the performance items themselves: Performance<Cat>Level<L>Item<I>, each category's Junkman
// item, and the rotary and castrol engine items. customize.cpp reaches the category level (PerformanceEngine as a
// menu title); without these a pack that renames and reskins every upgrade still shows the disc's own names.
//
// The game keeps no item table. CustomizePerformance::RefreshHeader (0x2A2CE0) works the three detail lines out on
// every refresh through the two lookups below, building the names from PD_<CAT>_%d_%d with FEngHashString, so
// wrapping the lookups is enough and the PC mod's whole RefreshHeader and Setup are not needed. Both wrappers run
// the game's own function first and keep its answer as the fallback, which is also what balances the stack.

namespace unlimiter {

namespace {

// the ini's name for each of the game's part types. The game's own SUSPENSION is the ini's Chassis, and type 5 is
// Turbo or Supercharger, which the lookups hand us as a flag of its own
constexpr const char* kCategories[kPerfPartTypes] = {
    "Tires", "Brakes", "Chassis", "Transmission", "Engine", nullptr, "Nitrous",
};

const char* CategoryKey(uint32_t part_type, bool turbo) {
    if (part_type >= kPerfPartTypes) return nullptr;
    if (part_type == kPerfInduction) return turbo ? "Turbo" : "Supercharger";
    return kCategories[part_type];
}

// IsTurbo, IsRotaryCar and IsCastrolCar are all thiscalls on the manager with no argument, answering in al
bool ManagerFlag(X86Context& ctx, uint32_t which) {
    X86Context saved = ctx;
    ctx.ecx = kCustomizeManager;
    uint32_t on = kernel::CallGuest(ctx, which, {});
    ctx = saved;
    return (on & 0xFF) != 0;
}

// the three engine items the mod names apart, as the game does itself (PD_ENGINE_%d_%d_ROTARY and _CASTROL): item 1
// of packages 2 and 4 on a rotary car, item 3 of package 4 on a castrol one
const char* EngineSuffix(X86Context& ctx, uint32_t part_type, uint32_t package, uint32_t part) {
    if (part_type != kPerfEngine) return "";
    if (part == 1 && (package == 2 || package == 4) && ManagerFlag(ctx, kIsRotaryCar)) return "Rotary";
    if (part == 3 && package == 4 && ManagerFlag(ctx, kIsCastrolCar)) return "Castrol";
    return "";
}

// the key for one item, the part 1 based as the name lookup takes it. Package 0 is the junkman one, which has a key
// of its own rather than a level
bool ItemKey(char* out, size_t size, X86Context& ctx, uint32_t part_type, uint32_t package, uint32_t part, bool turbo) {
    const char* category = CategoryKey(part_type, turbo);
    if (!category) return false;
    // the junkman item carries no level, and the game asks for it with no part either
    if (package == 0) {
        std::snprintf(out, size, "Performance%sJunkmanItem", category);
        return true;
    }
    if (package > 9 || part == 0 || part > 9) return false;
    std::snprintf(out, size, "Performance%sLevel%uItem%u%s", category, package, part,
                  EngineSuffix(ctx, part_type, package, part));
    return true;
}

uint32_t ShopCar(X86Context& ctx) {
    if (!Active() || !Ready()) return 0xFFFFFFFF;
    uint32_t type = CurrentCarType(ctx);
    return type < CarTypeCount() ? type : 0xFFFFFFFF;
}

// SelectablePart, the node the shop's buttons and the cart both carry: its level at +0x14, its part type at +0x18 and
// the flag that tells a performance package from a visual part at +0x1C
constexpr uint32_t kSelectedLevel = 0x14, kSelectedType = 0x18, kSelectedIsPerf = 0x1C;

// the ini's name for each tier, PN_LEVEL_0 to PN_LEVEL_6 and PN_LEVEL_JUNKMAN
constexpr const char* kTiers[kPerfTierCount] = {
    "Stock", "Street", "SuperStreet", "Race", "Pro", "SuperPro", "Ultimate", "Junkman",
};

bool TierKey(char* out, size_t size, X86Context& ctx, uint32_t part_type, uint32_t tier) {
    if (tier >= kPerfTierCount) return false;
    const char* category = CategoryKey(part_type, part_type == kPerfInduction && ManagerFlag(ctx, kIsTurbo));
    if (!category) return false;
    std::snprintf(out, size, "Performance%s%s", category, kTiers[tier]);
    return true;
}

uint32_t TierLabel(X86Context& ctx, bool icon, uint32_t part_type, uint32_t tier, uint32_t fallback) {
    uint32_t type = ShopCar(ctx);
    if (type == 0xFFFFFFFF) return fallback;
    char key[64];
    if (!TierKey(key, sizeof key, ctx, part_type, tier)) return fallback;
    uint32_t hash = icon ? IconOf(ctx, type, key, fallback) : NameOf(ctx, type, key, fallback);
    UNLIMITER_NOTE(16, "unlimiter: perf tier %s %s %08X%s", icon ? "icon" : "name", key, hash,
                   hash == fallback ? " (game's)" : "");
    return hash;
}

// the tier a button stands for. A category has fewer packages than the seven tiers and its own are the last of them,
// so the raw level has to be counted back from the category's top, exactly as the game does for the cart's name
uint32_t TierOf(X86Context& ctx, uint32_t part_type, uint32_t level) {
    if (level == kPerfJunkmanTier) return kPerfJunkmanTier;
    X86Context saved = ctx;
    uint32_t top = kernel::CallGuest(ctx, kUpgradesMaxLevel, {kPhysicsUpgrades, part_type});
    ctx = saved;
    uint32_t tier = level - top + 6;
    // stock, and any level the category does not reach, as the game's own tier names answer PN_LEVEL_0 for them
    return tier < kPerfJunkmanTier ? tier : 0;
}

}  // namespace

// the part is 1 based for the name and 0 based for the icon, as the PC mod's own calls have it
uint32_t ItemLabel(X86Context& ctx, bool icon, uint32_t part_type, uint32_t package, uint32_t part, bool turbo,
                   uint32_t fallback) {
    uint32_t type = ShopCar(ctx);
    if (type == 0xFFFFFFFF) return fallback;
    char key[64];
    if (!ItemKey(key, sizeof key, ctx, part_type, package, part, turbo)) return fallback;
    uint32_t hash = icon ? IconOf(ctx, type, key, fallback) : NameOf(ctx, type, key, fallback);
    UNLIMITER_NOTE(12, "unlimiter: perf %s %s %08X%s", icon ? "icon" : "name", key, hash,
                   hash == fallback ? " (game's)" : "");
    return hash;
}

// the cart's junkman tier, whose hash the game pushes as an immediate: paint.cpp's GetLocalizedString override is
// where that push can still be reached, so the key has to be reachable from there too
uint32_t PerfTierName(X86Context& ctx, uint32_t part_type, uint32_t tier, uint32_t fallback) {
    return TierLabel(ctx, false, part_type, tier, fallback);
}

}  // namespace unlimiter

// CustomizePerformance::GetPerfPkgDesc (thiscall, ret 0x10): the part type, the package, the part 1 based and whether
// the induction is a turbo. Package 0 asks for the category's junkman item
X86_EXTERN_FUNC(__imp__sub_00298730);
X86_FUNC(sub_00298730) {
    uint32_t part_type = kernel::R32(ctx.esp + 4), package = kernel::R32(ctx.esp + 8);
    uint32_t part = kernel::R32(ctx.esp + 12), turbo = kernel::R32(ctx.esp + 16);
    __imp__sub_00298730(ctx, base);
    ctx.eax = unlimiter::ItemLabel(ctx, false, part_type, package, part, turbo != 0, ctx.eax);
}

// GetPerfPkgBrand (thiscall, ret 0x0C, the PC's 0x7B2BD0): the same item's brand icon, its part 0 based. It takes no
// turbo flag, so the induction's own key is asked of the manager
X86_EXTERN_FUNC(__imp__sub_002A2900);
X86_FUNC(sub_002A2900) {
    uint32_t part_type = kernel::R32(ctx.esp + 4), package = kernel::R32(ctx.esp + 8);
    uint32_t part = kernel::R32(ctx.esp + 12);
    bool turbo = false;
    if (part_type == unlimiter::kPerfInduction) {
        X86Context saved = ctx;
        ctx.ecx = unlimiter::kCustomizeManager;
        turbo = (kernel::CallGuest(ctx, unlimiter::kIsTurbo, {}) & 0xFF) != 0;
        ctx = saved;
    }
    __imp__sub_002A2900(ctx, base);
    ctx.eax = unlimiter::ItemLabel(ctx, true, part_type, package, part + 1, turbo, ctx.eax);
}

// FEShoppingCartItem::GetPerfPkgLevelHash (stdcall, ret 0x04, the PC's 0x7A5EE0): the tier alone, 0 to 6, with no
// word of the category the ini key needs. Its only caller is DrawPartName (0x2A5B10), which keeps the part type in
// ebx from the SelectablePart it read at 0x2A5B34 and does not touch it across the call, so that is where it comes
// from rather than from the screen. The game answers PN_LEVEL_0 for a tier it has not, and so must the key
X86_EXTERN_FUNC(__imp__sub_00297620);
X86_FUNC(sub_00297620) {
    uint32_t tier = kernel::R32(ctx.esp + 4), part_type = ctx.ebx;
    __imp__sub_00297620(ctx, base);
    if (tier >= unlimiter::kPerfJunkmanTier) tier = 0;
    ctx.eax = unlimiter::PerfTierName(ctx, part_type, tier, ctx.eax);
}

// CustomizationScreen::AddPartOption (thiscall, ret 0x18). The performance shop's tier buttons all push the one
// PERFORMANCE_ICON_PACKAGE_1 (0x2A8B3B, 0x2A8C25 and 0x2A8CE1), so the ini's per tier icon goes in here; its six
// other call sites are visual parts, which the SelectablePart's own performance flag tells apart. The part is null
// when the allocation above the call failed, which the game itself goes on to pass
X86_EXTERN_FUNC(__imp__sub_002A1570);
X86_FUNC(sub_002A1570) {
    uint32_t part = kernel::R32(ctx.esp + 4);
    if (part && kernel::G<uint8_t>(part + unlimiter::kSelectedIsPerf)) {
        uint32_t part_type = kernel::R32(part + unlimiter::kSelectedType);
        uint32_t tier = unlimiter::TierOf(ctx, part_type, kernel::R32(part + unlimiter::kSelectedLevel));
        kernel::W32(ctx.esp + 8, unlimiter::TierLabel(ctx, true, part_type, tier, kernel::R32(ctx.esp + 8)));
    }
    __imp__sub_002A1570(ctx, base);
}
