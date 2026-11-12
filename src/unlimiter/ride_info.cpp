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
    if (slot == kSlotFrontWheel && part && Global().separate_rims) {
        uint32_t rear = FindPart(ctx, type, kSlotRearWheel, HashFrom("_REAR", kernel::R32(part + kPartNameHash)));
        if (rear) PutPart(ride, kSlotRearWheel, rear);
    }
}

struct StockKey {
    const char* key;
    uint32_t slot;
};

std::vector<StockKey> StockKeys() {
    std::vector<StockKey> out = {
        {"BodyKits", kSlotBody}, {"Spoilers", kSlotSpoiler}, {"Hoods", kSlotHood}, {"RoofScoops", kSlotRoof},
        {"Interior", kSlotInterior}, {"Roof", kSlotBase}, {"Brakes", kSlotFrontBrake}, {"Headlights", kSlotLeftHeadlight},
        {"Taillights", kSlotLeftBrakelight}, {"Mirrors", kSlotLeftMirror}, {"Paint", kSlotBasePaint},
        {"Vinyls", kSlotVinylLayer0}, {"RimPaint", kSlotPaintRim}, {"WindowTint", kSlotWindowTint},
        {"CustomGauges", kSlotCustomHud}, {"CustomGaugesBackingColor", kSlotHudBackingColour},
        {"CustomGaugesNeedleColor", kSlotHudNeedleColour}, {"CustomGaugesCharacterColor", kSlotHudCharacterColour},
        {"Driver", kSlotDriver}, {"LicensePlate", kSlotLicensePlate}, {"Tires", kSlotTire}, {"Neon", kSlotNeon},
        {"NumbersLeftL", kSlotDecalTextureFirst + 2 * kSlotDecalTexturesPerPlace + 6},
        {"NumbersLeftR", kSlotDecalTextureFirst + 2 * kSlotDecalTexturesPerPlace + 7},
        {"NumbersRightL", kSlotDecalTextureFirst + 3 * kSlotDecalTexturesPerPlace + 6},
        {"NumbersRightR", kSlotDecalTextureFirst + 3 * kSlotDecalTexturesPerPlace + 7},
    };
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        char key[32];
        for (int i = 0; i < 10; i++) {
            std::snprintf(key, sizeof key, "Attachment%d", i);
            n.push_back(key);
        }
        for (int i = 1; i <= 4; i++) {
            std::snprintf(key, sizeof key, "VinylColor%d", i);
            n.push_back(key);
        }
        static const char* const kPlaces[6] = {"DecalsWindshield", "DecalsRearWindow", "DecalsLeftDoor",
                                               "DecalsRightDoor", "DecalsLeftQuarter", "DecalsRightQuarter"};
        for (int p = 0; p < 6; p++)
            for (int i = 1; i <= 8; i++) {
                std::snprintf(key, sizeof key, "%s%d", kPlaces[p], i);
                n.push_back(key);
            }
        return n;
    }();
    for (uint32_t i = 0; i < 10; i++) out.push_back({names[i].c_str(), kSlotAttachment0 + i});
    for (uint32_t i = 0; i < 4; i++) out.push_back({names[10 + i].c_str(), kSlotVinylColour0 + i});
    for (uint32_t p = 0; p < 6; p++) {
        uint32_t count = p == 2 || p == 3 ? 6 : 8;
        for (uint32_t i = 0; i < count; i++)
            out.push_back({names[14 + p * 8 + i].c_str(), kSlotDecalTextureFirst + p * kSlotDecalTexturesPerPlace + i});
    }
    return out;
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
    static const std::vector<StockKey> keys = StockKeys();
    for (const StockKey& k : keys) apply(car.Text("StockParts", k.key), k.slot);
}

}  // namespace

}  // namespace unlimiter

X86_EXTERN_FUNC(__imp__sub_0025D650);
X86_FUNC(sub_0025D650) {
    uint32_t ride = ctx.ecx, slot = kernel::R32(ctx.esp + 4), part = kernel::R32(ctx.esp + 8);
    __imp__sub_0025D650(ctx, base);
    if (!unlimiter::Active() || !unlimiter::Ready() || slot >= unlimiter::kSlotCount) return;
    uint32_t result = ctx.eax;
    unlimiter::Pair(ctx, ride, slot, part);
    ctx.eax = result;
}

X86_EXTERN_FUNC(__imp__sub_0025F980);
X86_FUNC(sub_0025F980) {
    uint32_t ride = ctx.ecx;
    __imp__sub_0025F980(ctx, base);
    if (!unlimiter::Active() || !unlimiter::Ready()) return;
    uint32_t result = ctx.eax;
    unlimiter::StockParts(ctx, ride);
    ctx.eax = result;
}
