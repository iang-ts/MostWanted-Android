#include <algorithm>
#include <chrono>

#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"
#include "unlimiter/parts.h"

// the customize shop's camera, as the PC's FindScreenInfo and FindScreenCameraInfo hooks do it. GarageMainScreen's
// tick (0x2A9645, the menu at +0x94) asks FindScreenInfo (0x298EA0, cdecl: the package's name, the menu) for the
// frontend node customize_<package>_<menu>, or customize_<package> when there is none, and FindScreenCameraInfo
// (0x2991F0, cdecl: that node) for its cam_angle. The nodes of the Unlimiter's menus come from its VltEd scripts
// (UnlimiterImport, applied by CarConverter's build); without them a new menu has the package's camera, the front

namespace unlimiter {

namespace {

constexpr uint32_t kRimsPackageName = 0x3D81D0;  // "Rims.fng"
constexpr uint32_t kNoMenu = 0xFFFFFFFF;
constexpr uint32_t kMenuRimsFirst = 0x701, kMenuRimsLast = 0x7FF, kMenuVinylsFirst = 0x401, kMenuVinylsLast = 0x4FF;
constexpr uint32_t kVinylsCamera = 0x402, kVinylsAltCamera = 0x403;

// the screen node last handed out and the menu it was asked for
struct Asked {
    uint32_t node = 0, menu = kNoMenu;
} g_asked;

bool InRange(uint32_t menu, uint32_t first, uint32_t last) { return menu >= first && menu <= last; }

// the port's own visual menus are in the parts' block; the Unlimiter's nodes are named for the PC's menus
uint32_t NodeMenu(uint32_t menu) {
    switch (menu) {
        case kMenuDriver: return 0x308;
        case kMenuLicensePlate: return 0x309;
        case kMenuTireStyle: return 0x314;
        case kMenuNeon: return 0x315;
        default: break;
    }
    // every vinyl group takes the low camera or angle_customize_vinyl_2, from its own UseAltCamera
    if (menu == kMenuVinylCustom || (menu >= kMenuVinylGroupFirst && menu <= kMenuVinylGroupLast) ||
        (menu >= kMenuVinylExtra && menu < kMenuVinylExtra + kMaxVinylGroups))
        return VinylAltCamera(menu) ? kVinylsAltCamera : kVinylsCamera;
    return menu;
}

// the garage turns the front wheels from this angle, in degrees (0x299BEB: times 65536, then the first two wheel
// matrices). The disc leaves it at about 22. The tire menu eases it to -20 and back, as the PC's CarSelectTireSteerAngle
constexpr uint32_t kTireSteer = 0x46C4B8;

void SteerTires() {
    if (!Active() || !Ready()) return;
    static bool saved = false;
    static float rest = 0;
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    if (dt < 0 || dt > 0.1f) dt = 1.0f / 60.0f;
    float& angle = kernel::G<float>(kTireSteer);
    if (!saved) {
        rest = angle;
        saved = true;
    }
    float target = g_asked.menu == kMenuTireStyle ? -20.0f : rest;
    float step = 50.0f * dt;
    if (angle < target) angle = std::min(angle + step, target);
    else if (angle > target) angle = std::max(angle - step, target);
}

uint32_t OwnCamera(const CarConfig& c, uint32_t menu) {
    if (InRange(menu, kMenuRimsFirst, kMenuRimsLast)) {
        if (!Global().separate_rims || RimsMode() > 0) return c.cameras.rim_brand;
        return RimsMode() < 0 ? c.cameras.rim_brand_rear : c.cameras.rim_brand_all;
    }
    if (InRange(menu, kMenuVinylsFirst, kMenuVinylsLast) || menu == kMenuVinylCustom) return c.cameras.vinyl_group;
    if (menu >= kMenuDecalSlotFirst && menu < kMenuDecalSlotFirst + kMenuDecalSlots) {
        int place = DecalPlace(), slot = int(menu - kMenuDecalSlotFirst);
        if (place >= 0 && place < 6 && slot >= 0 && slot < 8) return c.cameras.decal_slot[place][slot];
    }
    auto it = c.cameras.by_menu.find(menu);
    return it == c.cameras.by_menu.end() ? 0 : it->second;
}

}  // namespace

}  // namespace unlimiter

X86_EXTERN_FUNC(__imp__sub_00298EA0);
X86_FUNC(sub_00298EA0) {
    uint32_t menu = kernel::R32(ctx.esp + 8);
    bool own = menu != unlimiter::kNoMenu && unlimiter::Active() && unlimiter::Ready();
    if (own) {
        // every rim brand, the Unlimiter's own too, takes the rims' node
        if (unlimiter::InRange(menu, unlimiter::kMenuRimsFirst, unlimiter::kMenuRimsLast)) {
            kernel::W32(ctx.esp + 4, unlimiter::kRimsPackageName);
            kernel::W32(ctx.esp + 8, unlimiter::kNoMenu);
        } else {
            kernel::W32(ctx.esp + 8, unlimiter::NodeMenu(menu));
        }
    }
    __imp__sub_00298EA0(ctx, base);
    if (own) unlimiter::g_asked = {ctx.eax, menu};
}

// as the PC's 0x7BA070: the tick takes cam_user_rotate (+0x5A) into the screen's +0x84, which lets HandleJoyEvents
// (0x294210) turn the camera, from the camera's node (its layout at [ebp-0x4C]) and not the screen's ([ebp-0x60]). The
// Unlimiter's attachments have angle4_rotate, a view of the whole car the player turns to find the part
void UnlimiterCamUserRotate(X86Context& ctx, uint8_t*) {
    if (!unlimiter::Active()) return;
    uint32_t angle = kernel::R32(ctx.ebp - 0x4C);
    if (!angle || !mem::IsCommitted(angle + 0x5A)) return;
    ctx.eax = (ctx.eax & ~0xFFu) | kernel::G<uint8_t>(angle + 0x5A);
}

X86_EXTERN_FUNC(__imp__sub_002991F0);
X86_FUNC(sub_002991F0) {
    uint32_t node = kernel::R32(ctx.esp + 4);
    unlimiter::SteerTires();
    __imp__sub_002991F0(ctx, base);
    if (!unlimiter::Active() || !unlimiter::Ready() || node != unlimiter::g_asked.node) return;
    uint32_t type = unlimiter::CurrentCarType(ctx);
    if (type >= unlimiter::CarTypeCount()) return;
    if (uint32_t camera = unlimiter::OwnCamera(unlimiter::Config(type), unlimiter::g_asked.menu)) ctx.eax = camera;
}
