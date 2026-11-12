#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace unlimiter {

uint32_t BinHash(const std::string& text);
uint32_t VltHash(const std::string& text);

class Ini {
public:
    using Section = std::map<std::string, std::string>;

    bool Load(const std::string& xbox_path);
    void Parse(const std::string& text);
    void Overlay(const Ini& other);
    bool Empty() const { return sections_.empty(); }
    bool Has(const std::string& section, const std::string& key) const;
    int Int(const std::string& section, const std::string& key, int fallback) const;
    float Float(const std::string& section, const std::string& key, float fallback) const;
    std::string Text(const std::string& section, const std::string& key, const std::string& fallback = {}) const;
    uint32_t Hash(const std::string& section, const std::string& key, uint32_t fallback) const;
    const Section* Find(const std::string& section) const;
    std::vector<std::string> Sections() const;

private:
    std::map<std::string, Section> sections_;
};

std::string Upper(std::string text);

}  // namespace unlimiter
