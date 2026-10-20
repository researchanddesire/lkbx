#pragma once
#include <string>
class String {
    std::string value;
public:
    String() = default;
    String(const char* text) : value(text ? text : "") {}
    String(int number) : value(std::to_string(number)) {}
    String(std::string text) : value(std::move(text)) {}
    bool endsWith(const char* suffix) const {
        const std::string end(suffix);
        return value.size() >= end.size() && value.compare(value.size()-end.size(), end.size(), end) == 0;
    }
    void remove(size_t from) { value.erase(from); }
    size_t length() const { return value.size(); }
    void clear() { value.clear(); }
    void concat(const char* text, size_t count) { value.append(text, count); }
    bool isEmpty() const { return value.empty(); }
    const char* c_str() const { return value.c_str(); }
    String& operator+=(const char* text) { value += text; return *this; }
    friend String operator+(const char* left, const String& right) { return String(std::string(left)+right.value); }
};
