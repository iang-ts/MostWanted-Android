#include <atomic>
#include <cstring>

#include "core/log.h"

#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"
#include "unlimiter/parts.h"

namespace unlimiter {

namespace {

constexpr uint32_t kWheels = 4;

// CarRenderConn, as the PC's TireState.h reads it: the ecar's attributes at +0x1C (_CarRenderConn[7]) and the
// CarRenderInfo at +0x44 ([17], which the mod's own RideHeightCave takes the same way). The wheel records
// UpdateTires (0x24E820) walks start at +0x138 and step 0x40 (0x24EAB4, 0x24F0E0); each one's world matrix is the
// 0x40 bytes at its -0x08
constexpr uint32_t kConnAttributes = 0x1C, kConnRenderInfo = 0x44;
constexpr uint32_t kConnWheels = 0x138, kConnWheelStride = 0x40, kMatrixBytes = 0x40, kMatrixPosY = 0x34;

// Attrib_Gen_ecar has the PC's layout here too, which the image settles by itself: RenderFrontEndCars loads
// ReflectionOffset from +0xF4 (0x257ADD) and UpdateRenderMatrix RideHeight from +0xFC (0x24F212), both of them
// where the PC's Attrib.h has them. TireSkidWidthKitScale is seven bVector2, front in x and rear in y
constexpr uint32_t kECarKitScale = 0x58, kKitScaleCount = 7;
constexpr uint32_t kRenderECar = 0x17C4;      // the ecar's attributes, the struct itself
constexpr uint32_t kRenderECarInst = 0x17BC;  // its Attrib_Instance, which GetAttributePointer wants

// FrontEndRenderingCar::LookupWheelPosition's own fields and the lookup it makes
constexpr uint32_t kFeRenderInfo = 0x318, kVectorZ = 0x08;
constexpr uint32_t kFeCompressions = 0x8D5BEB72, kGetAttributePointer = 0x490A0;

// the skid mark sits half a tire off the wheel's centre, less the camber allowance the PC mod uses
constexpr float kSkidCamberAdjust = 0.03f;

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

// the body kit of the ride a CarRenderInfo is drawing, which every attribute below is read off
uint32_t KitBody(uint32_t info) {
    uint32_t ride = info ? kernel::R32(info + kRenderRide) : 0;
    return ride ? PartAt(ride, kSlotBody) : 0;
}

// a float of our own in guest memory, for the two places that have to hand the game a pointer rather than a value
uint32_t OwnFloat(float v) {
    static uint32_t at = mem::AllocSystem(4);
    if (at) WF(at, v);
    return at;
}

// the PC's CarRenderInfo::GetTireWidth. The ecar's own TireSkidWidth for the wheel is what the image already has on
// the stack, so it is the fallback; the kit's tire width goes over it and the ecar's per kit scale multiplies it
float SkidWidth(uint32_t info, uint32_t body, uint32_t wheel, float original) {
    static const uint32_t kFront = BinHash("FRONT_TIRE_WIDTH"), kRear = BinHash("REAR_TIRE_WIDTH");
    static const uint32_t kKitNumber = BinHash("KITNUMBER");
    bool rear = wheel > 1;
    float width = PartFloat(body, rear ? kRear : kFront, original);
    uint32_t attributes = kernel::R32(info + kRenderECar);
    int kit = PartInt(body, kKitNumber, 0);
    if (attributes && kit >= 0 && kit < int(kKitScaleCount)) {
        float scale = RF(attributes + kECarKitScale + uint32_t(kit) * 8 + (rear ? 4 : 0));
        if (scale > 0) width *= scale;
    }
    return width;
}

// the vault's FECompressions for a wheel pair, read the way LookupWheelPosition itself reads it. A null answer is
// the image's own default area, which holds zero, so zero is the fallback
float VaultCompression(X86Context& ctx, uint32_t info, uint32_t pair) {
    X86Context saved = ctx;
    ctx.ecx = info + kRenderECarInst;
    uint32_t at = kernel::CallGuest(ctx, kGetAttributePointer, {kFeCompressions, pair});
    ctx = saved;
    return at ? RF(at) : 0.0f;
}

}  // namespace

bool KitWheelsOn() { return Active() && Ready() && !Global().disable_kit_wheel_modifications; }

// one float of the body kit over the value the image has just put on the FPU stack, which stays the fallback. The
// note only fires for a kit that really names the attribute, so a stock car leaves no trace
void KitFloatOnStack(X86Context& ctx, uint32_t info, uint32_t key, const char* what) {
    if (!KitWheelsOn()) return;
    uint32_t body = KitBody(info);
    if (!body) return;
    float original = float(X87_ST(0)), kit = PartFloat(body, key, original);
    if (kit == original) return;
    X87_ST(0) = kit;
    UNLIMITER_NOTE(4, "unlimiter: kit %s %.3f over %.3f", what, kit, original);
}

// Attrib_Gen_ecar_ExtraRearTireOffset (0x240160) has just answered a pointer to the vault's float. The PC mod swaps
// the call for one of its own that answers a float it owns, so the kit's value can be handed over without writing
// the vault's copy, which every car of that type shares
void KitRearTireOffset(X86Context& ctx) {
    if (!KitWheelsOn() || !ctx.eax) return;
    uint32_t body = KitBody(ctx.edi);
    if (!body) return;
    static const uint32_t kKey = BinHash("EXTRA_REAR_TIRE_OFFSET");
    float original = RF(ctx.eax), kit = PartFloat(body, kKey, original);
    if (kit == original) return;
    uint32_t own = OwnFloat(kit);
    if (!own) return;
    ctx.eax = own;
    UNLIMITER_NOTE(4, "unlimiter: kit rear tire offset %.3f over %.3f", kit, original);
}

// TireState::DoSkids (0x24D6C0, thiscall: the intensity, the delta, the tire's matrix, the car's and the skid
// width), about to be called from UpdateTires. edi is the CarRenderConn and esi walks its wheel records, so the
// wheel is (esi - 0x138) / 0x40. The width the image pushes is the ecar's TireSkidWidth for that wheel; the matrix
// it pushes is the live record, so the call is handed a copy with the mark moved out to the tire's edge instead,
// as the PC's TireState_DoSkids_Hook does, and the record itself is left alone
void KitSkid(X86Context& ctx) {
    if (!KitWheelsOn()) return;
    uint32_t conn = ctx.edi, info = kernel::R32(conn + kConnRenderInfo);
    uint32_t body = KitBody(info);
    if (!body || ctx.esi < conn + kConnWheels) return;
    uint32_t wheel = (ctx.esi - conn - kConnWheels) / kConnWheelStride;
    if (wheel >= kWheels) return;
    float original = RF(ctx.esp + 16), width = SkidWidth(info, body, wheel, original);
    if (width <= 0 || width == original) return;
    uint32_t matrix = kernel::R32(ctx.esp + 8);
    static uint32_t own = mem::AllocSystem(kMatrixBytes);
    if (!matrix || !own) return;
    for (uint32_t at = 0; at < kMatrixBytes; at += 4) kernel::W32(own + at, kernel::R32(matrix + at));
    float y = RF(own + kMatrixPosY), offset = width / 2 - kSkidCamberAdjust;
    WF(own + kMatrixPosY, y + (y < 0 ? offset : -offset));
    kernel::W32(ctx.esp + 8, own);
    WF(ctx.esp + 16, width);
    UNLIMITER_NOTE(8, "unlimiter: kit skid wheel %u width %.3f over %.3f", wheel, width, original);
}

// the kit's FECompressions over the vault's, after LookupWheelPosition has already added the vault's to z. The
// Xbox's own copy of the function stops there, with none of the ExtraPitch the PC mod goes on to add
void KitFeCompression(X86Context& ctx, uint32_t car, uint32_t index, uint32_t out) {
    uint32_t info = kernel::R32(car + kFeRenderInfo);
    uint32_t body = KitBody(info);
    if (!body) return;
    static const uint32_t kFront = BinHash("FRONT_FECOMPRESSION"), kRear = BinHash("REAR_FECOMPRESSION");
    uint32_t pair = index >> 1;
    float vault = VaultCompression(ctx, info, pair);
    float kit = PartFloat(body, pair ? kRear : kFront, vault);
    if (kit == vault) return;
    WF(out + kVectorZ, RF(out + kVectorZ) + kit - vault);
    UNLIMITER_NOTE(8, "unlimiter: kit fecompression wheel %u %.3f over %.3f", index, kit, vault);
}

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

// CarRenderConn::UpdateRenderMatrix (0x24F100), where esi is the conn. Each value is the vault's, already on the
// FPU stack, and the kit's goes over it, as the PC's GetExtraPitch and GetRideHeight do from the same two caves
// Both are the ecar's own units, which the multiply right after each load gives away: RIDE_HEIGHT is in inches,
// times 0.0254 at 0x24F21F, and EXTRA_PITCH in degrees, times 0.017453 at 0x24F1E5. An inch of height is 2.5cm and
// reads as nothing on screen, which is worth knowing before judging a kit by eye
void UnlimiterExtraPitch(X86Context& ctx, uint8_t*) {
    static const uint32_t kKey = unlimiter::BinHash("EXTRA_PITCH");
    unlimiter::KitFloatOnStack(ctx, kernel::R32(ctx.esi + unlimiter::kConnRenderInfo), kKey, "extra pitch");
}

void UnlimiterRideHeight(X86Context& ctx, uint8_t*) {
    static const uint32_t kKey = unlimiter::BinHash("RIDE_HEIGHT");
    unlimiter::KitFloatOnStack(ctx, kernel::R32(ctx.esi + unlimiter::kConnRenderInfo), kKey, "ride height");
}

// RenderFrontEndCars (0x2579E0), where edi is the CarRenderInfo itself and the load is off the ecar's attributes
void UnlimiterReflectionOffset(X86Context& ctx, uint8_t*) {
    static const uint32_t kKey = unlimiter::BinHash("REFLECTION_OFFSET");
    unlimiter::KitFloatOnStack(ctx, ctx.edi, kKey, "reflection offset");
}

void UnlimiterRearTireOffset(X86Context& ctx, uint8_t*) { unlimiter::KitRearTireOffset(ctx); }

void UnlimiterSkidWidth(X86Context& ctx, uint8_t*) { unlimiter::KitSkid(ctx); }

// FrontEndRenderingCar::LookupWheelPosition (0x2430C0, thiscall ret 0x08: the wheel, a bVector4 out). It answers
// false for a car with no render info, and then there is nothing of ours to add either
X86_EXTERN_FUNC(__imp__sub_002430C0);
X86_FUNC(sub_002430C0) {
    uint32_t car = ctx.ecx, index = kernel::R32(ctx.esp + 4), out = kernel::R32(ctx.esp + 8);
    __imp__sub_002430C0(ctx, base);
    if (!(ctx.eax & 0xFF) || !out || !unlimiter::KitWheelsOn()) return;
    unlimiter::KitFeCompression(ctx, car, index, out);
}
