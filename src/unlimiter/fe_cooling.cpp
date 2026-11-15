#include <cstring>

#include <atomic>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"
#include "unlimiter/parts.h"

// [FECooling] over CarCustomizeManager::UpdateHeatOnVehicle (0x29F190, the PC's 0x7AF330): how much heat a part takes
// off when the career applies it. A slot left at 0 falls back to the function itself, which reads the fecooling the
// VLT holds, so only the slots the ini names are ours; the slots the Unlimiter adds have nothing in the VLT and carry
// their own fallbacks instead.

namespace unlimiter {

namespace {

constexpr uint32_t kRecordHeat = 0x0C;

bool InCareer() {
    if (kernel::G<uint8_t>(kTestCareerCustomization)) return true;
    uint32_t database = kernel::R32(kFeDatabase);
    return database && mem::IsCommitted(database + kFeDatabaseCareer) &&
           (kernel::G<uint8_t>(database + kFeDatabaseCareer) & 1);
}

bool InBackRoom(X86Context& ctx) {
    X86Context saved = ctx;
    uint32_t in = kernel::CallGuest(ctx, kCustomizeInBackRoom, {});
    ctx = saved;
    return (in & 0xFF) != 0;
}

}  // namespace

// 0 when the slot keeps the VLT's own cooling, which leaves the call to the game
float Cooling(X86Context& ctx, uint32_t part) {
    if (!Active() || !Ready() || !part || !mem::IsCommitted(part + kPartSlotId)) return 0.0f;
    if (kernel::G<uint8_t>(part + kPartIsPerformance)) return 0.0f;
    if (!InCareer()) return 0.0f;
    uint32_t slot = kernel::R32(part + kPartSlotId);
    if (slot >= kSlotCount) return 0.0f;
    uint32_t type = CurrentCarType(ctx);
    if (type >= CarTypeCount()) return 0.0f;
    return Config(type).cooling[slot];
}

}  // namespace unlimiter

X86_EXTERN_FUNC(__imp__sub_0029F190);
X86_FUNC(sub_0029F190) {
    uint32_t part = kernel::R32(ctx.esp + 4), record = kernel::R32(ctx.esp + 8);
    float cooling = record ? unlimiter::Cooling(ctx, part) : 0.0f;
    if (cooling == 0.0f) return __imp__sub_0029F190(ctx, base);
    cooling *= unlimiter::InBackRoom(ctx) ? 0.75f : 1.0f;
    float heat;
    std::memcpy(&heat, kernel::HostPtr(record + unlimiter::kRecordHeat), sizeof heat);
    heat *= cooling;
    std::memcpy(kernel::HostPtr(record + unlimiter::kRecordHeat), &heat, sizeof heat);
    UNLIMITER_NOTE(8, "unlimiter: fecooling slot %u x%.2f", kernel::R32(part + unlimiter::kPartSlotId), cooling);
    ctx.esp += 4 + 8;  // the part and the record
}
