#include "view/interface.h"

#include <imgui.h>

#include <algorithm>
#include <format>

namespace
{
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
