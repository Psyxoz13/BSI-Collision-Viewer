#include "host/overlay.h"

#include "core/common.h"
#include "input/hotkeys.h"
#include "input/input.h"
#include "model/game.h"
#include "model/settings.h"
#include "view/interface.h"
#include "view/renderer.h"

#include <d3d11_1.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <wrl/client.h>

#include <atomic>
#include <format>
#include <memory>
#include <thread>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

namespace
{
    using Microsoft::WRL::ComPtr;

    std::mutex g_gameDevicesMutex;
    std::vector<ID3D11Device*> g_gameDevices;

    std::atomic<Game*> g_game;
    std::atomic<bool> g_invalidated;
    std::atomic<bool> g_collecting{true};
    std::mutex g_pendingMutex;
    std::unique_ptr<Scene> g_pendingScene;

    std::mutex g_imguiMutex;
    HWND g_window;
    WNDPROC g_gameWndProc;

    ComPtr<ID3D11Device> g_device;
    ComPtr<ID3D11DeviceContext1> g_context;
    ComPtr<ID3DDeviceContextState> g_state;
    std::unique_ptr<Renderer> g_renderer;
    Settings g_settings;
    Scene g_scene;
    Hotkeys g_hotkeys;
    SettingsPanel g_settingsPanel(g_settings, g_hotkeys);
    PlayerStateSettingsPanel g_playerStateSettingsPanel(g_settings, g_hotkeys);
    PlayerStatePanel g_playerStatePanel(g_settings);

    const std::wstring& settingsPath()
    {
        static const std::wstring path = moduleDirectory() + L"CollisionViewer.ini";
        return path;
    }

    void logScene(const Scene& scene)
    {
        std::array<int, KindCount> triangles = {}, groups = {};
        for (const Group& group : scene.groups)
        {
            triangles[static_cast<int>(group.kind)] += group.faces.count / 3;
            groups[static_cast<int>(group.kind)]++;
        }
        std::string text = std::format("collision: {} triangles in {} groups", scene.triangleCount(), scene.groups.size());
        for (int k = 0; k < KindCount; k++)
        {
            if (groups[k] != 0)
            {
                text += std::format("\n  {:<44}{:>8} triangles in {:>5} groups", KindLabels[k], triangles[k], groups[k]);
            }
        }
        logLine(text);
    }

    void buildStep(Game& game, std::optional<std::vector<ActorList>>& loaded)
    {
        try
        {
            if (!g_collecting)
            {
                loaded.reset();
                return;
            }
            std::vector<ActorList> signature = game.sceneSignature();
            if (g_invalidated.exchange(false) || signature != loaded)
            {
                auto scene = std::make_unique<Scene>(game.buildCollision(signature));
                logScene(*scene);
                std::lock_guard lock(g_pendingMutex);
                g_pendingScene = std::move(scene);
                loaded = std::move(signature);
            }
        }
        catch (const std::exception& e)
        {
            report("building the collision", e);
        }
    }

    bool guardedBuildStep(Game& game, std::optional<std::vector<ActorList>>& loaded)
    {
        __try
        {
            buildStep(game, loaded);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void runWorker()
    {
        Game* game = nullptr;
        for (std::string lastStatus; !game;)
        {
            try
            {
                game = new Game();
            }
            catch (const std::exception& e)
            {
                std::string status = dynamic_cast<const MemoryError*>(&e) ? "the game is still starting..." : std::string("waiting: ") + e.what();
                if (status != lastStatus)
                {
                    logLine(status);
                }
                lastStatus = status;
                Sleep(2000);
            }
        }
        logLine("attached: " + game->describe());
        g_game = game;
        std::optional<std::vector<ActorList>> loaded;
        for (bool faulted = false;;)
        {
            if (!guardedBuildStep(*game, loaded) && !faulted)
            {
                faulted = true;
                logLine("reading the collision faulted; that scan was dropped and the game was left alone");
            }
            Sleep(500);
        }
    }

    LRESULT CALLBACK wndProcHook(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (isInputLocked())
        {
            {
                std::unique_lock lock(g_imguiMutex, std::try_to_lock);
                if (lock.owns_lock())
                {
                    ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
                }
            }
            if (message == WM_INPUT || (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || (message >= WM_KEYFIRST && message <= WM_KEYLAST))
            {
                return DefWindowProcW(window, message, wParam, lParam);
            }
        }
        return CallWindowProcW(g_gameWndProc, window, message, wParam, lParam);
    }

    void startInterface(HWND window, ID3D11Device* device, ID3D11DeviceContext* context)
    {
        std::lock_guard lock(g_imguiMutex);
        if (ImGui::GetCurrentContext())
        {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplDX11_Init(device, context);
            return;
        }
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        static const std::string layoutPath = utf8(moduleDirectory() + L"CollisionViewer.imgui.ini");
        io.IniFilename = layoutPath.c_str();
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        ImGui_ImplWin32_Init(window);
        ImGui_ImplDX11_Init(device, context);
        g_settings = loadSettings(settingsPath());
        installInputLock();
        g_window = window;
        g_gameWndProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wndProcHook));
    }

    bool bind(IDXGISwapChain* swapChain, ID3D11Device* device)
    {
        static ID3D11Device* failedDevice = nullptr;
        if (device == failedDevice)
        {
            return false;
        }
        try
        {
            DXGI_SWAP_CHAIN_DESC desc;
            check(swapChain->GetDesc(&desc), "IDXGISwapChain::GetDesc");
            ComPtr<ID3D11DeviceContext> immediate;
            device->GetImmediateContext(&immediate);
            ComPtr<ID3D11DeviceContext1> context;
            ComPtr<ID3D11Device1> device1;
            check(immediate.As(&context), "getting the D3D 11.1 context");
            check(device->QueryInterface(IID_PPV_ARGS(&device1)), "getting the D3D 11.1 device");
            D3D_FEATURE_LEVEL level = device->GetFeatureLevel();
            UINT flags = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
            ComPtr<ID3DDeviceContextState> state;
            check(device1->CreateDeviceContextState(flags, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), nullptr, &state),
                  "creating the overlay's context state");
            auto renderer = std::make_unique<Renderer>(device);
            renderer->upload(g_scene);
            startInterface(desc.OutputWindow, device, immediate.Get());
            g_device = device;
            g_context = context;
            g_state = state;
            g_renderer = std::move(renderer);
            return true;
        }
        catch (const std::exception& e)
        {
            logLine(std::string("the overlay cannot draw with this device: ") + e.what());
            failedDevice = device;
            return false;
        }
    }

    void setMenuOpen(bool open)
    {
        setInputLocked(open);
        g_hotkeys.stopBinding();
        if (!open)
        {
            saveSettings(g_settings, settingsPath());
        }
    }

    void handleHotkeys()
    {
        bool focused = GetForegroundWindow() == g_window;
        bool toggleOverlay = g_hotkeys.pressed(g_settings.overlayKey);
        bool toggleMenu = g_hotkeys.pressed(g_settings.menuKey);
        bool togglePlayerState = g_hotkeys.pressed(g_settings.playerStateKey);
        if (!focused)
        {
            if (isInputLocked())
            {
                setMenuOpen(false);
            }
            return;
        }
        if (g_hotkeys.isBinding())
        {
            return;
        }
        if (toggleOverlay)
        {
            g_settings.enabled = !g_settings.enabled;
            saveSettings(g_settings, settingsPath());
        }
        if (togglePlayerState)
        {
            g_settings.playerStatePanel = !g_settings.playerStatePanel;
            saveSettings(g_settings, settingsPath());
        }
        if (toggleMenu)
        {
            setMenuOpen(!isInputLocked());
        }
    }

    void takePendingScene()
    {
        std::unique_ptr<Scene> scene;
        {
            std::lock_guard lock(g_pendingMutex);
            scene = std::move(g_pendingScene);
        }
        if (!scene)
        {
            return;
        }
        try
        {
            g_renderer->upload(*scene);
            g_scene = std::move(*scene);
        }
        catch (const std::exception& e)
        {
            report("uploading the collision", e);
            g_invalidated = true;
        }
    }

    void drawInterface(ID3D11RenderTargetView* target, int characters, const PlayerState& state)
    {
        if (!isInputLocked() && !g_settings.playerStatePanel)
        {
            return;
        }
        std::lock_guard lock(g_imguiMutex);
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        bool menuOpen = isInputLocked();
        io.MouseDrawCursor = menuOpen;
        io.FontGlobalScale = std::max(1.f, io.DisplaySize.y / 1080);
        ImGui::NewFrame();
        if (menuOpen)
        {
            if (g_hotkeys.isBinding())
            {
                g_hotkeys.capture();
            }
            g_playerStateSettingsPanel.draw();
            if (!g_settingsPanel.draw({g_scene.triangleCount(), g_scene.trackedObjectCount(), characters}))
            {
                setMenuOpen(false);
            }
        }
        if (g_settings.playerStatePanel)
        {
            g_playerStatePanel.draw(state);
        }
        ImGui::Render();
        g_context->OMSetRenderTargets(1, &target, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    class OwnContextState
    {
    public:
        OwnContextState() { g_context->SwapDeviceContextState(g_state.Get(), &gameState_); }

        ~OwnContextState()
        {
            g_context->OMSetRenderTargets(0, nullptr, nullptr);
            g_context->SwapDeviceContextState(gameState_.Get(), nullptr);
        }

    private:
        ComPtr<ID3DDeviceContextState> gameState_;
    };

    void drawFrame(IDXGISwapChain* swapChain)
    {
        handleHotkeys();
        g_collecting = g_settings.enabled || isInputLocked();
        takePendingScene();
        if (!g_settings.enabled && !g_settings.playerStatePanel && !isInputLocked())
        {
            return;
        }
        CharacterSnapshot characters;
        PlayerState playerState;
        std::optional<CameraPose> camera;
        if (Game* game = g_game; game && g_settings.playerStatePanel)
        {
            try
            {
                playerState = game->engine.readPlayerState();
            }
            catch (const std::exception& e)
            {
                report("the player state", e);
            }
        }
        if (Game* game = g_game; game && g_settings.enabled)
        {
            game->track(g_scene);
            if (g_scene.hasLostTracking())
            {
                g_invalidated = true;
            }
            characters = game->engine.readCharacters(g_scene.characterCylinders);
            try
            {
                camera = game->engine.readCamera();
            }
            catch (const std::exception& e)
            {
                report("the camera", e);
                g_invalidated = true;
            }
        }
        ComPtr<ID3D11Texture2D> backBuffer;
        check(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "IDXGISwapChain::GetBuffer");
        D3D11_TEXTURE2D_DESC desc;
        backBuffer->GetDesc(&desc);
        ComPtr<ID3D11RenderTargetView> target;
        check(g_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &target), "creating the back buffer view");

        OwnContextState ownState;
        if (camera)
        {
            g_renderer->draw(g_context.Get(), target.Get(), desc, g_scene, characters, *camera, g_settings);
        }
        drawInterface(target.Get(), characters.count, playerState);
    }

    bool isGameDevice(ID3D11Device* device)
    {
        std::lock_guard lock(g_gameDevicesMutex);
        return std::find(g_gameDevices.begin(), g_gameDevices.end(), device) != g_gameDevices.end();
    }

    void presentOverlay(IDXGISwapChain* swapChain)
    {
        try
        {
            ComPtr<ID3D11Device> device;
            if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device))) || !isGameDevice(device.Get()))
            {
                return;
            }
            if (device != g_device && !bind(swapChain, device.Get()))
            {
                return;
            }
            drawFrame(swapChain);
        }
        catch (const std::exception& e)
        {
            report("a frame", e);
        }
    }

    std::atomic<bool> g_faulted;

    void guardedPresentOverlay(IDXGISwapChain* swapChain)
    {
        __try
        {
            presentOverlay(swapChain);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_faulted = true;
        }
    }

    using PresentFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
    PresentFunction g_present;

    HRESULT STDMETHODCALLTYPE presentHook(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
    {
        if (!g_faulted)
        {
            guardedPresentOverlay(swapChain);
            if (g_faulted)
            {
                setInputLocked(false);
                logLine("drawing the overlay faulted; it stays off for the rest of this session, the game keeps running");
            }
        }
        return g_present(swapChain, syncInterval, flags);
    }

    bool hookPresent(ID3D11Device* device)
    {
        constexpr int PresentSlot = 8;
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory> factory;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
            FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
        {
            return false;
        }
        HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, nullptr, nullptr);
        DXGI_SWAP_CHAIN_DESC desc = {};
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1;
        desc.OutputWindow = window;
        desc.Windowed = TRUE;
        ComPtr<IDXGISwapChain> swapChain;
        bool hooked = false;
        if (window && SUCCEEDED(factory->CreateSwapChain(device, &desc, &swapChain)))
        {
            void** vtable = *reinterpret_cast<void***>(swapChain.Get());
            g_present = reinterpret_cast<PresentFunction>(vtable[PresentSlot]);
            hooked = patchPointer(&vtable[PresentSlot], reinterpret_cast<void*>(presentHook)) != nullptr;
        }
        swapChain.Reset();
        if (window)
        {
            DestroyWindow(window);
        }
        return hooked;
    }

    bool isGameProcess()
    {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        const wchar_t* name = wcsrchr(path, L'\\');
        return name && _wcsicmp(name + 1, L"BioShockInfinite.exe") == 0;
    }
}

void overlayDeviceCreated(ID3D11Device* device)
{
    static const bool isGame = isGameProcess();
    static bool presentHooked = false;
    if (!isGame)
    {
        return;
    }
    {
        std::lock_guard lock(g_gameDevicesMutex);
        g_gameDevices.push_back(device);
        if (g_gameDevices.size() == 1)
        {
            std::thread(runWorker).detach();
        }
    }
    if (!presentHooked)
    {
        presentHooked = hookPresent(device);
    }
}
