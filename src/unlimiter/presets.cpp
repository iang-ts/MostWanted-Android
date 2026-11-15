#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/presets.h"
#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "unlimiter/config.h"
#include "unlimiter/layout.h"

namespace unlimiter {

namespace {

constexpr uint32_t kFeDatabase = 0x48EDD4, kProfile = 0x10, kPlayerCarDb = 0x414;
constexpr uint32_t kEntry = 0x14, kEntries = 0xC8, kEntryFrontend = 0x04, kEntryCustomization = 0x10;
constexpr uint32_t kRecords = 0xFA0, kRecordSize = 0x198, kRecordCount = 0x4C, kRecordFree = 0x194;
constexpr uint32_t kPackNext = 0x00, kPackRows = 0x34, kPackRowCount = 0x38;
constexpr uint32_t kMagic = 0x31535055, kEmpty = 0xFFFFFFFF, kNoSlot = 0xFF;
constexpr uint32_t kSnapshotFrames = 120;
constexpr uint32_t kProfileName = 0x00, kProfileNamed = 0x20, kProfileNameSize = 0x20;

struct Saved {
    uint32_t handle, frontend;
    uint32_t hashes[kSlotCount];
};

uint32_t CarDbOf(uint32_t profile) {
    if (!profile || !mem::IsCommitted(profile + kPlayerCarDb + kRecords + kRecordCount * kRecordSize)) return 0;
    return profile + kPlayerCarDb;
}

uint32_t CurrentProfile() {
    uint32_t db = kernel::R32(kFeDatabase);
    if (!db || !mem::IsCommitted(db + kProfile)) return 0;
    return kernel::R32(db + kProfile);
}

std::string ProfileName(uint32_t profile) {
    if (!profile || !mem::IsCommitted(profile + kProfileNamed)) return {};
    if (!*static_cast<uint8_t*>(kernel::HostPtr(profile + kProfileNamed))) return {};
    const char* name = static_cast<const char*>(kernel::HostPtr(profile + kProfileName));
    return std::string(name, strnlen(name, kProfileNameSize));
}

template <typename F>
void ForEachRow(F&& f) {
    uint32_t head = kCarPartDatabase, pack = kernel::R32(head);
    uint32_t index = 0;
    for (int guard = 0; pack && pack != head && guard < 64; guard++, pack = kernel::R32(pack + kPackNext)) {
        uint32_t rows = kernel::R32(pack + kPackRows), count = kernel::R32(pack + kPackRowCount);
        if (!rows || !mem::IsCommitted(rows) || count > 0x10000) return;
        for (uint32_t i = 0; i < count; i++, index++) f(index, rows + i * kPartSize);
    }
}

uint32_t RowHash(int16_t index, const std::vector<uint32_t>& by_index) {
    if (index < 0 || uint32_t(index) >= by_index.size()) return 0;
    return by_index[uint32_t(index)];
}

std::vector<uint32_t> RowHashes() {
    std::vector<uint32_t> out;
    ForEachRow([&](uint32_t, uint32_t row) { out.push_back(kernel::R32(row + kPartNameHash)); });
    return out;
}

std::vector<Saved> Collect(uint32_t db, const std::vector<uint32_t>& hashes) {
    std::vector<Saved> out;
    for (uint32_t e = 0; e < kEntries; e++) {
        uint32_t entry = db + e * kEntry;
        uint32_t handle = kernel::R32(entry);
        uint8_t slot = *static_cast<uint8_t*>(kernel::HostPtr(entry + kEntryCustomization));
        if (handle == kEmpty || slot == kNoSlot || slot >= kRecordCount) continue;
        uint32_t record = db + kRecords + slot * kRecordSize;
        if (*static_cast<uint8_t*>(kernel::HostPtr(record + kRecordFree)) == kNoSlot) continue;
        Saved s{handle, kernel::R32(entry + kEntryFrontend), {}};
        for (uint32_t i = 0; i < kSlotCount; i++)
            s.hashes[i] = RowHash(int16_t(kernel::R16(record + i * 2)), hashes);
        out.push_back(s);
    }
    return out;
}

std::vector<uint8_t> Encode(const std::vector<Saved>& list) {
    std::vector<uint8_t> out(8 + list.size() * sizeof(Saved));
    uint32_t head[2] = {kMagic, uint32_t(list.size())};
    std::memcpy(out.data(), head, 8);
    if (!list.empty()) std::memcpy(out.data() + 8, list.data(), list.size() * sizeof(Saved));
    return out;
}

bool Decode(const std::vector<uint8_t>& in, std::vector<Saved>& list) {
    if (in.size() < 8) return false;
    uint32_t head[2];
    std::memcpy(head, in.data(), 8);
    if (head[0] != kMagic || 8 + size_t(head[1]) * sizeof(Saved) > in.size()) return false;
    list.resize(head[1]);
    if (head[1]) std::memcpy(list.data(), in.data() + 8, list.size() * sizeof(Saved));
    return true;
}

std::vector<uint8_t> g_last;

}  // namespace

void RestorePresets(uint32_t profile) {
    if (!Active() || !Global().presitter) return;
    uint32_t db = CarDbOf(profile);
    if (!db) return;
    std::vector<uint8_t> file;
    std::vector<Saved> saved;
    if (!presets::Read(ProfileName(profile), file) && !presets::Read({}, file)) return;
    if (!Decode(file, saved)) return;
    std::unordered_map<uint32_t, uint32_t> any_slot;
    ForEachRow([&](uint32_t index, uint32_t row) { any_slot.emplace(kernel::R32(row + kPartNameHash), index); });
    std::vector<uint32_t> hashes = RowHashes();
    uint32_t fixed = 0;
    for (const Saved& s : saved) {
        for (uint32_t e = 0; e < kEntries; e++) {
            uint32_t entry = db + e * kEntry;
            if (kernel::R32(entry) != s.handle || kernel::R32(entry + kEntryFrontend) != s.frontend) continue;
            uint8_t slot = *static_cast<uint8_t*>(kernel::HostPtr(entry + kEntryCustomization));
            if (slot == kNoSlot || slot >= kRecordCount) break;
            uint32_t record = db + kRecords + slot * kRecordSize;
            for (uint32_t i = 0; i < kSlotCount; i++) {
                uint32_t want = s.hashes[i];
                if (!want) continue;
                int16_t now = int16_t(kernel::R16(record + i * 2));
                if (RowHash(now, hashes) == want) continue;
                auto it = any_slot.find(want);
                if (it == any_slot.end()) continue;
                kernel::W16(record + i * 2, uint16_t(it->second));
                fixed++;
            }
            break;
        }
    }
    if (fixed) LOGR("unlimiter: %u customization parts put back by name", fixed);
}

void SnapshotPresets() {
    static uint32_t frames = 0;
    if (++frames % kSnapshotFrames) return;
    if (!Active() || !Ready() || !Global().presitter || !presets::SavesWritten()) return;
    uint32_t profile = CurrentProfile();
    std::string name = ProfileName(profile);
    uint32_t db = CarDbOf(profile);
    if (name.empty() || !db || kernel::R32(db) == kEmpty) return;
    std::vector<uint8_t> now = Encode(Collect(db, RowHashes()));
    if (now == g_last) return;
    g_last = now;
    presets::Write(name, now);
    LOGR("unlimiter: presets of %s kept", name.c_str());
}

// where an entry came from. BonusCar-1 is the ini's own default, the M3GTR the game puts in a fresh profile itself:
// the PC reads it into a struct apart from the list, so it takes the career category and none of the unlock fields,
// and the lookup below, which only ever answers for the bonus category, never finds it
enum class BonusSource { Fallback, Numbered, Folder };

void ReadBonus(const Ini& ini, const std::string& section, std::vector<BonusCar>& out, BonusSource from) {
    BonusCar car;
    car.preset = ini.Text(section, "PresetName");
    if (car.preset.empty()) return;
    bool fallback = from == BonusSource::Fallback;
    car.handle = ini.Int(section, "CarRecordID", fallback ? 0x12345678 : -1);
    car.category = uint32_t(ini.Int(section, "Category", fallback ? 0xF0020 : 0xF0008));
    if (!fallback) {
        car.ce_only = ini.Int(section, "CEOnly", 0) != 0;
        car.condition = ini.Int(section, "UnlockConditionType", -1);
        car.blacklist = ini.Int(section, "Blacklist", 15);
        car.unlock_text = ini.Hash(section, "UnlockConditionString", 0xFFFFFFFF);
    }
    // a folder ini naming a preset the numbered list already has replaces it, which is the only place the PC merges
    // by name: two numbered entries for one preset stay two, and each makes a record of its own
    if (from == BonusSource::Folder)
        for (BonusCar& had : out)
            if (had.preset == car.preset) {
                had = std::move(car);
                return;
            }
    out.push_back(std::move(car));
}

const std::vector<BonusCar>& BonusCars() {
    static const std::vector<BonusCar> cars = [] {
        std::vector<BonusCar> out;
        const Ini& ini = Data("_BonusCars.ini");
        int count = ini.Int("BonusCars", "NumberOfBonusCars", -2);
        if (count == -2) return out;
        ReadBonus(ini, "BonusCar-1", out, BonusSource::Fallback);
        for (int i = 0; i <= count; i++) ReadBonus(ini, "BonusCar" + std::to_string(i), out, BonusSource::Numbered);
        for (const Ini& file : Folder("BonusCars")) ReadBonus(file, "BonusCar", out, BonusSource::Folder);
        size_t ruled = 0;
        for (const BonusCar& car : out) ruled += car.condition >= 0;
        LOGR("unlimiter: %zu bonus cars, %zu with an unlock condition", out.size(), ruled);
        return out;
    }();
    return cars;
}

// FEHashUpper (0x157E40): BinHash's h * 33 over the name in upper case. FEPlayerCarDB::CreateCarRecord (0x140570)
// stores it at the new record's +0 as the handle, which is what the unlock and the car lot look a preset up by
uint32_t FeHash(const std::string& text) {
    uint32_t h = 0xFFFFFFFF;
    for (unsigned char c : text) h = h * 33 + (c >= 'a' && c <= 'z' ? c - 0x20 : c);
    return h;
}

// an entry of the ini keyed by the handle its preset name hashes to. Only the bonus category answers, as the PC's
// own lookup does, and a CarRecordID of its own takes a car out of reach here: ProfileCars writes it over the hash
// the game left at +0, so nothing matches it any more and the game keeps the last word on that car
const BonusCar* BonusCarOf(uint32_t handle) {
    for (const BonusCar& car : BonusCars())
        if (car.category == 0xF0008 && FeHash(car.preset) == handle) return &car;
    return nullptr;
}

uint32_t PresetName(const std::string& text) {
    static uint32_t buffer = 0;
    if (!buffer) buffer = mem::AllocSystem(64);
    if (!buffer) return 0;
    std::string cut = text.substr(0, 63);
    std::memcpy(kernel::HostPtr(buffer), cut.c_str(), cut.size() + 1);
    return buffer;
}

// after the disc fills a new profile: the ini's presets, then the stock bit on cars that are not a career car
void ProfileCars(X86Context& ctx, uint32_t db) {
    if (!Active() || !db) return;
    constexpr uint32_t kCreatePreset = 0x140570, kFilter = 0x0C, kCareerByte = 0x11;
    if (Global().bonus_cars) {
        for (const BonusCar& car : BonusCars()) {
            uint32_t name = PresetName(car.preset);
            if (!name) break;
            X86Context saved = ctx;
            ctx.ecx = db;
            uint32_t made = kernel::CallGuest(ctx, kCreatePreset, {name});
            ctx = saved;
            if (!made) continue;
            if (car.handle != -1) kernel::W32(made, uint32_t(car.handle));
            kernel::W32(made + kFilter, car.category);
        }
    }
    if (!Global().stock_cars) return;
    for (uint32_t i = 0; i < kEntries; i++) {
        uint32_t entry = db + i * kEntry;
        if (kernel::R32(entry) == kEmpty) continue;
        if (*static_cast<uint8_t*>(kernel::HostPtr(entry + kCareerByte)) != kNoSlot) continue;
        kernel::W32(entry + kFilter, kernel::R32(entry + kFilter) | 1u);
    }
}

}  // namespace unlimiter

// FEPlayerCarDB::Default, the fill of a new profile
X86_EXTERN_FUNC(__imp__sub_00140890);
X86_FUNC(sub_00140890) {
    uint32_t db = ctx.ecx;
    __imp__sub_00140890(ctx, base);
    unlimiter::ProfileCars(ctx, db);
}
