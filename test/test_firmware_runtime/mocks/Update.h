#pragma once
#include <cstddef>
#include <vector>
constexpr int U_FLASH = 0;
constexpr int U_SPIFFS = 100;
struct UpdateMock {
    bool beginOk = true, writeOk = true, endOk = true;
    int beginCalls = 0, abortCalls = 0, finalizeCalls = 0;
    std::vector<unsigned char> staged;
    bool begin(size_t, int) { ++beginCalls; return beginOk; }
    size_t write(unsigned char* data, size_t size) {
        if (!writeOk) return 0;
        staged.insert(staged.end(), data, data + size);
        return size;
    }
    void abort() { ++abortCalls; staged.clear(); }
    bool end(bool) { ++finalizeCalls; return endOk; }
};
inline UpdateMock Update;
