#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "unlimiter/config.h"

namespace unlimiter {

namespace {

constexpr uint32_t kCarRecordFrontend = 0x04;

const std::map<uint32_t, uint32_t>& Makes() {
    static const std::map<uint32_t, uint32_t> makes = [] {
        std::map<uint32_t, uint32_t> out;
        std::vector<uint8_t> file;
        if (!kernel::ReadGameFile("D:\\makes.txt", file)) return out;
        std::string text(file.begin(), file.end());
        std::map<std::string, uint32_t> strings;
        for (size_t at = 0; at < text.size();) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            uint32_t key;
            char make[32] = {};
            if (std::sscanf(text.substr(at, end - at).c_str(), "%x %31s", &key, make) == 2 && make[0]) {
                uint32_t& guest = strings[make];
                if (!guest) {
                    size_t n = std::strlen(make) + 1;
                    guest = mem::AllocSystem(uint32_t((n + 15) & ~size_t(15)));
                    std::memcpy(kernel::HostPtr(guest), make, n);
                }
                out[key] = guest;
            }
            at = end + 1;
        }
        LOGR("unlimiter: %zu add-on makes", out.size());
        return out;
    }();
    return makes;
}

bool ManufacturerHook() { return !Active() || Global().manufacturer_hook; }

}  // namespace

}  // namespace unlimiter

X86_EXTERN_FUNC(__imp__sub_00134250);
X86_FUNC(sub_00134250) {
    if (unlimiter::ManufacturerHook()) {
        uint32_t key = kernel::R32(ctx.ecx + unlimiter::kCarRecordFrontend);
        const auto& makes = unlimiter::Makes();
        auto it = makes.find(key);
        if (it != makes.end()) {
            ctx.eax = it->second;
            ctx.esp += 4;
            return;
        }
    }
    __imp__sub_00134250(ctx, base);
}
