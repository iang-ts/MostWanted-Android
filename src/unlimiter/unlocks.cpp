#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"
#include "unlimiter/parts.h"

namespace unlimiter {

namespace {

constexpr uint32_t kUnlockAllThings = 0x493D00;
constexpr uint32_t kFeDatabase = 0x48EDD4, kProfiles = 0x10, kPlayerCarDb = 0x414;
constexpr uint32_t kCarEntry = 0x14, kCarEntries = 0xC8, kEntryFilter = 0x0C;
constexpr uint32_t kProfileFlags = 0xAC, kProfileChallenge = 0xAE, kProfileBin = 0xB0;
constexpr uint32_t kFlagCareerDone = 0x4000, kFlagEvent68 = 0x8000, kFlagCastrol = 0x40000;
constexpr uint32_t kFilterAvailable = 0xF0000, kFilterStockOrMine = 0x7;
constexpr int kBlacklistSize = 17;

uint8_t Byte(uint32_t va) { return *static_cast<uint8_t*>(kernel::HostPtr(va)); }

uint32_t Profile() {
    uint32_t db = kernel::R32(kFeDatabase);
    return db ? kernel::R32(db + kProfiles) : 0;
}

int UnlockedBy(uint32_t part) {
    int type = PartInt(part, BinHash("UNLOCK_TYPE"), -1);
    if (type < 0 || type > 6) return -1;
    if (type == 0) return 1;
    uint32_t profile = Profile();
    if (!profile) return -1;
    uint32_t flags = kernel::R32(profile + kProfileFlags);
    switch (type) {
        case 1: {
            int bin = PartInt(part, BinHash("UNLOCK_AT"), kBlacklistSize);
            if (bin < 2 || bin > kBlacklistSize) bin = kBlacklistSize;
            return Byte(profile + kProfileBin) < bin ? 1 : 0;
        }
        case 2: return (flags & kFlagEvent68) ? 1 : 0;
        case 3: return (flags & kFlagCastrol) ? 1 : 0;
        case 4: return (Byte(profile + kProfileChallenge) & 1) ? 1 : 0;
        default: return (flags & kFlagCareerDone) ? 1 : 0;
    }
}

uint32_t CarRecord(uint32_t handle, uint32_t which) {
    if (which >= 2) return 0;
    uint32_t db = kernel::R32(kFeDatabase);
    uint32_t profile = db ? kernel::R32(db + kProfiles + which * 4) : 0;
    if (!profile) return 0;
    uint32_t cars = profile + kPlayerCarDb;
    for (uint32_t i = 0; i < kCarEntries; i++)
        if (kernel::R32(cars + i * kCarEntry) == handle) return cars + i * kCarEntry;
    return 0;
}

}  // namespace

}  // namespace unlimiter

X86_EXTERN_FUNC(__imp__sub_0013BEC0);
X86_FUNC(sub_0013BEC0) {
    uint32_t part = kernel::R32(ctx.esp + 12);
    __imp__sub_0013BEC0(ctx, base);
    if (!unlimiter::Active() || !part) return;
    ctx.eax = uint32_t(unlimiter::PartInt(part, unlimiter::BinHash("COST"), int(ctx.eax)));
}

X86_EXTERN_FUNC(__imp__sub_0013BA90);
X86_FUNC(sub_0013BA90) {
    uint32_t part = kernel::R32(ctx.esp + 12), backroom = kernel::R32(ctx.esp + 20) & 0xFF;
    if (unlimiter::Active() && part && !backroom && !unlimiter::Byte(unlimiter::kUnlockAllThings)) {
        int unlocked = unlimiter::UnlockedBy(part);
        if (unlocked >= 0) {
            ctx.eax = (ctx.eax & ~0xFFu) | uint32_t(unlocked);
            ctx.esp += 4;
            return;
        }
    }
    __imp__sub_0013BA90(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_0013B2F0);
X86_FUNC(sub_0013B2F0) {
    uint32_t handle = kernel::R32(ctx.esp + 8), which = kernel::R32(ctx.esp + 12);
    __imp__sub_0013B2F0(ctx, base);
    if (!unlimiter::Active() || !unlimiter::Ready() || (ctx.eax & 0xFF)) return;
    uint32_t record = unlimiter::CarRecord(handle, which);
    if (!record) return;
    uint32_t filter = kernel::R32(record + unlimiter::kEntryFilter);
    if (!(filter & unlimiter::kFilterAvailable) || !(filter & unlimiter::kFilterStockOrMine)) return;
    X86Context saved = ctx;
    ctx.ecx = record;
    uint32_t type = kernel::CallGuest(ctx, unlimiter::kCarRecordGetType, {});
    ctx = saved;
    if (unlimiter::Config(type).main.initially_unlocked == 1) ctx.eax = (ctx.eax & ~0xFFu) | 1u;
}
