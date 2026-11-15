#include <cstdio>
#include <random>

#include <atomic>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"
#include "unlimiter/parts.h"

// [RandomParts] over RideInfo::SetRandomParts (0x260F80). The image draws one of three levels and touches a handful of
// slots; the Unlimiter's ladder runs to four and reaches the slots the mod adds, so the whole function is ours. The
// picker stays the game's (SetRandomPart, 0x25F860), which already keeps pearl away from traffic and honours the
// excluded-part table, so only decals and numbers need a draw of our own.

namespace unlimiter {

namespace {

uint32_t Draw(uint32_t range) {
    static std::mt19937 random{std::random_device{}()};
    return range ? random() % range : 0;
}

void Pick(X86Context& ctx, uint32_t ride, uint32_t slot) {
    X86Context saved = ctx;
    ctx.ecx = ride;
    kernel::CallGuest(ctx, kRideInfoSetRandomPart, {slot, 0xFFFFFFFF});
    ctx = saved;
}

void Put(X86Context& ctx, uint32_t ride, uint32_t slot, uint32_t part) {
    X86Context saved = ctx;
    ctx.ecx = ride;
    kernel::CallGuest(ctx, kRideInfoSetPart, {slot, part, 1});
    ctx = saved;
}

void Mirror(X86Context& ctx, uint32_t ride, uint32_t from, uint32_t to) { Put(ctx, ride, to, PartAt(ride, from)); }

uint32_t PartCount(X86Context& ctx, uint32_t type, uint32_t slot) {
    X86Context saved = ctx;
    ctx.ecx = kCarPartDatabase;
    uint32_t count = kernel::CallGuest(ctx, kNewGetNumCarParts, {type, slot, 0, 0xFFFFFFFF});
    ctx = saved;
    return count;
}

// the nth part of a slot, walked the way the game's own picker walks it
uint32_t NthPart(X86Context& ctx, uint32_t type, uint32_t slot, uint32_t n) {
    X86Context saved = ctx;
    uint32_t part = 0;
    for (uint32_t i = 0; i < n; i++) {
        ctx.ecx = kCarPartDatabase;
        part = kernel::CallGuest(ctx, kNewGetCarPart, {type, slot, 0, part, 0xFFFFFFFF});
        if (!part) break;
    }
    ctx = saved;
    return part;
}

uint32_t BrandName(X86Context& ctx, uint32_t part) {
    X86Context saved = ctx;
    ctx.ecx = part;
    uint32_t brand = kernel::CallGuest(ctx, kPartBrandName, {});
    ctx = saved;
    return brand;
}

// a decal slot also holds the numbers, which carry a brand of their own: one drawn here would sit as a plain decal
void RandomDecal(X86Context& ctx, uint32_t ride, uint32_t type, uint32_t slot) {
    uint32_t count = PartCount(ctx, type, slot);
    if (!count) return;
    uint32_t part = NthPart(ctx, type, slot, Draw(count) + 1);
    if (!part) return;
    static const uint32_t kLeft = BinHash("NUMBER_LEFT"), kRight = BinHash("NUMBER_RIGHT");
    uint32_t brand = BrandName(ctx, part);
    if (brand == kLeft || brand == kRight) return;
    Put(ctx, ride, slot, part);
}

uint32_t NumberPart(X86Context& ctx, uint32_t type, uint32_t slot, const char* side) {
    char name[32];
    std::snprintf(name, sizeof name, "%s_%u", side, Draw(10));
    return FindPart(ctx, type, slot, BinHash(name));
}

// the two last left door slots carry the racer's number and the right ones repeat them. A pair that cannot be filled
// leaves neither half behind
void RandomNumbers(X86Context& ctx, uint32_t ride, uint32_t type) {
    constexpr uint32_t kLeftDoor = kSlotDecalTextureFirst + 2 * kSlotDecalTexturesPerPlace;
    constexpr uint32_t kRightDoor = kSlotDecalTextureFirst + 3 * kSlotDecalTexturesPerPlace;
    uint32_t left = NumberPart(ctx, type, kLeftDoor + 6, "NUMBER_LEFT");
    uint32_t right = left ? NumberPart(ctx, type, kLeftDoor + 7, "NUMBER_RIGHT") : 0;
    if (!left || !right) {
        Put(ctx, ride, kLeftDoor + 6, 0);
        Put(ctx, ride, kLeftDoor + 7, 0);
        return;
    }
    Put(ctx, ride, kLeftDoor + 6, left);
    Put(ctx, ride, kLeftDoor + 7, right);
    Mirror(ctx, ride, kLeftDoor + 6, kRightDoor + 6);
    Mirror(ctx, ride, kLeftDoor + 7, kRightDoor + 7);
}

// the places the mod draws on: the windshield and the rear window take eight each, the doors six before the numbers,
// the quarters eight. A right side either repeats the left or is drawn on its own
void RandomDecals(X86Context& ctx, uint32_t ride, uint32_t type, const CarConfig& c) {
    constexpr uint32_t kPerPlace = kSlotDecalTexturesPerPlace;
    for (uint32_t place = 0; place < 2; place++)
        for (uint32_t i = 0; i < kPerPlace; i++)
            RandomDecal(ctx, ride, type, kSlotDecalTextureFirst + place * kPerPlace + i);
    static const uint32_t kSides[2][2] = {{2, 3}, {4, 5}};
    for (const auto& side : kSides) {
        uint32_t left = kSlotDecalTextureFirst + side[0] * kPerPlace;
        uint32_t right = kSlotDecalTextureFirst + side[1] * kPerPlace;
        uint32_t count = side[0] == 2 ? 6 : kPerPlace;
        for (uint32_t i = 0; i < count; i++) RandomDecal(ctx, ride, type, left + i);
        for (uint32_t i = 0; i < count; i++) {
            if (c.random_parts.match_side_decals)
                Mirror(ctx, ride, left + i, right + i);
            else
                RandomDecal(ctx, ride, type, right + i);
        }
    }
}

void RandomGauges(X86Context& ctx, uint32_t ride) {
    Pick(ctx, ride, kSlotCustomHud);
    Pick(ctx, ride, kSlotHudBackingColour);
    Pick(ctx, ride, kSlotHudNeedleColour);
    Pick(ctx, ride, kSlotHudCharacterColour);
}

// the rear rim follows the front unless the draw asks for one of its own
void RandomWheels(X86Context& ctx, uint32_t ride, const CarConfig& c) {
    Pick(ctx, ride, kSlotFrontWheel);
    if (int(Draw(100)) < c.random_parts.rear_rim_chance)
        Pick(ctx, ride, kSlotRearWheel);
    else
        Mirror(ctx, ride, kSlotFrontWheel, kSlotRearWheel);
}

void RandomPaint(X86Context& ctx, uint32_t ride) {
    Pick(ctx, ride, kSlotBasePaint);
    Pick(ctx, ride, kSlotVinylLayer0);
    for (uint32_t i = 0; i < 4; i++) Pick(ctx, ride, kSlotVinylColour0 + i);
}

// the parts the Unlimiter adds, each behind the menu its car opens
void RandomOwnParts(X86Context& ctx, uint32_t ride, const CarConfig& c) {
    if (c.parts.roof_scoops) Pick(ctx, ride, kSlotRoof);
    if (c.parts.interior) Pick(ctx, ride, kSlotInterior);
    if (c.parts.roof) Pick(ctx, ride, kSlotBase);
    if (c.parts.brakes) Pick(ctx, ride, kSlotFrontBrake);
    if (c.parts.headlights) Pick(ctx, ride, kSlotLeftHeadlight);
    if (c.parts.taillights) Pick(ctx, ride, kSlotLeftBrakelight);
    if (c.parts.mirrors) Pick(ctx, ride, kSlotLeftMirror);
    for (int i = 0; i < c.parts.attachments; i++) Pick(ctx, ride, kSlotAttachment0 + uint32_t(i));
    if (c.visual.driver) Pick(ctx, ride, kSlotDriver);
    if (c.visual.license_plate) Pick(ctx, ride, kSlotLicensePlate);
    if (c.visual.tires) Pick(ctx, ride, kSlotTire);
    if (c.visual.neon) Pick(ctx, ride, kSlotNeon);
}

// the ladder the mod draws from: every level carries the ones under it. The level it settled on comes back, -1 when
// the car is left stock
int VanillaLevels(X86Context& ctx, uint32_t ride, uint32_t type, const CarConfig& c) {
    int level = c.random_parts.level;
    if (level == -2) level = int(Draw(5));
    if (level < 0) return -1;
    bool stock_body = Global().force_stock_parts_on_addon_opponents && type >= kStockCarTypes;
    switch (level) {
        case 4:
            RandomDecals(ctx, ride, type, c);
            if (int(Draw(100)) < c.random_parts.numbers_chance) RandomNumbers(ctx, ride, type);
            if (c.visual.custom_gauges) RandomGauges(ctx, ride);
            [[fallthrough]];
        case 3:
            RandomOwnParts(ctx, ride, c);
            [[fallthrough]];
        case 2:
            if (!stock_body) {
                Pick(ctx, ride, kSlotBody);
                Pick(ctx, ride, kSlotSpoiler);
                Pick(ctx, ride, kSlotRoof);
                Pick(ctx, ride, kSlotHood);
            }
            RandomWheels(ctx, ride, c);
            Pick(ctx, ride, kSlotPaintRim);
            Pick(ctx, ride, kSlotWindowTint);
            [[fallthrough]];
        case 1:
            RandomPaint(ctx, ride);
            break;
        default:
            // what the image itself draws at its lowest level
            if (!stock_body) {
                Pick(ctx, ride, kSlotBody);
                Pick(ctx, ride, kSlotSpoiler);
            }
            Pick(ctx, ride, kSlotBasePaint);
            Pick(ctx, ride, kSlotFrontWheel);
            Pick(ctx, ride, kSlotPaintRim);
            break;
    }
    return level;
}

// the slots the car's own [RandomParts] names, drawn after the ladder or in its place
void OwnSlots(X86Context& ctx, uint32_t ride, uint32_t type, const CarConfig& c) {
    const auto& r = c.random_parts;
    constexpr uint32_t kDecals = 6 * kSlotDecalTexturesPerPlace;
    for (const SlotKey& k : SlotKeys()) {
        uint32_t slot = k.slot;
        if (!r.slot[slot] || slot == kSlotFrontWheel || slot == kSlotRearWheel) continue;
        if (slot >= kSlotDecalTextureFirst && slot < kSlotDecalTextureFirst + kDecals)
            RandomDecal(ctx, ride, type, slot);
        else if (slot == kSlotVinylLayer0)
            RandomPaint(ctx, ride);
        else
            Pick(ctx, ride, slot);
    }
    if (!r.slot[kSlotFrontWheel]) return;
    Pick(ctx, ride, kSlotFrontWheel);
    if (r.slot[kSlotRearWheel] && int(Draw(100)) < r.rear_rim_chance)
        Pick(ctx, ride, kSlotRearWheel);
    else
        Mirror(ctx, ride, kSlotFrontWheel, kSlotRearWheel);
}

}  // namespace

void RandomParts(X86Context& ctx, uint32_t ride) {
    uint32_t type = kernel::R32(ride + kRideCarType);
    if (type >= CarTypeCount()) return;
    const CarConfig& c = Config(type);
    int level = c.random_parts.custom != 1 ? VanillaLevels(ctx, ride, type, c) : -1;
    if (c.random_parts.custom) OwnSlots(ctx, ride, type, c);
    UNLIMITER_NOTE(8, "unlimiter: random parts on %s, level %d, custom %d", CarTypeName(type).c_str(), level,
                   c.random_parts.custom);
}

}  // namespace unlimiter

// RideInfo::SetRandomParts. Every slot goes back to stock first, the way the image opens, and the mod's ladder takes
// the place of the three levels that follow
X86_EXTERN_FUNC(__imp__sub_00260F80);
X86_FUNC(sub_00260F80) {
    uint32_t ride = ctx.ecx;
    if (!unlimiter::Active() || !unlimiter::Ready() || !unlimiter::Global().random_parts)
        return __imp__sub_00260F80(ctx, base);
    X86Context saved = ctx;
    ctx.ecx = ride;
    kernel::CallGuest(ctx, unlimiter::kRideInfoSetStockPartsAll, {});
    ctx = saved;
    unlimiter::RandomParts(ctx, ride);
    ctx.esp += 4;  // thiscall with nothing on the stack
}
