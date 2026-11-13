#include <unordered_map>

#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"

namespace unlimiter {

namespace {

constexpr uint32_t kCustomizeManagerRecord = 0x04;
constexpr uint32_t kEasterEggs = 0x433F08, kIsEasterEggUnlocked = 0x1BE890, kCastrolEgg = 4;
constexpr uint32_t kBestXboxLod = 1;

uint32_t TypeOfHash(uint32_t name_hash) {
    static std::unordered_map<uint32_t, uint32_t> cache;
    auto it = cache.find(name_hash);
    if (it != cache.end()) return it->second;
    uint32_t type = CarTypeOfHash(name_hash);
    if (Ready()) cache[name_hash] = type;
    return type;
}

int EngineType(X86Context& ctx, uint32_t manager) {
    if (!Active() || !Ready()) return -1;
    X86Context saved = ctx;
    ctx.ecx = kernel::R32(manager + kCustomizeManagerRecord);
    uint32_t type = kernel::CallGuest(ctx, kCarRecordGetType, {});
    ctx = saved;
    return Config(type).main.engine_type;
}

}  // namespace

}  // namespace unlimiter

X86_EXTERN_FUNC(__imp__sub_00244F40);
X86_FUNC(sub_00244F40) {
    if (unlimiter::Active() && unlimiter::Ready()) {
        uint32_t lod = kernel::R32(ctx.esp + 12);
        if (lod > unlimiter::kBestXboxLod) {
            uint32_t type = unlimiter::TypeOfHash(kernel::R32(ctx.esp + 4));
            if (type != 0xFFFFFFFF && unlimiter::Config(type).main.force_lod_a)
                kernel::W32(ctx.esp + 12, unlimiter::kBestXboxLod);
        }
    }
    __imp__sub_00244F40(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_00296F60);
X86_FUNC(sub_00296F60) {
    int engine = unlimiter::EngineType(ctx, ctx.ecx);
    if (engine < 0) return __imp__sub_00296F60(ctx, base);
    uint32_t result = 0;
    if (engine == 1) {
        X86Context saved = ctx;
        ctx.ecx = unlimiter::kEasterEggs;
        result = kernel::CallGuest(ctx, unlimiter::kIsEasterEggUnlocked, {unlimiter::kCastrolEgg}) & 0xFF;
        ctx = saved;
    }
    ctx.eax = (ctx.eax & ~0xFFu) | result;
    ctx.esp += 4;
}

X86_EXTERN_FUNC(__imp__sub_00296F80);
X86_FUNC(sub_00296F80) {
    int engine = unlimiter::EngineType(ctx, ctx.ecx);
    if (engine < 0) return __imp__sub_00296F80(ctx, base);
    ctx.eax = (ctx.eax & ~0xFFu) | (engine == 2 ? 1u : 0u);
    ctx.esp += 4;
}

// the last compare of the stock cop types in CarRenderInfo's constructor. An add-on cop is not in that list, so it
// would skip the damage parts. A cop that is not the helicopter or the midsize takes the damage path; the jnz still
// runs when this returns false
bool UnlimiterCopDamage(X86Context& ctx, uint8_t*) {
    if (ctx.zf || !unlimiter::Active() || !unlimiter::Global().addon_cops_damage_fix) return false;
    uint32_t type = ctx.eax;
    if (type == 4 || type == 0x33 || type >= unlimiter::CarTypeCount()) return false;
    return kernel::R32(unlimiter::kCarTypeInfos + type * unlimiter::kCarTypeSize + unlimiter::kCarTypeUsage) == 1;
}
