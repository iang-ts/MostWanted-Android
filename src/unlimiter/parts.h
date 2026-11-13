#pragma once
#include <cstdint>
#include <string>

struct X86Context;

namespace unlimiter {

uint32_t HashFrom(const char* text, uint32_t start);
uint32_t PartAt(uint32_t ride, uint32_t slot);
void PutPart(uint32_t ride, uint32_t slot, uint32_t part);
uint32_t FindPart(X86Context& ctx, uint32_t car_type, uint32_t slot, uint32_t hash);
bool HasParts(X86Context& ctx, uint32_t car_type, uint32_t slot);
bool PartAttribute(uint32_t part, uint32_t key, uint32_t& value);
float PartFloat(uint32_t part, uint32_t key, float fallback);
int PartInt(uint32_t part, uint32_t key, int fallback);
uint32_t CurrentCarType(X86Context& ctx);
bool StringExists(X86Context& ctx, uint32_t hash);
bool TextureExists(X86Context& ctx, uint32_t hash);
uint32_t Label(X86Context& ctx, uint32_t hash, const std::string& own_text, bool replace = false);
inline constexpr int kLanguageCount = 7;
int LanguageIndex(X86Context& ctx);
uint32_t AddOwnOption(X86Context& ctx, uint32_t screen, uint32_t package, uint32_t icon, uint32_t title, uint32_t menu);
bool IsOwnMenu(uint32_t menu);
uint32_t OptionCount();
bool VinylGroupMenu(uint32_t menu);
int VinylGroupIndex(uint32_t menu);
bool VinylAltCamera(uint32_t menu);
bool ForceVinylColourBrand(uint32_t ret, uint32_t part, uint32_t key);
uint32_t PaintCartFilter(X86Context& ctx, uint32_t brand);
int VinylOption(int group);
int DecalPlace();
bool SetupDecalSlots(X86Context& ctx, uint32_t screen);
void AddExtraDecalSlots(X86Context& ctx, uint32_t screen);
uint32_t DecalTextureSlot(uint32_t menu);
void BeginOptions(uint32_t screen);
void NoteOption(uint32_t screen, uint32_t option);
void PlaceInitial(uint32_t screen);
void EndOptions(uint32_t screen);

}  // namespace unlimiter
