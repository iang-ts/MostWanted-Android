#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "unlimiter/ini.h"
#include "unlimiter/layout.h"

struct X86Context;

// LOG_FIRST writes at Info, which a release build drops: a note that has to survive one is a Report. The file that
// uses this includes <atomic> and core/log.h
#define UNLIMITER_NOTE(n, ...)                                                       do {                                                                                 static std::atomic<int> _note{0};                                                if (_note.fetch_add(1, std::memory_order_relaxed) < (n)) LOGR(__VA_ARGS__);     } while (0)

namespace unlimiter {

struct CarConfig {
    struct {
        bool force_lod_a = false;
        int initially_unlocked = -1;
        int engine_type = -1;
        bool scale_brakes_with_rims = true;
        bool attachments_under_parts = true;
    } main;
    struct {
        bool headlight_off = false, damage_lights = false, centre_brake = false, reverse = false;
        bool brakelight_onf_in_game = false, tire_inner_mask = false;
    } textures;
    struct {
        bool parts = true, performance = true, visual = true;
    } category;
    struct {
        bool body_kits = true, spoilers = true, rims = true, rims_custom = false, hoods = true, roof_scoops = true;
        bool interior = false, roof = false, brakes = false, headlights = false, taillights = false, mirrors = false;
        int attachments = 0;
    } parts;
    struct {
        bool engine = true, transmission = true, chassis = true, nitrous = true, tires = true, brakes = true;
        bool induction = true;
    } performance;
    struct {
        bool paint = true, paint_custom = false, vinyls = true, vinyls_custom = false, rim_paint = true;
        bool window_tint = true, decals = true, numbers = true, custom_gauges = true, driver = false;
        bool license_plate = false, tires = true, neon = true;
        bool decal_places[6] = {true, true, true, true, true, true};
    } visual;
    struct {
        std::map<uint32_t, uint32_t> by_menu;  // a menu's frontend camera node, as an Attrib key
        uint32_t rim_brand = 0, rim_brand_rear = 0, rim_brand_all = 0, vinyl_group = 0;
        uint32_t decal_slot[6][8] = {};
    } cameras;
    struct {
        // -2 draws a level 0 to 2, -1 leaves the ride stock. 0 picks no vanilla level, 1 skips the vanilla pass and
        // takes the slots below, 2 runs both
        int level = -2;
        int custom = 0;
        int rear_rim_chance = 25, numbers_chance = 25;
        bool match_side_decals = false, in_traffic = false;
        bool slot[kSlotCount] = {};
    } random_parts;
    // [FECooling]: a slot's heat multiplier, 0 for the one the VLT already holds
    float cooling[kSlotCount] = {};
};

struct GlobalSettings {
    int replacement_model = 1;
    bool manufacturer_hook = true, my_cars_backroom = true, cop_destroyed_strings = true, random_parts = true;
    bool bonus_cars = false, stock_cars = false, presitter = true, randomize_traffic = false, separate_rims = true;
    bool secondary_logo_fix = true, addon_cops_damage_fix = true;
    bool challenge_series_name_fix = true, light_material_crash_fix = false, ride_height_fix = true, fng_fix = false;
    bool car_skin_fix = true, scaling_light_fix = false, cop_bright_lights_order_fix = true, cop_spoiler_damage_fix = true;
    bool expand_memory_pools = true, force_stock_parts_on_addon_opponents = false, force_lights_on_in_fe = false;
    bool light_texture_swap_in_fe = true, disable_camera_auto_center = false;
    int frame_delay = -1, car_skin_count = 20;
    bool disable_texture_replacement = false, disable_neon = false, disable_light_flare_colors = false;
    bool disable_kit_wheel_modifications = false, test_career_customization = false, force_signals_on = false;
};

// an entry of _BonusCars.ini. The unlock fields belong to the numbered and folder entries alone: BonusCar-1 is the
// M3GTR a fresh profile already starts with, which the PC keeps in a struct of its own and never looks up
struct BonusCar {
    std::string preset;
    int handle = -1;             // CarRecordID, -1 to keep the handle the game hashes out of the preset name
    uint32_t category = 0xF0008;  // the record's filter bits: 0xF0000 to be shown at all, 8 for a bonus car
    bool ce_only = false;
    int condition = -1;          // UnlockConditionType, -1 leaving the answer to the game
    int blacklist = 15;
    uint32_t unlock_text = 0xFFFFFFFF;
};

bool Imported();
bool Active();
bool Ready();
const Ini& Settings();
const GlobalSettings& Global();
const Ini& General();
const Ini& Car(uint32_t car_type);
const CarConfig& Config(uint32_t car_type);
const Ini& Data(const std::string& name);
uint32_t CopPhrase();
void NoteCopVehicle(uint32_t vehicle);
std::vector<Ini> Folder(const std::string& name);
std::string CarTypeName(uint32_t car_type);
// a car type's record, 0 when the table is not mapped yet. kCarTypeInfos points at the table, it is not the table
uint32_t CarTypeRecord(uint32_t car_type);
uint32_t CarTypeCount();
uint32_t CarTypeOfHash(uint32_t name_hash);

bool Setting(const char* section, const char* key, bool fallback);
int SettingInt(const char* section, const char* key, int fallback);
int CarInt(uint32_t car_type, const char* section, const char* key, int fallback);
uint32_t CarHash(uint32_t car_type, const char* section, const char* key, uint32_t fallback);
uint32_t CarTextHash(uint32_t car_type, const char* section, const char* key, const char* fallback);

bool OwnRimBrands();
bool NoRimSize(uint32_t menu);
int RimsMode();
uint32_t WheelCartTitle(uint32_t slot);
void RearWheelTextures(X86Context& ctx, uint32_t info, uint32_t ride);
uint32_t SpinnerTexture(X86Context& ctx, uint32_t hash);

bool MoreCarSkins();
uint32_t CarSkinSlots();
void MarkSkinnable();
void ApplyFixes();

void Frame(X86Context& ctx);
void ExpandPools();
void RestorePresets(uint32_t profile);
// the ini's bonus car a record's handle stands for, null when it names none of them (presets.cpp)
const BonusCar* BonusCarOf(uint32_t handle);
void SnapshotPresets();
void SavePresets();

}  // namespace unlimiter
