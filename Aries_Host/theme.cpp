#include "theme.h"

#include <imgui.h>

void apply_aries_theme()
{
    auto &style = ImGui::GetStyle();
    style.WindowRounding = 7.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 7.0f;
    style.TabRounding = 5.0f;
    style.FramePadding = ImVec2( 9.0f, 6.0f );
    style.ItemSpacing = ImVec2( 8.0f, 8.0f );
    style.WindowPadding = ImVec2( 14.0f, 12.0f );

    auto *colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4( 0.86f, 0.87f, 0.89f, 1.00f );
    colors[ImGuiCol_TextDisabled] = ImVec4( 0.50f, 0.51f, 0.54f, 1.00f );
    colors[ImGuiCol_WindowBg] = ImVec4( 0.075f, 0.078f, 0.090f, 1.00f );
    colors[ImGuiCol_ChildBg] = ImVec4( 0.090f, 0.094f, 0.108f, 1.00f );
    colors[ImGuiCol_PopupBg] = ImVec4( 0.090f, 0.094f, 0.108f, 0.98f );
    colors[ImGuiCol_Border] = ImVec4( 0.19f, 0.20f, 0.23f, 1.00f );
    colors[ImGuiCol_FrameBg] = ImVec4( 0.12f, 0.125f, 0.145f, 1.00f );
    colors[ImGuiCol_FrameBgHovered] = ImVec4( 0.18f, 0.16f, 0.24f, 1.00f );
    colors[ImGuiCol_FrameBgActive] = ImVec4( 0.23f, 0.18f, 0.31f, 1.00f );
    colors[ImGuiCol_TitleBg] = ImVec4( 0.075f, 0.078f, 0.090f, 1.00f );
    colors[ImGuiCol_TitleBgActive] = ImVec4( 0.090f, 0.094f, 0.108f, 1.00f );
    colors[ImGuiCol_Button] = ImVec4( 0.25f, 0.17f, 0.38f, 1.00f );
    colors[ImGuiCol_ButtonHovered] = ImVec4( 0.34f, 0.22f, 0.51f, 1.00f );
    colors[ImGuiCol_ButtonActive] = ImVec4( 0.40f, 0.26f, 0.59f, 1.00f );
    colors[ImGuiCol_Header] = ImVec4( 0.20f, 0.14f, 0.31f, 1.00f );
    colors[ImGuiCol_HeaderHovered] = ImVec4( 0.29f, 0.19f, 0.43f, 1.00f );
    colors[ImGuiCol_HeaderActive] = ImVec4( 0.34f, 0.22f, 0.51f, 1.00f );
    colors[ImGuiCol_Tab] = ImVec4( 0.10f, 0.105f, 0.12f, 1.00f );
    colors[ImGuiCol_TabHovered] = ImVec4( 0.22f, 0.16f, 0.32f, 1.00f );
    colors[ImGuiCol_TabSelected] = ImVec4( 0.26f, 0.17f, 0.39f, 1.00f );
    colors[ImGuiCol_CheckMark] = ImVec4( 0.13f, 0.83f, 0.93f, 1.00f );
    colors[ImGuiCol_SliderGrab] = ImVec4( 0.55f, 0.37f, 0.95f, 1.00f );
    colors[ImGuiCol_Separator] = ImVec4( 0.18f, 0.19f, 0.22f, 1.00f );
    colors[ImGuiCol_ResizeGrip] = ImVec4( 0.13f, 0.83f, 0.93f, 0.30f );
    colors[ImGuiCol_ScrollbarGrab] = ImVec4( 0.28f, 0.29f, 0.33f, 1.00f );
}
