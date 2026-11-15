#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"
#include "unlimiter/parts.h"

namespace unlimiter {

namespace {

constexpr uint32_t kNoPrefix = 0;

uint32_t StylePrefix(uint32_t type, uint32_t part, const char* slot_name) {
    static std::unordered_map<uint64_t, uint32_t> cache;
    uint32_t hash = kernel::R32(part + kPartNameHash);
    uint64_t key = uint64_t(type) << 32 | hash;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    std::string car = CarTypeName(type);
    uint32_t found = kNoPrefix;
    if (!car.empty()) {
        uint32_t prefix = BinHash(car + "_");
        if (HashFrom(slot_name, prefix) == hash) found = prefix;
        char style[48];
        for (int i = 0; i <= 99 && found == kNoPrefix; i++) {
            std::snprintf(style, sizeof style, "%s_STYLE%02d_", car.c_str(), i);
            prefix = BinHash(style);
            if (HashFrom(slot_name, prefix) == hash) found = prefix;
        }
    }
    cache[key] = found;
    return found;
}

struct Pairing {
    uint32_t slot;
    const char* name;
    std::vector<std::pair<uint32_t, const char*>> follow;
};

const std::vector<Pairing>& Pairings() {
    static const std::vector<Pairing> list = {
        {kSlotBase, "BASE",
         {{kSlotFrontLeftWindow, "FRONT_LEFT_WINDOW"}, {kSlotFrontRightWindow, "FRONT_RIGHT_WINDOW"},
          {kSlotFrontWindow, "FRONT_WINDOW"}, {kSlotRearLeftWindow, "REAR_LEFT_WINDOW"},
          {kSlotRearRightWindow, "REAR_RIGHT_WINDOW"}, {kSlotRearWindow, "REAR_WINDOW"},
          {kSlotDecalFrontWindow, "DECAL_FRONT_WINDOW_WIDE_MEDIUM"}, {kSlotDecalRearWindow, "DECAL_REAR_WINDOW_WIDE_MEDIUM"}}},
        {kSlotFrontBrake, "FRONT_BRAKE", {{kSlotRearBrake, "REAR_BRAKE"}}},
        {kSlotLeftHeadlight, "LEFT_HEADLIGHT",
         {{kSlotRightHeadlight, "RIGHT_HEADLIGHT"}, {kSlotLeftHeadlightGlass, "LEFT_HEADLIGHT_GLASS"},
          {kSlotRightHeadlightGlass, "RIGHT_HEADLIGHT_GLASS"}}},
        {kSlotLeftBrakelight, "LEFT_BRAKELIGHT",
         {{kSlotRightBrakelight, "RIGHT_BRAKELIGHT"}, {kSlotLeftBrakelightGlass, "LEFT_BRAKELIGHT_GLASS"},
          {kSlotRightBrakelightGlass, "RIGHT_BRAKELIGHT_GLASS"}}},
        {kSlotLeftMirror, "LEFT_SIDE_MIRROR", {{kSlotRightMirror, "RIGHT_SIDE_MIRROR"}}},
    };
    return list;
}

void Pair(X86Context& ctx, uint32_t ride, uint32_t slot, uint32_t part) {
    uint32_t type = kernel::R32(ride + kRideCarType);
    for (const Pairing& p : Pairings()) {
        if (p.slot != slot) continue;
        if (!part) return;
        uint32_t prefix = StylePrefix(type, part, p.name);
        if (prefix == kNoPrefix) return;
        for (const auto& [follow, name] : p.follow) {
            uint32_t paired = FindPart(ctx, type, follow, HashFrom(name, prefix));
            if (paired) PutPart(ride, follow, paired);
        }
        return;
    }
}

// a rim's own rear (<rim>_REAR), as the PC's RideInfo::SetPart for REAR_WHEEL; 0 for none
uint32_t RearOf(X86Context& ctx, uint32_t type, uint32_t part) {
    return part ? FindPart(ctx, type, kSlotRearWheel, HashFrom("_REAR", kernel::R32(part + kPartNameHash))) : 0;
}

// the rear wheels follow the front while they have none of their own, or the front's: an empty REAR_WHEEL draws the
// front's models, its _REAR is put there. A ride written from a record sets the rear after the front
void FollowFront(X86Context& ctx, uint32_t ride, uint32_t before) {
    uint32_t type = kernel::R32(ride + kRideCarType), rear = PartAt(ride, kSlotRearWheel);
    if (rear && rear != before && rear != RearOf(ctx, type, before)) return;
    PutPart(ride, kSlotRearWheel, RearOf(ctx, type, PartAt(ride, kSlotFrontWheel)));
}

// the game's SetPart (0x25D650) leaves REAR_WHEEL as it is, it only answers what is there: the rear is put here, a
// rim's own _REAR for it, and none follows the front. As the game's, the parts are enabled again after (0x2585D0)
constexpr uint32_t kEnableParts = 0x2585D0;

void SetRear(X86Context& ctx, uint32_t ride, uint32_t part, bool update) {
    uint32_t type = kernel::R32(ride + kRideCarType);
    uint32_t rear = part ? RearOf(ctx, type, part) : RearOf(ctx, type, PartAt(ride, kSlotFrontWheel));
    PutPart(ride, kSlotRearWheel, part && !rear ? part : rear);
    if (!update) return;
    X86Context saved = ctx;
    ctx.ecx = ride;
    kernel::CallGuest(ctx, kEnableParts, {});
    ctx = saved;
}

void SetPart(X86Context& ctx, uint32_t ride, uint32_t slot, uint32_t part) {
    X86Context saved = ctx;
    ctx.ecx = ride;
    kernel::CallGuest(ctx, kRideInfoSetPart, {slot, part, 1});
    ctx = saved;
}

void StockParts(X86Context& ctx, uint32_t ride) {
    uint32_t type = kernel::R32(ride + kRideCarType);
    if (!CarInt(type, "StockParts", "CustomStockParts", 0)) return;
    const Ini& car = Car(type);
    auto apply = [&](const std::string& name, uint32_t slot) {
        if (name.empty()) return;
        uint32_t part = FindPart(ctx, type, slot, BinHash(Upper(name)));
        if (part) SetPart(ctx, ride, slot, part);
    };
    std::string rims = car.Text("StockParts", "Rims");
    apply(rims.empty() ? car.Text("StockParts", "RimsFront") : rims, kSlotFrontWheel);
    apply(rims.empty() ? car.Text("StockParts", "RimsRear") : rims, kSlotRearWheel);
    for (const SlotKey& k : SlotKeys()) apply(car.Text("StockParts", k.key), k.slot);
}

// a traffic car's body paint, drawn again until it is not pearl: pearl wants a second texture layer traffic has none
// of. The player and the cops are left as they were, and a car says for itself whether it takes part
void TrafficPaint(X86Context& ctx, uint32_t ride) {
    if (!Global().randomize_traffic) return;
    uint32_t type = kernel::R32(ride + kRideCarType);
    uint32_t record = CarTypeRecord(type);
    if (!record || kernel::R32(record + kCarTypeUsage) != 2) return;
    if (!Config(type).random_parts.in_traffic) return;
    constexpr uint32_t kBrand = 0xEBB03E66, kPearl = 0x03797533;
    X86Context saved = ctx;
    for (int attempt = 0; attempt < 10; attempt++) {
        ctx.ecx = ride;
        kernel::CallGuest(ctx, kRideInfoSetRandomPart, {kSlotBasePaint, 0xFFFFFFFF});
        uint32_t part = PartAt(ride, kSlotBasePaint), brand = 0;
        if (!part || !PartAttribute(part, kBrand, brand) || brand != kPearl) break;
    }
    ctx = saved;
}

}  // namespace

}  // namespace unlimiter

X86_EXTERN_FUNC(__imp__sub_0025D650);
X86_FUNC(sub_0025D650) {
    uint32_t ride = ctx.ecx, slot = kernel::R32(ctx.esp + 4), part = kernel::R32(ctx.esp + 8);
    bool update = kernel::G<uint8_t>(ctx.esp + 12) != 0;
    bool on = unlimiter::Active() && unlimiter::Ready() && slot < unlimiter::kSlotCount;
    uint32_t front_before = on && slot == unlimiter::kSlotFrontWheel ? unlimiter::PartAt(ride, slot) : 0;
    __imp__sub_0025D650(ctx, base);
    if (!on) return;
    uint32_t result = ctx.eax;
    unlimiter::Pair(ctx, ride, slot, part);
    if (slot == unlimiter::kSlotFrontWheel) unlimiter::FollowFront(ctx, ride, front_before);
    if (slot == unlimiter::kSlotRearWheel) unlimiter::SetRear(ctx, ride, part, update);
    ctx.eax = result;
}

X86_EXTERN_FUNC(__imp__sub_0025F980);
X86_FUNC(sub_0025F980) {
    uint32_t ride = ctx.ecx;
    __imp__sub_0025F980(ctx, base);
    if (!unlimiter::Active() || !unlimiter::Ready()) return;
    uint32_t result = ctx.eax;
    unlimiter::StockParts(ctx, ride);
    unlimiter::TrafficPaint(ctx, ride);
    ctx.eax = result;
}
