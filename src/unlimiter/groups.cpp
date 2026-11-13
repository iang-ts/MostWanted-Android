#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"
#include "unlimiter/parts.h"

namespace unlimiter {

namespace {

constexpr uint32_t kRimsPackage = 0x46C4F0;
constexpr uint32_t kCustomBrand = 0xFFFFFFFF;
constexpr uint32_t kRimsIcon = 0x294D2A3;
constexpr uint32_t kOwnBrandFirst = kMenuRimBrandExtra, kMaxBrands = kMaxRimBrands, kStockBrands = kStockRimBrands;
constexpr uint32_t kRimsCategory = 0x0B;

struct Brand {
    uint32_t hash, icon, title;
    std::string sort_name;
    bool no_rim_size;
};

std::vector<Brand> LoadBrands() {
    std::vector<Brand> out;
    const Ini& ini = Data("_RimBrands.ini");
    if (ini.Empty()) return out;
    int count = std::min(ini.Int("RimBrands", "NumberOfRimBrands", 0), int(kMaxBrands));
    auto add = [&](const Ini& from, const std::string& section, const std::string& sort_name) {
        if (!from.Has(section, "BrandName")) return;
        std::string name = from.Text(section, "BrandName");
        Brand b{BinHash(name), from.Hash(section, "Texture", kRimsIcon), from.Hash(section, "String", 0), sort_name,
                from.Int(section, "NoRimSize", 0) != 0};
        for (Brand& have : out)
            if (have.hash == b.hash) {
                have = b;
                return;
            }
        out.push_back(b);
    };
    char section[16];
    for (int i = 1; i <= count; i++) {
        std::snprintf(section, sizeof section, "Brand%d", i);
        add(ini, section, ini.Text(section, "BrandName"));
    }
    for (const Ini& file : Folder("RimBrands")) add(file, "Brand", file.Text("Brand", "BrandName"));
    if (ini.Int("RimBrands", "SortAlphabetically", 0))
        std::stable_sort(out.begin(), out.end(), [](const Brand& a, const Brand& b) { return a.sort_name < b.sort_name; });
    if (out.size() > kMaxBrands) out.resize(kMaxBrands);
    if (!out.empty()) LOGR("unlimiter: %zu rim brands", out.size());
    return out;
}

const std::vector<Brand>& Brands() {
    static const std::vector<Brand> brands = LoadBrands();
    return brands;
}

bool g_own_brands = false;

uint32_t BrandMenu(size_t index) {
    return index < kStockBrands ? kMenuRimBrandFirst + uint32_t(index) : kOwnBrandFirst + uint32_t(index - kStockBrands);
}

int BrandOfMenu(uint32_t menu) {
    if (menu >= kMenuRimBrandFirst && menu <= kMenuRimBrandLast) return int(menu - kMenuRimBrandFirst);
    if (menu >= kOwnBrandFirst && menu < kOwnBrandFirst + kMaxBrands - kStockBrands) return int(menu - kOwnBrandFirst + kStockBrands);
    return -1;
}

struct RimOptions {
    std::vector<uint32_t> brands;
    uint32_t first = 0;
};

RimOptions g_rim_options;

void AddRimOption(X86Context& ctx, uint32_t screen, uint32_t icon, uint32_t title, uint32_t menu, uint32_t brand) {
    AddOwnOption(ctx, screen, kernel::R32(kRimsPackage), icon, title, menu);
    g_rim_options.brands.push_back(brand);
}

const Brand* CustomBrandEntry() {
    for (const Brand& b : Brands())
        if (b.hash == kCustomBrand) return &b;
    return nullptr;
}

void AddCustomBrand(X86Context& ctx, uint32_t screen, uint32_t type) {
    const CarConfig& config = Config(type);
    if (!config.parts.rims || !config.parts.rims_custom) return;
    const Brand* entry = CustomBrandEntry();
    static const char* const kText[kLanguageCount] = {"CUSTOM",         "PERSONNALISEES", "EIGENE", "PERSONALIZZATI",
                                                      "PERSONALIZADAS", "Personalizadas", "KUSTOM"};
    const char* fallback_title = "CO_RIMS_CUSTOM";
    uint32_t title = CarTextHash(type, "Names", "PartsRimsCustom", fallback_title);
    if (title == BinHash(fallback_title) && entry && entry->title && entry->title != 0xFFFFFFFF) title = entry->title;
    Label(ctx, title, kText[LanguageIndex(ctx)]);
    uint32_t icon = CarHash(type, "Icons", "PartsRimsCustom", 0xFFFFFFFF);
    if ((icon == 0xFFFFFFFF || !TextureExists(ctx, icon)) && entry) icon = entry->icon;
    if (icon == 0xFFFFFFFF || !TextureExists(ctx, icon)) icon = kRimsIcon;
    AddRimOption(ctx, screen, icon, title, kMenuRimBrandOwn, kCustomBrand);
}

void AddBrands(X86Context& ctx, uint32_t screen) {
    for (size_t i = 0; i < Brands().size(); i++) {
        const Brand& b = Brands()[i];
        if (b.hash == kCustomBrand) continue;
        uint32_t icon = TextureExists(ctx, b.icon) ? b.icon : kRimsIcon;
        AddRimOption(ctx, screen, icon, b.title, BrandMenu(i), b.hash);
    }
}

void AddRimOptions(X86Context& ctx, uint32_t screen) {
    g_rim_options = RimOptions{};
    uint32_t type = CurrentCarType(ctx);
    if (type == 0xFFFFFFFF) return;
    bool own = !Brands().empty();
    g_rim_options.first = own ? 2 : 2 + kStockBrands;
    AddCustomBrand(ctx, screen, type);
    if (own) AddBrands(ctx, screen);
}

bool OwnBrandMenu(uint32_t menu) {
    return menu == kMenuRimBrandOwn || BrandOfMenu(menu) >= int(kStockBrands);
}

}  // namespace

bool OwnRimBrands() { return g_own_brands; }

// a brand that lists every rim size together (NoRimSize): the car's own rims unless _RimBrands.ini's Brand0 or the
// custom brand's file says otherwise
bool NoRimSize(uint32_t menu) {
    if (!Active() || !Ready()) return false;
    if (menu == kMenuRimBrandOwn) {
        if (const Brand* entry = CustomBrandEntry()) return entry->no_rim_size;
        return Data("_RimBrands.ini").Int("Brand0", "NoRimSize", 1) != 0;
    }
    int index = Brands().empty() ? -1 : BrandOfMenu(menu);
    return index >= 0 && size_t(index) < Brands().size() && Brands()[size_t(index)].no_rim_size;
}

}  // namespace unlimiter

X86_EXTERN_FUNC(__imp__sub_002AB8B0);
X86_FUNC(sub_002AB8B0) {
    uint32_t screen = ctx.ecx;
    bool active = unlimiter::Active() && unlimiter::Ready();
    unlimiter::g_own_brands = active && !unlimiter::Brands().empty();
    unlimiter::g_rim_options = unlimiter::RimOptions{};
    if (active) unlimiter::BeginOptions(screen);
    __imp__sub_002AB8B0(ctx, base);
    unlimiter::g_own_brands = false;
    if (!active) return;
    unlimiter::EndOptions(screen);
    uint32_t previous = kernel::R32(screen + unlimiter::kScreenPreviousMenu);
    bool stock = previous >= unlimiter::kMenuRimBrandFirst - 1 && previous <= unlimiter::kMenuRimBrandLast;
    if (stock || unlimiter::OwnBrandMenu(previous))
        kernel::W32(screen + unlimiter::kScreenPreviousMenu, unlimiter::kMenuCategoryParts);
}

void UnlimiterAddRimBrands(X86Context& ctx, uint8_t*) {
    if (!unlimiter::Active() || !unlimiter::Ready()) return;
    unlimiter::AddRimOptions(ctx, ctx.esi);
    unlimiter::PlaceInitial(ctx.esi);
}

X86_EXTERN_FUNC(__imp__sub_00297BA0);
X86_FUNC(sub_00297BA0) {
    const unlimiter::RimOptions& o = unlimiter::g_rim_options;
    if (unlimiter::Active() && o.first) {
        uint32_t brand = kernel::R32(ctx.esp + 4);
        for (size_t i = 0; i < o.brands.size(); i++)
            if (o.brands[i] == brand) {
                ctx.eax = o.first + uint32_t(i);
                ctx.esp += 4 + 4;
                return;
            }
        if (!unlimiter::Brands().empty()) {
            ctx.eax = 1;
            ctx.esp += 4 + 4;
            return;
        }
    }
    __imp__sub_00297BA0(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_002983B0);
X86_FUNC(sub_002983B0) {
    if (unlimiter::Active()) {
        uint32_t menu = kernel::R32(ctx.ecx + unlimiter::kScreenMenu);
        if (menu == unlimiter::kMenuRimBrandOwn) {
            ctx.eax = unlimiter::kCustomBrand;
            ctx.esp += 4;
            return;
        }
        int index = unlimiter::Brands().empty() ? -1 : unlimiter::BrandOfMenu(menu);
        if (index >= 0 && size_t(index) < unlimiter::Brands().size()) {
            ctx.eax = unlimiter::Brands()[size_t(index)].hash;
            ctx.esp += 4;
            return;
        }
    }
    __imp__sub_002983B0(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_00296FA0);
X86_FUNC(sub_00296FA0) {
    uint32_t menu = kernel::R32(ctx.esp + 4);
    if (unlimiter::Active() && (menu == unlimiter::kMenuRimBrandOwn || unlimiter::BrandOfMenu(menu) >= int(unlimiter::kStockBrands))) {
        ctx.eax = unlimiter::kRimsCategory;
        ctx.esp += 4;
        return;
    }
    if (unlimiter::Active() && (menu == unlimiter::kMenuVinylCustom ||
                               (menu >= unlimiter::kMenuVinylExtra && menu < unlimiter::kMenuVinylExtra + unlimiter::kMaxVinylGroups))) {
        ctx.eax = unlimiter::kVinylCategory;
        ctx.esp += 4;
        return;
    }
    __imp__sub_00296FA0(ctx, base);
}
