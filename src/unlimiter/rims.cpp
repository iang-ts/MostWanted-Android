#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>
#include <string>

#include "core/log.h"
#include "gpu/texture.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"
#include "unlimiter/parts.h"

// The rims as the PC's SeparateRims (CustomizeSub, CustomizeRims, SetStockRimOption, FEShoppingCartItem): picking Rims
// in Parts asks for the front, the rear or all wheels (DialogInterface::ShowThreeButtons 0x147210), and the rim screen
// lists, previews, prices and adds to the cart for those. The Xbox's own code knows only FRONT_WHEEL: where it asks for
// that slot on the rear's behalf, the call is told apart by its return address. CarCustomizeManager::AddToCart
// (0x2A5020) copies the SelectablePart, so its slot is changed for the call and put back. The render already draws the
// rear wheels from REAR_WHEEL's models (CarRenderInfo +0x10D0, FRONT_WHEEL's +0x10BC), the front's when it has none.
// Also NoRimSize (a brand of _RimBrands.ini listing every size), LANGUAGEHASH (a rim's name from the string table) and
// ScaleBrakesWithRims (the brakes sized to the rim, as CarRenderInfo_AdjustBrakeScale)

namespace unlimiter {

namespace {

constexpr uint32_t kManager = 0x4DEC68;
constexpr uint32_t kFEng = 0x48E94C, kQueuePackageSwitch = 0xEB160;
constexpr uint32_t kShowThreeButtons = 0x147210, kEmptyText = 0x3CA680, kDialogCancel = 0xB4EDEB6D;
constexpr uint32_t kFindObject = 0xEA370, kSetInvisible = 0xE00B0, kPrintf = 0xEABE0, kMoneyFormat = 0x3D542C;
constexpr uint32_t kListCount = 0x4E8D0;
constexpr uint32_t kGetUnlockFilter = 0x296690, kGetCarPartCost = 0x13BEC0, kGetInstalledCarPart = 0x296480;
constexpr uint32_t kPreviewPart = 0x29EF70, kBaseNotification = 0x2A69A0, kInBackRoom = 0x12B700;
constexpr uint32_t kCareerFlags = 0x48EDD4, kCustomizeAnywhere = 0x4DEC5D;
constexpr float kTradeInFactor = 0.25f;

constexpr uint32_t kPadAccept = 0xC407210, kPadLeftTrigger = 0x5073EF13, kPadRightTrigger = 0xD9FEEC59;
constexpr uint32_t kInnerRadius = 0xEB0101E2;

// CustomizeSub: the category at +0x14C, the scroller's option at +0x38 (its package +0x4C, its menu +0x50)
constexpr uint32_t kSubCategory = 0x14C, kSubOption = 0x38, kOptionPackage = 0x4C, kOptionMenu = 0x50;
// CustomizeRims: the package's name at +0x10, the brand's menu at +0x144, the rim size at +0x1B8, the list at +0x30
// (empty when its count is +0xE8's), the meters' package at +0x154
constexpr uint32_t kScreenPackage = 0x10, kRimSize = 0x1B8, kRimList = 0x30, kRimListEnd = 0xE8, kMetersPackage = 0x154;
// SelectablePart: the part at +0x0C, its slot at +0x10, its price at +0x24
constexpr uint32_t kSelectedPart = 0x0C, kSelectedSlot = 0x10, kSelectedPrice = 0x24;

// where the Xbox asks for FRONT_WHEEL and means the wheels being customized
constexpr uint32_t kListActivePart = 0x2A766C, kSetupActivePart = 0x2AD10B;  // GetActivePartFromSlot 0x29EB50
constexpr uint32_t kBrandsStockPart = 0x2AB8F1;                             // GetStockCarPart 0x296400
constexpr uint32_t kBrandInCart = 0x2AB22D, kBrandOptionInCart = 0x2AAFA3, kStockInCart = 0x2AB061;  // 0x29EA40
constexpr uint32_t kBrandInstalled = 0x2AB25D;                              // GetInstalledCarPart 0x296480
constexpr uint32_t kListRadius = 0x2A769E;                                  // CarPart::GetAppliedAttributeIParam
constexpr uint32_t kHeaderPreview = 0x2A24F8;                               // PreviewPart
constexpr uint32_t kHeaderName = 0x2A251D, kCartName = 0x2A6169;            // CarPart::GetName 0x244FC0

int g_mode = 0;  // -1 the rear wheels, 0 all, 1 the front
uint32_t g_cart_title = 0;

uint32_t Text(X86Context& ctx, const char* name, const char* const (&text)[kLanguageCount]) {
    return Label(ctx, BinHash(name), text[LanguageIndex(ctx)]);
}

uint32_t FrontTitle(X86Context& ctx) {
    static const char* const kText[kLanguageCount] = {"FRONT WHEELS",      "ROUES AVANT",       "VORDERRAEDER",
                                                      "RUOTE ANTERIORI",   "RUEDAS DELANTERAS", "Rodas dianteiras",
                                                      "RODA DEPAN"};
    return Text(ctx, "CUSTOMIZE_FRONT_WHEEL", kText);
}

uint32_t RearTitle(X86Context& ctx) {
    static const char* const kText[kLanguageCount] = {"REAR WHEELS",       "ROUES ARRIERE",   "HINTERRAEDER",
                                                      "RUOTE POSTERIORI",  "RUEDAS TRASERAS", "Rodas traseiras",
                                                      "RODA BELAKANG"};
    return Text(ctx, "CUSTOMIZE_REAR_WHEEL", kText);
}

uint32_t AllTitle(X86Context& ctx) {
    static const char* const kText[kLanguageCount] = {"ALL WHEELS",     "TOUTES LES ROUES", "ALLE RAEDER",
                                                      "TUTTE LE RUOTE", "TODAS LAS RUEDAS", "Todas as rodas",
                                                      "SEMUA RODA"};
    return Text(ctx, "CUSTOMIZE_ALL_WHEELS", kText);
}

uint32_t ChooseText(X86Context& ctx) {
    static const char* const kText[kLanguageCount] = {
        "Which wheels do you want to customize?", "Quelles roues voulez-vous personnaliser ?",
        "Welche Raeder moechten Sie anpassen?",   "Quali ruote vuoi personalizzare?",
        "Que ruedas quieres personalizar?",       "Quais rodas voc\xEA quer personalizar?",
        "Roda mana yang ingin Anda ubah?"};
    return Text(ctx, "CUSTOMIZE_CHOOSE_FRONT_REAR_WHEEL", kText);
}

uint32_t Call(X86Context& ctx, uint32_t fn, uint32_t self, std::initializer_list<uint32_t> args) {
    X86Context saved = ctx;
    ctx.ecx = self;
    uint32_t result = kernel::CallGuest(ctx, fn, args);
    ctx = saved;
    return result;
}

bool On() { return Active() && Ready(); }

// the slot the customized wheels' calls stand for: the rear's only when only the rear is customized
uint32_t WheelSlot() { return g_mode < 0 ? kSlotRearWheel : kSlotFrontWheel; }

void SwitchToOption(X86Context& ctx, uint32_t option) {
    Call(ctx, kQueuePackageSwitch, kernel::R32(kFEng),
         {kernel::R32(option + kOptionPackage), kernel::R32(option + kOptionMenu), 0, 0});
}

void AskWheels(X86Context& ctx, uint32_t screen) {
    uint32_t front = FrontTitle(ctx), rear = RearTitle(ctx), all = AllTitle(ctx), text = ChooseText(ctx);
    Call(ctx, kShowThreeButtons, 0,
         {kernel::R32(screen + kScreenPackage), kEmptyText, 3, front, rear, all, front, rear, all, kDialogCancel, 2, text});
}

uint32_t PartCost(X86Context& ctx, uint32_t filter, uint32_t slot, uint32_t part) {
    return Call(ctx, kGetCarPartCost, 0, {filter, slot, part, 0});
}

bool ShowsMoney(X86Context& ctx) {
    uint32_t flags = kernel::R32(kCareerFlags);
    bool career = (flags && (kernel::G<uint8_t>(flags + 0x12C) & 1)) || kernel::G<uint8_t>(kCustomizeAnywhere);
    return career && !(Call(ctx, kInBackRoom, 0, {}) & 0xFF);
}

void HideObject(X86Context& ctx, uint32_t package, uint32_t hash) {
    if (uint32_t object = Call(ctx, kFindObject, 0, {package, hash})) Call(ctx, kSetInvisible, 0, {object});
}

// what CustomizeRims_RefreshHeader adds to the game's: no rim name nor size for an empty list, no size for a brand
// with NoRimSize, and the price and trade-in of the wheels customized (all of them cost two)
void RefreshRims(X86Context& ctx, uint32_t screen, uint32_t selected) {
    uint32_t package = kernel::R32(screen + kScreenPackage);
    bool empty = Call(ctx, kListCount, screen + kRimList, {0}) == kernel::R32(screen + kRimListEnd);
    if (empty)
        for (uint32_t hash : {0x8D1559A4u, 0x5E7B09C9u, 0xF2492598u}) HideObject(ctx, package, hash);
    if (NoRimSize(kernel::R32(screen + kScreenMenu))) {
        uint32_t inch = Call(ctx, kFindObject, 0, {package, 0x4BF687BF});
        if (!inch) inch = Call(ctx, kFindObject, 0, {package, 0x4BF5F2DC});
        if (inch) Call(ctx, kSetInvisible, 0, {inch});
    }
    if (empty || !selected || !ShowsMoney(ctx)) return;
    uint32_t filter = Call(ctx, kGetUnlockFilter, kManager, {});
    uint32_t front = Call(ctx, kGetInstalledCarPart, kManager, {kSlotFrontWheel});
    uint32_t rear = Call(ctx, kGetInstalledCarPart, kManager, {kSlotRearWheel});
    uint32_t trade_in = 0;
    if (front && g_mode >= 0) trade_in += PartCost(ctx, filter, kSlotFrontWheel, front);
    if (rear && g_mode <= 0) trade_in += PartCost(ctx, filter, kSlotRearWheel, rear);
    uint32_t cost = kernel::R32(selected + kSelectedPrice) * (g_mode == 0 ? 2 : 1);
    uint32_t meters = kernel::R32(screen + kMetersPackage);
    Call(ctx, kPrintf, 0, {meters, 0xDBB80EDD, kMoneyFormat, cost});
    Call(ctx, kPrintf, 0, {meters, 0xA91EDA8A, kMoneyFormat, uint32_t(int(float(trade_in) * kTradeInFactor))});
}

// a guest copy of a rim's name, six characters longer than shown: the game cuts the size (" 17_IN") off its end
uint32_t NameBuffer(const std::string& text) {
    static uint32_t buffer = mem::AllocSystem(64);
    if (!buffer) return 0;
    std::string padded = text.substr(0, 57) + "      ";
    std::memcpy(kernel::HostPtr(buffer), padded.c_str(), padded.size() + 1);
    return buffer;
}

std::string GuestText(uint32_t text) {
    std::string out;
    if (!text || !mem::IsCommitted(text)) return out;
    for (uint32_t i = 0; i < 64; i++) {
        char c = *static_cast<char*>(kernel::HostPtr(text + i));
        if (!c) break;
        out += c;
    }
    return out;
}

std::string LanguageText(X86Context& ctx, uint32_t hash) {
    if (!StringExists(ctx, hash)) return {};
    X86Context saved = ctx;
    ctx.edx = hash;
    uint32_t text = kernel::CallGuest(ctx, kSearchForString, {});
    ctx = saved;
    return GuestText(text);
}

// as CarRenderInfo_AdjustBrakeScale: a brake's x and z grow with the rim's inner radius, at most 1.15 of the wheel
void ScaleBrake(uint32_t info, uint32_t matrix, int wheel) {
    uint32_t ride = kernel::R32(info + kRenderRide);
    if (!ride || !mem::IsCommitted(ride) || !matrix) return;
    uint32_t type = kernel::R32(ride + kRideCarType);
    if (type >= CarTypeCount() || !Config(type).main.scale_brakes_with_rims) return;
    bool rear = wheel > 1;
    uint32_t rim = PartAt(ride, rear ? kSlotRearWheel : kSlotFrontWheel);
    if (rear && !rim) rim = PartAt(ride, kSlotFrontWheel);
    uint32_t brake = PartAt(ride, rear ? kSlotRearBrake : kSlotFrontBrake);
    if (!rim || !brake) return;
    static const uint32_t kOuter = BinHash("OUTER_RADIUS"), kInner = BinHash("INNER_RADIUS"), kScale = BinHash("SCALE");
    int outer = PartInt(rim, kOuter, 23), inner = PartInt(rim, kInner, 20);
    if (!PartInt(rim, kScale, 1) || !PartInt(brake, kScale, 1) || (outer == 23 && inner == 20) || inner <= 16) return;
    float wheel_scale = kernel::G<float>(info + kRenderWheelRadiusScale + uint32_t(wheel) * 4);
    float* m = static_cast<float*>(kernel::HostPtr(matrix));
    float sx = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    float sz = std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
    if (sx <= 0 || sz <= 0) return;
    float k = 0.85f * wheel_scale * (1 + (inner - 17) * 0.07f), most = 1.15f * wheel_scale;
    float kx = k, kz = k;
    if (sx * k > most) {
        kx = most / sx;
        kz = most / sz;
    }
    for (int i = 0; i < 3; i++) {
        m[i] *= kx;
        m[8 + i] *= kz;
    }
}

uint32_t g_render_info = 0;

int BrakeWheel(uint32_t ret) {
    switch (ret) {
        case 0x2570D4: return 0;
        case 0x25724B: return 1;
        case 0x2573CB: return 2;
        case 0x25750F:
        case 0x25757F: return 3;
    }
    return -1;
}

}  // namespace

int RimsMode() { return g_mode; }

// the rear rim's own texture, where it is not the front's: as the PC, it takes the spinner's mapping (+0x2D0), which
// the render replaces with the ride's DUMMY_SPINNER<n> (+0x44, the info's +0x330), painted as DUMMY_WHEEL<n> is
constexpr uint32_t kUsedMappedSpinner = 0x2D0, kRideCompositeSpinner = 0x44, kCompositeWheel = 0x251690;

uint32_t RearRimTexture(X86Context& ctx, uint32_t ride) {
    uint32_t rear = PartAt(ride, kSlotRearWheel);
    if (!rear || rear == PartAt(ride, kSlotFrontWheel)) return 0;
    return Call(ctx, kPartTextureNameHash, rear, {});
}

// the Xbox's CARS\TEXTURES.BIN has DUMMY_WHEEL1 to 4 (a racer's painted rims each) and no DUMMY_SPINNER, which the PC
// has: GetTextureInfo is answered for DUMMY_SPINNER<n> with a copy of DUMMY_WHEEL<n> with its own image and palette.
// A TextureInfo's image is at +0x70 and its palette at +0x74 (their sizes +0x38, +0x3C), which CompositeWheel paints
// through; its platform part (+0x00) has the D3D format at +0x0C, the texture's header at +0x10 and the palette's at
// +0x24, each with the data's physical address at +4, as 0xC1AE0 makes them
constexpr uint32_t kInfoImage = 0x70, kInfoPalette = 0x74, kInfoImageSize = 0x38, kInfoPaletteSize = 0x3C;
constexpr uint32_t kPlatFormat = 0x0C, kPlatTexture = 0x10, kPlatPalette = 0x24, kFormatP8 = 0x0B;

uint32_t CopyOf(uint32_t from, uint32_t bytes) {
    if (!from || !bytes || bytes > 0x400000 || !mem::IsCommitted(from)) return 0;
    uint32_t to = mem::AllocContiguous(bytes, 0, 0xFFFFFFFF, 0x1000);
    if (to) std::memcpy(kernel::HostPtr(to), kernel::HostPtr(from), bytes);
    return to;
}

uint32_t SpinnerTexture(X86Context& ctx, uint32_t hash) {
    static const uint32_t kFirst = BinHash("DUMMY_SPINNER1");
    uint32_t n = hash - kFirst;
    if (n >= 4) return 0;
    static uint32_t made[4] = {};
    if (made[n]) return made[n];
    constexpr uint32_t kInfoBytes = 0x80, kPlatBytes = 0x40, kInfoHash = 0x24;
    uint32_t wheel = Call(ctx, kGetTextureInfo, 0, {BinHash("DUMMY_WHEEL" + std::to_string(n + 1)), 0, 0});
    if (!wheel || !kernel::R32(wheel) || !kernel::R32(wheel + kInfoImage)) return 0;
    uint32_t wheel_plat = kernel::R32(wheel);
    uint32_t image = CopyOf(kernel::R32(wheel + kInfoImage), kernel::R32(wheel + kInfoImageSize));
    bool p8 = kernel::R32(wheel_plat + kPlatFormat) == kFormatP8;
    uint32_t palette = p8 ? CopyOf(kernel::R32(wheel + kInfoPalette), kernel::R32(wheel + kInfoPaletteSize)) : 0;
    if (!image || (p8 && !palette)) return 0;
    uint32_t info = mem::AllocSystem(kInfoBytes), plat = mem::AllocSystem(kPlatBytes);
    if (!info || !plat) return 0;
    std::memcpy(kernel::HostPtr(info), kernel::HostPtr(wheel), kInfoBytes);
    std::memcpy(kernel::HostPtr(plat), kernel::HostPtr(wheel_plat), kPlatBytes);
    kernel::W32(info, plat);
    kernel::W32(info + kInfoHash, hash);
    kernel::W32(info + kInfoImage, image);
    kernel::W32(plat + kPlatTexture + 4, mem::PhysicalFromVirtual(image));
    if (p8) {
        kernel::W32(info + kInfoPalette, palette);
        kernel::W32(plat + kPlatPalette + 4, mem::PhysicalFromVirtual(palette));
    }
    LOGR("unlimiter: DUMMY_SPINNER%u made from DUMMY_WHEEL%u (%u bytes, palette %u)", n + 1, n + 1,
         kernel::R32(wheel + kInfoImageSize), p8 ? kernel::R32(wheel + kInfoPaletteSize) : 0);
    made[n] = info;
    return info;
}

// GetUsedCarTextureInfo's temporary textures (+0x15C, their count at +0x2BC, 87 at most) have the front rim's
// <TEXTURE_NAME>_WHEEL and _WHEEL_INNER_MASK only (0x259F10): a rear rim of its own drew untextured. As the PC's
// GetTempCarSkinTextures, the rear's are loaded too
void RearWheelTextures(X86Context& ctx, uint32_t info, uint32_t ride) {
    constexpr uint32_t kTemp = 0x15C, kTempCount = 0x2BC, kTempMax = 0x57;
    uint32_t texture = RearRimTexture(ctx, ride);
    if (!texture) return;
    kernel::W32(info + kUsedMappedSpinner, HashFrom("_WHEEL", texture));
    for (const char* suffix : {"_WHEEL", "_WHEEL_INNER_MASK"}) {
        uint32_t hash = HashFrom(suffix, texture), n = kernel::R32(info + kTempCount);
        bool have = false;
        for (uint32_t i = 0; i < n && i < kTempMax && !have; i++) have = kernel::R32(info + kTemp + i * 4) == hash;
        if (have || n >= kTempMax) continue;
        kernel::W32(info + kTemp + n * 4, hash);
        kernel::W32(info + kTempCount, n + 1);
    }
}

// the rear rim painted on its DUMMY_SPINNER<n>, once for a rim and a paint, when its textures are in
void PaintRearRim(X86Context& ctx, uint32_t ride, bool again) {
    static const uint32_t kFirst = BinHash("DUMMY_SPINNER1");
    struct Done {
        uint32_t texture, paint;
    };
    static Done done[4] = {};
    uint32_t composite = kernel::R32(ride + kRideCompositeSpinner), n = composite - kFirst;
    if (n >= 4) return;
    uint32_t texture = RearRimTexture(ctx, ride), paint = PartAt(ride, kSlotPaintRim);
    if (!texture) return;
    if (again) done[n] = {};
    if (done[n].texture == texture && done[n].paint == paint) return;
    uint32_t wheel = HashFrom("_WHEEL", texture), mask = HashFrom("_WHEEL_INNER_MASK", texture);
    if (!Call(ctx, kGetTextureInfo, 0, {wheel, 0, 0}) || !Call(ctx, kGetTextureInfo, 0, {mask, 0, 0})) return;
    uint32_t info = SpinnerTexture(ctx, composite);
    if (!info) return;
    Call(ctx, kCompositeWheel, 0, {ride, composite, wheel, mask, kSlotPaintRim});
    // the game's composites are inside its locks of DUMMY_WHEEL<n>, which have the renderer upload them again
    gpu::MarkDirty(kernel::R32(info) + 0x10);
    done[n] = {texture, paint};
}

uint32_t WheelCartTitle(uint32_t slot) {
    return slot == kSlotFrontWheel ? g_cart_title : 0;
}

}  // namespace unlimiter

using unlimiter::g_mode;

// CustomizeSub::NotificationMessage (thiscall: the message, the object, two params): Rims in Parts asks which wheels
// first; the answer opens the brands
X86_EXTERN_FUNC(__imp__sub_002AAEC0);
X86_FUNC(sub_002AAEC0) {
    uint32_t screen = ctx.ecx, message = kernel::R32(ctx.esp + 4);
    if (unlimiter::On()) {
        uint32_t option = kernel::R32(screen + unlimiter::kSubOption);
        bool rims = option && (kernel::R32(option + unlimiter::kOptionMenu) & 0xFFFF) == unlimiter::kMenuRims &&
                    kernel::R32(screen + unlimiter::kSubCategory) == unlimiter::kMenuCategoryParts;
        if (rims && message == unlimiter::kPadAccept) {
            if (unlimiter::Global().separate_rims) {
                unlimiter::AskWheels(ctx, screen);
                ctx.esp += 4 + 16;
                return;
            }
            g_mode = 0;
        }
        static const uint32_t kFront = unlimiter::BinHash("CUSTOMIZE_FRONT_WHEEL");
        static const uint32_t kRear = unlimiter::BinHash("CUSTOMIZE_REAR_WHEEL");
        static const uint32_t kAll = unlimiter::BinHash("CUSTOMIZE_ALL_WHEELS");
        int chosen = 2;
        if (message == kFront) chosen = 1;
        else if (message == kRear) chosen = -1;
        else if (message == kAll) chosen = 0;
        if (chosen != 2 && rims) {
            g_mode = chosen;
            unlimiter::SwitchToOption(ctx, option);
            ctx.esp += 4 + 16;
            return;
        }
    }
    __imp__sub_002AAEC0(ctx, base);
}

// CustomizeRims::NotificationMessage: the triggers change the rim size, which a NoRimSize brand has not
X86_EXTERN_FUNC(__imp__sub_002AF920);
X86_FUNC(sub_002AF920) {
    uint32_t screen = ctx.ecx, message = kernel::R32(ctx.esp + 4);
    if (unlimiter::On() && (message == unlimiter::kPadLeftTrigger || message == unlimiter::kPadRightTrigger) &&
        unlimiter::NoRimSize(kernel::R32(screen + unlimiter::kScreenMenu))) {
        unlimiter::Call(ctx, unlimiter::kBaseNotification, screen,
                        {message, kernel::R32(ctx.esp + 8), kernel::R32(ctx.esp + 12), kernel::R32(ctx.esp + 16)});
        ctx.esp += 4 + 16;
        return;
    }
    __imp__sub_002AF920(ctx, base);
}

// CustomizeRims::RefreshHeader
X86_EXTERN_FUNC(__imp__sub_002A24B0);
X86_FUNC(sub_002A24B0) {
    uint32_t screen = ctx.ecx;
    __imp__sub_002A24B0(ctx, base);
    if (!unlimiter::On()) return;
    X86Context saved = ctx;
    ctx.ecx = screen + unlimiter::kRimList;
    uint32_t selected = 0;
    if (kernel::CallGuest(ctx, unlimiter::kListCount, {0}) != kernel::R32(screen + unlimiter::kRimListEnd)) {
        ctx.ecx = screen;
        selected = kernel::CallGuest(ctx, kernel::R32(kernel::R32(screen) + 0x14), {});
    }
    ctx = saved;
    unlimiter::RefreshRims(ctx, screen, selected);
}

// CarCustomizeManager::AddToCart (thiscall: the SelectablePart): a rim goes in for the wheels customized
X86_EXTERN_FUNC(__imp__sub_002A5020);
X86_FUNC(sub_002A5020) {
    uint32_t manager = ctx.ecx, part = kernel::R32(ctx.esp + 4);
    if (!unlimiter::On() || !part || kernel::R32(part + unlimiter::kSelectedSlot) != unlimiter::kSlotFrontWheel || g_mode > 0)
        return __imp__sub_002A5020(ctx, base);
    if (g_mode < 0) {
        kernel::W32(part + unlimiter::kSelectedSlot, unlimiter::kSlotRearWheel);
        __imp__sub_002A5020(ctx, base);
        kernel::W32(part + unlimiter::kSelectedSlot, unlimiter::kSlotFrontWheel);
        return;
    }
    __imp__sub_002A5020(ctx, base);
    uint32_t result = ctx.eax;
    kernel::W32(part + unlimiter::kSelectedSlot, unlimiter::kSlotRearWheel);
    unlimiter::Call(ctx, 0x2A5020, manager, {part});
    kernel::W32(part + unlimiter::kSelectedSlot, unlimiter::kSlotFrontWheel);
    ctx.eax = result;
}

// CarCustomizeManager::PreviewPart (thiscall: the slot, the part), from the rims' header: the wheels customized
X86_EXTERN_FUNC(__imp__sub_0029EF70);
X86_FUNC(sub_0029EF70) {
    uint32_t manager = ctx.ecx, ret = kernel::R32(ctx.esp), slot = kernel::R32(ctx.esp + 4), part = kernel::R32(ctx.esp + 8);
    if (!unlimiter::On() || ret != unlimiter::kHeaderPreview || slot != unlimiter::kSlotFrontWheel || g_mode > 0)
        return __imp__sub_0029EF70(ctx, base);
    if (g_mode < 0) {
        kernel::W32(ctx.esp + 4, unlimiter::kSlotRearWheel);
        return __imp__sub_0029EF70(ctx, base);
    }
    __imp__sub_0029EF70(ctx, base);
    unlimiter::Call(ctx, 0x29EF70, manager, {unlimiter::kSlotRearWheel, part});
}

// FECustomizationRecord::SetInstalledPart (thiscall: the slot, the part): a record with no rear wheel of its own keeps
// the front's there before the front changes, so a front alone leaves the rear as it was
X86_EXTERN_FUNC(__imp__sub_00126B70);
X86_FUNC(sub_00126B70) {
    uint32_t record = ctx.ecx, slot = kernel::R32(ctx.esp + 4);
    if (slot == unlimiter::kSlotFrontWheel && unlimiter::On() && unlimiter::Global().separate_rims) {
        uint16_t front = kernel::R16(record + unlimiter::kSlotFrontWheel * 2);
        uint16_t rear = kernel::R16(record + unlimiter::kSlotRearWheel * 2);
        if (rear == 0xFFFF && front != 0xFFFF) kernel::W16(record + unlimiter::kSlotRearWheel * 2, front);
    }
    __imp__sub_00126B70(ctx, base);
}

namespace {

uint32_t RearWhenOnly(uint32_t ret, std::initializer_list<uint32_t> places, uint32_t slot) {
    if (slot != unlimiter::kSlotFrontWheel || g_mode >= 0 || !unlimiter::On()) return slot;
    for (uint32_t at : places)
        if (at == ret) return unlimiter::kSlotRearWheel;
    return slot;
}

}  // namespace

// CarCustomizeManager::GetActivePartFromSlot
X86_EXTERN_FUNC(__imp__sub_0029EB50);
X86_FUNC(sub_0029EB50) {
    uint32_t slot = kernel::R32(ctx.esp + 4);
    kernel::W32(ctx.esp + 4, RearWhenOnly(kernel::R32(ctx.esp), {unlimiter::kListActivePart, unlimiter::kSetupActivePart}, slot));
    __imp__sub_0029EB50(ctx, base);
}

// CarCustomizeManager::GetStockCarPart
X86_EXTERN_FUNC(__imp__sub_00296400);
X86_FUNC(sub_00296400) {
    uint32_t slot = kernel::R32(ctx.esp + 4);
    kernel::W32(ctx.esp + 4, RearWhenOnly(kernel::R32(ctx.esp), {unlimiter::kBrandsStockPart}, slot));
    __imp__sub_00296400(ctx, base);
}

// CarCustomizeManager::IsPartTypeInCart (the slot)
X86_EXTERN_FUNC(__imp__sub_0029EA40);
X86_FUNC(sub_0029EA40) {
    uint32_t slot = kernel::R32(ctx.esp + 4);
    kernel::W32(ctx.esp + 4, RearWhenOnly(kernel::R32(ctx.esp),
                                          {unlimiter::kBrandInCart, unlimiter::kBrandOptionInCart, unlimiter::kStockInCart}, slot));
    __imp__sub_0029EA40(ctx, base);
}

// CarCustomizeManager::GetInstalledCarPart
X86_EXTERN_FUNC(__imp__sub_00296480);
X86_FUNC(sub_00296480) {
    uint32_t slot = kernel::R32(ctx.esp + 4);
    kernel::W32(ctx.esp + 4, RearWhenOnly(kernel::R32(ctx.esp), {unlimiter::kBrandInstalled}, slot));
    __imp__sub_00296480(ctx, base);
}

// CarPart::GetAppliedAttributeIParam (thiscall: the key, the default), from BuildRimsList's size test: a rear rim
// (REAR) is not listed, and a NoRimSize brand lists every size
X86_EXTERN_FUNC(__imp__sub_0024FA00);
X86_FUNC(sub_0024FA00) {
    uint32_t ret = kernel::R32(ctx.esp), part = ctx.ecx, key = kernel::R32(ctx.esp + 4), screen = ctx.edi;
    __imp__sub_0024FA00(ctx, base);
    if (unlimiter::ForceVinylColourBrand(ret, part, key)) {
        ctx.eax = ctx.ebx;
        return;
    }
    if (ret != unlimiter::kListRadius || key != unlimiter::kInnerRadius || !unlimiter::On()) return;
    int32_t size = int32_t(kernel::R32(screen + unlimiter::kRimSize));
    static const uint32_t kRear = unlimiter::BinHash("REAR");
    if (unlimiter::PartInt(part, kRear, 0)) ctx.eax = uint32_t(size == 0 ? 1 : 0);
    else if (unlimiter::NoRimSize(kernel::R32(screen + unlimiter::kScreenMenu))) ctx.eax = uint32_t(size) & 0xFF;
}

// CarPart::GetName, for the rims' header and the cart: a rim's LANGUAGEHASH, or its whole name in a NoRimSize brand
X86_EXTERN_FUNC(__imp__sub_00244FC0);
X86_FUNC(sub_00244FC0) {
    uint32_t ret = kernel::R32(ctx.esp), part = ctx.ecx, screen = ctx.esi;
    __imp__sub_00244FC0(ctx, base);
    if ((ret != unlimiter::kHeaderName && ret != unlimiter::kCartName) || !unlimiter::On()) return;
    static const uint32_t kLanguageHash = unlimiter::BinHash("LANGUAGEHASH");
    uint32_t language = 0;
    std::string text;
    if (unlimiter::PartAttribute(part, kLanguageHash, language) && language && language != 0xFFFFFFFF)
        text = unlimiter::LanguageText(ctx, language);
    if (text.empty() && ret == unlimiter::kHeaderName && unlimiter::NoRimSize(kernel::R32(screen + unlimiter::kScreenMenu)))
        text = unlimiter::GuestText(ctx.eax);
    if (text.empty()) return;
    if (uint32_t buffer = unlimiter::NameBuffer(text)) ctx.eax = buffer;
}

// FEShoppingCartItem's name (thiscall, the item at +0x54, its SelectablePart at +0x0C): a rear wheel's is a rim's, and
// with separate rims each says which wheels
X86_EXTERN_FUNC(__imp__sub_002A5B10);
X86_FUNC(sub_002A5B10) {
    uint32_t item = kernel::R32(ctx.ecx + 0x54);
    uint32_t selected = item ? kernel::R32(item + 0x0C) : 0;
    uint32_t slot = selected ? kernel::R32(selected + unlimiter::kSelectedSlot) : 0;
    bool wheel = slot == unlimiter::kSlotFrontWheel || slot == unlimiter::kSlotRearWheel;
    if (!wheel || !unlimiter::On()) return __imp__sub_002A5B10(ctx, base);
    if (unlimiter::Global().separate_rims)
        unlimiter::g_cart_title = slot == unlimiter::kSlotRearWheel ? unlimiter::RearTitle(ctx) : unlimiter::FrontTitle(ctx);
    kernel::W32(selected + unlimiter::kSelectedSlot, unlimiter::kSlotFrontWheel);
    __imp__sub_002A5B10(ctx, base);
    kernel::W32(selected + unlimiter::kSelectedSlot, slot);
    unlimiter::g_cart_title = 0;
}

// CarRenderInfo::Render, whose brakes are drawn by eViewPlatInterface::Render (0xD4D40, thiscall: the model, its
// local_world, ...) from five places, one per wheel and two for the last
X86_EXTERN_FUNC(__imp__sub_00255490);
X86_FUNC(sub_00255490) {
    uint32_t previous = unlimiter::g_render_info;
    unlimiter::g_render_info = ctx.ecx;
    if (unlimiter::On()) {
        uint32_t ride = kernel::R32(ctx.ecx + unlimiter::kRenderRide);
        if (ride && mem::IsCommitted(ride)) unlimiter::PaintRearRim(ctx, ride, false);
    }
    __imp__sub_00255490(ctx, base);
    unlimiter::g_render_info = previous;
}

X86_EXTERN_FUNC(__imp__sub_000D4D40);
X86_FUNC(sub_000D4D40) {
    if (unlimiter::g_render_info) {
        int wheel = unlimiter::BrakeWheel(kernel::R32(ctx.esp));
        if (wheel >= 0 && unlimiter::On())
            unlimiter::ScaleBrake(unlimiter::g_render_info, kernel::R32(ctx.esp + 8), wheel);
    }
    __imp__sub_000D4D40(ctx, base);
}

// CompositeRim (cdecl: the ride): the rim paint on DUMMY_WHEEL<n> from the front rim. A rear rim of its own gets it on
// DUMMY_SPINNER<n> (CompositeWheel 0x251690, cdecl: the ride, the composite, the rim's _WHEEL, its _WHEEL_INNER_MASK, the
// paint's slot), as the PC's CompositeRim; the game's composite comes as the car's load ends, the rear's textures often
// after it, and CompositeWheel does nothing without them: it is tried again at each draw of the car till it is done
X86_EXTERN_FUNC(__imp__sub_0025A1D0);
X86_FUNC(sub_0025A1D0) {
    uint32_t ride = kernel::R32(ctx.esp + 4);
    __imp__sub_0025A1D0(ctx, base);
    if (!unlimiter::On() || !ride || !mem::IsCommitted(ride)) return;
    uint32_t result = ctx.eax;
    unlimiter::PaintRearRim(ctx, ride, true);
    ctx.eax = result;
}

// The rims of CARS\WHEELS are streamed by name. A loaded car (the CarLoader's entry, 0x259AD0: its ride copied at +0x1C)
// keeps one wheel info (+0x374, made by 0x2595B0 from the ride's FRONT_WHEEL): its models' names per LOD (+0x20, five,
// the range at +0x74 and +0x78) are loaded with every car's in one batch (0x2612B0, its 0xD58E0) and unloaded by
// 0x25FB20 (0xC4220, the names), its _WHEEL (+0x34, a room for four) loaded and bound with them. A rear rim of its own was never
// loaded: its model drew what another car had loaded and, that unloaded, freed memory, and the game fell. Each such car
// gets a second wheel info made from its rear, whose names go with the front's batch and unloading, its _WHEEL in a free
// room of the front's
namespace unlimiter {
namespace {

constexpr uint32_t kEntryRide = 0x1C, kEntryWheels = 0x374, kWheelInfoBytes = 0x7C, kRideBytes = 0x310;
constexpr uint32_t kWheelNames = 0x20, kWheelTextures = 0x34, kWheelLodFirst = 0x74, kWheelLodLast = 0x78;
constexpr uint32_t kMakeWheelInfo = 0x2595B0, kUnloadSolids = 0xC4220;
constexpr uint32_t kBatchLoad = 0x2613A2, kBatchRoom = 0x80;

// a loaded car's wheel info: its rear's, and the names loaded for it. A solid's unload (0xC3820) takes one from its count
// whoever loaded it and frees it at none, so only what was loaded for the rear is unloaded with it
struct RearWheels {
    uint32_t shadow = 0;
    std::vector<uint32_t> loaded;
};
std::map<uint32_t, RearWheels> g_rear_wheels;
std::vector<uint32_t> g_free_wheels;

void MakeRearWheels(X86Context& ctx, uint32_t entry, bool front_end) {
    uint32_t info = entry + kEntryWheels, ride = entry + kEntryRide;
    if (auto it = g_rear_wheels.find(info); it != g_rear_wheels.end()) {
        g_free_wheels.push_back(it->second.shadow);
        g_rear_wheels.erase(it);
    }
    uint32_t front = kernel::R32(ride + kRideParts + kSlotFrontWheel * 4);
    uint32_t rear = kernel::R32(ride + kRideParts + kSlotRearWheel * 4);
    if (!rear || rear == front) return;
    static uint32_t fake = mem::AllocSystem(kRideBytes);
    uint32_t shadow = 0;
    if (!g_free_wheels.empty()) {
        shadow = g_free_wheels.back();
        g_free_wheels.pop_back();
    } else {
        shadow = mem::AllocSystem(kWheelInfoBytes);
    }
    if (!fake || !shadow) return;
    std::memcpy(kernel::HostPtr(fake), kernel::HostPtr(ride), kRideBytes);
    kernel::W32(fake + kRideParts + kSlotFrontWheel * 4, rear);
    Call(ctx, kMakeWheelInfo, shadow, {fake, front_end ? 1u : 0u});
    bool names = false;
    for (uint32_t i = 0; i < 5; i++) names |= kernel::R32(shadow + kWheelNames + i * 4) != 0;
    if (!names) {
        g_free_wheels.push_back(shadow);
        return;
    }
    if (uint32_t texture = kernel::R32(shadow + kWheelTextures)) {
        for (uint32_t at = kWheelTextures; at < kWheelTextures + 16; at += 4) {
            uint32_t have = kernel::R32(info + at);
            if (have == texture) break;
            if (!have) {
                kernel::W32(info + at, texture);
                break;
            }
        }
    }
    g_rear_wheels[info] = RearWheels{shadow, {}};
}

// the batch's names (the caller's room of 128): each rear's after its front's
uint32_t AddRearNames(uint32_t list, uint32_t count) {
    auto has = [&](uint32_t name) {
        for (uint32_t i = 0; i < count; i++)
            if (kernel::R32(list + i * 4) == name) return true;
        return false;
    };
    for (auto& [info, rear] : g_rear_wheels) {
        uint32_t shadow = rear.shadow;
        if (kernel::R32(info + 8) != 1 || !rear.loaded.empty()) continue;
        uint32_t first = kernel::R32(info + kWheelLodFirst), last = kernel::R32(info + kWheelLodLast);
        bool front = false;
        for (uint32_t l = first; l <= last && l < 5 && !front; l++) {
            uint32_t name = kernel::R32(info + kWheelNames + l * 4);
            front = name && has(name);
        }
        if (!front) continue;
        first = kernel::R32(shadow + kWheelLodFirst);
        last = kernel::R32(shadow + kWheelLodLast);
        for (uint32_t l = first; l <= last && l < 5 && count < kBatchRoom; l++) {
            uint32_t name = kernel::R32(shadow + kWheelNames + l * 4);
            if (!name) continue;
            kernel::W32(list + 4 * count++, name);
            rear.loaded.push_back(name);
        }
    }
    return count;
}

}  // namespace
}  // namespace unlimiter

// CarLoader's entry for a car (thiscall: the ride, front end, ...)
X86_EXTERN_FUNC(__imp__sub_00259AD0);
X86_FUNC(sub_00259AD0) {
    uint32_t entry = ctx.ecx;
    bool front_end = kernel::R32(ctx.esp + 8) != 0;
    __imp__sub_00259AD0(ctx, base);
    if (!unlimiter::On()) return;
    uint32_t result = ctx.eax;
    unlimiter::MakeRearWheels(ctx, entry, front_end);
    ctx.eax = result;
}

// the solids' streamed load (cdecl: the names, their count, the callback, ...), from the wheels' batch
X86_EXTERN_FUNC(__imp__sub_000D58E0);
X86_FUNC(sub_000D58E0) {
    if (kernel::R32(ctx.esp) == unlimiter::kBatchLoad && !unlimiter::g_rear_wheels.empty())
        kernel::W32(ctx.esp + 8, unlimiter::AddRearNames(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8)));
    __imp__sub_000D58E0(ctx, base);
}

// a wheel info's unloading (thiscall: the info), its models' (0xC4220) when they were loaded (+0x08, 2): its rear's with it
X86_EXTERN_FUNC(__imp__sub_0025FB20);
X86_FUNC(sub_0025FB20) {
    uint32_t info = kernel::R32(ctx.esp + 4);
    bool loaded = info && kernel::R32(info + 8) == 2;
    __imp__sub_0025FB20(ctx, base);
    auto it = unlimiter::g_rear_wheels.find(info);
    if (it == unlimiter::g_rear_wheels.end()) return;
    unlimiter::RearWheels rear = it->second;
    uint32_t result = ctx.eax;
    unlimiter::g_rear_wheels.erase(it);
    if (loaded && !rear.loaded.empty()) {
        static uint32_t names = mem::AllocSystem(4 * 5);
        uint32_t n = uint32_t(std::min<size_t>(rear.loaded.size(), 5));
        for (uint32_t i = 0; i < n; i++) kernel::W32(names + i * 4, rear.loaded[i]);
        if (names) unlimiter::Call(ctx, unlimiter::kUnloadSolids, 0, {names, n});
    }
    unlimiter::g_free_wheels.push_back(rear.shadow);
    ctx.eax = result;
}
