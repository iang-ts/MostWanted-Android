#include <string>
#include <vector>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "unlimiter/config.h"

// _FNGFixes.ini clones the last numbered object of a package until NumberOfObjects, the same way the PC does it
// on FEPackage::Startup. The object's name is at +0x10, its links at +4 and +8, as FEPackage::FindObject walks them.

namespace unlimiter {

namespace {

constexpr uint32_t kFindObject = 0x15CA70;
constexpr uint32_t kNext = 0x4, kPrev = 0x8, kName = 0x10, kCached = 0x58, kCount = 0x48;

struct Fix {
    std::string package, prefix;
    int count = 0;
};

const std::vector<Fix>& Fixes() {
    static const std::vector<Fix> fixes = [] {
        std::vector<Fix> out;
        const Ini& ini = Data("_FNGFixes.ini");
        int count = ini.Int("FNGFixes", "NumberOfFNGFixes", 0);
        for (int i = 1; i <= count; i++) {
            std::string section = "FNG" + std::to_string(i);
            Fix fix;
            fix.package = ini.Text(section, "FNGName");
            fix.prefix = ini.Text(section, "ObjectPrefix");
            fix.count = ini.Int(section, "NumberOfObjects", 0);
            if (!fix.package.empty() && !fix.prefix.empty() && fix.count > 1) out.push_back(fix);
        }
        return out;
    }();
    return fixes;
}

uint32_t Find(X86Context& ctx, uint32_t package, uint32_t hash) {
    X86Context saved = ctx;
    ctx.ecx = package;
    uint32_t object = kernel::CallGuest(ctx, kFindObject, {hash});
    ctx = saved;
    return object;
}

void Clone(X86Context& ctx, uint32_t package, const Fix& fix) {
    int have = 0;
    uint32_t last = 0;
    for (int i = 1; i <= fix.count; i++) {
        uint32_t found = Find(ctx, package, BinHash(fix.prefix + std::to_string(i)));
        if (!found) break;
        last = found;
        have = i;
    }
    if (!last || have >= fix.count) return;
    for (int i = have + 1; i <= fix.count; i++) {
        uint32_t vtbl = kernel::R32(last);
        if (!vtbl) return;
        X86Context saved = ctx;
        ctx.ecx = last;
        uint32_t copy = kernel::CallGuest(ctx, kernel::R32(vtbl + 4), {0});
        ctx = saved;
        if (!copy) return;
        kernel::W32(copy + kName, BinHash(fix.prefix + std::to_string(i)));
        kernel::W32(copy + kCached, 0);
        kernel::W32(copy + kPrev, last);
        kernel::W32(copy + kNext, kernel::R32(last + kNext));
        kernel::W32(last + kNext, copy);
        kernel::W32(package + kCount, kernel::R32(package + kCount) + 1);
        last = copy;
    }
    LOGR("unlimiter: %s in %s cloned up to %d", fix.prefix.c_str(), fix.package.c_str(), fix.count);
}

}  // namespace

void ClonePackageObjects(X86Context& ctx, uint32_t package, const char* name) {
    if (!Active() || !Global().fng_fix || !package || !name || Fixes().empty()) return;
    static std::vector<uint32_t> done;
    for (uint32_t seen : done)
        if (seen == package) return;
    bool any = false;
    for (const Fix& fix : Fixes()) {
        if (fix.package != name) continue;
        Clone(ctx, package, fix);
        any = true;
    }
    if (any) done.push_back(package);
}

}  // namespace unlimiter

// cFEng::FindPackage (thiscall: the name). A fix for that package clones its numbered objects once
X86_EXTERN_FUNC(__imp__sub_000E1B10);
X86_FUNC(sub_000E1B10) {
    uint32_t name = kernel::R32(ctx.esp + 4);
    __imp__sub_000E1B10(ctx, base);
    if (!ctx.eax || !name || !mem::IsCommitted(name)) return;
    unlimiter::ClonePackageObjects(ctx, ctx.eax, static_cast<const char*>(kernel::HostPtr(name)));
}
