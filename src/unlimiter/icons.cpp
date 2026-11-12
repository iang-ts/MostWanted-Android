#include <cstdint>
#include <string>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "unlimiter/config.h"

namespace unlimiter {

constexpr uint32_t kFEng = 0x48E94C;
constexpr uint32_t kFindPackage = 0xE1B10;
constexpr uint32_t kFindObject = 0x15CA70;
constexpr uint32_t kScrollerPackage = 0x18;
constexpr uint32_t kScrollerPrefix = 0x1C;
constexpr uint32_t kScrollerIndex = 0x24;
constexpr uint32_t kObjectNext = 0x04;
constexpr uint32_t kObjectPrev = 0x08;
constexpr uint32_t kObjectName = 0x10;
constexpr uint32_t kObjectPlace = 0x2C;
constexpr uint32_t kObjectPlaceSize = 0x30;
constexpr uint32_t kObjectCached = 0x58;
constexpr uint32_t kUnlinked = 0xABADCAFE;
constexpr int kMaxSlot = 256;

uint32_t FngHash(const std::string& text) {
    uint32_t h = 0xFFFFFFFF;
    for (unsigned char c : text) {
        if (c >= 'a' && c <= 'z') c -= 32;
        h = h * 33 + c;
    }
    return h;
}

std::string Text(uint32_t va) {
    std::string out;
    if (!va) return out;
    for (int i = 0; i < 32; i++) {
        char c = *static_cast<char*>(kernel::HostPtr(va + uint32_t(i)));
        if (!c) break;
        out += c;
    }
    return out;
}

uint32_t Object(X86Context& ctx, uint32_t package_name, const std::string& prefix, int index) {
    char name[40];
    std::snprintf(name, sizeof name, "%s%d", prefix.c_str(), index);
    ctx.ecx = kernel::R32(kFEng);
    uint32_t package = kernel::CallGuest(ctx, kFindPackage, {package_name});
    if (!package) return 0;
    ctx.ecx = package;
    return kernel::CallGuest(ctx, kFindObject, {FngHash(name)});
}

void Place(uint32_t dest, uint32_t cur, uint32_t prev, int steps) {
    uint32_t db = kernel::R32(dest + kObjectPlace), cb = kernel::R32(cur + kObjectPlace);
    uint32_t n = kernel::R32(dest + kObjectPlaceSize);
    if (!db || !cb || !n || n > 128 || (n & 3) || n != kernel::R32(cur + kObjectPlaceSize)) return;
    auto* d = static_cast<float*>(kernel::HostPtr(db));
    auto* c = static_cast<float*>(kernel::HostPtr(cb));
    float* p = nullptr;
    if (prev && kernel::R32(prev + kObjectPlaceSize) == n && kernel::R32(prev + kObjectPlace))
        p = static_cast<float*>(kernel::HostPtr(kernel::R32(prev + kObjectPlace)));
    for (uint32_t i = 0; i < n / 4; i++) d[i] = p ? c[i] + (c[i] - p[i]) * float(steps) : c[i];
}

uint32_t Clone(X86Context& ctx, uint32_t target, uint32_t hash) {
    uint32_t fn = kernel::R32(kernel::R32(target) + 4);
    ctx.ecx = target;
    uint32_t dest = kernel::CallGuest(ctx, fn, {0});
    if (!dest) return 0;
    kernel::W32(dest + kObjectName, hash);
    kernel::W32(dest + kObjectCached, 0);
    uint32_t next = kernel::R32(target + kObjectNext);
    kernel::W32(dest + kObjectNext, next);
    kernel::W32(dest + kObjectPrev, target);
    kernel::W32(target + kObjectNext, dest);
    if (next && next != kUnlinked) kernel::W32(next + kObjectPrev, dest);
    return dest;
}

}  // namespace unlimiter

void UnlimiterIconSlot(X86Context& ctx, uint8_t*) {
    if (ctx.ebx || !unlimiter::Active()) return;
    if (!kernel::R32(ctx.ebp + 8)) return;
    uint32_t scroller = ctx.esi;
    int slot = int(kernel::R32(scroller + unlimiter::kScrollerIndex));
    uint32_t package_name = kernel::R32(scroller + unlimiter::kScrollerPackage);
    std::string prefix = unlimiter::Text(kernel::R32(scroller + unlimiter::kScrollerPrefix));
    if (!package_name || prefix.empty() || slot < 1 || slot > unlimiter::kMaxSlot) return;

    X86Context saved = ctx;
    uint32_t cur = 0, prev = 0;
    int cur_i = 0;
    for (int i = slot - 1; i >= 1 && !prev; i--) {
        uint32_t found = unlimiter::Object(saved, package_name, prefix, i);
        if (!found) continue;
        if (!cur) {
            cur = found;
            cur_i = i;
        } else {
            prev = found;
        }
    }
    if (!cur) return;

    char name[40];
    std::snprintf(name, sizeof name, "%s%d", prefix.c_str(), slot);
    uint32_t dest = unlimiter::Clone(saved, cur, unlimiter::FngHash(name));
    if (!dest) return;
    unlimiter::Place(dest, cur, prev, slot - cur_i);
    ctx.ebx = dest;
    LOGR("unlimiter: menu slot %s", name);
}
