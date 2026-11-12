#include "unlimiter/ini.h"

#include <cctype>
#include <cstdlib>

#include "kernel/kernel.h"

namespace unlimiter {

namespace {

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

std::string StripComment(const std::string& s) {
    bool quoted = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '"') quoted = !quoted;
        if (quoted) continue;
        if (c == ';' || c == '#') return s.substr(0, i);
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') return s.substr(0, i);
    }
    return s;
}

std::string Unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
}

}  // namespace

std::string Upper(std::string text) {
    for (char& c : text) c = char(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

uint32_t BinHash(const std::string& text) {
    uint32_t h = 0xFFFFFFFF;
    for (unsigned char c : text) h = h * 33 + c;
    return h;
}

// Attrib::StringToKey (0x047520), Jenkins' lookup2 from 0xABCDEF00, of the text in lower case as the Unlimiter reads
// a vault name (Attrib_StringToLowerCaseKey)
uint32_t VltHash(const std::string& text) {
    auto mix = [](uint32_t& a, uint32_t& b, uint32_t& c) {
        a -= b + c; a ^= c >> 13;
        b -= c + a; b ^= a << 8;
        c -= a + b; c ^= b >> 13;
        a -= b + c; a ^= c >> 12;
        b -= c + a; b ^= a << 16;
        c -= a + b; c ^= b >> 5;
        a -= b + c; a ^= c >> 3;
        b -= c + a; b ^= a << 10;
        c -= a + b; c ^= b >> 15;
    };
    std::string k = text;
    for (char& ch : k) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    auto byte = [&](size_t i) { return uint32_t(uint8_t(k[i])); };
    auto word = [&](size_t i) { return byte(i) | byte(i + 1) << 8 | byte(i + 2) << 16 | byte(i + 3) << 24; };
    uint32_t a = 0x9E3779B9, b = 0x9E3779B9, c = 0xABCDEF00;
    size_t i = 0;
    for (; k.size() - i >= 12; i += 12) {
        a += word(i);
        b += word(i + 4);
        c += word(i + 8);
        mix(a, b, c);
    }
    c += uint32_t(k.size());
    for (size_t j = k.size() - i; j-- > 0;) {
        if (j >= 8) c += byte(i + j) << (8 * (j - 7));
        else if (j >= 4) b += byte(i + j) << (8 * (j - 4));
        else a += byte(i + j) << (8 * j);
    }
    mix(a, b, c);
    return c;
}

bool Ini::Load(const std::string& xbox_path) {
    std::vector<uint8_t> file;
    if (!kernel::ReadGameFile(xbox_path, file)) return false;
    Parse(std::string(file.begin(), file.end()));
    return true;
}

void Ini::Parse(const std::string& text) {
    std::string section;
    size_t at = 0;
    if (text.size() >= 3 && uint8_t(text[0]) == 0xEF && uint8_t(text[1]) == 0xBB && uint8_t(text[2]) == 0xBF) at = 3;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = Trim(StripComment(text.substr(at, end - at)));
        at = end + 1;
        if (line.empty()) continue;
        if (line.front() == '[') {
            size_t close = line.find(']');
            section = Upper(Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
            sections_[section];
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Upper(Trim(line.substr(0, eq)));
        if (key.empty()) continue;
        sections_[section][key] = Unquote(Trim(line.substr(eq + 1)));
    }
}

void Ini::Overlay(const Ini& other) {
    for (const auto& [name, keys] : other.sections_)
        for (const auto& [key, value] : keys) sections_[name][key] = value;
}

const Ini::Section* Ini::Find(const std::string& section) const {
    auto it = sections_.find(Upper(section));
    return it == sections_.end() ? nullptr : &it->second;
}

bool Ini::Has(const std::string& section, const std::string& key) const {
    const Section* s = Find(section);
    return s && s->count(Upper(key));
}

std::string Ini::Text(const std::string& section, const std::string& key, const std::string& fallback) const {
    const Section* s = Find(section);
    if (!s) return fallback;
    auto it = s->find(Upper(key));
    return it == s->end() ? fallback : it->second;
}

int Ini::Int(const std::string& section, const std::string& key, int fallback) const {
    std::string v = Text(section, key);
    if (v.empty()) return fallback;
    char* end = nullptr;
    long n = std::strtol(v.c_str(), &end, 0);
    return end == v.c_str() ? fallback : int(n);
}

float Ini::Float(const std::string& section, const std::string& key, float fallback) const {
    std::string v = Text(section, key);
    if (v.empty()) return fallback;
    char* end = nullptr;
    float n = std::strtof(v.c_str(), &end);
    return end == v.c_str() ? fallback : n;
}

uint32_t Ini::Hash(const std::string& section, const std::string& key, uint32_t fallback) const {
    if (!Has(section, key)) return fallback;
    std::string v = Text(section, key);
    if (v.size() == 10 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) return uint32_t(std::strtoul(v.c_str(), nullptr, 16));
    return BinHash(v);
}

std::vector<std::string> Ini::Sections() const {
    std::vector<std::string> out;
    for (const auto& [name, keys] : sections_) out.push_back(name);
    return out;
}

}  // namespace unlimiter
