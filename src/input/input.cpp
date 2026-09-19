#include "input/input.h"

#include "core/common.h"

#include <windows.h>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <atomic>
#include <cstring>
#include <mutex>

namespace
{
    std::atomic<bool> g_locked;

    std::mutex g_clipMutex;
    RECT g_gameClip;
    bool g_gameClips;

    BOOL WINAPI clipCursorHook(const RECT* rect)
    {
        std::lock_guard lock(g_clipMutex);
        if (!g_locked)
        {
            return ClipCursor(rect);
        }
        g_gameClips = rect != nullptr;
        if (rect)
        {
            g_gameClip = *rect;
        }
        return TRUE;
    }

    BOOL WINAPI setCursorPosHook(int x, int y)
    {
        return g_locked ? TRUE : SetCursorPos(x, y);
    }

    void hookImport(void* original, void* hook)
    {
        auto base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        auto headers = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
        const IMAGE_DATA_DIRECTORY& imports = headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (imports.VirtualAddress == 0)
        {
            return;
        }
        for (auto library = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress); library->Name != 0; library++)
        {
            for (auto thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + library->FirstThunk); thunk->u1.Function != 0; thunk++)
            {
                if (thunk->u1.Function == reinterpret_cast<uintptr_t>(original))
                {
                    patchPointer(reinterpret_cast<void**>(&thunk->u1.Function), hook);
                }
            }
        }
    }

    using GetDeviceStateFunction = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, void*);
    using GetDeviceDataFunction = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, DIDEVICEOBJECTDATA*, DWORD*, DWORD);

    struct DeviceVtable
    {
        void** vtable;
        GetDeviceStateFunction getDeviceState;
        GetDeviceDataFunction getDeviceData;
    };

    DeviceVtable g_devices[4];
    std::atomic<int> g_deviceCount;

    const DeviceVtable& originalsOf(IDirectInputDevice8W* device)
    {
        void** vtable = *reinterpret_cast<void***>(device);
        for (int i = 0; i < g_deviceCount; i++)
        {
            if (g_devices[i].vtable == vtable)
            {
                return g_devices[i];
            }
        }
        return g_devices[0];
    }

    HRESULT STDMETHODCALLTYPE getDeviceStateHook(IDirectInputDevice8W* device, DWORD size, void* data)
    {
        HRESULT result = originalsOf(device).getDeviceState(device, size, data);
        if (SUCCEEDED(result) && data && g_locked)
        {
            memset(data, 0, size);
        }
        return result;
    }

    HRESULT STDMETHODCALLTYPE getDeviceDataHook(IDirectInputDevice8W* device, DWORD objectSize, DIDEVICEOBJECTDATA* data, DWORD* count, DWORD flags)
    {
        HRESULT result = originalsOf(device).getDeviceData(device, objectSize, data, count, flags);
        if (SUCCEEDED(result) && count && g_locked)
        {
            *count = 0;
        }
        return result;
    }

    void hookDirectInput()
    {
        HMODULE dinput = GetModuleHandleW(L"dinput8.dll");
        auto create = dinput ? reinterpret_cast<decltype(&DirectInput8Create)>(GetProcAddress(dinput, "DirectInput8Create")) : nullptr;
        if (!create)
        {
            return;
        }
        for (const IID* version : {&IID_IDirectInput8W, &IID_IDirectInput8A})
        {
            IDirectInput8W* input = nullptr;
            if (FAILED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, *version, reinterpret_cast<void**>(&input), nullptr)))
            {
                continue;
            }
            for (const GUID* kind : {&GUID_SysKeyboard, &GUID_SysMouse})
            {
                IDirectInputDevice8W* device = nullptr;
                if (FAILED(input->CreateDevice(*kind, &device, nullptr)))
                {
                    continue;
                }
                void** vtable = *reinterpret_cast<void***>(device);
                int count = g_deviceCount;
                bool known = false;
                for (int i = 0; i < count; i++)
                {
                    known |= g_devices[i].vtable == vtable;
                }
                if (!known && count < static_cast<int>(std::size(g_devices)))
                {
                    g_devices[count] = {vtable, reinterpret_cast<GetDeviceStateFunction>(vtable[9]), reinterpret_cast<GetDeviceDataFunction>(vtable[10])};
                    g_deviceCount = count + 1;
                    patchPointer(&vtable[9], reinterpret_cast<void*>(getDeviceStateHook));
                    patchPointer(&vtable[10], reinterpret_cast<void*>(getDeviceDataHook));
                }
                device->Release();
            }
            input->Release();
        }
    }
}

void installInputLock()
{
    hookImport(reinterpret_cast<void*>(&ClipCursor), reinterpret_cast<void*>(clipCursorHook));
    hookImport(reinterpret_cast<void*>(&SetCursorPos), reinterpret_cast<void*>(setCursorPosHook));
    hookDirectInput();
}

void setInputLocked(bool locked)
{
    std::lock_guard lock(g_clipMutex);
    if (locked == g_locked)
    {
        return;
    }
    if (locked)
    {
        g_gameClips = GetClipCursor(&g_gameClip) != FALSE;
        ClipCursor(nullptr);
    }
    else
    {
        ClipCursor(g_gameClips ? &g_gameClip : nullptr);
    }
    g_locked = locked;
}

bool isInputLocked()
{
    return g_locked;
}
