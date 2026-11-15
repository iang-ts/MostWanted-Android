#include <atomic>
#include <vector>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"

// The settings' [Fixes] and [Debug] switches that stand on their own, away from the modules that own a subject.

namespace unlimiter {

// The image's zone-to-slot map names no DAMAGE_COP_SPOILER, so a cop's spoiler is in no zone and never comes off.
// The PC mod hands the ctor a table of its own instead; this builds the same one from the image's, with the spoiler
// first in the three rear zones and third on top, which is where the mod puts it
uint32_t CopSpoilerZoneMap() {
    static uint32_t table = [] {
        std::vector<uint32_t> pairs;
        uint32_t zone = kZoneMapEnd, run = 0;
        for (uint32_t at = kSlotZoneMap;; at += 8) {
            uint32_t z = kernel::R32(at), slot = kernel::R32(at + 4);
            if (z != zone) {
                zone = z;
                run = 0;
            }
            bool rear = z == kZoneRear || z == kZoneLeftRear || z == kZoneRightRear;
            if ((run == 0 && rear) || (run == 2 && z == kZoneTop)) {
                pairs.push_back(z);
                pairs.push_back(kDamageCopSpoiler);
            }
            pairs.push_back(z);
            pairs.push_back(slot);
            if (z == kZoneMapEnd) break;
            run++;
        }
        uint32_t guest = mem::AllocSystem(uint32_t(pairs.size() * 4));
        for (size_t i = 0; i < pairs.size(); i++) kernel::W32(guest + uint32_t(i) * 4, pairs[i]);
        LOGR("unlimiter: cop spoiler damage, %zu zone slots", pairs.size() / 2 - 1);
        return guest;
    }();
    return table;
}

// TestCareerCustomization lets the career's prices and heat be tried outside a career. The byte is the game's own
// g_bTestCareerCustomization, which UpdateHeatOnVehicle already reads
void ApplyFixes() {
    if (!Active()) return;
    if (Global().test_career_customization) kernel::G<uint8_t>(kTestCareerCustomization) = 1;
}

}  // namespace unlimiter

// eModel::ReplaceLightMaterial. The image already steps around a model with no solid, but not around no model at all:
// it reads the solid off the pointer first. An add-on car whose model never loaded takes that path
X86_EXTERN_FUNC(__imp__sub_000C3DA0);
X86_FUNC(sub_000C3DA0) {
    constexpr uint32_t kModelSolid = 0x0C;
    if (unlimiter::Active() && unlimiter::Global().light_material_crash_fix &&
        (!ctx.ecx || !mem::IsCommitted(ctx.ecx + kModelSolid))) {
        UNLIMITER_NOTE(4, "unlimiter: light material on no model");
        ctx.esp += 4 + 8;  // thiscall: the name hash and the material
        return;
    }
    __imp__sub_000C3DA0(ctx, base);
}

// VehiclePartDamageBehaviour::VehiclePartDamageBehaviour, between the push of the zone map and the push of the zone
// index. Swapping the pushed pointer is what the PC mod does by rewriting the instruction's operand
void UnlimiterCopSpoilerDamage(X86Context& ctx, uint8_t*) {
    if (!unlimiter::Active() || !unlimiter::Global().cop_spoiler_damage_fix) return;
    kernel::W32(ctx.esp, unlimiter::CopSpoilerZoneMap());
}
