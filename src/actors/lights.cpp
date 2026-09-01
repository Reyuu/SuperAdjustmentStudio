#include "lights.h"

#include "IconsFontAwesome6.h"
#include "application.h"
#include "imgui.h"
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
    if (!ImGui::CollapsingHeader(ICON_FA_LIGHTBULB " Lights")) {
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

    if (ImGui::Button(ICON_FA_LIGHTBULB " Add light")) {
        spawn("Engine.SpotLightMovable", "Spot light");
    }

    // if (ImGui::GetTime() - lastRefresh > 0.25f) {
    //     lastRefresh = ImGui::GetTime();
    //     Application::instance().engine().postGameThreadTask([this]() {
    //         updateActiveLights();
    //     });
    // }

    ImGui::Separator();
    ImGui::Text("Active:");
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
                ImGui::TextDisabled("(no active lights)");
            } else {
                for (size_t i = 0; i < entries.size(); ++i) {
                    const LightEntry& entry = entries[i];
                    ImGui::Text("%s: %s", entry.type.c_str(), entry.name.c_str());
                    ImGui::SameLine();
                    if (ImGui::Button((std::string("Select##light_") + std::to_string(i)).c_str())) {
                        selectLight(entry.actor);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button((std::string(ICON_FA_MINUS) + "##light_" + std::to_string(i)).c_str())) {
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
        ImGui::Text("Selected light:");
        bool changed = false;
        if (ImGui::CollapsingHeader("Light")) {
            changed = ImGui::ColorEdit3("Color##light", selectedSettings.color) || changed;
            changed = ImGui::DragFloat("Intensity##light", &selectedSettings.brightness, 0.1f, 0.0f, 100.0f) || changed;
            changed = ImGui::Checkbox("Enabled##light", &selectedSettings.enabled) || changed;
        }

        if (selectedSettings.isPoint && ImGui::CollapsingHeader("Shape")) {
            changed = ImGui::DragFloat("Radius##light", &selectedSettings.radius, 1.0f, 0.0f, 10000.0f) || changed;
            changed = ImGui::DragFloat("Falloff exponent##light", &selectedSettings.falloffExponent, 0.05f, 0.0f, 16.0f) || changed;
            changed = ImGui::DragFloat("Shadow radius multiplier##light", &selectedSettings.shadowRadiusMultiplier, 0.05f, 0.0f, 16.0f) || changed;
            if (selectedSettings.isSpot) {
                changed = ImGui::DragFloat("Inner cone##light", &selectedSettings.innerConeAngle, 0.5f, 0.0f, 89.0f) || changed;
                changed = ImGui::DragFloat("Outer cone##light", &selectedSettings.outerConeAngle, 0.5f, 0.0f, 89.0f) || changed;
                changed = ImGui::DragFloat("Light shaft cone##light", &selectedSettings.lightShaftConeAngle, 0.5f, 0.0f, 89.0f) || changed;
            }
        }

        if (ImGui::CollapsingHeader("Shadows")) {
            static const char* projectionItems[] = {"Default", "PCF", "VSM", "BPCF Low", "BPCF Medium", "BPCF High"};
            static const char* qualityItems[] = {"Low", "Medium", "High"};
            static const char* modeItems[] = {"Normal", "Modulate", "Modulate Better"};
            changed = ImGui::Checkbox("Cast shadows##light", &selectedSettings.castShadows) || changed;
            changed = ImGui::Checkbox("Cast dynamic shadows##light", &selectedSettings.castDynamicShadows) || changed;
            changed =
                ImGui::Combo("Projection technique##light", &selectedSettings.shadowProjectionTechnique, projectionItems, IM_ARRAYSIZE(projectionItems)) ||
                changed;
            changed = ImGui::Combo("Filter quality##light", &selectedSettings.shadowFilterQuality, qualityItems, IM_ARRAYSIZE(qualityItems)) || changed;
            changed = ImGui::Combo("Mode##light", &selectedSettings.lightShadowMode, modeItems, IM_ARRAYSIZE(modeItems)) || changed;
        }

        if (ImGui::CollapsingHeader("Bloom and shafts")) {
            changed = ImGui::Checkbox("Render light shafts##light", &selectedSettings.renderLightShafts) || changed;
            changed = ImGui::DragFloat("Bloom scale##light", &selectedSettings.bloomScale, 0.01f, 0.0f, 100.0f) || changed;
            changed = ImGui::DragFloat("Bloom threshold##light", &selectedSettings.bloomThreshold, 0.01f, 0.0f, 100.0f) || changed;
            changed = ImGui::DragFloat("Bloom screen blend threshold##light", &selectedSettings.bloomScreenBlendThreshold, 0.01f, 0.0f, 100.0f) || changed;
            changed = ImGui::ColorEdit3("Bloom tint##light", selectedSettings.bloomTint) || changed;
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
