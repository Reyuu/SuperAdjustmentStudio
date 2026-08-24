#include "toast_notifications.h"
#include <string>
#include <algorithm>
#include <vector>


void ToastNotificationManager::addToastNotification(const std::string& message, ToastTypeNotification type, double duration) {
    ToastNotification t;
    t.message = message;
    t.type = type;
    t.startTime = ImGui::GetTime();
    t.duration = duration;
    toastNotifications.push_back(t);
}

void ToastNotificationManager::renderToastNotifications() {
    if (toastNotifications.empty()) {
        return;
    }

    ImVec2 windowSize = ImGui::GetIO().DisplaySize;
    float yOffset = toastNotificationSpacing;

    for (int i = (int)toastNotifications.size() - 1; i >= 0; --i) {
        ToastNotification& t = toastNotifications[i];
        double elapsed = ImGui::GetTime() - t.startTime;
        if (elapsed > t.duration) {
            continue;
        }

        float alpha = 1.0f;
        if (elapsed > t.duration * 0.8) {
            alpha = 1.0f - (float)((elapsed - t.duration * 0.8) / (t.duration * 0.2));
            alpha = std::clamp(alpha, 0.0f, 1.0f);
        }

        float h = (t.height > 0.0f) ? t.height : 80.0f;
        ImGui::SetNextWindowSizeConstraints(ImVec2(toastNotificationWidth, 0), ImVec2(toastNotificationWidth, FLT_MAX));
        ImVec2 toastPos = ImVec2(windowSize.x - toastNotificationWidth - toastNotificationSpacing, windowSize.y - yOffset - h);
        ImGui::SetNextWindowPos(toastPos);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.9f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::Begin(("Toast##" + std::to_string(t.startTime)).c_str(), nullptr, toastNotificationWindowFlags);

        // Close button at top right
        ImGui::SetCursorPosX(toastNotificationWidth - toastNotificationPadding - 24);
        if (ImGui::Button((ICON_FA_XMARK "##close_toast" + std::to_string(t.startTime)).c_str())) {
            t.duration = 0;
        }
        ImGui::SameLine();
        ImGui::SetCursorPosX(toastNotificationPadding);

        const char* icon = toastNotificationIcon(t.type);
        ImVec4 color = toastNotificationColor(t.type);
        switch (t.type) {
            case ToastTypeSuccess: {
                ImGui::TextColored(color, "%s Success", icon);
                break;
            }
            case ToastTypeInfo: {
                ImGui::TextColored(color, "%s Info", icon);
                break;
            }
            case ToastTypeWarning: {
                ImGui::TextColored(color, "%s Warning", icon);
                break;
            }
            case ToastTypeError: {
                ImGui::TextColored(color, "%s Error", icon);
                break;
            }
        }

        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + toastNotificationWidth - toastNotificationPadding * 2);
        ImGui::TextWrapped("%s", t.message.c_str());
        ImGui::PopTextWrapPos();

        float progress = (float)(elapsed / t.duration);
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 16.0f), "");

        t.height = ImGui::GetWindowHeight();
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        yOffset += t.height + toastNotificationSpacing;
    }

    toastNotifications.erase(std::remove_if(toastNotifications.begin(), toastNotifications.end(), [](const ToastNotification& t) {
        return ImGui::GetTime() - t.startTime > t.duration;
    }), toastNotifications.end());
}