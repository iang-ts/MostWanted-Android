#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

#include "common/options.h"
#include "core/input.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"

// FREE CAMERA in the garage and the shops: the car seen from anywhere, as the PC's mouse lets it be. The garage's tick
// (0x2A94E0) runs HandleJoyEvents (0x294210, thiscall on GarageMainScreen, src/game/fe_mouse.cpp's hook calls
// GarageCameraTick after it), which hands the right stick and the triggers to its camera (+0x4C) as speeds: yaw
// (0x5F150), pitch (0x5F210) and zoom (0x5F2D0), each thiscall with a float. It does so only on a screen whose camera
// node lets the player turn it (cam_user_rotate, the screen's +0x84), and a while after the last turn (the frames at
// 0x4DEC28, which the tick counts down) the camera goes back to the node's. Free, the camera is given the speeds every
// tick from the pad as the filter takes it (the game itself sees no pad, so no menu moves), and the count down is held,
// so it stays where it was left. FEng is hidden as in cinematic mode (src/game/cinematic.cpp's 0x0D82D0). Leaving, the
// screen's camera node is forgotten (+0x80), and the next tick moves the camera back to the menu's view. On the touch
// screen (native/touch.cpp) a button shown in the garage toggles it, a drag turns the car and a pinch zooms; on a pad
// the right thumb toggles it and B leaves.

namespace game {

namespace {

constexpr uint32_t kScreenCamera = 0x4C, kScreenCameraNode = 0x80;
constexpr uint32_t kCameraYaw = 0x5F150, kCameraPitch = 0x5F210, kCameraZoom = 0x5F2D0;
constexpr uint32_t kRecentreFrames = 0x4DEC28;
constexpr int64_t kGarageHoldMs = 250;  // the garage's tick ran this lately: its camera is up
constexpr uint16_t kRightThumb = 1u << 7;
constexpr int kAnalogB = 1, kAnalogLT = 6, kAnalogRT = 7;
constexpr float kDeadZone = 0.12f;

std::atomic<int64_t> g_tick_ms{-kGarageHoldMs * 4};
std::atomic<bool> g_on{false};
std::atomic<bool> g_drain{false};  // left by B: the pad stays the camera's till it is let go
std::atomic<float> g_yaw{0}, g_pitch{0}, g_zoom{0};
bool g_moving = false;  // game thread: speeds were given, the camera is to be stopped when they are no more

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// a stick's axis in -1 to 1, its dead zone out and the rest stretched over the range
float Axis(int16_t v) {
    float a = std::clamp(float(v) / 32767.0f, -1.0f, 1.0f);
    if (std::abs(a) < kDeadZone) return 0;
    return (a - (a > 0 ? kDeadZone : -kDeadZone)) / (1 - kDeadZone);
}

bool Held(const input::Pad& pad) {
    constexpr int kRest = 8000;
    auto moved = [](int16_t v) { return v > kRest || v < -kRest; };
    return pad.buttons || moved(pad.lx) || moved(pad.ly) || moved(pad.rx) || moved(pad.ry) ||
           std::any_of(std::begin(pad.analog), std::end(pad.analog), [](uint8_t a) { return a > 0x30; });
}

void Speed(X86Context& ctx, uint32_t fn, uint32_t camera, float value) {
    uint32_t raw;
    std::memcpy(&raw, &value, 4);
    X86Context saved = ctx;
    ctx.ecx = camera;
    kernel::CallGuest(ctx, fn, {raw});
    ctx = saved;
}

void Speeds(X86Context& ctx, uint32_t camera, float yaw, float pitch, float zoom) {
    Speed(ctx, kCameraYaw, camera, yaw);
    Speed(ctx, kCameraPitch, camera, pitch);
    Speed(ctx, kCameraZoom, camera, zoom);
}

void Leave() {
    g_on = false;
    g_yaw = g_pitch = g_zoom = 0;
}

}  // namespace

bool GarageCameraAvailable() { return NowMs() - g_tick_ms.load() < kGarageHoldMs; }

bool GarageCameraActive() { return g_on && GarageCameraAvailable(); }

void ToggleGarageCamera() {
    if (g_on || !GarageCameraAvailable()) Leave();
    else g_on = true;
}

// the pad filter, before the game sees it: true while the pad is the camera's
bool GarageCameraPad(int port, const input::Pad& pad) {
    if (port != 0) return GarageCameraActive();
    static bool thumb_was = false, b_was = false;
    bool thumb = pad.buttons & kRightThumb, b = pad.analog[kAnalogB] > 0x80;
    bool thumb_pressed = thumb && !thumb_was, b_pressed = b && !b_was;
    thumb_was = thumb;
    b_was = b;
    if (!GarageCameraAvailable()) {
        Leave();
        g_drain = false;
        return false;
    }
    if (thumb_pressed) ToggleGarageCamera();
    if (g_on && b_pressed) {
        Leave();
        g_drain = true;
    }
    if (g_on) {
        g_yaw = Axis(pad.rx);
        g_pitch = Axis(pad.ry);
        g_zoom = float(int(pad.analog[kAnalogRT]) - int(pad.analog[kAnalogLT])) / 255.0f;
        return true;
    }
    if (g_drain && !Held(pad)) g_drain = false;
    return g_drain || thumb;
}

// game thread, after HandleJoyEvents (thiscall on the garage's screen)
void GarageCameraTick(X86Context& ctx, uint32_t screen) {
    g_tick_ms = NowMs();
    uint32_t camera = kernel::R32(screen + kScreenCamera);
    if (!camera) return;
    if (!GarageCameraActive()) {
        if (g_moving) {
            Speeds(ctx, camera, 0, 0, 0);
            kernel::W32(screen + kScreenCameraNode, 0);
            g_moving = false;
        }
        return;
    }
    g_moving = true;
    if (int32_t(kernel::R32(kRecentreFrames)) < 2) kernel::W32(kRecentreFrames, 2);
    Speeds(ctx, camera, g_yaw, g_pitch, g_zoom);
}

}  // namespace game
