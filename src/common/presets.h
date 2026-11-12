#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace presets {

void SetDir(const std::filesystem::path& dir);
void SetSaves(const std::filesystem::path& dir);
bool SavesWritten();
bool Read(const std::string& profile, std::vector<uint8_t>& out);
void Write(const std::string& profile, const std::vector<uint8_t>& data);

}  // namespace presets
