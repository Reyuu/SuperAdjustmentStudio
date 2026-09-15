#include "native_renderer.h"
#include "IconsFontAwesome6.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"
#include "implot.h"
#include "kiero.h"
#include "logger.h"
#include "settings.h"
#include "translation.h"
#include <algorithm>
#include <sstream>
#include <windows.h>

#define IDR_FA_FONT 101
static void fontModuleAnchor() {
}

NativeRenderer* NativeRenderer::instancePtr = nullptr;

NativeRenderer::NativeRenderer() {
    instancePtr = this;
}

bool NativeRenderer::installHooks(PresentFn presentDetourArg, ResizeBuffersFn resizeBuffersDetourArg) {
    if (areHooksInstalledBool) {
        return true;
    }
    if (kiero::init(kiero::RenderType::D3D11) != kiero::Status::Success) {
        Logger->debug("kiero init failed!");
        return false;
    }

    if (kiero::bind(8, (void**)&origPresentPointer, (void*)presentDetourArg) != kiero::Status::Success ||
        kiero::bind(13, (void**)&origResizeBuffersPointer, (void*)resizeBuffersDetourArg) != kiero::Status::Success) {
        Logger->debug("kiero bind failed!");
        removeHooks();
        return false;
    }

    areHooksInstalledBool = true;
    std::ostringstream ss;
    ss << "Present=0x" << origPresentPointer << " ResizeBuffers=0x" << origResizeBuffersPointer;
    Logger->debug(ss.str());
    return true;
}

void NativeRenderer::removeHooks() {
    if (kiero::getRenderType() != kiero::RenderType::None) {
        kiero::unbind(13);
        kiero::unbind(8);
        kiero::shutdown();
    }
    areHooksInstalledBool = false;
}

static void setupDefaultStyle() {
    // AdobeInspired style by nexacopic from ImThemes
    ImGuiStyle& style = ImGui::GetStyle();

    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.6f;
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.WindowRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowMinSize = ImVec2(32.0f, 32.0f);
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.ChildRounding = 4.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupRounding = 4.0f;
    style.PopupBorderSize = 1.0f;
    style.FramePadding = ImVec2(4.0f, 3.0f);
    style.FrameRounding = 4.0f;
    style.FrameBorderSize = 1.0f;
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.CellPadding = ImVec2(4.0f, 2.0f);
    style.IndentSpacing = 21.0f;
    style.ColumnsMinSpacing = 6.0f;
    style.ScrollbarSize = 14.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabMinSize = 10.0f;
    style.GrabRounding = 20.0f;
    style.TabRounding = 4.0f;
    style.TabBorderSize = 1.0f;
    // style.TabMinWidthForCloseButton = 0.0f;
    style.ColorButtonPosition = ImGuiDir_Right;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

    style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.49803922f, 0.49803922f, 0.49803922f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.11372549f, 0.11372549f, 0.11372549f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.078431375f, 0.078431375f, 0.078431375f, 0.94f);
    style.Colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.16309011f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.08627451f, 0.08627451f, 0.08627451f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15294118f, 0.15294118f, 0.15294118f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.1882353f, 0.1882353f, 0.1882353f, 1.0f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.11372549f, 0.11372549f, 0.11372549f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.105882354f, 0.105882354f, 0.105882354f, 1.0f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.51f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.11372549f, 0.11372549f, 0.11372549f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.019607844f, 0.019607844f, 0.019607844f, 0.53f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30980393f, 0.30980393f, 0.30980393f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40784314f, 0.40784314f, 0.40784314f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50980395f, 0.50980395f, 0.50980395f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.8784314f, 0.8784314f, 0.8784314f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.98039216f, 0.98039216f, 0.98039216f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.14901961f, 0.14901961f, 0.14901961f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24705882f, 0.24705882f, 0.24705882f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.32941177f, 0.32941177f, 0.32941177f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.9764706f, 0.9764706f, 0.9764706f, 0.30980393f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.9764706f, 0.9764706f, 0.9764706f, 0.8f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.9764706f, 0.9764706f, 0.9764706f, 1.0f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.42745098f, 0.42745098f, 0.49803922f, 0.5f);
    style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.7490196f, 0.7490196f, 0.7490196f, 0.78039217f);
    style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.7490196f, 0.7490196f, 0.7490196f, 1.0f);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.9764706f, 0.9764706f, 0.9764706f, 0.2f);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.9372549f, 0.9372549f, 0.9372549f, 0.67058825f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.9764706f, 0.9764706f, 0.9764706f, 0.9490196f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.22352941f, 0.22352941f, 0.22352941f, 0.8627451f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.32156864f, 0.32156864f, 0.32156864f, 0.8f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.27450982f, 0.27450982f, 0.27450982f, 1.0f);
    style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.14509805f, 0.14509805f, 0.14509805f, 0.972549f);
    style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.42352942f, 0.42352942f, 0.42352942f, 1.0f);
    style.Colors[ImGuiCol_PlotLines] = ImVec4(0.60784316f, 0.60784316f, 0.60784316f, 1.0f);
    style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.42745098f, 0.34901962f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.8980392f, 0.69803923f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.6f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882353f, 0.1882353f, 0.2f, 1.0f);
    style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.30980393f, 0.30980393f, 0.34901962f, 1.0f);
    style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.22745098f, 0.22745098f, 0.24705882f, 1.0f);
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25882354f, 0.5882353f, 0.9764706f, 0.35f);
    style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.9f);
    style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.25882354f, 0.5882353f, 0.9764706f, 1.0f);
    style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
    style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.35f);
}

static void setupPaperAndInkStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // --- 1. Sizing & Spacing (Clean & Rigid) ---
    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 12.0f;

    // --- 2. Borders & Rounding (Technical/Drafting feel) ---
    style.WindowRounding = 2.0f;
    style.ChildRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;

    // --- 3. Full Color Palette ---

    // Main Text & Background
    colors[ImGuiCol_Text] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f); // Deep Carbon Ink
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.96f, 0.96f, 0.94f, 1.00f); // Warm Paper
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.03f);
    colors[ImGuiCol_PopupBg] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f); // Clean White Popups

    // Borders & Separators
    colors[ImGuiCol_Border] = ImVec4(0.75f, 0.75f, 0.72f, 1.00f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.80f, 0.80f, 0.78f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.17f, 0.34f, 0.59f, 0.78f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.17f, 0.34f, 0.59f, 1.00f);

    // Frames (Inputs, Checkboxes, etc)
    colors[ImGuiCol_FrameBg] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.90f, 0.92f, 0.95f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.85f, 0.88f, 0.92f, 1.00f);

    // Titles & Menus
    colors[ImGuiCol_TitleBg] = ImVec4(0.92f, 0.92f, 0.90f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.88f, 0.88f, 0.86f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.92f, 0.92f, 0.90f, 0.75f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.92f, 0.92f, 0.90f, 1.00f);

    // Scrollbars
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.96f, 0.96f, 0.94f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.80f, 0.78f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.70f, 0.70f, 0.68f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.60f, 0.60f, 0.58f, 1.00f);

    // Interactables (Blueprint Blue)
    colors[ImGuiCol_CheckMark] = ImVec4(0.17f, 0.34f, 0.59f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.17f, 0.34f, 0.59f, 0.70f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.17f, 0.34f, 0.59f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.17f, 0.34f, 0.59f, 0.08f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.17f, 0.34f, 0.59f, 0.20f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.17f, 0.34f, 0.59f, 0.35f);

    // Header (Selection in lists/trees)
    colors[ImGuiCol_Header] = ImVec4(0.17f, 0.34f, 0.59f, 0.12f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.17f, 0.34f, 0.59f, 0.25f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.17f, 0.34f, 0.59f, 0.40f);

    // Tables (Crucial for Light Mode)
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.90f, 0.90f, 0.88f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.75f, 0.75f, 0.72f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.85f, 0.85f, 0.82f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.00f, 0.00f, 0.00f, 0.03f);

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.92f, 0.92f, 0.90f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.92f, 0.92f, 0.90f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.96f, 0.96f, 0.94f, 1.00f);

    // Misc
    colors[ImGuiCol_PlotLines] = ImVec4(0.17f, 0.34f, 0.59f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.17f, 0.34f, 0.59f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.17f, 0.34f, 0.59f, 0.25f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.17f, 0.34f, 0.59f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.17f, 0.34f, 0.59f, 1.00f);

#ifdef IMGUI_HAS_DOCK
    colors[ImGuiCol_DockingPreview] = ImVec4(0.17f, 0.34f, 0.59f, 0.40f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.96f, 0.96f, 0.94f, 1.00f);
#endif
}

static void setupSpectrumStyle() {
    ImGuiStyle* style = &ImGui::GetStyle();
    style->GrabRounding = 4.0f;

    ImVec4* colors = style->Colors;
    colors[ImGuiCol_Text] = ImVec4(0.29411765f, 0.29411765f, 0.29411765f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.70196078f, 0.70196078f, 0.70196078f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.96078431f, 0.96078431f, 0.96078431f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.88235294f, 0.88235294f, 0.88235294f, 1.00f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.98039216f, 0.98039216f, 0.98039216f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.91764706f, 0.91764706f, 0.91764706f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.88235294f, 0.88235294f, 0.88235294f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.91764706f, 0.91764706f, 0.91764706f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.79215686f, 0.79215686f, 0.79215686f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.96078431f, 0.96078431f, 0.96078431f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.96078431f, 0.96078431f, 0.96078431f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.79215686f, 0.79215686f, 0.79215686f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.55686275f, 0.55686275f, 0.55686275f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.43921569f, 0.43921569f, 0.43921569f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.07843137f, 0.45098039f, 0.90196078f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.43921569f, 0.43921569f, 0.43921569f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.29411765f, 0.29411765f, 0.29411765f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.98039216f, 0.98039216f, 0.98039216f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.91764706f, 0.91764706f, 0.91764706f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.14901961f, 0.50196078f, 0.92156863f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.07843137f, 0.45098039f, 0.90196078f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.05098039f, 0.40000000f, 0.81568627f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.79215686f, 0.79215686f, 0.79215686f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.55686275f, 0.55686275f, 0.55686275f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.43921569f, 0.43921569f, 0.43921569f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.79215686f, 0.79215686f, 0.79215686f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.55686275f, 0.55686275f, 0.55686275f, 1.00f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.43921569f, 0.43921569f, 0.43921569f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.14901961f, 0.50196078f, 0.92156863f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.05098039f, 0.40000000f, 0.81568627f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.14901961f, 0.50196078f, 0.92156863f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.05098039f, 0.40000000f, 0.81568627f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.14901961f, 0.50196078f, 0.92156863f, 0.20f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.17254902f, 0.17254902f, 0.17254902f, 0.03921569f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
}

static void setupMaroonStyle() {
    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.02f, 0.02f, 0.85f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.14f, 0.11f, 0.11f, 0.92f);
    colors[ImGuiCol_Border] = ImVec4(0.50f, 0.50f, 0.50f, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.43f, 0.43f, 0.43f, 0.39f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.70f, 0.41f, 0.41f, 0.40f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.75f, 0.48f, 0.48f, 0.69f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.48f, 0.18f, 0.18f, 0.65f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.52f, 0.12f, 0.12f, 0.87f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.80f, 0.40f, 0.40f, 0.20f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.80f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.30f, 0.20f, 0.20f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.96f, 0.17f, 0.17f, 0.30f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1.00f, 0.07f, 0.07f, 0.40f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(1.00f, 0.36f, 0.36f, 0.60f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.90f, 0.90f, 0.90f, 0.50f);
    colors[ImGuiCol_SliderGrab] = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.80f, 0.39f, 0.39f, 0.60f);
    colors[ImGuiCol_Button] = ImVec4(0.71f, 0.18f, 0.18f, 0.62f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.71f, 0.27f, 0.27f, 0.79f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.80f, 0.46f, 0.46f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.56f, 0.16f, 0.16f, 0.45f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.53f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.87f, 0.53f, 0.53f, 0.80f);
    colors[ImGuiCol_Separator] = ImVec4(0.50f, 0.50f, 0.50f, 0.60f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.60f, 0.60f, 0.70f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.70f, 0.70f, 0.90f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.78f, 0.82f, 1.00f, 0.60f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.78f, 0.82f, 1.00f, 0.90f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.68f, 0.21f, 0.21f, 0.80f);
    colors[ImGuiCol_Tab] = ImVec4(0.47f, 0.12f, 0.12f, 0.79f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.68f, 0.21f, 0.21f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.95f, 0.84f, 0.84f, 0.40f);
    colors[ImGuiCol_TabDimmed] = ImVec4(0.00f, 0.00f, 0.00f, 0.83f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.00f, 0.00f, 0.00f, 0.83f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.55f, 0.23f, 0.23f, 1.00f);
#ifdef IMGUI_HAS_DOCK
    colors[ImGuiCol_DockingPreview] = ImVec4(0.90f, 0.40f, 0.40f, 0.31f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
#endif
    colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.56f, 0.16f, 0.16f, 0.45f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.68f, 0.21f, 0.21f, 0.80f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.26f, 0.26f, 0.28f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.07f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.45f, 0.45f, 0.90f, 0.80f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
}

static void setupImGuiStyle(const std::string& theme) {
    if (theme == "paper") {
        ImGui::StyleColorsLight();
        setupPaperAndInkStyle();
        return;
    }
    if (theme == "spectrum") {
        ImGui::StyleColorsLight();
        setupSpectrumStyle();
        return;
    }
    ImGui::StyleColorsDark();
    if (theme == "dark") {
        return;
    }
    if (theme == "maroon") {
        setupMaroonStyle();
        return;
    }
    setupDefaultStyle();
}

bool NativeRenderer::initImGuiInGame(IDXGISwapChain* pSwapChain) {
    if (Translation::instance().startup) {
        Translation::instance().startup = false;
        Translation::instance().loadTranslations();
        SettingsOptions& options = Settings::instance().options;
        if (options.language.empty()) {
            options.language = "en";
        }
        Translation::instance().setLanguage(options.language);
    }
    if (isImGuiInitializedBool) {
        return true;
    }
    if (!pSwapChain) {
        Logger->debug("null swapchain!");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc;
    if (FAILED(pSwapChain->GetDesc(&desc))) {
        Logger->debug("GetDesc failed!");
        return false;
    }

    HWND hGameWnd = desc.OutputWindow;
    if (!hGameWnd) {
        Logger->debug("no game HWND from the swapchain!");
        return false;
    }

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    // COM magic
    if (FAILED(pSwapChain->GetDevice(IID_PPV_ARGS(&device)))) {
        Logger->debug("GetDevice failed!");
        return false;
    }
    device->GetImmediateContext(&context);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    setupImGuiStyle(themeValue);

    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = fontScaleValue;
    ImFontConfig baseFontConfig;
    baseFontConfig.SizePixels = 15.0f;
    io.Fonts->AddFontDefault(&baseFontConfig);

    ImFontConfig trebuchetConfig;
    trebuchetConfig.SizePixels = baseFontConfig.SizePixels + 2.0f;
    io.FontDefault = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\trebuc.ttf", trebuchetConfig.SizePixels, &trebuchetConfig);

    // CJK fallback: merged into the default font, glyphs load on demand (no ranges needed).
    {
        static const char* cjkCandidates[] = {
            "C:\\Windows\\Fonts\\msyh.ttc",     // Microsoft YaHei (broad CJK)
            "C:\\Windows\\Fonts\\msgothic.ttc", // MS Gothic (Japanese)
            "C:\\Windows\\Fonts\\YuGothM.ttc",  // Yu Gothic
            "C:\\Windows\\Fonts\\malgun.ttf",   // Malgun Gothic (Korean)
            "C:\\Windows\\Fonts\\simsun.ttc",   // SimSun
            "C:\\Windows\\Fonts\\mingliub.ttc", // MingLiU (Traditional)
        };
        ImFontConfig cjkConfig;
        cjkConfig.MergeMode = true;
        cjkConfig.PixelSnapH = true;
        for (const char* path : cjkCandidates) {
            if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
                continue;
            }
            if (io.Fonts->AddFontFromFileTTF(path, trebuchetConfig.SizePixels, &cjkConfig) != nullptr) {
                break;
            }
        }
    }

    HMODULE hMod = nullptr;
    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&fontModuleAnchor, &hMod)) {

        HRSRC hRes = FindResourceA(hMod, MAKEINTRESOURCEA(IDR_FA_FONT), RT_RCDATA);
        if (hRes) {
            HGLOBAL hMem = LoadResource(hMod, hRes);
            DWORD size = SizeofResource(hMod, hRes);
            void* pData = LockResource(hMem);
            if (pData && size > 0) {
                ImFontConfig config;
                config.MergeMode = true;
                config.PixelSnapH = true;
                config.FontDataOwnedByAtlas = false;
                static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
                io.Fonts->AddFontFromMemoryTTF(pData, (int)size, baseFontConfig.SizePixels, &config, icon_ranges);
            }
        }
    }

    ImGui_ImplWin32_Init(hGameWnd);
    if (!ImGui_ImplDX11_Init(device, context)) {
        Logger->debug("ImGui_ImplDX11_Init failed!");
        return false;
    }

    pd3dDevice = device;
    pd3dContext = context;
    isImGuiInitializedBool = true;
    Logger->debug("ImGui initialized using game HWND");
    return true;
}

void NativeRenderer::applySettings(const SettingsOptions& options) {
    const float scale = std::clamp((float)options.fontSize / SETTINGS_FONT_BASE_PX, SETTINGS_FONT_SCALE_MIN, SETTINGS_FONT_SCALE_MAX);
    fontScaleValue = scale;
    themeValue = options.theme;
    if (!isImGuiInitializedBool) {
        return;
    }
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    ImGui::GetIO().FontGlobalScale = scale;
    setupImGuiStyle(themeValue);
}

//  lazy create the render target view from swapchain back buffer
bool NativeRenderer::ensureRenderTarget(IDXGISwapChain* pSwapChain) {
    if (pRenderTargetView) {
        return true;
    }

    ID3D11Texture2D* pBackBuffer = nullptr;
    if (FAILED(pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)))) {
        return false;
    }

    pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView);
    pBackBuffer->Release();
    return pRenderTargetView != nullptr;
}

//  release render target from the chain
void NativeRenderer::releaseRenderTargetView() {
    if (pRenderTargetView) {
        pRenderTargetView->Release();
        pRenderTargetView = nullptr;
    }
}

//  bind the render target before imgui frame
void NativeRenderer::beginRender() {
    pd3dContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
}

//  release all and clear imgui flag
void NativeRenderer::shutdown() {
    releaseRenderTargetView();
    if (pd3dContext) {
        pd3dContext->Release();
        pd3dContext = nullptr;
    }
    if (pd3dDevice) {
        pd3dDevice->Release();
        pd3dDevice = nullptr;
    }
    isImGuiInitializedBool = false;
}