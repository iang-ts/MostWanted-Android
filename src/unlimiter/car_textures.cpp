#include <array>
#include <string>

#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"
#include "unlimiter/parts.h"

namespace unlimiter {

namespace {

const uint32_t kTextureName = BinHash("TEXTURE_NAME");

uint32_t TextureOf(uint32_t part) {
    uint32_t name;
    return part && PartAttribute(part, kTextureName, name) && name != 0xFFFFFFFF ? name : 0;
}

uint32_t StyledLight(uint32_t ride, uint32_t slot) { return TextureOf(PartAt(ride, slot)) ? PartAt(ride, slot) : 0; }

bool Replacing() { return Active() && Ready() && !Global().disable_texture_replacement; }

// The body's replacements 47 to 72 are the decals' leftovers the game never draws, which the Unlimiter takes for its
// own (the PC's 0x74DA9F jump over CarRenderInfo::UpdateDecalTextures' loop): the interior, the plate, the tires and
// the attachments, from 49; 47 and 48 are the reverse lights'
constexpr int kFirstFree = 47, kInterior = 49, kLicensePlate = 51, kTire = 53, kFirstAttachment = 55;
constexpr uint32_t kLicensePlateTexture = 0x4C95C6F8, kTireTexture = 0xE4CEA024;  // LICENSEPLATE, TIRE_STYLE01

// as the PC's ReplacementTextureTable_WriteEntry: a changed entry is looked up again at its next draw
void WriteEntry(uint32_t table, int index, uint32_t from, uint32_t to) {
    if (index < 0 || uint32_t(index) >= kReplacementCount) return;
    uint32_t e = table + uint32_t(index) * kReplacementSize;
    if (kernel::R32(e + 4) == to) return;
    kernel::W32(e, from);
    kernel::W32(e + 4, to);
    kernel::W32(e + 8, 0xFFFFFFFF);
}

// the texture and its normal map (_N), as the PC's bStringHash2("_N", hash)
void WritePair(uint32_t table, int index, uint32_t from, uint32_t to) {
    WriteEntry(table, index, from, to);
    WriteEntry(table, index + 1, HashFrom("_N", from), HashFrom("_N", to));
}

// as the PC's CarRenderInfo_UpdateCarReplacementTextures, before the carbon fibre copy
void OwnReplacements(uint32_t info) {
    uint32_t ride = kernel::R32(info + kRenderRide);
    if (!ride || !mem::IsCommitted(ride)) return;
    uint32_t table = info + kRenderReplacements;
    std::string car = CarTypeName(kernel::R32(ride + kRideCarType));
    if (uint32_t to = TextureOf(PartAt(ride, kSlotInterior))) WritePair(table, kInterior, BinHash(car + "_INTERIOR"), to);
    if (uint32_t to = TextureOf(PartAt(ride, kSlotLicensePlate))) WritePair(table, kLicensePlate, kLicensePlateTexture, to);
    if (uint32_t to = TextureOf(PartAt(ride, kSlotTire))) WritePair(table, kTire, kTireTexture, to);
    static const uint32_t kTextureNameOld = BinHash("TEXTURE_NAME_OLD");
    int slot = kFirstAttachment;
    for (uint32_t i = 0; i < kSlotAttachments; i++) {
        uint32_t part = PartAt(ride, kSlotAttachment0 + i), from;
        uint32_t to = TextureOf(part);
        if (!to || !PartAttribute(part, kTextureNameOld, from) || !from) continue;
        // a normal map for ATTACHMENT0 to 4 only: the table has no room for ten
        if (i < 5) {
            WritePair(table, slot, from, to);
            slot += 2;
        } else {
            WriteEntry(table, slot++, from, to);
        }
    }
}

// the leftovers' entries as they were before the decals' loop, given back after it
struct Leftovers {
    uint32_t info = 0;
    std::array<uint32_t, (kReplacementCount - kFirstFree) * kReplacementSize / 4> words{};
} g_leftovers;

}  // namespace

}  // namespace unlimiter

void UnlimiterLightParts(X86Context& ctx, uint8_t*) {
    if (!unlimiter::Replacing()) return;
    uint32_t ride = ctx.eax;
    if (uint32_t head = unlimiter::StyledLight(ride, unlimiter::kSlotLeftHeadlight)) ctx.ecx = head;
    if (uint32_t brake = unlimiter::StyledLight(ride, unlimiter::kSlotLeftBrakelight)) ctx.edx = brake;
}

// CarRenderInfo's constructor (0x265204): the carbon fibre table is the body's copied, ours in it already
X86_EXTERN_FUNC(__imp__sub_002432D0);
X86_FUNC(sub_002432D0) {
    if (unlimiter::Replacing()) unlimiter::OwnReplacements(ctx.ecx);
    __imp__sub_002432D0(ctx, base);
}

// CarRenderInfo::UpdateDecalTextures (0x254CE0, the CarRenderInfo in edi): before and after its loop over the leftovers
void UnlimiterDecalLeftoversBegin(X86Context& ctx, uint8_t*) {
    auto& l = unlimiter::g_leftovers;
    l.info = 0;
    if (!unlimiter::Replacing()) return;
    l.info = ctx.edi;
    uint32_t at = ctx.edi + unlimiter::kRenderReplacements + unlimiter::kFirstFree * unlimiter::kReplacementSize;
    for (size_t i = 0; i < l.words.size(); i++) l.words[i] = kernel::R32(at + uint32_t(i) * 4);
}

void UnlimiterDecalLeftoversEnd(X86Context& ctx, uint8_t*) {
    auto& l = unlimiter::g_leftovers;
    if (!l.info || l.info != ctx.edi) return;
    uint32_t at = ctx.edi + unlimiter::kRenderReplacements + unlimiter::kFirstFree * unlimiter::kReplacementSize;
    for (size_t i = 0; i < l.words.size(); i++) kernel::W32(at + uint32_t(i) * 4, l.words[i]);
    l.info = 0;
}
