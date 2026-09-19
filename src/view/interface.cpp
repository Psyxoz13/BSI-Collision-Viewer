#include "view/interface.h"

#include <imgui.h>

#include <algorithm>
#include <format>

namespace
{
    const char* physicsName(uint8_t physics)
    {
        static const char* const names[] = {"none", "walking", "falling", "swimming", "flying", "rotating", "projectile",
                                            "interpolating", "spider", "ladder", "rigid body", "soft body", "navmesh"};
        return physics < std::size(names) ? names[physics] : "custom";
    }

    void slider(const char* label, float& value, ValueRange range, const char* format = "%.3f", int flags = 0)
    {
        ImGui::SliderFloat(label, &value, range.minimum, range.maximum, format, flags);
    }

    void distanceSlider(const char* label, float& value, ValueRange range)
    {
        slider(label, value, range, "%.0f", ImGuiSliderFlags_Logarithmic);
    }

    void hotkeyToggle(const char* label, int key, bool& value)
    {
        ImGui::Checkbox(std::format("{} ({})###{}", label, Hotkeys::name(key), label).c_str(), &value);
    }

    void hotkeyButton(Hotkeys& hotkeys, const char* label, int& key)
    {
        bool binding = hotkeys.isBinding(key);
        std::string text = (binding ? std::string("Press a key...") : Hotkeys::name(key)) + "###" + label;
        if (ImGui::Button(text.c_str(), ImVec2(ImGui::GetFontSize() * 8, 0)))
        {
            hotkeys.toggleBinding(key);
        }
        ImGui::SetItemTooltip("Click, then press the new key; Esc cancels");
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
    }

    void kindRow(Settings& settings, int kind)
    {
        ImGui::PushID(kind);
        ImGui::Checkbox("##show", &settings.visible[kind]);
        ImGui::SetItemTooltip("Draw this kind");
        ImGui::SameLine();
        if (settings.occlusion == Occlusion::NearestCollider)
        {
            ImGui::Checkbox("##occlude", &settings.occluding[kind]);
            ImGui::SetItemTooltip("Hide the collision behind this kind; it stays hidden by the others either way");
            ImGui::SameLine();
        }
        ImGui::ColorEdit3(KindLabels[kind], &settings.colors[kind].x, ImGuiColorEditFlags_NoInputs);
        ImGui::PopID();
    }

    template <class... Values>
    void playerStateRow(const Settings& settings, PlayerStateRow row, const char* format, Values... values)
    {
        if (settings.shows(row))
        {
            ImGui::Text(format, values...);
        }
    }
}

bool Panel::render(bool* open)
{
    place();
    if (ImGui::Begin(title_, open, flags_))
    {
        body();
    }
    ImGui::End();
    return open == nullptr || *open;
}

SettingsPanel::SettingsPanel(Settings& settings, Hotkeys& hotkeys)
    : Panel("Collision viewer", ImGuiWindowFlags_AlwaysAutoResize), settings_(settings), hotkeys_(hotkeys)
{
}

bool SettingsPanel::draw(const Statistics& statistics)
{
    statistics_ = statistics;
    bool open = true;
    return render(&open);
}

void SettingsPanel::place() const
{
    ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
}

void SettingsPanel::body()
{
    static const char* const occlusionLabels[] = {"No occlusion", "Nearest collider", "Distance fade"};
    hotkeyToggle("Overlay", settings_.overlayKey, settings_.enabled);
    int occlusion = static_cast<int>(settings_.occlusion);
    ImGui::Combo("Mode", &occlusion, occlusionLabels, 3);
    settings_.occlusion = static_cast<Occlusion>(occlusion);
    ImGui::Checkbox("Faces", &settings_.drawFaces);
    ImGui::SameLine();
    ImGui::Checkbox("Edges", &settings_.drawEdges);
    slider("Face opacity", settings_.faceOpacity, Settings::OpacityRange);
    slider("Edge opacity", settings_.edgeOpacity, Settings::OpacityRange);
    if (settings_.occlusion == Occlusion::DistanceFade)
    {
        distanceSlider("Fade start", settings_.fadeStart, Settings::FadeStartRange);
        distanceSlider("Fade end", settings_.fadeEnd, Settings::FadeEndRange);
        settings_.fadeEnd = std::max(settings_.fadeEnd, settings_.fadeStart + 1);
    }
    else
    {
        distanceSlider("Draw distance", settings_.drawDistance, Settings::DrawDistanceRange);
    }
    slider("FOV scale", settings_.fovScale, Settings::FovScaleRange);

    ImGui::SeparatorText("Hotkeys");
    hotkeyButton(hotkeys_, "Overlay", settings_.overlayKey);
    hotkeyButton(hotkeys_, "Settings", settings_.menuKey);

    ImGui::SeparatorText("Shapes");
    for (int k = 0; k < KindCount; k++)
    {
        kindRow(settings_, k);
    }
    ImGui::Separator();
    ImGui::TextDisabled("%d triangles, %d moving objects, %d characters", statistics_.triangles, statistics_.movingObjects,
                        statistics_.characters);
}

PlayerStateSettingsPanel::PlayerStateSettingsPanel(Settings& settings, Hotkeys& hotkeys)
    : Panel("Player state panel", ImGuiWindowFlags_AlwaysAutoResize), settings_(settings), hotkeys_(hotkeys)
{
}

void PlayerStateSettingsPanel::draw()
{
    render();
}

void PlayerStateSettingsPanel::place() const
{
    ImGui::SetNextWindowPos(ImVec2(560, 40), ImGuiCond_FirstUseEver);
}

void PlayerStateSettingsPanel::body()
{
    hotkeyToggle("Show", settings_.playerStateKey, settings_.playerStatePanel);
    ImGui::SetItemTooltip("The player's speed, read from the pawn every frame");
    hotkeyButton(hotkeys_, "Hotkey", settings_.playerStateKey);
    slider("Panel X", settings_.playerStatePanelX, Settings::PanelPositionRange, "%.2f");
    slider("Panel Y", settings_.playerStatePanelY, Settings::PanelPositionRange, "%.2f");
    ImGui::SeparatorText("Rows");
    for (int r = 0; r < PlayerStateRowCount; r++)
    {
        ImGui::PushID(r);
        ImGui::Checkbox(PlayerStateRowLabels[r], &settings_.playerStateRows[r]);
        ImGui::PopID();
    }
}

PlayerStatePanel::PlayerStatePanel(const Settings& settings)
    : Panel("Player state", ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs),
      settings_(settings)
{
}

void PlayerStatePanel::draw(const PlayerState& state)
{
    state_ = state;
    render();
}

void PlayerStatePanel::place() const
{
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImVec2 pivot(settings_.playerStatePanelX, settings_.playerStatePanelY);
    ImGui::SetNextWindowPos(ImVec2(pivot.x * screen.x, pivot.y * screen.y), ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(0.4f);
}

void PlayerStatePanel::body()
{
    if (!state_.valid)
    {
        ImGui::TextUnformatted("waiting for the player...");
        return;
    }
    playerStateRow(settings_, PlayerStateRow::Speed, "speed    %7.0f   peak %7.0f", state_.speed, state_.peakSpeed);
    playerStateRow(settings_, PlayerStateRow::Speed, "up       %+7.0f   peak %7.0f", state_.verticalSpeed, state_.peakVerticalSpeed);
    playerStateRow(settings_, PlayerStateRow::Physics, "physics  %s", physicsName(state_.physics));
    playerStateRow(settings_, PlayerStateRow::WorldCollision, "collides world %d", state_.collidesWithWorld);
    playerStateRow(settings_, PlayerStateRow::Cylinder, "cylinder radius %.0f   height %.0f", state_.collisionRadius, state_.collisionHeight);
    playerStateRow(settings_, PlayerStateRow::Position, "position %8.0f %8.0f %8.0f", state_.position.x, state_.position.y, state_.position.z);
}
