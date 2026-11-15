#include <atomic>
#include <cstring>

#include "core/log.h"

#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"
#include "unlimiter/parts.h"

namespace unlimiter {

namespace {

constexpr uint32_t kWheels = 4;

float RF(uint32_t va) {
    uint32_t raw = kernel::R32(va);
    float v;
    std::memcpy(&v, &raw, 4);
    return v;
}

void WF(uint32_t va, float v) {
    uint32_t raw;
    std::memcpy(&raw, &v, 4);
    kernel::W32(va, raw);
}

void ApplyKitWheels(uint32_t info) {
    uint32_t ride = kernel::R32(info + kRenderRide);
    if (!ride || !kernel::R32(info + kRenderCarType)) return;
    static const uint32_t kFrontOffset = BinHash("FRONT_TIRE_OFFSET"), kRearOffset = BinHash("REAR_TIRE_OFFSET");
    static const uint32_t kFrontWidth = BinHash("FRONT_TIRE_WIDTH"), kRearWidth = BinHash("REAR_TIRE_WIDTH");
    static const uint32_t kFrontRadius = BinHash("FRONT_TIRE_RADIUS"), kRearRadius = BinHash("REAR_TIRE_RADIUS");
    float extra[2] = {0, 0};
    auto add = [&](uint32_t slot) {
        uint32_t part = PartAt(ride, slot);
        if (!part) return;
        extra[0] += PartFloat(part, kFrontOffset, 0);
        extra[1] += PartFloat(part, kRearOffset, 0);
    };
    add(kSlotDamageBody);
    add(kSlotBody);
    for (uint32_t i = 0; i < kSlotAttachments; i++) add(kSlotAttachment0 + i);
    uint32_t body = PartAt(ride, kSlotBody);
    for (uint32_t w = 0; w < kWheels; w++) {
        uint32_t rear = w >= 2 ? 1 : 0;
        uint32_t at = info + kRenderWheelOffset + w * 4;
        float offset = RF(at), add_on = extra[rear];
        if (add_on != 0) WF(at, offset + (offset <= 0 ? -add_on : add_on));
        if (!body) continue;
        float base_width = RF(info + kRenderWheelWidth + rear * 4), base_radius = RF(info + kRenderWheelRadius + rear * 4);
        float width = PartFloat(body, rear ? kRearWidth : kFrontWidth, 0);
        float radius = PartFloat(body, rear ? kRearRadius : kFrontRadius, 0);
        if (width > 0 && base_width > 0) WF(info + kRenderWheelWidthScale + w * 4, width / base_width);
        if (radius > 0 && base_radius > 0) WF(info + kRenderWheelRadiusScale + w * 4, radius / base_radius);
    }
}

}  // namespace

bool KitWheelsOn() { return Active() && Ready() && !Global().disable_kit_wheel_modifications; }

// The body kit's camber, as the PC's GetCamber. The ecar's own value, already on the FPU stack where the image left
// it, is the fallback
float KitCamber(uint32_t info, bool rear, float original) {
    static const uint32_t kFront = BinHash("FRONT_CAMBER"), kRear = BinHash("REAR_CAMBER");
    uint32_t ride = kernel::R32(info + kRenderRide);
    uint32_t body = ride ? PartAt(ride, kSlotBody) : 0;
    return body ? PartFloat(body, rear ? kRear : kFront, original) : original;
}

// st0 holds what the image just loaded off the ecar, which is also the fallback. The note only fires for a kit that
// really names the attribute, so a stock car leaves no trace
void ApplyKitCamber(X86Context& ctx, bool rear) {
    if (!KitWheelsOn()) return;
    float original = float(X87_ST(0)), kit = KitCamber(ctx.edi, rear, original);
    X87_ST(0) = kit;
    if (kit != original)
        UNLIMITER_NOTE(4, "unlimiter: kit camber %s %.3f over %.3f", rear ? "rear" : "front", kit, original);
}

}  // namespace unlimiter

X86_EXTERN_FUNC(__imp__sub_002549B0);
X86_FUNC(sub_002549B0) {
    uint32_t info = ctx.ecx;
    __imp__sub_002549B0(ctx, base);
    if (unlimiter::KitWheelsOn()) unlimiter::ApplyKitWheels(info);
}

// CarRenderInfo::Render, right after the ecar's own camber is loaded. The PC mod swaps the load itself for a call
// that leaves its value on the FPU stack; here the image's value is already in st0, so the hook overwrites it
void UnlimiterCamberFront(X86Context& ctx, uint8_t*) { unlimiter::ApplyKitCamber(ctx, false); }

void UnlimiterCamberRear(X86Context& ctx, uint8_t*) { unlimiter::ApplyKitCamber(ctx, true); }

// the test that keeps the camber block off every model past the second. The PC mod nops the jump so a kit's camber
// reaches the LODs too, and the block leaves the stack at the same depth either way
bool UnlimiterCamberLod(X86Context&, uint8_t*) { return unlimiter::KitWheelsOn(); }
