#include "host/overlay.h"

#include <windows.h>

#include <string>

static_assert(sizeof(void*) == 4, "BioShock Infinite is a 32-bit game: build the Win32 platform");

namespace
{
    HMODULE systemD3D11()
    {
        static const HMODULE module = []
        {
            wchar_t directory[MAX_PATH] = {};
            GetSystemDirectoryW(directory, MAX_PATH);
            return LoadLibraryW((std::wstring(directory) + L"\\d3d11.dll").c_str());
        }();
        return module;
    }
}

extern "C" HRESULT WINAPI Proxy_D3D11CreateDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driverType, HMODULE software, UINT flags,
                                                  const D3D_FEATURE_LEVEL* featureLevels, UINT featureLevelCount, UINT sdkVersion,
                                                  ID3D11Device** device, D3D_FEATURE_LEVEL* featureLevel, ID3D11DeviceContext** context)
{
    static const auto create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(systemD3D11(), "D3D11CreateDevice"));
    if (!create)
    {
        return E_FAIL;
    }
    HRESULT result = create(adapter, driverType, software, flags, featureLevels, featureLevelCount, sdkVersion, device, featureLevel, context);
    if (SUCCEEDED(result) && device && *device)
    {
        overlayDeviceCreated(*device);
    }
    return result;
}
#pragma comment(linker, "/export:D3D11CreateDevice=_Proxy_D3D11CreateDevice@40")

#define FORWARD(name)                                                                                  \
    extern "C" FARPROC __stdcall Resolve_##name()                                                      \
    {                                                                                                  \
        static const FARPROC real = GetProcAddress(systemD3D11(), #name);                              \
        return real;                                                                                   \
    }                                                                                                  \
    extern "C" __declspec(naked) void Forward_##name()                                                 \
    {                                                                                                  \
        __asm call Resolve_##name                                                                      \
        __asm jmp eax                                                                                  \
    }                                                                                                  \
    __pragma(comment(linker, "/export:" #name "=_Forward_" #name))

FORWARD(CreateDirect3D11DeviceFromDXGIDevice)
FORWARD(CreateDirect3D11SurfaceFromDXGISurface)
FORWARD(D3D11CoreCreateDevice)
FORWARD(D3D11CoreCreateLayeredDevice)
FORWARD(D3D11CoreGetLayeredDeviceSize)
FORWARD(D3D11CoreRegisterLayers)
FORWARD(D3D11CreateDeviceAndSwapChain)
FORWARD(D3D11CreateDeviceForD3D12)
FORWARD(D3D11On12CreateDevice)
FORWARD(D3DKMTCloseAdapter)
FORWARD(D3DKMTCreateAllocation)
FORWARD(D3DKMTCreateContext)
FORWARD(D3DKMTCreateDevice)
FORWARD(D3DKMTCreateSynchronizationObject)
FORWARD(D3DKMTDestroyAllocation)
FORWARD(D3DKMTDestroyContext)
FORWARD(D3DKMTDestroyDevice)
FORWARD(D3DKMTDestroySynchronizationObject)
FORWARD(D3DKMTEscape)
FORWARD(D3DKMTGetContextSchedulingPriority)
FORWARD(D3DKMTGetDeviceState)
FORWARD(D3DKMTGetDisplayModeList)
FORWARD(D3DKMTGetMultisampleMethodList)
FORWARD(D3DKMTGetRuntimeData)
FORWARD(D3DKMTGetSharedPrimaryHandle)
FORWARD(D3DKMTLock)
FORWARD(D3DKMTOpenAdapterFromHdc)
FORWARD(D3DKMTOpenResource)
FORWARD(D3DKMTPresent)
FORWARD(D3DKMTQueryAdapterInfo)
FORWARD(D3DKMTQueryAllocationResidency)
FORWARD(D3DKMTQueryResourceInfo)
FORWARD(D3DKMTRender)
FORWARD(D3DKMTSetAllocationPriority)
FORWARD(D3DKMTSetContextSchedulingPriority)
FORWARD(D3DKMTSetDisplayMode)
FORWARD(D3DKMTSetDisplayPrivateDriverFormat)
FORWARD(D3DKMTSetGammaRamp)
FORWARD(D3DKMTSetVidPnSourceOwner)
FORWARD(D3DKMTSignalSynchronizationObject)
FORWARD(D3DKMTUnlock)
FORWARD(D3DKMTWaitForSynchronizationObject)
FORWARD(D3DKMTWaitForVerticalBlankEvent)
FORWARD(D3DPerformance_BeginEvent)
FORWARD(D3DPerformance_EndEvent)
FORWARD(D3DPerformance_GetStatus)
FORWARD(D3DPerformance_SetMarker)
FORWARD(EnableFeatureLevelUpgrade)
FORWARD(OpenAdapter10)
FORWARD(OpenAdapter10_2)
