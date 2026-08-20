#include "native_renderer.h"
#include "kiero.h"
#include "logger.h"
#include <sstream>
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

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


static void setupImGuiStyle()
{
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
	//style.TabMinWidthForCloseButton = 0.0f;
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

bool NativeRenderer::initImGuiInGame(IDXGISwapChain* pSwapChain) {
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
    ImGui::StyleColorsDark();
    setupImGuiStyle();
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