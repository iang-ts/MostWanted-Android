#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"
#include "unlimiter/parts.h"

namespace unlimiter {

namespace {

uint32_t StyledLight(uint32_t ride, uint32_t slot) {
    static const uint32_t kTextureName = BinHash("TEXTURE_NAME");
    uint32_t part = PartAt(ride, slot), name;
    return part && PartAttribute(part, kTextureName, name) && name && name != 0xFFFFFFFF ? part : 0;
}

}  // namespace

}  // namespace unlimiter

void UnlimiterLightParts(X86Context& ctx, uint8_t*) {
    if (!unlimiter::Active() || !unlimiter::Ready() || unlimiter::Global().disable_texture_replacement) return;
    uint32_t ride = ctx.eax;
    if (uint32_t head = unlimiter::StyledLight(ride, unlimiter::kSlotLeftHeadlight)) ctx.ecx = head;
    if (uint32_t brake = unlimiter::StyledLight(ride, unlimiter::kSlotLeftBrakelight)) ctx.edx = brake;
}
