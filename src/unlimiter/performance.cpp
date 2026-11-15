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

bool EngineFlavour(X86Context& ctx, uint32_t which) {
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
    if (part == 1 && (package == 2 || package == 4) && EngineFlavour(ctx, kIsRotaryCar)) return "Rotary";
    if (part == 3 && package == 4 && EngineFlavour(ctx, kIsCastrolCar)) return "Castrol";
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
