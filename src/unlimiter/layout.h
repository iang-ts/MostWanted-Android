#pragma once
#include <cstdint>

namespace unlimiter {

inline constexpr uint32_t kStringHash = 0x051170;
inline constexpr uint32_t kStringHashFrom = 0x0511A0;
inline constexpr uint32_t kSPrintf = 0x053AF0;
inline constexpr uint32_t kGetTextureInfo = 0x0C5900;
inline constexpr uint32_t kSearchForString = 0x124B50;

inline constexpr uint32_t kCarTypeInfos = 0x4D5800;
inline constexpr uint32_t kCarTypeSize = 0xD0;
inline constexpr uint32_t kCarTypeName = 0x00;
inline constexpr uint32_t kCarTypeBaseName = 0x10;
inline constexpr uint32_t kCarTypeManufacturer = 0x40;
inline constexpr uint32_t kCarTypeNameHash = 0x50;
inline constexpr uint32_t kCarTypeId = 0x90;
inline constexpr uint32_t kCarTypeUsage = 0x94;
inline constexpr uint32_t kCarTypeSkinnable = 0xC7;

inline constexpr uint32_t kCarPartDatabase = 0x4D7890;
inline constexpr uint32_t kCarPartPack = 0x4D57FC;
inline constexpr uint32_t kPackAttributeLists = 0x14;
inline constexpr uint32_t kPackAttributes = 0x1C;
inline constexpr uint32_t kPartNameHash = 0x00;
inline constexpr uint32_t kPartSlot = 0x04;
inline constexpr uint32_t kPartUpgrade = 0x05;
inline constexpr uint32_t kPartCarIndex = 0x07;
inline constexpr uint32_t kPartAttributeList = 0x0A;
inline constexpr uint32_t kPartSize = 14;

inline constexpr uint32_t kGetCarPartByIndex = 0x24FA80;
inline constexpr uint32_t kNewGetCarPart = 0x24FAC0;
inline constexpr uint32_t kNewGetNumCarParts = 0x258580;
inline constexpr uint32_t kPartAppliedAttributeU = 0x24FA00;
inline constexpr uint32_t kPartBrandName = 0x2542D0;
inline constexpr uint32_t kPartTextureNameHash = 0x254330;
inline constexpr uint32_t kPartLightMaterialHash = 0x254390;

inline constexpr uint32_t kRideInfoInit = 0x245100;
inline constexpr uint32_t kRideInfoSetPart = 0x25D650;
inline constexpr uint32_t kRideInfoSetStockParts = 0x25F980;
inline constexpr uint32_t kRideInfoSetRandomPaint = 0x25F610;
inline constexpr uint32_t kRideCarType = 0x000;
inline constexpr uint32_t kRideParts = 0x048;
inline constexpr uint32_t kRidePartsEnabled = 0x274;
inline constexpr uint32_t kSlotCount = 0x8B;

inline constexpr uint32_t kWriteRecordIntoRide = 0x126BB0;
inline constexpr uint32_t kCarRecordGetType = 0x134160;
inline constexpr uint32_t kBuildRideForPlayer = 0x140BE0;

inline constexpr uint32_t kSlotBase = 0x00;
inline constexpr uint32_t kSlotBody = 0x17;
inline constexpr uint32_t kSlotFrontBrake = 0x18;
inline constexpr uint32_t kSlotFrontLeftWindow = 0x19;
inline constexpr uint32_t kSlotFrontRightWindow = 0x1A;
inline constexpr uint32_t kSlotFrontWindow = 0x1B;
inline constexpr uint32_t kSlotInterior = 0x1C;
inline constexpr uint32_t kSlotLeftBrakelight = 0x1D;
inline constexpr uint32_t kSlotLeftBrakelightGlass = 0x1E;
inline constexpr uint32_t kSlotLeftHeadlight = 0x1F;
inline constexpr uint32_t kSlotLeftHeadlightGlass = 0x20;
inline constexpr uint32_t kSlotLeftMirror = 0x21;
inline constexpr uint32_t kSlotRearBrake = 0x22;
inline constexpr uint32_t kSlotRearLeftWindow = 0x23;
inline constexpr uint32_t kSlotRearRightWindow = 0x24;
inline constexpr uint32_t kSlotRearWindow = 0x25;
inline constexpr uint32_t kSlotRightBrakelight = 0x26;
inline constexpr uint32_t kSlotRightBrakelightGlass = 0x27;
inline constexpr uint32_t kSlotRightHeadlight = 0x28;
inline constexpr uint32_t kSlotRightHeadlightGlass = 0x29;
inline constexpr uint32_t kSlotRightMirror = 0x2A;
inline constexpr uint32_t kSlotDriver = 0x2B;
inline constexpr uint32_t kSlotSpoiler = 0x2C;
inline constexpr uint32_t kSlotAttachment0 = 0x34;
inline constexpr uint32_t kSlotAttachments = 10;
inline constexpr uint32_t kSlotRoof = 0x3E;
inline constexpr uint32_t kSlotHood = 0x3F;
inline constexpr uint32_t kSlotTire = 0x40;
inline constexpr uint32_t kSlotNeon = 0x41;
inline constexpr uint32_t kSlotFrontWheel = 0x42;
inline constexpr uint32_t kSlotRearWheel = 0x43;
inline constexpr uint32_t kSlotLicensePlate = 0x45;
inline constexpr uint32_t kSlotDecalFrontWindow = 0x46;
inline constexpr uint32_t kSlotDecalRearWindow = 0x47;
inline constexpr uint32_t kSlotBasePaint = 0x4C;
inline constexpr uint32_t kSlotVinylLayer0 = 0x4D;
inline constexpr uint32_t kSlotPaintRim = 0x4E;
inline constexpr uint32_t kSlotVinylColour0 = 0x4F;
inline constexpr uint32_t kSlotDecalTextureFirst = 0x53;
inline constexpr uint32_t kSlotDecalTexturesPerPlace = 8;
inline constexpr uint32_t kSlotWindowTint = 0x83;
inline constexpr uint32_t kSlotCustomHud = 0x84;
inline constexpr uint32_t kSlotHudBackingColour = 0x85;
inline constexpr uint32_t kSlotHudNeedleColour = 0x86;
inline constexpr uint32_t kSlotHudCharacterColour = 0x87;
inline constexpr uint32_t kSlotDamageBody = 0x02;
inline constexpr uint32_t kSlotSpinner = 0x44;
inline constexpr uint32_t kSlotDecalLeftDoor = 0x48;
inline constexpr uint32_t kSlotDecalRightDoor = 0x49;
inline constexpr uint32_t kSlotDecalLeftQuarter = 0x4A;
inline constexpr uint32_t kSlotDecalRightQuarter = 0x4B;

inline constexpr uint32_t kPartTypeVinyl = 0x4F;
inline constexpr uint32_t kPartTypeWheel = 0x43;

inline constexpr uint32_t kAddCustomOption = 0x2AABD0;
inline constexpr uint32_t kSetupParts = 0x2AB330;
inline constexpr uint32_t kSetupPerformance = 0x2AB4C0;
inline constexpr uint32_t kSetupVisual = 0x2AB700;
inline constexpr uint32_t kSetupRimBrands = 0x2AB8B0;
inline constexpr uint32_t kSetupVinylGroups = 0x2ABC20;
inline constexpr uint32_t kSetupDecalLocations = 0x2ABF50;
inline constexpr uint32_t kSetupDecalSlots = 0x2AC0A0;
inline constexpr uint32_t kSetupMain = 0x2AC2A0;
inline constexpr uint32_t kMenuCategoryParts = 0x801, kMenuCategoryPerformance = 0x802, kMenuCategoryVisual = 0x803;
inline constexpr uint32_t kRimBrandOfMenu = 0x2983B0;
inline constexpr uint32_t kCategoryOfMenu = 0x296FA0;
inline constexpr uint32_t kScreenMenu = 0x144;
inline constexpr uint32_t kScreenPreviousMenu = 0x150;
inline constexpr uint32_t kOwnMenuFirst = 0x100, kOwnMenus = 0x20;
inline constexpr uint32_t kPartNamePair = 0x4D3FD0;
inline constexpr uint32_t kMenuVinylGroupFirst = 0x401, kMenuVinylGroupLast = 0x409;
// the game's parts setup gives 0x402 to 0x409 the group index menu - 0x402. A group past those, and the custom
// group, is a menu of ours
inline constexpr uint32_t kMenuVinylStockFirst = 0x402, kMenuVinylStockCount = 8;
inline constexpr uint32_t kMenuVinylExtra = 0x40A, kMaxVinylGroups = 32, kMaxPaintGroups = 64;
inline constexpr uint32_t kVinylCategory = 0x0E;
inline constexpr uint32_t kMenuInterior = 0x106, kMenuRoof = 0x107, kMenuBrakeParts = 0x108, kMenuHeadlights = 0x109,
                          kMenuTaillights = 0x10A, kMenuMirrors = 0x10B, kMenuAttachments = 0x10C,
                          kMenuAttachmentFirst = 0x10D;
inline constexpr uint32_t kMenuDriver = 0x117, kMenuLicensePlate = 0x118, kMenuTireStyle = 0x119, kMenuNeon = 0x11A;
inline constexpr uint32_t kMenuVinylCustom = 0x11F;
inline constexpr uint32_t kMenuBodyKits = 0x101, kMenuSpoilers = 0x102, kMenuRims = 0x103, kMenuHoods = 0x104,
                          kMenuRoofScoops = 0x105;
inline constexpr uint32_t kMenuEngine = 0x201, kMenuTransmission = 0x202, kMenuChassis = 0x203, kMenuNitrous = 0x204,
                          kMenuTires = 0x205, kMenuBrakes = 0x206, kMenuInduction = 0x207;
inline constexpr uint32_t kIconTurbo = 0x06EF789C, kIconSupercharger = 0x93603DFB;
inline constexpr uint32_t kMenuDecalPlaceFirst = 0x501, kMenuDecalPlaceLast = 0x506;
inline constexpr uint32_t kMenuDecalSlotFirst = 0x601, kMenuDecalSlots = 8;
inline constexpr uint32_t kSlotPartTypes = 0x469D60;
inline constexpr uint32_t kMenuPaint = 0x301, kMenuVinyls = 0x302, kMenuRimPaint = 0x303, kMenuWindowTint = 0x304,
                          kMenuDecals = 0x305, kMenuNumbers = 0x306, kMenuCustomGauges = 0x307;
inline constexpr uint32_t kMenuRimBrandFirst = 0x702, kMenuRimBrandLast = 0x70B, kMenuRimBrandOwn = 0x70C;
inline constexpr uint32_t kMenuRimBrandExtra = 0x710, kMaxRimBrands = 63, kStockRimBrands = 10;

inline constexpr uint32_t kGetUsedCarTextureInfo = 0x258640;
inline constexpr uint32_t kUsedLightPartsAt = 0x25896F;
inline constexpr uint32_t kRideHeadlightStyle = 0x148;
inline constexpr uint32_t kRideBrakelightStyle = 0x14C;
inline constexpr uint32_t kUsedPermCount = 0x57;
inline constexpr uint32_t kUsedNumPerm = 0x2B8;

inline constexpr uint32_t kCarRenderInfoCtor = 0x264B40;
inline constexpr uint32_t kCarRenderInfoRender = 0x255490;
inline constexpr uint32_t kUpdateWheelYRenderOffset = 0x2549B0;
inline constexpr uint32_t kRenderRide = 0x90;
inline constexpr uint32_t kRenderCarType = 0x94;
inline constexpr uint32_t kRenderWheelOffset = 0x120;
inline constexpr uint32_t kRenderWheelWidth = 0x130;
inline constexpr uint32_t kRenderWheelRadius = 0x138;
inline constexpr uint32_t kRenderWheelWidthScale = 0x140;
inline constexpr uint32_t kRenderWheelRadiusScale = 0x150;
inline constexpr uint32_t kRenderEcar = 0x17C4;
// the body's texture replacements (12 bytes: from, to, the TextureInfo or -1 to look up again), the PC's at +0x23C,
// and the carbon fibre copy after them (0x2432D0)
inline constexpr uint32_t kRenderReplacements = 0x24C, kRenderCarbonReplacements = 0x5B8;
inline constexpr uint32_t kReplacementCount = 73, kReplacementSize = 12;
// the UsedCarTextureInfo (GetUsedCarTextureInfo, 0x258640) the render keeps, and its lights' masks: on, broken
inline constexpr uint32_t kRenderUsedTextures = 0x12B0, kRenderLightsOn = 0x1630, kRenderLightsBroken = 0x1634;
// UsedCarTextureInfo: off, on and damaged for the headlights, their glass, the brakelights, their glass, the reverse
inline constexpr uint32_t kUsedHeadlight = 0x340, kUsedHeadlightGlass = 0x34C, kUsedBrakelight = 0x358;
inline constexpr uint32_t kUsedBrakelightGlass = 0x364, kUsedReverse = 0x370;
// the cheats' and the light flares' lights held on (the PC's 0x9B092C on)
inline constexpr uint32_t kForceHeadlights = 0x4D5734, kForceBrakelights = 0x4D5738, kForceReverselights = 0x4D573C;
inline constexpr uint32_t kGameFlowState = 0x4936B8, kGameFlowFrontEnd = 3;

inline constexpr uint32_t kDoesStringExist = 0x124BF0;

}  // namespace unlimiter
