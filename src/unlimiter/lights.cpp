#include <string>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"
#include "unlimiter/parts.h"

// The lights' textures by their state, as the PC's GetUsedCarTextureInfo and CarRenderInfo_UpdateLightStateTextures.
// The game's (0x243390) has the headlights always on and a brakelight off or on; the Unlimiter's, per car
// ([Textures] in its ini): HeadlightOff (_OFF when they are), DamageLights (_DAMAGE0 when broken), BrakelightOnfInGame
// (_ONF for off in a race), CentreBrake (the centre brakelight of its own, <CAR>_CENTRE_BRAKELIGHT) and Reverse
// (<CAR>_REVERSE on REVERSE_LEFT and REVERSE_RIGHT)

namespace unlimiter {

namespace {

constexpr uint32_t kLightLeftHead = 1, kLightRightHead = 2, kLightLeftBrake = 8, kLightRightBrake = 16;
constexpr uint32_t kLightCentreBrake = 32, kLightReverse = 64 | 128;
constexpr int kOff = 0, kOn = 1, kDamaged = 2;

bool Replacing() { return Active() && Ready() && !Global().disable_texture_replacement; }

uint32_t Attribute(uint32_t part, const char* key, uint32_t fallback) {
    uint32_t value;
    return part && PartAttribute(part, BinHash(key), value) && value && value != 0xFFFFFFFF ? value : fallback;
}

// the brakelights' texture name, as 0x258640 takes it: the styled part's (UnlimiterLightParts), else <CAR>_KIT00_BRAKELIGHT
uint32_t BrakelightTexture(uint32_t ride, const std::string& car) {
    return Attribute(PartAt(ride, kSlotLeftBrakelight), "TEXTURE_NAME", BinHash(car + "_KIT00_BRAKELIGHT"));
}

uint32_t CentreTexture(uint32_t ride, const std::string& car) {
    return Attribute(PartAt(ride, kSlotLeftBrakelight), "TEXTURE_NAME_CENTRE", BinHash(car + "_CENTRE_BRAKELIGHT"));
}

// a texture the car's pack loads (TexturesToLoadPerm, its count at +0x2B8), once; the list has room for 87
void AddPerm(uint32_t info, uint32_t hash) {
    if (!hash) return;
    uint32_t n = kernel::R32(info + kUsedNumPerm);
    for (uint32_t i = 0; i < n && i < kUsedPermCount; i++)
        if (kernel::R32(info + i * 4) == hash) return;
    if (n >= kUsedPermCount) {
        LOGR("unlimiter: no room to load light texture %08x", hash);
        return;
    }
    kernel::W32(info + n * 4, hash);
    kernel::W32(info + kUsedNumPerm, n + 1);
}

void AddStates(uint32_t info, uint32_t texture) {
    for (const char* s : {"_OFF", "_ON", "_DAMAGE0"}) AddPerm(info, HashFrom(s, texture));
}

// TireInnerMask: a rim with no texture of its own (0x2586DA, MappedWheelHash +0x2CC "_WHEEL" on none) has the car's
// <CAR>_TIRE take the wheels' paint, as the PC's GetUsedCarTextureInfo; else nothing is
constexpr uint32_t kUsedMappedWheel = 0x2CC;

void MoreUsedTextures(uint32_t info, uint32_t ride) {
    uint32_t type = kernel::R32(ride + kRideCarType);
    if (type >= CarTypeCount()) return;
    const CarConfig& c = Config(type);
    std::string car = CarTypeName(type);
    if (uint32_t wheel = PartAt(ride, kSlotFrontWheel); wheel && !Attribute(wheel, "TEXTURE_NAME", 0))
        kernel::W32(info + kUsedMappedWheel, c.textures.tire_inner_mask ? BinHash(car + "_TIRE") : 0);
    uint32_t brake = BrakelightTexture(ride, car);
    // the game's name for them, or another way of naming we would get wrong
    if (kernel::R32(info + kUsedBrakelight + 4) != HashFrom("_ON", brake)) return;
    bool front_end = kernel::R32(kGameFlowState) == kGameFlowFrontEnd;
    if (c.textures.brakelight_onf_in_game && !front_end) {
        kernel::W32(info + kUsedBrakelight, HashFrom("_ONF", brake));
        AddPerm(info, HashFrom("_ONF", brake));
    }
    if (c.textures.centre_brake) AddStates(info, CentreTexture(ride, car));
    if (c.textures.reverse) {
        uint32_t reverse = Attribute(PartAt(ride, kSlotLeftBrakelight), "TEXTURE_NAME_REVERSE", BinHash(car + "_REVERSE"));
        kernel::W32(info + kUsedReverse, HashFrom("_OFF", reverse));
        kernel::W32(info + kUsedReverse + 4, HashFrom("_ON", reverse));
        kernel::W32(info + kUsedReverse + 8, HashFrom("_DAMAGE0", reverse));
        AddStates(info, reverse);
    }
}

// a replacement's texture, looked up again at its next draw when it changed
void UpdateEntry(uint32_t table, int index, uint32_t to) {
    uint32_t e = table + uint32_t(index) * kReplacementSize;
    if (kernel::R32(e + 4) == to) return;
    kernel::W32(e + 4, to);
    kernel::W32(e + 8, 0xFFFFFFFF);
}

void WriteEntry(uint32_t table, int index, uint32_t from, uint32_t to) {
    uint32_t e = table + uint32_t(index) * kReplacementSize;
    if (kernel::R32(e + 4) == to) return;
    kernel::W32(e, from);
    kernel::W32(e + 4, to);
    kernel::W32(e + 8, 0xFFFFFFFF);
}

// the body's table and its carbon fibre copy alike
void UpdateBoth(uint32_t info, int index, uint32_t to) {
    UpdateEntry(info + kRenderReplacements, index, to);
    UpdateEntry(info + kRenderCarbonReplacements, index, to);
}

int LightState(uint32_t info, uint32_t light, bool damage_lights) {
    int state = kOff;
    if (kernel::R32(info + kRenderLightsOn) & light) state = kOn;
    if (kernel::R32(info + kRenderLightsBroken) & light) state = damage_lights ? kDamaged : kOff;
    return state;
}

void UpdateLightStates(uint32_t info) {
    uint32_t ride = kernel::R32(info + kRenderRide);
    uint32_t type = ride && mem::IsCommitted(ride) ? kernel::R32(ride + kRideCarType) : 0xFFFFFFFF;
    const CarConfig& c = Config(type);
    uint32_t used = info + kRenderUsedTextures;
    auto hash = [&](uint32_t field, int state) { return kernel::R32(used + field + uint32_t(state) * 4); };
    bool damage = c.textures.damage_lights;

    bool force_head = kernel::R32(kForceHeadlights) != 0, force_brake = kernel::R32(kForceBrakelights) != 0;
    int left = force_head ? kOn : LightState(info, kLightLeftHead, damage);
    int right = force_head ? kOn : LightState(info, kLightRightHead, damage);
    if (!c.textures.headlight_off) {
        if (left == kOff) left = kOn;
        if (right == kOff) right = kOn;
    }
    UpdateBoth(info, 27, hash(kUsedHeadlight, left));
    UpdateBoth(info, 37, hash(kUsedHeadlight, left));
    UpdateBoth(info, 28, hash(kUsedHeadlight, right));
    UpdateBoth(info, 38, hash(kUsedHeadlight, right));
    UpdateBoth(info, 32, hash(kUsedHeadlightGlass, left));
    UpdateBoth(info, 42, hash(kUsedHeadlightGlass, left));
    UpdateBoth(info, 33, hash(kUsedHeadlightGlass, right));
    UpdateBoth(info, 43, hash(kUsedHeadlightGlass, right));

    int brake_left = force_brake ? kOn : LightState(info, kLightLeftBrake, damage);
    int brake_right = force_brake ? kOn : LightState(info, kLightRightBrake, damage);
    int centre = LightState(info, kLightCentreBrake, damage);
    uint32_t centre_texture = hash(kUsedBrakelight, centre);
    if (c.textures.centre_brake && type < CarTypeCount()) {
        static const char* const kStates[] = {"_OFF", "_ON", "_DAMAGE0"};
        centre_texture = HashFrom(kStates[centre], CentreTexture(ride, CarTypeName(type)));
    }
    UpdateBoth(info, 29, hash(kUsedBrakelight, brake_left));
    UpdateBoth(info, 39, hash(kUsedBrakelight, brake_left));
    UpdateBoth(info, 30, hash(kUsedBrakelight, brake_right));
    UpdateBoth(info, 40, hash(kUsedBrakelight, brake_right));
    UpdateBoth(info, 31, centre_texture);
    UpdateBoth(info, 41, centre_texture);
    UpdateBoth(info, 34, hash(kUsedBrakelightGlass, brake_left));
    UpdateBoth(info, 44, hash(kUsedBrakelightGlass, brake_left));
    UpdateBoth(info, 35, hash(kUsedBrakelightGlass, brake_right));
    UpdateBoth(info, 45, hash(kUsedBrakelightGlass, brake_right));
    UpdateBoth(info, 36, hash(kUsedBrakelightGlass, centre));
    UpdateBoth(info, 46, hash(kUsedBrakelightGlass, centre));

    if (c.textures.reverse) {
        int reverse = kernel::R32(kForceReverselights) ? kOn : LightState(info, kLightReverse, damage);
        static const uint32_t kLeft = BinHash("REVERSE_LEFT"), kRight = BinHash("REVERSE_RIGHT");
        for (uint32_t table : {info + kRenderReplacements, info + kRenderCarbonReplacements}) {
            WriteEntry(table, 47, kLeft, hash(kUsedReverse, reverse));
            WriteEntry(table, 48, kRight, hash(kUsedReverse, reverse));
        }
    }
}

// the CarRenderInfo whose flares are being drawn (RenderFlaresOnCar, game.cpp). 0 outside that call
uint32_t g_flare_car = 0;

// a part's colour channels, 0 when the part has none: the draw keeps its own tint
uint32_t Channels(uint32_t part, uint32_t red, uint32_t green, uint32_t blue) {
    if (!part) return 0;
    int r = PartInt(part, red, 0) & 255, g = PartInt(part, green, 0) & 255, b = PartInt(part, blue, 0) & 255;
    if (!r && !g && !b) return 0;
    return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16);
}

// the flare's tint from the light part, as the PC's eRenderLightFlare_Hook. The right side uses the left when it has
// no colour of its own
uint32_t FlareTint(uint32_t ride, uint32_t name) {
    static const uint32_t kRed = BinHash("RED"), kGreen = BinHash("GREEN"), kBlue = BinHash("BLUE");
    static const uint32_t kRed2 = BinHash("RED2"), kGreen2 = BinHash("GREEN2"), kBlue2 = BinHash("BLUE2");
    static const uint32_t kDispRed = BinHash("DISPRED"), kDispGreen = BinHash("DISPGREEN"), kDispBlue = BinHash("DISPBLUE");
    uint32_t left_head = PartAt(ride, kSlotLeftHeadlight), right_head = PartAt(ride, kSlotRightHeadlight);
    uint32_t left_brake = PartAt(ride, kSlotLeftBrakelight), right_brake = PartAt(ride, kSlotRightBrakelight);
    switch (name) {
        case 0xD09091C6: {  // RIGHT_HEADLIGHT
            uint32_t tint = Channels(right_head, kRed, kGreen, kBlue);
            return tint ? tint : Channels(left_head, kRed, kGreen, kBlue);
        }
        case 0x7A5BCF69: return Channels(left_head, kDispRed, kDispGreen, kDispBlue);  // CENTRE_HEADLIGHT
        case 0x9DB90133: return Channels(left_head, kRed, kGreen, kBlue);              // LEFT_HEADLIGHT
        case 0xBF700A79: {  // RIGHT_BRAKELIGHT
            uint32_t tint = Channels(right_brake, kRed, kGreen, kBlue);
            return tint ? tint : Channels(left_brake, kRed, kGreen, kBlue);
        }
        case 0xA2A2FC7C: return Channels(left_brake, kDispRed, kDispGreen, kDispBlue);  // CENTRE_BRAKELIGHT
        case 0x31A66786: return Channels(left_brake, kRed, kGreen, kBlue);              // LEFT_BRAKELIGHT
        case 0x7ADF7EF8: {  // RIGHT_REVERSE
            uint32_t tint = Channels(right_brake, kRed2, kGreen2, kBlue2);
            return tint ? tint : Channels(left_brake, kRed2, kGreen2, kBlue2);
        }
        case 0x7A5B2F25: return Channels(left_brake, kRed2, kGreen2, kBlue2);  // LEFT_REVERSE
        default: return 0;
    }
}

}  // namespace

void FlareCar(uint32_t info) { g_flare_car = info; }

}  // namespace unlimiter

// GetUsedCarTextureInfo (cdecl: the info, the ride, front end only): the lights' states and the tire's inner mask
X86_EXTERN_FUNC(__imp__sub_00258640);
X86_FUNC(sub_00258640) {
    uint32_t info = kernel::R32(ctx.esp + 4), ride = kernel::R32(ctx.esp + 8);
    __imp__sub_00258640(ctx, base);
    if (!unlimiter::Active() || !unlimiter::Ready() || !ride || !mem::IsCommitted(ride)) return;
    uint32_t result = ctx.eax;
    unlimiter::RearWheelTextures(ctx, info, ride);
    if (unlimiter::Replacing()) unlimiter::MoreUsedTextures(info, ride);
    ctx.eax = result;
}

// CarRenderInfo::UpdateLightStateTextures (thiscall, from the render at 0x25578A)
X86_EXTERN_FUNC(__imp__sub_00243390);
X86_FUNC(sub_00243390) {
    if (!unlimiter::Replacing()) {
        __imp__sub_00243390(ctx, base);
        return;
    }
    unlimiter::UpdateLightStates(ctx.ecx);
    kernel::Return(ctx, 0, 0);
}

// eRenderLightFlare (cdecl: view, flare, matrix, intensity, a5, a6, a7, colour override, size). The car's call passes
// a zero override, and a zero there keeps the flare's own colour. A part's tint replaces it. Only while
// RenderFlaresOnCar is drawing this car
X86_EXTERN_FUNC(__imp__sub_000D37F0);
X86_FUNC(sub_000D37F0) {
    uint32_t info = unlimiter::g_flare_car;
    if (info && unlimiter::Active() && unlimiter::Ready() && !unlimiter::Global().disable_light_flare_colors) {
        uint32_t ride = kernel::R32(info + unlimiter::kRenderRide);
        uint32_t flare = kernel::R32(ctx.esp + 8);
        if (ride && flare && mem::IsCommitted(ride) && mem::IsCommitted(flare + 8)) {
            if (uint32_t tint = unlimiter::FlareTint(ride, kernel::R32(flare + 8))) {
                kernel::W32(ctx.esp + 0x20, tint);
                kernel::W32(flare + 0x0C, tint);
            }
        }
    }
    __imp__sub_000D37F0(ctx, base);
}

// UpdateLightFlareParameters writes the two bright cop flares one after the other. The fix swaps which colour is which
void UnlimiterCopBright(X86Context& ctx, uint8_t*) {
    if (!unlimiter::Active() || !unlimiter::Global().cop_bright_lights_order_fix) return;
    uint32_t first = kernel::R32(ctx.ebp - 0x58), second = kernel::R32(ctx.ebp - 0x54);
    kernel::W32(ctx.ebp - 0x58, second);
    kernel::W32(ctx.ebp - 0x54, first);
}
