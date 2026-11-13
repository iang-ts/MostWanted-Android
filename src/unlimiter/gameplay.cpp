#include <cstdio>
#include <random>
#include <string>
#include <vector>

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

struct CopLine {
    uint32_t vehicle = 0, text = 0;
};

const std::vector<CopLine>& CopLines() {
    static const std::vector<CopLine> lines = [] {
        std::vector<CopLine> out;
        const Ini& ini = Data("_CopDestroyedStrings.ini");
        int count = ini.Int("CopDestroyedStrings", "NumberOfCopDestroyedStrings", -1);
        if (count < 0) return out;
        for (int i = 0; i <= count; i++) {
            std::string section = "String" + std::to_string(i);
            std::string name = ini.Text(section, "PVehicle");
            uint32_t text = ini.Hash(section, "String", 0);
            if (!text) continue;
            out.push_back({name.empty() ? 0 : VltHash(name), text});
        }
        LOGR("unlimiter: %zu cop destroyed strings", out.size());
        return out;
    }();
    return lines;
}

uint32_t g_cop_vehicle = 0;

}  // namespace

uint32_t CopPhrase() {
    if (!Active() || !Global().cop_destroyed_strings) return 0;
    uint32_t fallback = 0, vehicle = g_cop_vehicle;
    bool any = false;
    for (const CopLine& line : CopLines()) {
        any = true;
        if (!line.vehicle) fallback = line.text;
        else if (vehicle && line.vehicle == vehicle) return line.text;
    }
    return any ? fallback : 0;
}

void NoteCopVehicle(uint32_t vehicle) { g_cop_vehicle = vehicle; }

}  // namespace unlimiter

// PursuitBoard::SetNumCopsDestroyed (thiscall: the new count, the cop's hash, the combo, the bounty). The phrase is
// chosen inside, and GetLocalizedString's caller 0x144121 is where the ini's string replaces the disc's
X86_EXTERN_FUNC(__imp__sub_00143FD0);
X86_FUNC(sub_00143FD0) {
    bool ours = unlimiter::Active() && unlimiter::Global().cop_destroyed_strings;
    if (ours) unlimiter::NoteCopVehicle(kernel::R32(ctx.esp + 8));
    __imp__sub_00143FD0(ctx, base);
    if (ours) unlimiter::NoteCopVehicle(0);
}

void UnlimiterRacerName(X86Context& ctx, uint8_t*) {
    if (!unlimiter::Active()) return;
    uint32_t count = unlimiter::RacerNames(ctx);
    if (count <= unlimiter::kStockRacerNames) return;
    static std::mt19937 random{std::random_device{}()};
    ctx.eax = uint32_t(random() % count);
}
