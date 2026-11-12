#include <cstdio>
#include <random>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "unlimiter/config.h"

namespace unlimiter {

namespace {

constexpr uint32_t kDoesStringExist = 0x124BF0;
constexpr uint32_t kStockRacerNames = 150, kMaxRacerNames = 1000;

uint32_t RacerNames(X86Context& ctx) {
    static uint32_t count = 0;
    if (count) return count;
    X86Context saved = ctx;
    char name[24];
    uint32_t n = 0;
    while (n < kMaxRacerNames) {
        std::snprintf(name, sizeof name, "RACERNAME_%03u", n);
        if (!(kernel::CallGuest(ctx, kDoesStringExist, {BinHash(name)}) & 0xFF)) break;
        n++;
    }
    ctx = saved;
    count = n < kStockRacerNames ? kStockRacerNames : n;
    LOGR("unlimiter: %u racer names", count);
    return count;
}

}  // namespace

}  // namespace unlimiter

void UnlimiterRacerName(X86Context& ctx, uint8_t*) {
    if (!unlimiter::Active()) return;
    uint32_t count = unlimiter::RacerNames(ctx);
    if (count <= unlimiter::kStockRacerNames) return;
    static std::mt19937 random{std::random_device{}()};
    ctx.eax = uint32_t(random() % count);
}
