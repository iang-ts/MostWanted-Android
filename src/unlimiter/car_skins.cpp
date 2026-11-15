#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"

// CarSkinFix and RandomizeTraffic. The vehicle render hands a ride one of the composite skin slots it keeps free in a
// mask; the image walks 1 to 4 for the hero cars and 5 to 12 for the rest, and the mask itself holds 32. CarSkinCount
// opens the second range, which is where traffic and the racers sit, and a car says in its [RandomParts] whether it
// takes a skin at all. The slots want DUMMY_SKIN%d, DUMMY_WHEEL%d and DUMMY_SPINNER%d in the car textures.

namespace unlimiter {

uint32_t CarSkinSlots() {
    int count = Global().car_skin_count;
    if (count < int(kSkinSlots)) return kSkinSlots;
    return count > int(kSkinSlotLimit) ? kSkinSlotLimit : uint32_t(count);
}

bool MoreCarSkins() {
    return Active() && (Global().car_skin_fix || Global().randomize_traffic) && CarSkinSlots() > kSkinSlots;
}

// a traffic car the pack randomizes has to be skinnable for the render to compose a skin for it at all
void MarkSkinnable() {
    if (!Active() || !Global().randomize_traffic) return;
    uint32_t marked = 0;
    for (uint32_t type = 0, n = CarTypeCount(); type < n; type++) {
        if (!Config(type).random_parts.in_traffic) continue;
        uint32_t record = CarTypeRecord(type);
        if (!record || kernel::G<uint8_t>(record + kCarTypeSkinnable)) continue;
        kernel::G<uint8_t>(record + kCarTypeSkinnable) = 1;
        marked++;
    }
    LOGR("unlimiter: %u car types made skinnable, %u skin slots", marked, CarSkinSlots());
}

}  // namespace unlimiter

// the free-slot walk gives up at 12. While CarSkinCount asks for more, and the mask still has room, the loop goes on
bool UnlimiterCarSkinSlot(X86Context& ctx, uint8_t*) {
    if (!unlimiter::MoreCarSkins()) return false;
    return ctx.edx > unlimiter::kSkinSlots && ctx.edx <= unlimiter::CarSkinSlots();
}

// RideInfo::SetCompositeNameHash. The hashes are built for any id; only the skinned flag stops past the second range,
// so the call is the game's and the flag is ours
X86_EXTERN_FUNC(__imp__sub_0024FD00);
X86_FUNC(sub_0024FD00) {
    uint32_t ride = ctx.ecx, id = kernel::R32(ctx.esp + 4);
    __imp__sub_0024FD00(ctx, base);
    if (!unlimiter::MoreCarSkins() || id <= unlimiter::kSkinSlots || id > unlimiter::CarSkinSlots()) return;
    kernel::G<uint8_t>(ride + unlimiter::kRideSkinned) = 2;
}
