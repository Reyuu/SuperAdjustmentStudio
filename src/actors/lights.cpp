#include "lights.h"
#include "../../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "IconsFontAwesome6.h"
#include "application.h"
#include "imgui.h"
#include "ui/helpers/labels.h"
#include "ui/helpers/raii_guards.h"
#include "util.h"
#include <algorithm>

static void copyColor(const FColor& source, float target[3]) {
    target[0] = source.R / 255.0f;
    target[1] = source.G / 255.0f;
    target[2] = source.B / 255.0f;
}

static FColor makeColor(const float source[3]) {
    return {static_cast<unsigned char>(source[2] * 255.0f), static_cast<unsigned char>(source[1] * 255.0f), static_cast<unsigned char>(source[0] * 255.0f),
            255};
}

void LightManager::addLight(AActor* actor, const std::string& type) {
    if (!isLiveObject(actor)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(lightsMtx);
        lightEntries.push_back({FStringToUtf8(actor->GetName()), type, actor});
        pendingSelection = actor;
    }
}

void LightManager::removeLight(AActor* actor) {
    if (isLiveObject(actor)) {
        if (!actor->Destroy()) {
            actor->LifeSpan = 0.001f;
        }
    }
    std::lock_guard<std::mutex> lock(lightsMtx);
    lightEntries.erase(std::remove_if(lightEntries.begin(), lightEntries.end(),
                                      [actor](const LightEntry& entry) {
                                          return entry.actor == actor;
                                      }),
                       lightEntries.end());
}

void LightManager::updateActiveLights() {
    std::lock_guard<std::mutex> lock(lightsMtx);
    lightEntries.erase(std::remove_if(lightEntries.begin(), lightEntries.end(),
                                      [](const LightEntry& entry) {
                                          return !isLiveObject(entry.actor);
                                      }),
                       lightEntries.end());
}

void LightManager::selectLight(AActor* actor) {
    ALight* light = static_cast<ALight*>(actor);
    if (!isLiveObject(light) || !isLiveObject(light->LightComponent)) {
        return;
    }

    ULightComponent* component = light->LightComponent;
    selectedLight = actor;
    selectedSettings = {};
    selectedSettings.brightness = component->Brightness;
    copyColor(component->LightColor, selectedSettings.color);
    selectedSettings.enabled = component->bEnabled != 0;
    selectedSettings.castShadows = component->CastShadows != 0;
    selectedSettings.castDynamicShadows = component->CastDynamicShadows != 0;
    selectedSettings.renderLightShafts = component->bRenderLightShafts != 0;
    selectedSettings.shadowProjectionTechnique = component->ShadowProjectionTechnique;
    selectedSettings.shadowFilterQuality = component->ShadowFilterQuality;
    selectedSettings.lightShadowMode = component->LightShadowMode;
    selectedSettings.bloomScale = component->BloomScale;
    selectedSettings.bloomThreshold = component->BloomThreshold;
    selectedSettings.bloomScreenBlendThreshold = component->BloomScreenBlendThreshold;
    copyColor(component->BloomTint, selectedSettings.bloomTint);

    if (component->IsA(UPointLightComponent::StaticClass())) {
        UPointLightComponent* point = static_cast<UPointLightComponent*>(component);
        selectedSettings.isPoint = true;
        selectedSettings.radius = point->Radius;
        selectedSettings.falloffExponent = point->FalloffExponent;
        selectedSettings.shadowRadiusMultiplier = point->ShadowRadiusMultiplier;
        if (component->IsA(USpotLightComponent::StaticClass())) {
            USpotLightComponent* spot = static_cast<USpotLightComponent*>(component);
            selectedSettings.isSpot = true;
            selectedSettings.innerConeAngle = spot->InnerConeAngle;
            selectedSettings.outerConeAngle = spot->OuterConeAngle;
            selectedSettings.lightShaftConeAngle = spot->LightShaftConeAngle;
        }
    }

    Application::instance().ui().selectActor(actor);
}

void LightManager::applyLightProperties(AActor* actor, const LightSettings& settings) {
    ALight* light = static_cast<ALight*>(actor);
    if (!isLiveObject(light) || !isLiveObject(light->LightComponent)) {
        return;
    }

    ULightComponent* component = light->LightComponent;
    component->SetEnabled(settings.enabled ? 1 : 0);
    component->SetLightProperties(settings.brightness, makeColor(settings.color), component->Function);
    component->CastShadows = settings.castShadows ? 1 : 0;
    component->CastDynamicShadows = settings.castDynamicShadows ? 1 : 0;
    const bool prevRenderLightShafts = component->bRenderLightShafts != 0;
    component->bRenderLightShafts = settings.renderLightShafts ? 1 : 0;
    component->ShadowProjectionTechnique = static_cast<unsigned char>(settings.shadowProjectionTechnique);
    component->ShadowFilterQuality = static_cast<unsigned char>(settings.shadowFilterQuality);
    component->LightShadowMode = static_cast<unsigned char>(settings.lightShadowMode);
    component->BloomScale = settings.bloomScale;
    component->BloomThreshold = settings.bloomThreshold;
    component->BloomScreenBlendThreshold = settings.bloomScreenBlendThreshold;
    component->BloomTint = makeColor(settings.bloomTint);

    if (settings.isPoint && component->IsA(UPointLightComponent::StaticClass())) {
        UPointLightComponent* point = static_cast<UPointLightComponent*>(component);
        point->Radius = settings.radius;
        point->FalloffExponent = settings.falloffExponent;
        point->ShadowRadiusMultiplier = settings.shadowRadiusMultiplier;
        if (isLiveObject(point->PreviewLightRadius)) {
            point->PreviewLightRadius->SphereRadius = settings.radius;
        }
        if (settings.isSpot && component->IsA(USpotLightComponent::StaticClass())) {
            USpotLightComponent* spot = static_cast<USpotLightComponent*>(component);
            spot->InnerConeAngle = settings.innerConeAngle;
            spot->OuterConeAngle = settings.outerConeAngle;
            spot->LightShaftConeAngle = settings.lightShaftConeAngle;
        }
    }

    component->UpdateLightShaftParameters();
    // force rebuild of light shafts if the setting changed, since it doesn't happen automatically
    if (prevRenderLightShafts != settings.renderLightShafts) {
        light->ReattachComponent(component);
    }

    component->OnUpdatePropertyBloomScale();
    component->OnUpdatePropertyBloomTint();
}

void LightManager::renderUI() {
    if (!ImGui::CollapsingHeader((std::string(ICON_FA_LIGHTBULB " ") + t("ui.lights")).c_str())) {
        return;
    }

    ImGui::Indent();
    AActor* pending = nullptr;
    {
        std::lock_guard<std::mutex> lock(lightsMtx);
        pending = pendingSelection;
        pendingSelection = nullptr;
    }

    if (pending) {
        selectLight(pending);
    }
    auto spawn = [this](const char* className, const char* type) {
        Transform transform = Application::instance().ui().getSelectedTransform();
        Application::instance().engine().postGameThreadTask([this, className = std::string(className), type = std::string(type), transform]() {
            AActor* actor = Application::instance().engine().spawnClass(className, transform);
            if (actor) {
                addLight(actor, type);
            }
        });
    };

    if (ImGui::Button((std::string(ICON_FA_LIGHTBULB " ") + t("ui.lights_table.add")).c_str())) {
        spawn("Engine.SpotLightMovable", "Spot light");
    }

    // if (ImGui::GetTime() - lastRefresh > 0.25f) {
    //     lastRefresh = ImGui::GetTime();
    //     Application::instance().engine().postGameThreadTask([this]() {
    //         updateActiveLights();
    //     });
    // }

    ImGui::Separator();
    ImGui::Text(t("ui.lights_table.active"));
    {
        ChildScope child("##light_active_list", ImVec2(0, 160), true);
        if (child.open) {
            if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##light_active_refresh")) {
                Application::instance().engine().postGameThreadTask([this]() {
                    updateActiveLights();
                });
            }

            ImGui::Separator();
            std::vector<LightEntry> entries;
            {
                std::lock_guard<std::mutex> lock(lightsMtx);
                entries = lightEntries;
            }

            if (entries.empty()) {
                ImGui::TextDisabled(t("ui.lights_table.no_active"));
            } else {
                for (size_t i = 0; i < entries.size(); ++i) {
                    const LightEntry& entry = entries[i];
                    ImGui::Text("%s: %s", entry.type.c_str(), entry.name.c_str());
                    ImGui::SameLine();
                    if (ImGui::Button((std::string(t("ui.lights_table.select")) + "##light_" + std::to_string(i)).c_str())) {
                        selectLight(entry.actor);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button((std::string(ICON_FA_TRASH_CAN) + "##light_" + std::to_string(i)).c_str())) {
                        Application::instance().engine().postGameThreadTask([this, actor = entry.actor]() {
                            removeLight(actor);
                        });
                    }
                }
            }
        }
    }

    ALight* selected = static_cast<ALight*>(selectedLight);
    if (isLiveObject(selected) && isLiveObject(selected->LightComponent)) {
        ImGui::Separator();
        ImGui::Text(t("ui.lights_table.selected"));
        bool changed = false;
        if (ImGui::CollapsingHeader(t("ui.lights_table.light"))) {
            labelAbove((std::string(t("ui.lights_table.color")) + "##light").c_str());
            changed = ImGui::ColorEdit3("##light", selectedSettings.color) || changed;
            labelAbove((std::string(t("ui.lights_table.intensity")) + "##light").c_str());
            changed = ImGui::DragFloat("##light", &selectedSettings.brightness, 0.1f, SETTINGS_LIGHT_BRIGHTNESS_MIN, SETTINGS_LIGHT_BRIGHTNESS_MAX) || changed;
            changed = ImGui::Checkbox((std::string(t("ui.lights_table.enabled")) + "##light").c_str(), &selectedSettings.enabled) || changed;
        }

        if (selectedSettings.isPoint && ImGui::CollapsingHeader(t("ui.lights_table.shape"))) {
            labelAbove((std::string(t("ui.lights_table.radius")) + "##light").c_str());
            changed = ImGui::DragFloat("##light", &selectedSettings.radius, 1.0f, SETTINGS_LIGHT_RADIUS_MIN, SETTINGS_LIGHT_RADIUS_MAX) || changed;
            labelAbove((std::string(t("ui.lights_table.falloff")) + "##light").c_str());
            changed = ImGui::DragFloat("##light", &selectedSettings.falloffExponent, 0.05f, SETTINGS_LIGHT_FALLOFF_MIN, SETTINGS_LIGHT_FALLOFF_MAX) || changed;
            labelAbove((std::string(t("ui.lights_table.shadow_radius_mult")) + "##light").c_str());
            changed = ImGui::DragFloat("##light", &selectedSettings.shadowRadiusMultiplier, 0.05f, SETTINGS_LIGHT_SHADOW_RADIUS_MULT_MIN,
                                       SETTINGS_LIGHT_SHADOW_RADIUS_MULT_MAX) ||
                      changed;
            if (selectedSettings.isSpot) {
                labelAbove((std::string(t("ui.lights_table.inner_cone_angle")) + "##light").c_str());
                changed = ImGui::DragFloat("##light", &selectedSettings.innerConeAngle, 0.5f, SETTINGS_LIGHT_CONE_MIN, SETTINGS_LIGHT_CONE_MAX) || changed;
                labelAbove((std::string(t("ui.lights_table.outer_cone_angle")) + "##light").c_str());
                changed = ImGui::DragFloat("##light", &selectedSettings.outerConeAngle, 0.5f, SETTINGS_LIGHT_CONE_MIN, SETTINGS_LIGHT_CONE_MAX) || changed;
                labelAbove((std::string(t("ui.lights_table.light_shaft_cone")) + "##light").c_str());
                changed = ImGui::DragFloat("##light", &selectedSettings.lightShaftConeAngle, 0.5f, SETTINGS_LIGHT_CONE_MIN, SETTINGS_LIGHT_CONE_MAX) || changed;
            }
        }

        if (ImGui::CollapsingHeader(t("ui.lights_table.shadows"))) {
            const std::string proj0 = Translation::instance().translate("ui.lights_table.default");
            const std::string proj3 = Translation::instance().translate("ui.lights_table.bpcf_low");
            const std::string proj4 = Translation::instance().translate("ui.lights_table.bpcf_medium");
            const std::string proj5 = Translation::instance().translate("ui.lights_table.bpcf_high");
            const std::string qual0 = Translation::instance().translate("ui.lights_table.low");
            const std::string qual1 = Translation::instance().translate("ui.lights_table.medium");
            const std::string qual2 = Translation::instance().translate("ui.lights_table.high");
            const std::string mode0 = Translation::instance().translate("ui.lights_table.normal");
            const std::string mode1 = Translation::instance().translate("ui.lights_table.modulate");
            const std::string mode2 = Translation::instance().translate("ui.lights_table.modulate_plus");
            const char* projectionItems[] = {proj0.c_str(), "PCF", "VSM", proj3.c_str(), proj4.c_str(), proj5.c_str()};
            const char* qualityItems[] = {qual0.c_str(), qual1.c_str(), qual2.c_str()};
            const char* modeItems[] = {mode0.c_str(), mode1.c_str(), mode2.c_str()};
            changed = ImGui::Checkbox((std::string(t("ui.lights_table.cast_shadows")) + "##light").c_str(), &selectedSettings.castShadows) || changed;
            changed = ImGui::Checkbox((std::string(t("ui.lights_table.cast_dyn_shadow")) + "##light").c_str(), &selectedSettings.castDynamicShadows) || changed;
            labelAbove((std::string(t("ui.lights_table.projection_technique")) + "##light").c_str());
            changed = ImGui::Combo("##light", &selectedSettings.shadowProjectionTechnique, projectionItems, IM_ARRAYSIZE(projectionItems)) || changed;
            labelAbove((std::string(t("ui.lights_table.filter_quality")) + "##light").c_str());
            changed = ImGui::Combo("##light", &selectedSettings.shadowFilterQuality, qualityItems, IM_ARRAYSIZE(qualityItems)) || changed;
            labelAbove((std::string(t("ui.lights_table.mode")) + "##light").c_str());
            changed = ImGui::Combo("##light", &selectedSettings.lightShadowMode, modeItems, IM_ARRAYSIZE(modeItems)) || changed;
        }

        if (ImGui::CollapsingHeader(t("ui.lights_table.boom_and_shafts"))) {
            changed =
                ImGui::Checkbox((std::string(t("ui.lights_table.render_light_shafts")) + "##light").c_str(), &selectedSettings.renderLightShafts) || changed;
            labelAbove((std::string(t("ui.lights_table.bloom_scale")) + "##light").c_str());
            changed = ImGui::DragFloat("##light", &selectedSettings.bloomScale, 0.01f, SETTINGS_LIGHT_BLOOM_MIN, SETTINGS_LIGHT_BLOOM_MAX) || changed;
            labelAbove((std::string(t("ui.lights_table.bloom_threshold")) + "##light").c_str());
            changed = ImGui::DragFloat("##light", &selectedSettings.bloomThreshold, 0.01f, SETTINGS_LIGHT_BLOOM_MIN, SETTINGS_LIGHT_BLOOM_MAX) || changed;
            labelAbove((std::string(t("ui.lights_table.bloom_sceen_threshold")) + "##light").c_str());
            changed =
                ImGui::DragFloat("##light", &selectedSettings.bloomScreenBlendThreshold, 0.01f, SETTINGS_LIGHT_BLOOM_MIN, SETTINGS_LIGHT_BLOOM_MAX) || changed;
            labelAbove((std::string(t("ui.lights_table.bloom_tint")) + "##light").c_str());
            changed = ImGui::ColorEdit3("##light", selectedSettings.bloomTint) || changed;
        }

        if (changed) {
            AActor* actor = selectedLight;
            LightSettings settings = selectedSettings;
            Application::instance().engine().postGameThreadTask([this, actor, settings]() {
                applyLightProperties(actor, settings);
            });
        }
    }
    ImGui::Unindent();
}
