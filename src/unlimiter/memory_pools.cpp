#include "core/log.h"
#include "kernel/kernel.h"
#include "unlimiter/config.h"

namespace unlimiter {

namespace {

constexpr uint32_t kFEngPoolSize = 0x4322F8;
constexpr uint32_t kFEngPoolExpanded = 0xBE6E0;

}  // namespace

void ExpandPools() {
    if (!Active() || !Global().expand_memory_pools) return;
    if (kernel::R32(kFEngPoolSize) < kFEngPoolExpanded) kernel::W32(kFEngPoolSize, kFEngPoolExpanded);
    LOGR("unlimiter: FEng pool %u KB", kernel::R32(kFEngPoolSize) >> 10);
}

}  // namespace unlimiter
