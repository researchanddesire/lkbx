#pragma once
#include <string>
class String {
    std::string value;
public:
    String() = default;
    String(const char *v) : value(v) {}
    explicit String(std::string v) : value(std::move(v)) {}
    const char *c_str() const { return value.c_str(); }
    size_t length() const { return value.size(); }
    void clear() { value.clear(); }
    bool concat(const char *v, size_t n) { value.append(v, n); return true; }
};
struct JsonDocument { std::string text; };
inline size_t measureJson(const JsonDocument &doc) { return doc.text.size(); }
inline void serializeJson(const JsonDocument &doc, String &out) { out = String(doc.text); }
