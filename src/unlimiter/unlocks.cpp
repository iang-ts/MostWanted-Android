#include <atomic>

#include "core/log.h"
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
constexpr uint32_t kFilterAvailable = 0xF0000, kFilterStockOrMine = 0x7, kFilterBonus = 0x8;
constexpr int kBlacklistSize = 17;

uint8_t Byte(uint32_t va) { return *static_cast<uint8_t*>(kernel::HostPtr(va)); }

uint32_t Profile() {
    uint32_t db = kernel::R32(kFeDatabase);
    return db ? kernel::R32(db + kProfiles) : 0;
}

// the Unlimiter's seven unlock conditions, which a part spells UNLOCK_TYPE and UNLOCK_AT and a bonus car spells
// UnlockConditionType and Blacklist: 1 met, 0 not met, and -1 for a type the ini does not give, which leaves the
// answer to the game. The profile's own career state is the whole of it, so one reading serves both
int UnlockedWhen(int type, int bin) {
    if (type < 0 || type > 6) return -1;
    if (type == 0) return 1;
    uint32_t profile = Profile();
    if (!profile) return -1;
    uint32_t flags = kernel::R32(profile + kProfileFlags);
    switch (type) {
        case 1: return int(Byte(profile + kProfileBin)) < bin ? 1 : 0;
        case 2: return (flags & kFlagEvent68) ? 1 : 0;
        case 3: return (flags & kFlagCastrol) ? 1 : 0;
        case 4: return (Byte(profile + kProfileChallenge) & 1) ? 1 : 0;
        default: return (flags & kFlagCareerDone) ? 1 : 0;
    }
}

int UnlockedBy(uint32_t part) {
    int bin = PartInt(part, BinHash("UNLOCK_AT"), kBlacklistSize);
    if (bin < 2 || bin > kBlacklistSize) bin = kBlacklistSize;
    return UnlockedWhen(PartInt(part, BinHash("UNLOCK_TYPE"), -1), bin);
}

// a bonus car's own rule. CEOnly is read and has nothing to gate on: FEPlayerCarDB::Default (0x140890) hashes every
// preset name and sends the four Collectors Edition ones (0x54655133, 0x3D8A6D1, 0x2D642B8, 0x3D3401A) to the same
// 0x1409B1 as the ordinary bonus cars, with no test of an edition anywhere, so this build has no locked half of a
// pair for the PC's _IsCollectorsEdition to pick
int BonusUnlocked(uint32_t handle) {
    const BonusCar* car = BonusCarOf(handle);
    if (!car) return -1;
    int own = UnlockedWhen(car->condition, car->blacklist);
    UNLIMITER_NOTE(8, "unlimiter: bonus car %s condition %d bin %d -> %s", car->preset.c_str(), car->condition,
                   car->blacklist, own < 0 ? "game's" : own ? "unlocked" : "locked");
    return own;
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

// QuickRaceUnlocker::IsCarUnlocked, cdecl: the unlock filters, the record's handle and which profile. A record the
// front end may show at all carries 0xF0000; the low bits say what it is, and the PC reads them in this order, stock,
// career or my cars before a bonus car, since a record can wear both
X86_EXTERN_FUNC(__imp__sub_0013B2F0);
X86_FUNC(sub_0013B2F0) {
    uint32_t handle = kernel::R32(ctx.esp + 8), which = kernel::R32(ctx.esp + 12);
    __imp__sub_0013B2F0(ctx, base);
    if (!unlimiter::Active() || !unlimiter::Ready()) return;
    uint32_t record = unlimiter::CarRecord(handle, which);
    if (!record) return;
    uint32_t filter = kernel::R32(record + unlimiter::kEntryFilter);
    if (!(filter & unlimiter::kFilterAvailable)) return;
    if (filter & unlimiter::kFilterStockOrMine) {
        if (ctx.eax & 0xFF) return;
        X86Context saved = ctx;
        ctx.ecx = record;
        uint32_t type = kernel::CallGuest(ctx, unlimiter::kCarRecordGetType, {});
        ctx = saved;
        if (unlimiter::Config(type).main.initially_unlocked == 1) ctx.eax = (ctx.eax & ~0xFFu) | 1u;
        return;
    }
    // the one place the port overrules the game downwards as well as up: a bonus car the ini gives a condition is
    // locked until that condition is met, UnlockAllThings included, which is what the PC's own unlocker does
    if (!(filter & unlimiter::kFilterBonus)) return;
    int own = unlimiter::BonusUnlocked(handle);
    if (own >= 0) ctx.eax = (ctx.eax & ~0xFFu) | uint32_t(own);
}

// UIQRCarSelect::GetBonusUnlockText (stdcall, ret 0x04, the PC's hook at 0x7ADD80): the record, and a string hash for
// why the car is still locked. The game picks it from a list of the preset handles it ships with and answers 0 for one
// it does not know, which is every add-on bonus car. Its only caller (0x29D9F7, in RefreshHeader 0x29D6A0) has already
// checked the 0xF0008 category, the same one the ini's entries must carry to be found here at all
X86_EXTERN_FUNC(__imp__sub_00295620);
X86_FUNC(sub_00295620) {
    uint32_t record = kernel::R32(ctx.esp + 4);
    __imp__sub_00295620(ctx, base);
    if (!unlimiter::Active() || !unlimiter::Ready() || !record) return;
    const unlimiter::BonusCar* car = unlimiter::BonusCarOf(kernel::R32(record));
    if (!car) return;
    // a string of the pack's own first, as long as the language file really has it
    if (car->unlock_text != 0xFFFFFFFF && unlimiter::StringExists(ctx, car->unlock_text)) {
        ctx.eax = car->unlock_text;
        return;
    }
    // CARLOT_UNLOCK_MSG_01 for the blacklist, which is the one the caller spots to fill a rival's name in, and
    // CARLOT_BONUSCAR_UNLOCK_MSG_01 to _04 and _06 for the rest. _05 is the Collectors Edition line, and its hash
    // 0xBD8BAC95 is referred to nowhere in this image, matching a build with no edition test
    static constexpr uint32_t kText[7] = {0xBD8BAC96, 0x4EF2A115, 0xBD8BAC91, 0xBD8BAC97,
                                          0xBD8BAC92, 0xBD8BAC93, 0xBD8BAC94};
    if (car->condition < 0 || car->condition > 6) return;
    // the game itself only ever pushes _01 to _04, so _06 and _07 may be in no language file on this disc: a line
    // that is not there would print as a missing string, and the game's own answer reads better than that
    uint32_t text = kText[car->condition];
    if (unlimiter::StringExists(ctx, text)) ctx.eax = text;
}

// UIQRCarSelect::GetBonusUnlockBinNumber (stdcall, ret 0x04, the PC's hook at 0x7ADD8D): the blacklist member the line
// above names. The caller only asks when the text came back CARLOT_UNLOCK_MSG_01, so only condition 1 can answer
X86_EXTERN_FUNC(__imp__sub_002956A0);
X86_FUNC(sub_002956A0) {
    uint32_t record = kernel::R32(ctx.esp + 4);
    __imp__sub_002956A0(ctx, base);
    if (!unlimiter::Active() || !unlimiter::Ready() || !record) return;
    const unlimiter::BonusCar* car = unlimiter::BonusCarOf(kernel::R32(record));
    if (car && car->condition == 1) ctx.eax = uint32_t(car->blacklist);
}
