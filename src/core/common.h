#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

struct MemoryError : std::runtime_error
{
    MemoryError() : std::runtime_error("game memory is not readable right now") {}
};

struct UnsupportedGame : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

void readBytes(uint32_t address, void* out, size_t size);

template <class T>
T read(uint32_t address)
{
    T value;
    readBytes(address, &value, sizeof value);
    return value;
}

template <class T>
std::vector<T> readArray(uint32_t address, int64_t count)
{
    if (count < 0 || count * static_cast<int64_t>(sizeof(T)) > (64 << 20))
    {
        throw MemoryError();
    }
    std::vector<T> values(static_cast<size_t>(count));
    readBytes(address, values.data(), values.size() * sizeof(T));
    return values;
}

template <class T>
std::vector<T> readCountedArray(uint32_t address)
{
    return readArray<T>(read<uint32_t>(address), std::clamp(read<int32_t>(address + 4), 0, 1 << 22));
}

std::string readString(uint32_t address, int maximumLength = 128);

void* patchPointer(void** slot, void* value);

void check(long result, const char* what);

std::string utf8(const std::wstring& text);
const std::wstring& moduleDirectory();
void logLine(const std::string& line);
void report(const char* context, const std::exception& error);
