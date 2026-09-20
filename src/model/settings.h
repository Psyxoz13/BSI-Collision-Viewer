#pragma once

#include "model/collision.h"

#include <windows.h>

#include <cmath>

namespace Hotkey
{
    constexpr int KeyMask = 0xFF;
    constexpr int Ctrl = 1 << 8;
    constexpr int Shift = 1 << 9;
    constexpr int Alt = 1 << 10;
    constexpr int ModifierMask = Ctrl | Shift | Alt;
}

enum class Occlusion { None, NearestCollider, DistanceFade };

enum class PlayerStateRow { Speed, Physics, WorldCollision, Cylinder, Position, Count };
constexpr int PlayerStateRowCount = static_cast<int>(PlayerStateRow::Count);
extern const char* const PlayerStateRowLabels[PlayerStateRowCount];

struct ValueRange
{
    float minimum;
    float maximum;

    float clamp(float value, float fallback) const { return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback; }
};

struct Settings
{
    static constexpr ValueRange OpacityRange{0, 1};
    static constexpr ValueRange DrawDistanceRange{5, 5000};
    static constexpr ValueRange FadeStartRange{1, 2000};
    static constexpr ValueRange FadeEndRange{1, 5000};
    static constexpr ValueRange FovScaleRange{0.8f, 1.2f};
    static constexpr ValueRange PanelPositionRange{0, 1};

    std::array<bool, KindCount> visible = {true, true, true, true, true, true, true, true, true};
    std::array<bool, KindCount> occluding = {true, true, true, true, true, true, false, false, false};
    std::array<Float3, KindCount> colors = {{{0.2f, 0.85f, 1}, {0.3f, 1, 0.35f}, {1, 0.85f, 0.2f}, {1, 0.3f, 0.9f}, {1, 0.55f, 0.15f},
                                             {1, 0.25f, 0.25f}, {0.6f, 0.6f, 0.65f}, {0.6f, 0.35f, 1}, {0.3f, 0.6f, 0.6f}}};
    bool enabled = true;
    bool drawFaces = true;
    bool drawEdges = true;
    Occlusion occlusion = Occlusion::NearestCollider;
    float faceOpacity = 0.15f;
    float edgeOpacity = 0.6f;
    float drawDistance = 300;
    float fadeStart = 15;
    float fadeEnd = 60;
    float fovScale = 1;
    bool playerStatePanel = false;
    std::array<bool, PlayerStateRowCount> playerStateRows = {true, true, true, true, true};
    float playerStatePanelX = 0.02f;
    float playerStatePanelY = 0.02f;
    int overlayKey = VK_F8;
    int menuKey = VK_F7;
    int playerStateKey = VK_F6;

    bool shows(PlayerStateRow row) const { return playerStateRows[static_cast<int>(row)]; }
    bool isVisible(Kind kind) const { return visible[static_cast<int>(kind)]; }
    bool isOccluder(Kind kind) const { return isVisible(kind) && occluding[static_cast<int>(kind)]; }
    void normalize();
};

bool isKeyboardKey(int binding);
Settings loadSettings(const std::wstring& path);
void saveSettings(const Settings& settings, const std::wstring& path);
