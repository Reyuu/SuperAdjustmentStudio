#include "photo_overlay.h"

#include <cstring>

#include "imgui.h"
#include "implot.h"
#include "IconsFontAwesome6.h"
#include "translation.h"
#include "settings.h"
#include "application.h"

static const std::string CONSOLE_COMMAND_SHOW_FOG = "show fog";
static const std::string CONSOLE_COMMAND_SHOW_LENS_FLARES = "show lensflares";

static void hLine(ImDrawList* dl, float y, float w, ImU32 color, float thickness = 1.0f) {
    dl->AddLine(ImVec2(0, y), ImVec2(w, y), color, thickness);
}

static void vLine(ImDrawList* dl, float x, float h, ImU32 color, float thickness = 1.0f) {
    dl->AddLine(ImVec2(x, 0), ImVec2(x, h), color, thickness);
}

void PhotoOverlay::render(ID3D11Device* device) {
    if (!enabledState && !filterState) {
        return;
    }

    const ImVec2 size = ImGui::GetIO().DisplaySize;
    if (size.x <= 0.0f || size.y <= 0.0f) {
        return;
    }

    const float width = size.x;
    const float height = size.y;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    //

    const ImU32 color = ImGui::GetColorU32(ImVec4(lineColor[0], lineColor[1], lineColor[2], lineColor[3]));

    if (filterState) {
        if (tintStrength > 0.0f) {
            dl->AddRectFilled(ImVec2(0, 0), size, ImGui::GetColorU32(ImVec4(tintColor[0], tintColor[1], tintColor[2], tintStrength * 0.5f)));
        }
        if (grainIntensity > 0.0f && grainOpacity > 0.0f) {
            unsigned int seed = (unsigned int)ImGui::GetFrameCount() * 2654443741u;
            const int n = (int)(grainIntensity * 2200.0f);
            const int wi = (int)width;
            const int hi = (int)height;
            for (int i = 0; i < n; ++i) {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                const float x = (seed % wi);
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                const float y = (seed % hi);
                const float v = (seed >> 7) & 1;
                int a = (int)((70 + (seed & 100)) * grainOpacity);
                a = a > 255 ? 255 : a;
                dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 2.5f, y + 2.5f), v ? IM_COL32(255, 255, 255, a) : IM_COL32(0, 0, 0, a));
            }
        }
    }
    if (!enabledState) {
        return;
    }

    if ((int)aspectRatio > 0 && (int)aspectRatio < ASPECT_RATIO_COUNT) {
        const float ratio = ASPECT_RATIOS[(int)aspectRatio][0] / ASPECT_RATIOS[(int)aspectRatio][1];
        float tw = width;
        float th = tw / ratio;
        if (th > height) {
            th = height;
            tw = th * ratio;
        }

        const float offsetX = (width - tw) * 0.5f;
        const float offsetY = (height - th) * 0.5f;
        const ImU32 mask = IM_COL32(0, 0, 0, static_cast<int>(maskOpacity * 255.0f));
        // Draw the mask around the aspect ratio rectangle
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(width, offsetY), mask);
        dl->AddRectFilled(ImVec2(0, offsetY + th), ImVec2(width, height), mask);
        dl->AddRectFilled(ImVec2(0, offsetY), ImVec2(offsetX, offsetY + th), mask);
        dl->AddRectFilled(ImVec2(offsetX + tw, offsetY), ImVec2(width, offsetY + th), mask);
        dl->AddRect(ImVec2(offsetX, offsetY), ImVec2(offsetX + tw, offsetY + th), color, lineThickness);
    }

    switch (gridIndex) {
        case GRID_3X3: {
            const float thirdW = width / 3.0f;
            const float thirdH = height / 3.0f;
            hLine(dl, thirdH, width, color, lineThickness);
            hLine(dl, 2 * thirdH, width, color, lineThickness);
            vLine(dl, thirdW, height, color, lineThickness);
            vLine(dl, 2 * thirdW, height, color, lineThickness);
            break;
        }
        case GRID_4X4: {
            const float quarterW = width / 4.0f;
            const float quarterH = height / 4.0f;
            hLine(dl, quarterH, width, color, lineThickness);
            hLine(dl, 2 * quarterH, width, color, lineThickness);
            hLine(dl, 3 * quarterH, width, color, lineThickness);
            vLine(dl, quarterW, height, color, lineThickness);
            vLine(dl, 2 * quarterW, height, color, lineThickness);
            vLine(dl, 3 * quarterW, height, color, lineThickness);
            break;
        }
        case GRID_CROSSHAIR: {
            hLine(dl, height / 2.0f, width, color, lineThickness);
            vLine(dl, width / 2.0f, height, color, lineThickness);
            break;
        }
        case GRID_DIAGONALS: {
            dl->AddLine(ImVec2(0, 0), ImVec2(width, height), color, lineThickness);
            dl->AddLine(ImVec2(width, 0), ImVec2(0, height), color, lineThickness);
            break;
        }
        case GRID_PHI_GRID: {
            const float phiW = width * PHI_HI;
            const float phiH = height * PHI_HI;
            hLine(dl, phiH, width, color, lineThickness);
            hLine(dl, height - phiH, width, color, lineThickness);
            vLine(dl, phiW, height, color, lineThickness);
            vLine(dl, width - phiW, height, color, lineThickness);
            break;
        }
        case GRID_GOLDEN_TRIANGLES: {
            dl->AddLine(ImVec2(0, 0), ImVec2(width, height), color, lineThickness);
            dl->AddLine(ImVec2(width, 0), ImVec2(0, height), color, lineThickness);
            dl->AddLine(ImVec2(width / 2.0f, 0), ImVec2(width, height / 2.0f), color, lineThickness);
            dl->AddLine(ImVec2(0, height / 2.0f), ImVec2(width / 2.0f, height), color, lineThickness);
            break;
        }
        default: {
            break;
        }
    }

    if (safeFrame) {
        dl->AddRect(ImVec2(width * 0.05f, height * 0.05f), ImVec2(width * 0.95f, height * 0.95f), color, lineThickness);
    }
    if (centerDot) {
        dl->AddCircle(ImVec2(width / 2.0f, height / 2.0f), lineThickness, color);
    }
    if (readout) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s || %s", gridName(gridIndex), aspectRatioName(aspectRatio));
        dl->AddText(ImVec2(10, height - 30), color, buf);
    }
}

void PhotoOverlay::sample(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* context) {
    if (!histogram || !swapChain || !device || !context) {
        return;
    }

    if ((histTick++ % HIST_UPDATE_INTERVAL) != 0) {
        return;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer) {
        return;
    }

    D3D11_TEXTURE2D_DESC backBufferDesc{};
    backBuffer->GetDesc(&backBufferDesc);
    const bool bgra = backBufferDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM;
    if (backBufferDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && !bgra) {
        backBuffer->Release();
        return;
    }

    if (!histogramTexture || histW != backBufferDesc.Width || histH != backBufferDesc.Height || histFmt != (int)backBufferDesc.Format) {
        if (histogramTexture) {
            histogramTexture->Release();
            histogramTexture = nullptr;
        }
        D3D11_TEXTURE2D_DESC staged = backBufferDesc;
        staged.BindFlags = 0;
        staged.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staged.Usage = D3D11_USAGE_STAGING;
        staged.MiscFlags = 0;
        if (FAILED(device->CreateTexture2D(&staged, nullptr, &histogramTexture))) {
            backBuffer->Release();
            return;
        }
        histW = backBufferDesc.Width;
        histH = backBufferDesc.Height;
        histFmt = (int)backBufferDesc.Format;
    }
    context->CopyResource(histogramTexture, backBuffer);
    backBuffer->Release();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(histogramTexture, 0, D3D11_MAP_READ, 0, &mapped))) {
        return;
    }

    // process the mapped data here
    float r[HIST_BINS] = {};
    float g[HIST_BINS] = {};
    float b[HIST_BINS] = {};
    float l[HIST_BINS] = {};
    size_t n = 0;
    size_t lo = 0;
    size_t hi = 0;
    const unsigned char* data = static_cast<const unsigned char*>(mapped.pData);
    for (unsigned y = 0; y < backBufferDesc.Height; y += 4) {
        const unsigned char* px = data + (size_t)y * mapped.RowPitch;
        for (unsigned x = 0; x < backBufferDesc.Width; x += 4) {
            const unsigned char rr = bgra ? px[2] : px[0];
            const unsigned char gg = px[1];
            const unsigned char bb = bgra ? px[0] : px[2];
            r[rr * HIST_BINS / 256]++;
            g[gg * HIST_BINS / 256]++;
            b[bb * HIST_BINS / 256]++;
            const unsigned char ll = (rr * 77u + gg * 150u + bb * 29u) >> 8; // perceived luminance
            l[ll * HIST_BINS / 256]++;
            lo += (ll <= (unsigned)clipLoThr) ? 1 : 0;
            hi += (ll >= (unsigned)clipHiThr) ? 1 : 0;
            ++n;
            px += 16; // 4 pixels at a time, 4 bytes per pixel
        }
    }

    context->Unmap(histogramTexture, 0);
    if (n == 0) {
        return; // no pixels were processed, exit early
    }
    float peak = 1.0f;
    for (int i = 0; i < HIST_BINS; ++i) {
        if (r[i] > peak) {
            peak = r[i];
        }
        if (g[i] > peak) {
            peak = g[i];
        }
        if (b[i] > peak) {
            peak = b[i];
        }
        if (l[i] > peak) {
            peak = l[i];
        }
    }
    for (int i = 0; i < HIST_BINS; ++i) {
        r[i] /= peak;
        g[i] /= peak;
        b[i] /= peak;
        l[i] /= peak;
    }
    memcpy(histR, r, sizeof(histR));
    memcpy(histG, g, sizeof(histG));
    memcpy(histB, b, sizeof(histB));
    memcpy(histL, l, sizeof(histL));
    // convert clipped pixel counts to percentages
    clipLo = 100.0f * (static_cast<float>(lo) / static_cast<float>(n));
    clipHi = 100.0f * (static_cast<float>(hi) / static_cast<float>(n));
}

void PhotoOverlay::renderHistogram() {
    static float xs[HIST_BINS] = {};
    static bool xsInitialized = false;
    if (!xsInitialized) {
        for (int i = 0; i < HIST_BINS; ++i) {
            xs[i] = static_cast<float>(i);
        }
        xsInitialized = true;
    }

    ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.11f, 0.11f, 0.11f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
    if (ImPlot::BeginPlot("##photo_hist", ImVec2(-1.0f, 130.0f), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoFrame)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoGridLines,
                          ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxesLimits(0, HIST_BINS, 0, 1, ImPlotCond_Always);

        ImPlotSpec luma;
        luma.FillColor = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
        luma.FillAlpha = 0.55f;
        ImPlot::PlotShaded("L", xs, histL, HIST_BINS, 0.0f, luma);

        ImPlotSpec red;
        red.FillColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
        red.FillAlpha = 0.45f;
        ImPlot::PlotShaded("R", xs, histR, HIST_BINS, 0.0f, red);

        ImPlotSpec green;
        green.FillColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
        green.FillAlpha = 0.45f;
        ImPlot::PlotShaded("G", xs, histG, HIST_BINS, 0.0f, green);

        ImPlotSpec blue;
        blue.FillColor = ImVec4(0.2f, 0.2f, 1.0f, 1.0f);
        blue.FillAlpha = 0.45f;
        ImPlot::PlotShaded("B", xs, histB, HIST_BINS, 0.0f, blue);

        ImDrawList* plotDrawList = ImPlot::GetPlotDrawList();
        const ImVec2 plotPos = ImPlot::GetPlotPos();
        const ImVec2 plotSize = ImPlot::GetPlotSize();
        const float tri = 9.0f;
        const ImU32 triOn = IM_COL32(255, 255, 255, 255);
        const ImU32 triOff = IM_COL32(90, 90, 90, 255);
        plotDrawList->AddTriangleFilled(ImVec2(plotPos.x + 3, plotPos.y + 3), ImVec2(plotPos.x + 3 + tri, plotPos.y + 3),
                                        ImVec2(plotPos.x + 3, plotPos.y + 3 + tri), clipLo > 0.1f ? triOn : triOff);
        plotDrawList->AddTriangleFilled(ImVec2(plotPos.x + plotSize.x - 3, plotPos.y + 3), ImVec2(plotPos.x + plotSize.x - 3 - tri, plotPos.y + 3),
                                        ImVec2(plotPos.x + plotSize.x - 3, plotPos.y + 3 + tri), clipHi > 0.1f ? triOn : triOff);
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleColor(2);
}

void PhotoOverlay::renderUi() {
    if (ImGui::CollapsingHeader((std::string(ICON_FA_TABLE_CELLS " ") + t("ui.photo")).c_str())) {
        ImGui::Indent();
        ImGui::Checkbox((std::string(ICON_FA_EYE " ") + t("ui.photo_table.enable")).c_str(), &enabledState);
        ImGui::TextDisabled("%s", t("ui.photo_table.hint"));

        ImGui::Text(t("ui.photo_table.grid"));
        ImGui::PushItemWidth(-100);
        ImGui::Combo("##photo_grid", reinterpret_cast<int*>(&gridIndex), gridLabels(), PHOTO_OVERLAY_GRID_COUNT);
        ImGui::Text(t("ui.photo_table.aspect_ratio"));
        ImGui::Combo("##photo_aspect_ratio", reinterpret_cast<int*>(&aspectRatio), aspectRatioLabels(), ASPECT_RATIO_COUNT);
        if (aspectRatio > 0) {
            ImGui::Text(t("ui.photo_table.mask_opacity"));
            ImGui::SliderFloat("##photo_mask_opacity", &maskOpacity, SETTINGS_PHOTO_MASK_OPACITY_MIN, SETTINGS_PHOTO_MASK_OPACITY_MAX);
        }
        ImGui::Text(t("ui.photo_table.line_width"));
        ImGui::ColorEdit4("##photo_line_color", lineColor);
        ImGui::SliderFloat("##photo_line_width", &lineThickness, SETTINGS_PHOTO_LINE_THICKNESS_MIN, SETTINGS_PHOTO_LINE_THICKNESS_MAX);
        ImGui::PopItemWidth();

        ImGui::Checkbox(t("ui.photo_table.center_dot"), &centerDot);
        ImGui::Checkbox(t("ui.photo_table.safe_frame"), &safeFrame);
        ImGui::Checkbox(t("ui.photo_table.readout"), &readout);
        ImGui::Separator();

        if (ImGui::Checkbox(t("ui.photo_table.histogram"), &histogram) && !histogram) {
            histTick = 0;
        }
        if (histogram) {
            ImGui::Text(t("ui.photo_table.clip_lo"));
            ImGui::SliderInt("##photo_clip_lo", &clipLoThr, SETTINGS_PHOTO_CLIP_MIN, SETTINGS_PHOTO_CLIP_MAX);
            ImGui::Text(t("ui.photo_table.clip_hi"));
            ImGui::SliderInt("##photo_clip_hi", &clipHiThr, SETTINGS_PHOTO_CLIP_MIN, SETTINGS_PHOTO_CLIP_MAX);
            renderHistogram();
            char clipBuf[160];
            snprintf(clipBuf, sizeof(clipBuf), t("ui.photo_table.clipped"), Settings::instance().options.freecamFOV, histW, histH, clipLo, clipHi);
            ImGui::TextDisabled("%s", clipBuf);
        }

        ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader((std::string(ICON_FA_WAND_SPARKLES " ") + t("ui.filter")).c_str())) {
        ImGui::Indent();
        ImGui::Checkbox((std::string(ICON_FA_EYE " ") + t("ui.filter_table.enable")).c_str(), &filterState);
        if (ImGui::Checkbox(t("ui.photo_table.disable_fog"), &noFogState)) {
            Application::instance().engine().consoleCommand(CONSOLE_COMMAND_SHOW_FOG);
        }
        if (ImGui::Checkbox(t("ui.photo_table.disable_lens_flares"), &noLensFlareState)) {
            Application::instance().engine().consoleCommand(CONSOLE_COMMAND_SHOW_LENS_FLARES);
        }
        ImGui::Separator();
        ImGui::TextDisabled("%s", t("ui.filter_table.hint"));
        ImGui::Text(t("ui.filter_table.tint"));
        ImGui::PushItemWidth(-100);
        ImGui::ColorEdit3("##photo_tint", tintColor);
        ImGui::SliderFloat("##photo_tintstr", &tintStrength, SETTINGS_PHOTO_TINT_STRENGTH_MIN, SETTINGS_PHOTO_TINT_STRENGTH_MAX, "%.2f");
        ImGui::Text(t("ui.filter_table.grain"));
        ImGui::SliderFloat("##photo_grain", &grainIntensity, SETTINGS_PHOTO_GRAIN_INTENSITY_MIN, SETTINGS_PHOTO_GRAIN_INTENSITY_MAX, "%.2f");
        ImGui::Text(t("ui.filter_table.grain_opacity"));
        ImGui::SliderFloat("##photo_grainop", &grainOpacity, SETTINGS_PHOTO_GRAIN_OPACITY_MIN, SETTINGS_PHOTO_GRAIN_OPACITY_MAX, "%.2f");
        ImGui::PopItemWidth();
        ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader((std::string(ICON_FA_CIRCLE_HALF_STROKE " ") + t("ui.lut_panel.title")).c_str())) {
        ImGui::Indent();
        renderLutStackUi();
        ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader((std::string(ICON_FA_CAMERA " ") + t("ui.screenshot")).c_str())) {
        ImGui::Indent();
        const int busy = Application::instance().screenshot().isBusy() ? 1 : 0;
        ImGui::BeginDisabled(busy != 0);
        SettingsOptions& options = Settings::instance().options;

        ImGui::Text(t("ui.shot_table.multiplier"));
        ImGui::PushItemWidth(-100);
        ImGui::SliderInt("##shot_multiplier", &options.shotMultiplier, SETTINGS_SHOT_MULTIPLIER_MIN, SETTINGS_SHOT_MULTIPLIER_MAX);
        ImGui::Text(t("ui.shot_table.overlap"));
        ImGui::SliderInt("##shot_overlap", &options.shotOverlap, SETTINGS_SHOT_OVERLAP_MIN, SETTINGS_SHOT_OVERLAP_MAX);
        ImGui::PopItemWidth();

        ImGui::Text(t("ui.shot_table.format"));
        static const char* formats[] = {"PNG", "JPEG", "BMP"};
        int fmtIdx = 0;
        if (options.shotFormat == "PNG") {
            fmtIdx = 0;
        } else if (options.shotFormat == "JPEG") {
            fmtIdx = 1;
        } else if (options.shotFormat == "BMP") {
            fmtIdx = 2;
        }
        if (ImGui::Combo("##shot_format", &fmtIdx, formats, IM_ARRAYSIZE(formats))) {
            options.shotFormat = formats[fmtIdx];
            Application::instance().settings().markChanged();
        }

        ImGui::Text(t("ui.shot_table.save_directory"));
        char dirBuf[1024] = {};
        snprintf(dirBuf, sizeof(dirBuf), "%s", Screenshot::defaultOutputDirectory().string().c_str());
        ImGui::TextDisabled("%s", dirBuf);
        if (ImGui::Button(t("ui.shot_table.browse"))) {
            if (pickFolder(options.shotSaveDir)) {
                Application::instance().settings().markChanged();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(t("ui.shot_table.revert_default"))) {
            options.shotSaveDir.clear();
            Application::instance().settings().markChanged();
        }

        if (ImGui::Checkbox(t("ui.shot_table.extra_unlit_shot"), &options.shotExtraUnlit)) {
            Application::instance().settings().markChanged();
        }
        if (ImGui::Button((std::string(ICON_FA_CAMERA " ") + t("ui.shot_table.take")).c_str())) {
            ScreenshotFormat format = ScreenshotFormat::PNG;
            if (options.shotFormat == "PNG") {
                format = ScreenshotFormat::PNG;
            } else if (options.shotFormat == "JPEG") {
                format = ScreenshotFormat::JPEG;
            } else if (options.shotFormat == "BMP") {
                format = ScreenshotFormat::BMP;
            }

            Application::instance().screenshot().start(options.shotMultiplier, options.shotOverlap, format, options.shotSaveDir, options.shotExtraUnlit);
        }
        ImGui::EndDisabled();
        const std::string status = Application::instance().screenshot().getStatus();
        if (!status.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
            ImGui::TextDisabled("%s", status.c_str());
            ImGui::PopTextWrapPos();
        }
        if (busy) {
            ImGui::TextDisabled("%s", t("ui.shot_table.busy"));
        }
        ImGui::Unindent();
    }

    if (Application::instance().screenshot().isBusy()) {
        ImGui::OpenPopup("##sas_capture_modal");
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.3f));
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("##sas_capture_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
            ImGui::TextUnformatted(t("ui.shot_table.busy"));
            const std::string status = Application::instance().screenshot().getStatus();
            if (!status.empty()) {
                ImGui::TextDisabled("%s", status.c_str());
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor();
    }
}

void PhotoOverlay::shutdown() {
    if (histogramTexture) {
        histogramTexture->Release();
        histogramTexture = nullptr;
    }
    histW = 0;
    histH = 0;
    histFmt = 0;
}