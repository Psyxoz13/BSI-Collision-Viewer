#include "model/settings.h"

#include <windows.h>

#include <cwchar>
#include <format>

const char* const PlayerStateRowLabels[PlayerStateRowCount] = {"Speed", "Physics state", "World collision", "Player cylinder", "Position"};

namespace
{
    constexpr wchar_t Section[] = L"CollisionViewer";

    std::wstring text(const std::wstring& path, const std::wstring& key)
    {
        wchar_t buffer[128] = {};
        GetPrivateProfileStringW(Section, key.c_str(), L"", buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());
        return buffer;
    }

    float number(const std::wstring& path, const std::wstring& key, float fallback)
    {
        std::wstring value = text(path, key);
        wchar_t* end = nullptr;
        float parsed = wcstof(value.c_str(), &end);
        return end != value.c_str() ? parsed : fallback;
    }

    Float3 color(const std::wstring& path, const std::wstring& key, Float3 fallback)
    {
        Float3 value;
        return swscanf_s(text(path, key).c_str(), L"%f %f %f", &value.x, &value.y, &value.z) == 3 ? value : fallback;
    }

    bool write(const std::wstring& path, const std::wstring& key, const std::wstring& value)
    {
        return WritePrivateProfileStringW(Section, key.c_str(), value.c_str(), path.c_str()) != FALSE;
    }
}

void Settings::normalize()
{
    const Settings defaults;
    for (int k = 0; k < KindCount; k++)
    {
        Float3& c = colors[k];
        c = std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z)
                ? Float3(std::clamp(c.x, 0.f, 1.f), std::clamp(c.y, 0.f, 1.f), std::clamp(c.z, 0.f, 1.f))
                : defaults.colors[k];
    }
    if (occlusion < Occlusion::None || occlusion > Occlusion::DistanceFade)
    {
        occlusion = defaults.occlusion;
    }
    faceOpacity = OpacityRange.clamp(faceOpacity, defaults.faceOpacity);
    edgeOpacity = OpacityRange.clamp(edgeOpacity, defaults.edgeOpacity);
    drawDistance = DrawDistanceRange.clamp(drawDistance, defaults.drawDistance);
    fadeStart = FadeStartRange.clamp(fadeStart, defaults.fadeStart);
    fadeEnd = std::max(FadeEndRange.clamp(fadeEnd, defaults.fadeEnd), fadeStart + 1);
    fovScale = FovScaleRange.clamp(fovScale, defaults.fovScale);
    playerStatePanelX = PanelPositionRange.clamp(playerStatePanelX, defaults.playerStatePanelX);
    playerStatePanelY = PanelPositionRange.clamp(playerStatePanelY, defaults.playerStatePanelY);
    overlayKey = isKeyboardKey(overlayKey) ? overlayKey : defaults.overlayKey;
    menuKey = isKeyboardKey(menuKey) ? menuKey : defaults.menuKey;
    playerStateKey = isKeyboardKey(playerStateKey) ? playerStateKey : defaults.playerStateKey;
}

bool isKeyboardKey(int binding)
{
    int key = binding & Hotkey::KeyMask;
    return (binding & ~(Hotkey::KeyMask | Hotkey::ModifierMask)) == 0 && key >= VK_BACK && key <= 0xFE;
}

Settings loadSettings(const std::wstring& path)
{
    Settings settings;
    for (int k = 0; k < KindCount; k++)
    {
        settings.visible[k] = number(path, std::format(L"Show{}", k), settings.visible[k]) != 0;
        settings.occluding[k] = number(path, std::format(L"Occlude{}", k), settings.occluding[k]) != 0;
        settings.colors[k] = color(path, std::format(L"Color{}", k), settings.colors[k]);
    }
    settings.enabled = number(path, L"Enabled", settings.enabled) != 0;
    settings.drawFaces = number(path, L"Faces", settings.drawFaces) != 0;
    settings.drawEdges = number(path, L"Edges", settings.drawEdges) != 0;
    settings.occlusion = static_cast<Occlusion>(static_cast<int>(number(path, L"Mode", static_cast<float>(settings.occlusion))));
    settings.faceOpacity = number(path, L"FaceAlpha", settings.faceOpacity);
    settings.edgeOpacity = number(path, L"EdgeAlpha", settings.edgeOpacity);
    settings.drawDistance = number(path, L"Distance", settings.drawDistance);
    settings.fadeStart = number(path, L"FadeStart", settings.fadeStart);
    settings.fadeEnd = number(path, L"FadeEnd", settings.fadeEnd);
    settings.fovScale = number(path, L"FovScale", settings.fovScale);
    settings.playerStatePanel = number(path, L"PlayerStatePanel", settings.playerStatePanel) != 0;
    for (int r = 0; r < PlayerStateRowCount; r++)
    {
        settings.playerStateRows[r] = number(path, std::format(L"PlayerStateRow{}", r), settings.playerStateRows[r]) != 0;
    }
    settings.playerStatePanelX = number(path, L"PlayerStatePanelX", settings.playerStatePanelX);
    settings.playerStatePanelY = number(path, L"PlayerStatePanelY", settings.playerStatePanelY);
    settings.overlayKey = static_cast<int>(number(path, L"OverlayKey", static_cast<float>(settings.overlayKey)));
    settings.menuKey = static_cast<int>(number(path, L"MenuKey", static_cast<float>(settings.menuKey)));
    settings.playerStateKey = static_cast<int>(number(path, L"PlayerStateKey", static_cast<float>(settings.playerStateKey)));
    settings.normalize();
    return settings;
}

void saveSettings(const Settings& settings, const std::wstring& path)
{
    bool saved = true;
    for (int k = 0; k < KindCount; k++)
    {
        const Float3& c = settings.colors[k];
        saved &= write(path, std::format(L"Show{}", k), settings.visible[k] ? L"1" : L"0");
        saved &= write(path, std::format(L"Occlude{}", k), settings.occluding[k] ? L"1" : L"0");
        saved &= write(path, std::format(L"Color{}", k), std::format(L"{} {} {}", c.x, c.y, c.z));
    }
    saved &= write(path, L"Enabled", settings.enabled ? L"1" : L"0");
    saved &= write(path, L"Faces", settings.drawFaces ? L"1" : L"0");
    saved &= write(path, L"Edges", settings.drawEdges ? L"1" : L"0");
    saved &= write(path, L"Mode", std::to_wstring(static_cast<int>(settings.occlusion)));
    saved &= write(path, L"FaceAlpha", std::format(L"{}", settings.faceOpacity));
    saved &= write(path, L"EdgeAlpha", std::format(L"{}", settings.edgeOpacity));
    saved &= write(path, L"Distance", std::format(L"{}", settings.drawDistance));
    saved &= write(path, L"FadeStart", std::format(L"{}", settings.fadeStart));
    saved &= write(path, L"FadeEnd", std::format(L"{}", settings.fadeEnd));
    saved &= write(path, L"FovScale", std::format(L"{}", settings.fovScale));
    saved &= write(path, L"PlayerStatePanel", settings.playerStatePanel ? L"1" : L"0");
    for (int r = 0; r < PlayerStateRowCount; r++)
    {
        saved &= write(path, std::format(L"PlayerStateRow{}", r), settings.playerStateRows[r] ? L"1" : L"0");
    }
    saved &= write(path, L"PlayerStatePanelX", std::format(L"{}", settings.playerStatePanelX));
    saved &= write(path, L"PlayerStatePanelY", std::format(L"{}", settings.playerStatePanelY));
    saved &= write(path, L"OverlayKey", std::to_wstring(settings.overlayKey));
    saved &= write(path, L"MenuKey", std::to_wstring(settings.menuKey));
    saved &= write(path, L"PlayerStateKey", std::to_wstring(settings.playerStateKey));
    if (!saved)
    {
        logLine("the settings could not be saved beside d3d11.dll");
    }
}
