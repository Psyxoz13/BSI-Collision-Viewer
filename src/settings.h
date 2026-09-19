#pragma once

#include "collision.h"

#include <windows.h>

#include <cmath>

enum class Occlusion { None, NearestCollider, DistanceFade };

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
    int overlayKey = VK_F8;
    int menuKey = VK_F7;

    bool isVisible(Kind kind) const { return visible[static_cast<int>(kind)]; }
    bool isOccluder(Kind kind) const { return isVisible(kind) && occluding[static_cast<int>(kind)]; }
    void normalize();
};

bool isKeyboardKey(int key);
Settings loadSettings(const std::wstring& path);
void saveSettings(const Settings& settings, const std::wstring& path);
