#include <cstring>

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

void KitWheels(uint32_t info) {
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

}  // namespace unlimiter

X86_EXTERN_FUNC(__imp__sub_002549B0);
X86_FUNC(sub_002549B0) {
    uint32_t info = ctx.ecx;
    __imp__sub_002549B0(ctx, base);
    if (unlimiter::Active() && unlimiter::Ready() && !unlimiter::Global().disable_kit_wheel_modifications)
        unlimiter::KitWheels(info);
}
