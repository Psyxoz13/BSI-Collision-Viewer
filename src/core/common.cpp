#include "core/common.h"

#include <windows.h>

#include <cstring>
#include <format>
#include <fstream>
#include <mutex>
#include <set>

namespace
{
    bool copyGuarded(void* out, uint32_t address, size_t size)
    {
        __try
        {
            memcpy(out, reinterpret_cast<const void*>(address), size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

void readBytes(uint32_t address, void* out, size_t size)
{
    if (address < 0x10000 || !copyGuarded(out, address, size))
    {
        throw MemoryError();
    }
}

std::string readString(uint32_t address, int maximumLength)
{
    std::string text;
    for (int i = 0; i < maximumLength; i++)
    {
        char c = 0;
        if (i == 0)
        {
            c = read<char>(address);
        }
        else if (!copyGuarded(&c, address + i, 1))
        {
            break;
        }
        if (c == 0)
        {
            break;
        }
        text += c;
    }
    return text;
}

void* patchPointer(void** slot, void* value)
{
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &protection))
    {
        return nullptr;
    }
    void* previous = *slot;
    *slot = value;
    VirtualProtect(slot, sizeof(void*), protection, &protection);
    return previous;
}

void check(long result, const char* what)
{
    if (FAILED(result))
    {
        throw std::runtime_error(std::format("{} failed (0x{:08x})", what, static_cast<unsigned long>(result)));
    }
}

std::string utf8(const std::wstring& text)
{
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

const std::wstring& moduleDirectory()
{
    static const std::wstring directory = []
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&moduleDirectory), &self);
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(self, path, MAX_PATH);
        std::wstring text = path;
        return text.substr(0, text.find_last_of(L"\\/") + 1);
    }();
    return directory;
}

void logLine(const std::string& line)
{
    static std::mutex mutex;
    static std::ofstream file(moduleDirectory() + L"CollisionViewer.log");
    std::lock_guard lock(mutex);
    file << line << std::endl;
}

void report(const char* context, const std::exception& error)
{
    if (dynamic_cast<const MemoryError*>(&error))
    {
        return;
    }
    static std::mutex mutex;
    static std::set<std::string> reported;
    std::string line = std::string("skipped ") + context + ": " + error.what();
    {
        std::lock_guard lock(mutex);
        if (!reported.insert(line).second)
        {
            return;
        }
    }
    logLine(line);
}
