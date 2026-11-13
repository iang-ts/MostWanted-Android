#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "common/options.h"
#include "core/input.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

// ORBIT CAMERA: the car seen from any side while it is driven about the city (free roam; not in an event, nor under a
// camera the game directs), as a PC's mouse turns the view about it. The player's
// camera mover (the chase camera, the bumper's, a cutscene's) hands its world to view matrix to Camera::SetCameraMatrix
// (0x599E0, thiscall on the player view's camera: the matrix, dt) once a frame, while the race runs (see
// src/game/cinematic.cpp). Orbiting, that matrix is read for the place and way the game's camera looks, and a matrix
// of the same look turned about the point it looks at is handed on instead: the mover keeps following the car with its
// own state untouched, and SetCameraMatrix keeps its keys and velocities consistent from one frame to the next. The
// turn and the distance are the player's: a drag or the right stick turns, a pinch brings the camera in or out; off,
// the game's camera is back as it was. Matrices are row vectors, columns 0 to 2 the right, down and forward axes, row 3
// minus the position along each; world up is +Z, in metres.

namespace game {

namespace {

constexpr uint32_t kViews = 0x48AC80, kViewSize = 0x70, kPlayerView = 1, kCubeView = 0x12, kViewCamera = 0x40;
constexpr uint32_t kCamPosition = 0x40;
constexpr uint32_t kSetCameraMatrix = 0x599E0;
// GRaceStatus (0x48FC50): its play mode at +0x1A2C, 2 roaming the city (its constructor's, 0x181AD0; the pause menu
// makes free roam's icons on it, 0xEEDB7), the other an event. The cameras the game directs itself, a race's or an
// arrival's introduction, a pursuit's cut, are counted at 0x4C8800 (by their mover's constructor, 0x5A130, and the
// NIS), which the game reads to turn the precull off for them (0x23FB7A)
constexpr uint32_t kRaceStatus = 0x48FC50, kPlayMode = 0x1A2C, kRoaming = 2;
constexpr uint32_t kDirectedCameras = 0x4C8800;
constexpr float kLookDownMost = 1.45f, kLookUpMost = 0.25f;  // radians of the camera's pitch: from above, from below
constexpr float kScaleMin = 0.35f, kScaleMax = 3.5f;     // the distance, of the game's camera's
constexpr float kLookMin = 2.0f, kLookMax = 15.0f;        // metres to the point turned about
constexpr float kDragTurn = 3.2f, kDragTilt = 2.2f;       // radians a screen height's drag
constexpr float kStickTurn = 2.4f, kStickTilt = 1.6f;     // radians a second at full stick
constexpr float kDeadZone = 0.16f;

struct Vec {
    float x = 0, y = 0, z = 0;
};
Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec operator*(Vec a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec Cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(Vec a) { return std::sqrt(Dot(a, a)); }
Vec Normal(Vec a) {
    float l = Length(a);
    return l > 1e-6f ? a * (1.0f / l) : Vec{1, 0, 0};
}

float F(uint32_t va) { return kernel::G<float>(va); }
void SetF(uint32_t va, float v) { kernel::G<float>(va) = v; }

std::atomic<bool> g_on{false};
std::atomic<float> g_turn{0}, g_tilt{0}, g_scale{1};  // the player's: about the game's camera
std::atomic<float> g_stick_x{0}, g_stick_y{0};      // the right stick, the pad filter's
uint32_t g_matrix = 0;                              // guest memory for the matrix handed on

uint32_t PlayerCamera() {
    uint32_t cam = kernel::R32(kViews + kPlayerView * kViewSize + kViewCamera);
    return cam && mem::IsCommitted(cam) ? cam : 0;
}

float Stick(int16_t v) {
    float f = std::clamp(float(v) / 32767.0f, -1.0f, 1.0f), a = std::fabs(f);
    if (a < kDeadZone) return 0;
    a = (a - kDeadZone) / (1 - kDeadZone);
    return std::copysign(a * a, f);
}

void Reset() {
    g_turn = 0;
    g_tilt = 0;
    g_scale = 1;
}

// the game's matrix turned about the point it looks at: its place from row 3 and the axes, the point at the distance
// of the car's middle (the reflection cube's camera, drawn about the car) along its forward axis
void Orbit(uint32_t from, float dt) {
    if (!g_matrix) g_matrix = mem::AllocSystem(64);
    if (!g_matrix) return;
    Vec right{F(from + 0x00), F(from + 0x10), F(from + 0x20)};
    Vec down{F(from + 0x04), F(from + 0x14), F(from + 0x24)};
    Vec fwd = Normal({F(from + 0x08), F(from + 0x18), F(from + 0x28)});
    Vec pos = (right * F(from + 0x30) + down * F(from + 0x34) + fwd * F(from + 0x38)) * -1.0f;
    float look = 6.0f;
    if (uint32_t cube = kernel::R32(kViews + kCubeView * kViewSize + kViewCamera);
        cube && mem::IsCommitted(cube + kCamPosition + 12)) {
        Vec car{F(cube + kCamPosition), F(cube + kCamPosition + 4), F(cube + kCamPosition + 8)};
        look = std::clamp(Dot(car - pos, fwd), kLookMin, kLookMax);
    }
    Vec pivot = pos + fwd * look;
    if (dt > 0 && dt < 0.5f) {
        g_turn = g_turn - g_stick_x * kStickTurn * dt;
        g_tilt = g_tilt - g_stick_y * kStickTilt * dt;
    }
    float yaw = std::atan2(fwd.y, fwd.x) + g_turn;
    float base_pitch = std::asin(std::clamp(fwd.z, -1.0f, 1.0f));
    float pitch = std::clamp(base_pitch + g_tilt, -kLookDownMost, kLookUpMost);
    g_tilt = pitch - base_pitch;
    float cp = std::cos(pitch);
    Vec dir{cp * std::cos(yaw), cp * std::sin(yaw), std::sin(pitch)};
    Vec eye = pivot - dir * (look * g_scale);
    Vec r = Normal(Cross(dir, {0, 0, 1}));
    Vec d = Cross(r, dir) * -1.0f;
    const Vec cols[3] = {r, d, dir};
    for (uint32_t row = 0; row < 3; row++) {
        for (uint32_t col = 0; col < 3; col++) {
            const Vec& c = cols[col];
            SetF(g_matrix + row * 16 + col * 4, row == 0 ? c.x : row == 1 ? c.y : c.z);
        }
        SetF(g_matrix + row * 16 + 12, 0);
    }
    SetF(g_matrix + 0x30, -Dot(eye, r));
    SetF(g_matrix + 0x34, -Dot(eye, d));
    SetF(g_matrix + 0x38, -Dot(eye, dir));
    SetF(g_matrix + 0x3C, 1);
}

}  // namespace

// roaming the city, under the player's own camera
bool OrbitCameraAvailable() {
    if (!Racing() || CinematicActive() || ReplayActive()) return false;
    uint32_t status = kernel::R32(kRaceStatus);
    if (!status || !mem::IsCommitted(status + kPlayMode) || kernel::R32(status + kPlayMode) != kRoaming) return false;
    return int32_t(kernel::R32(kDirectedCameras)) <= 0;
}

bool OrbitCameraActive() { return g_on && OrbitCameraAvailable(); }

void ToggleOrbitCamera() {
    bool on = !g_on && OrbitCameraAvailable();
    Reset();
    g_on = on;
}

void OrbitCameraTurn(float dx, float dy) {
    if (!OrbitCameraActive()) return;
    g_turn = g_turn - dx * kDragTurn;
    g_tilt = g_tilt + dy * kDragTilt;
}

void OrbitCameraZoom(float ratio) {
    if (!OrbitCameraActive() || !(ratio > 0)) return;
    g_scale = std::clamp(g_scale / ratio, kScaleMin, kScaleMax);
}

// the pad filter: orbiting, the right stick (the gear lever in a drag race) turns the camera, not the car
void OrbitCameraPad(int port, input::Pad& pad) {
    if (port != 0 || !OrbitCameraActive()) {
        g_stick_x = g_stick_y = 0;
        return;
    }
    g_stick_x = Stick(pad.rx);
    g_stick_y = Stick(pad.ry);
    pad.rx = pad.ry = 0;
}

}  // namespace game

// Camera::SetCameraMatrix (thiscall: the matrix, dt), from the player's camera mover
X86_EXTERN_FUNC(__imp__sub_000599E0);
X86_FUNC(sub_000599E0) {
    if (game::OrbitCameraActive() && ctx.ecx == game::PlayerCamera()) {
        uint32_t from = kernel::R32(ctx.esp + 4);
        float dt = kernel::G<float>(ctx.esp + 8);
        if (from && mem::IsCommitted(from + 0x3C)) {
            game::Orbit(from, dt);
            if (game::g_matrix) kernel::W32(ctx.esp + 4, game::g_matrix);
        }
    }
    __imp__sub_000599E0(ctx, base);
}
