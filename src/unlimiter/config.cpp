#include "unlimiter/config.h"

#include <cstring>
#include <map>
#include <mutex>

#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "unlimiter/layout.h"

namespace unlimiter {

namespace {

constexpr const char* kRoot = "D:\\unlimiter\\";

struct State {
    bool loaded_general = false;
    bool imported = false;
    bool ready = false;
    Ini settings;
    GlobalSettings global;
    Ini general;
    CarConfig general_config;
    std::vector<Ini> cars;
    std::vector<CarConfig> configs;
    std::map<std::string, Ini> data;
};

bool On(const Ini& ini, const char* section, const char* key, bool fallback) {
    return ini.Int(section, key, fallback ? 1 : 0) != 0;
}

// [Cameras]: a frontend camera node's name for a menu, over the one its screen node names. The decal slots by place wait
// for the menu that picks them
uint32_t CameraKey(const Ini& ini, const char* key) {
    std::string name = ini.Text("Cameras", key);
    return name.empty() ? 0 : VltHash(name);
}

void ReadCameras(const Ini& ini, CarConfig& c) {
    static const struct {
        const char* key;
        uint32_t menu;
    } kMenus[] = {
        {"PartsBodyKits", kMenuBodyKits},          {"PartsSpoilers", kMenuSpoilers},
        {"PartsRims", kMenuRims},                  {"PartsHoods", kMenuHoods},
        {"PartsRoofScoops", kMenuRoofScoops},      {"PartsInterior", kMenuInterior},
        {"PartsRoof", kMenuRoof},                      {"PartsBrakes", kMenuBrakeParts},
        {"PartsHeadlights", kMenuHeadlights},                {"PartsTaillights", kMenuTaillights},
        {"PartsMirrors", kMenuMirrors},                   {"PartsAttachments", kMenuAttachments},
        {"VisualPaint", kMenuPaint},               {"VisualVinyls", kMenuVinyls},
        {"VisualRimPaint", kMenuRimPaint},         {"VisualWindowTint", kMenuWindowTint},
        {"VisualDecals", kMenuDecals},             {"VisualNumbers", kMenuNumbers},
        {"VisualCustomGauges", kMenuCustomGauges}, {"VisualDriver", kMenuDriver},
        {"VisualLicensePlate", kMenuLicensePlate}, {"VisualTires", kMenuTireStyle},
        {"VisualNeon", kMenuNeon},                 {"VisualDecalsWindshield", kMenuDecalPlaceFirst},
        {"VisualDecalsRearWindow", kMenuDecalPlaceFirst + 1}, {"VisualDecalsLeftDoor", kMenuDecalPlaceFirst + 2},
        {"VisualDecalsRightDoor", kMenuDecalPlaceFirst + 3},  {"VisualDecalsLeftQuarter", kMenuDecalPlaceFirst + 4},
        {"VisualDecalsRightQuarter", kMenuDecalPlaceFirst + 5},
    };
    auto& cams = c.cameras;
    for (const auto& m : kMenus)
        if (uint32_t key = CameraKey(ini, m.key)) cams.by_menu[m.menu] = key;
    for (uint32_t i = 0; i < kSlotAttachments; i++) {
        std::string key = "PartsAttachment" + std::to_string(i);
        if (uint32_t camera = CameraKey(ini, key.c_str())) cams.by_menu[kMenuAttachmentFirst + i] = camera;
    }
    static const char* const kDecal[] = {"Windshield", "RearWindow", "LeftDoor", "RightDoor", "LeftQuarter", "RightQuarter"};
    for (int place = 0; place < 6; place++) {
        for (int slot = 1; slot <= 8; slot++) {
            std::string key = std::string("VisualDecals") + kDecal[place] + std::to_string(slot);
            if (uint32_t camera = CameraKey(ini, key.c_str())) cams.decal_slot[place][slot - 1] = camera;
        }
    }
    cams.rim_brand = CameraKey(ini, "PartsRimsBrand");
    cams.rim_brand_rear = CameraKey(ini, "PartsRimsBrandRear");
    cams.rim_brand_all = CameraKey(ini, "PartsRimsBrandAll");
    cams.vinyl_group = CameraKey(ini, "VisualVinylsGroup");
}

CarConfig ReadConfig(const Ini& ini) {
    CarConfig c;
    c.main.force_lod_a = On(ini, "Main", "ForceLODA", c.main.force_lod_a);
    c.main.initially_unlocked = ini.Int("Main", "InitiallyUnlocked", c.main.initially_unlocked);
    c.main.engine_type = ini.Int("Main", "EngineType", c.main.engine_type);
    c.main.scale_brakes_with_rims = On(ini, "Main", "ScaleBrakesWithRims", c.main.scale_brakes_with_rims);
    c.main.attachments_under_parts = On(ini, "Main", "ListAttachmentsUnderParts", c.main.attachments_under_parts);
    c.category.parts = On(ini, "Category", "Parts", On(ini, "Main", "Parts", c.category.parts));
    c.category.performance = On(ini, "Category", "Performance", On(ini, "Main", "Performance", c.category.performance));
    c.category.visual = On(ini, "Category", "Visual", On(ini, "Main", "Visual", c.category.visual));
    c.textures.headlight_off = On(ini, "Textures", "HeadlightOff", c.textures.headlight_off);
    c.textures.damage_lights = On(ini, "Textures", "DamageLights", c.textures.damage_lights);
    c.textures.centre_brake = On(ini, "Textures", "CentreBrake", c.textures.centre_brake);
    c.textures.reverse = On(ini, "Textures", "Reverse", c.textures.reverse);
    c.textures.brakelight_onf_in_game = On(ini, "Textures", "BrakelightOnfInGame", c.textures.brakelight_onf_in_game);
    c.textures.tire_inner_mask = On(ini, "Textures", "TireInnerMask", c.textures.tire_inner_mask);
    auto& p = c.parts;
    p.body_kits = On(ini, "Parts", "BodyKits", p.body_kits);
    p.spoilers = On(ini, "Parts", "Spoilers", p.spoilers);
    p.rims = On(ini, "Parts", "Rims", p.rims);
    p.rims_custom = On(ini, "Parts", "RimsCustom", p.rims_custom);
    p.hoods = On(ini, "Parts", "Hoods", p.hoods);
    p.roof_scoops = On(ini, "Parts", "RoofScoops", p.roof_scoops);
    p.interior = On(ini, "Parts", "Interior", p.interior);
    p.roof = On(ini, "Parts", "Roof", p.roof);
    p.brakes = On(ini, "Parts", "Brakes", p.brakes);
    p.headlights = On(ini, "Parts", "Headlights", p.headlights);
    p.taillights = On(ini, "Parts", "Taillights", p.taillights);
    p.mirrors = On(ini, "Parts", "Mirrors", p.mirrors);
    p.attachments = ini.Int("Parts", "Attachments", p.attachments) % 11;
    auto& f = c.performance;
    f.engine = On(ini, "Performance", "Engine", f.engine);
    f.transmission = On(ini, "Performance", "Transmission", f.transmission);
    f.chassis = On(ini, "Performance", "Chassis", f.chassis);
    f.nitrous = On(ini, "Performance", "Nitrous", f.nitrous);
    f.tires = On(ini, "Performance", "Tires", f.tires);
    f.brakes = On(ini, "Performance", "Brakes", f.brakes);
    f.induction = On(ini, "Performance", "Induction", f.induction);
    auto& v = c.visual;
    v.paint = On(ini, "Visual", "Paint", v.paint);
    v.paint_custom = On(ini, "Visual", "PaintCustom", v.paint_custom);
    v.vinyls = On(ini, "Visual", "Vinyls", v.vinyls);
    v.vinyls_custom = On(ini, "Visual", "VinylsCustom", v.vinyls_custom);
    v.rim_paint = On(ini, "Visual", "RimPaint", v.rim_paint);
    v.window_tint = On(ini, "Visual", "WindowTint", v.window_tint);
    v.decals = On(ini, "Visual", "Decals", v.decals);
    v.numbers = On(ini, "Visual", "Numbers", v.numbers);
    v.custom_gauges = On(ini, "Visual", "CustomGauges", v.custom_gauges);
    v.driver = On(ini, "Visual", "Driver", v.driver);
    v.license_plate = On(ini, "Visual", "LicensePlate", v.license_plate);
    v.tires = On(ini, "Visual", "Tires", v.tires);
    v.neon = On(ini, "Visual", "Neon", v.neon);
    static const char* const kPlaces[6] = {"DecalsWindshield", "DecalsRearWindow", "DecalsLeftDoor",
                                           "DecalsRightDoor", "DecalsLeftQuarter", "DecalsRightQuarter"};
    for (int i = 0; i < 6; i++) v.decal_places[i] = On(ini, "Visual", kPlaces[i], v.decal_places[i]);
    ReadCameras(ini, c);
    return c;
}

GlobalSettings ReadGlobal(const Ini& ini) {
    GlobalSettings g;
    g.replacement_model = ini.Int("Main", "ReplacementModel", g.replacement_model);
    g.manufacturer_hook = On(ini, "Main", "EnableManufacturerHook", g.manufacturer_hook);
    g.my_cars_backroom = On(ini, "Main", "MyCarsBackroom", g.my_cars_backroom);
    g.cop_destroyed_strings = On(ini, "Main", "EnableCopDestroyedStringHook", g.cop_destroyed_strings);
    g.random_parts = On(ini, "Main", "EnableRandomPartsHook", g.random_parts);
    g.bonus_cars = On(ini, "Main", "EnableBonusCarsHook", g.bonus_cars);
    g.stock_cars = On(ini, "Main", "EnableStockCarsHook", g.stock_cars);
    g.presitter = On(ini, "Main", "EnablePresitter", g.presitter);
    g.randomize_traffic = On(ini, "Main", "RandomizeTraffic", g.randomize_traffic);
    g.separate_rims = On(ini, "Main", "SeparateRims", On(ini, "Main", "SeperateRims", g.separate_rims));
    g.disappearing_wheels_fix = On(ini, "Fixes", "DisappearingWheelsFix", g.disappearing_wheels_fix);
    g.secondary_logo_fix = On(ini, "Fixes", "SecondaryLogoFix", g.secondary_logo_fix);
    g.addon_cops_damage_fix = On(ini, "Fixes", "AddOnCopsDamageFix", g.addon_cops_damage_fix);
    g.challenge_series_name_fix = On(ini, "Fixes", "ChallengeSeriesOpponentNameFix", g.challenge_series_name_fix);
    g.light_material_crash_fix = On(ini, "Fixes", "LightMaterialCrashFix", g.light_material_crash_fix);
    g.ride_height_fix = On(ini, "Fixes", "RideHeightFix", g.ride_height_fix);
    g.fng_fix = On(ini, "Fixes", "FNGFix", g.fng_fix);
    g.car_skin_fix = On(ini, "Fixes", "CarSkinFix", g.car_skin_fix);
    g.scaling_light_fix = On(ini, "Fixes", "ScalingLightFix", g.scaling_light_fix);
    g.cop_bright_lights_order_fix = On(ini, "Fixes", "CopBrightLightsOrderFix", g.cop_bright_lights_order_fix);
    g.cop_spoiler_damage_fix = On(ini, "Fixes", "CopSpoilerDamageFix", g.cop_spoiler_damage_fix);
    g.expand_memory_pools = On(ini, "Misc", "ExpandMemoryPools", g.expand_memory_pools);
    g.frame_delay = ini.Int("Misc", "FrameDelay", g.frame_delay);
    g.force_stock_parts_on_addon_opponents =
        On(ini, "Misc", "ForceStockPartsOnAddOnOpponents", g.force_stock_parts_on_addon_opponents);
    g.force_lights_on_in_fe = On(ini, "Misc", "ForceLightsOnInFE", g.force_lights_on_in_fe);
    g.light_texture_swap_in_fe = On(ini, "Misc", "LightTextureSwapInFE", g.light_texture_swap_in_fe);
    g.car_skin_count = ini.Int("Misc", "CarSkinCount", g.car_skin_count);
    g.disable_camera_auto_center = On(ini, "Misc", "DisableCameraAutoCenter", g.disable_camera_auto_center);
    g.disable_texture_replacement = On(ini, "Debug", "DisableTextureReplacement", g.disable_texture_replacement);
    g.disable_neon = On(ini, "Debug", "DisableNeon", g.disable_neon);
    g.disable_light_flare_colors = On(ini, "Debug", "DisableLightFlareColors", g.disable_light_flare_colors);
    g.disable_kit_wheel_modifications = On(ini, "Debug", "DisableKitWheelModifications", g.disable_kit_wheel_modifications);
    g.test_career_customization = On(ini, "Debug", "TestCareerCustomization", g.test_career_customization);
    g.force_signals_on = On(ini, "Debug", "ForceSignalsOn", g.force_signals_on);
    return g;
}

State& S() {
    static State s;
    return s;
}

void LoadGeneral() {
    State& s = S();
    if (s.loaded_general) return;
    s.loaded_general = true;
    s.imported = s.settings.Load(std::string(kRoot) + "NFSMWUnlimiterSettings.ini");
    s.general.Load(std::string(kRoot) + "_General.ini");
    s.global = ReadGlobal(s.settings);
    s.general_config = ReadConfig(s.general);
    if (s.imported) LOGR("unlimiter: settings and _General.ini read");
}

Ini LoadCar(const std::string& name) {
    Ini car = S().general;
    Ini own;
    if (own.Load(std::string(kRoot) + "cars\\" + name + ".ini") || own.Load(std::string(kRoot) + name + ".ini")) {
        car.Overlay(own);
        LOGR("unlimiter: %s.ini read", name.c_str());
    }
    return car;
}

}  // namespace

bool Imported() {
    LoadGeneral();
    return S().imported;
}

bool Active() { return Imported() && game::Opt().unlimiter; }

bool Ready() { return S().ready; }

const Ini& Settings() {
    LoadGeneral();
    return S().settings;
}

const GlobalSettings& Global() {
    LoadGeneral();
    return S().global;
}

const Ini& General() {
    LoadGeneral();
    return S().general;
}

const Ini& Car(uint32_t car_type) {
    State& s = S();
    if (car_type < s.cars.size()) return s.cars[car_type];
    return General();
}

const CarConfig& Config(uint32_t car_type) {
    State& s = S();
    if (car_type < s.configs.size()) return s.configs[car_type];
    LoadGeneral();
    return s.general_config;
}

const Ini& Data(const std::string& name) {
    State& s = S();
    auto it = s.data.find(name);
    if (it != s.data.end()) return it->second;
    Ini& ini = s.data[name];
    ini.Load(std::string(kRoot) + name);
    return ini;
}

std::vector<Ini> Folder(const std::string& name) {
    std::vector<Ini> out;
    std::vector<uint8_t> list;
    if (!kernel::ReadGameFile(std::string(kRoot) + name + "\\list.txt", list)) return out;
    std::string text(list.begin(), list.end());
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string file = text.substr(at, end - at);
        at = end + 1;
        while (!file.empty() && (file.back() == '\r' || file.back() == ' ')) file.pop_back();
        if (file.empty()) continue;
        Ini ini;
        if (ini.Load(std::string(kRoot) + name + "\\" + file)) out.push_back(std::move(ini));
    }
    return out;
}

uint32_t CarTypeCount() { return game::CarTypeCount(); }

std::string CarTypeName(uint32_t car_type) {
    uint32_t table = kernel::R32(kCarTypeInfos);
    if (!table || car_type >= CarTypeCount()) return {};
    char name[17] = {};
    std::memcpy(name, kernel::HostPtr(table + car_type * kCarTypeSize + kCarTypeName), 16);
    return name;
}

uint32_t CarTypeOfHash(uint32_t name_hash) {
    uint32_t table = kernel::R32(kCarTypeInfos);
    if (!table) return 0xFFFFFFFF;
    for (uint32_t i = 0, n = CarTypeCount(); i < n; i++)
        if (kernel::R32(table + i * kCarTypeSize + kCarTypeNameHash) == name_hash) return i;
    return 0xFFFFFFFF;
}

bool Setting(const char* section, const char* key, bool fallback) { return Settings().Int(section, key, fallback ? 1 : 0) != 0; }

int SettingInt(const char* section, const char* key, int fallback) { return Settings().Int(section, key, fallback); }

int CarInt(uint32_t car_type, const char* section, const char* key, int fallback) { return Car(car_type).Int(section, key, fallback); }

uint32_t CarHash(uint32_t car_type, const char* section, const char* key, uint32_t fallback) {
    return Car(car_type).Hash(section, key, fallback);
}

uint32_t CarTextHash(uint32_t car_type, const char* section, const char* key, const char* fallback) {
    uint32_t hash = Car(car_type).Hash(section, key, BinHash(fallback));
    return hash == 0xFFFFFFFF && *fallback ? BinHash(fallback) : hash;
}

void Frame(X86Context&) {
    State& s = S();
    if (s.ready || !Imported()) return;
    uint32_t table = kernel::R32(kCarTypeInfos);
    if (!table || !mem::IsCommitted(table)) return;
    uint32_t n = CarTypeCount();
    s.cars.clear();
    s.configs.clear();
    s.cars.reserve(n);
    s.configs.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
        s.cars.push_back(LoadCar(CarTypeName(i)));
        s.configs.push_back(ReadConfig(s.cars.back()));
    }
    s.ready = true;
    LOGR("unlimiter: %u car types configured", n);
}

}  // namespace unlimiter
