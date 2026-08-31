#ifndef SAS_TOAST_NOTIFICATIONS_H
#define SAS_TOAST_NOTIFICATIONS_H

#include "../../core/util.h"
#include "IconsFontAwesome6.h"
#include "imgui.h"
#include <string>
#include <vector>

typedef enum ToastTypeNotification {
    ToastTypeSuccess,
    ToastTypeInfo,
    ToastTypeWarning,
    ToastTypeError
} ToastTypeNotification;

struct ToastNotification {
    public:
        std::string message;
        ToastTypeNotification type;
        double startTime;
        double duration;
        float height = 0.0f;
};

static ImVec4 toastNotificationColor(ToastTypeNotification type) {
    switch (type) {
        case ToastTypeSuccess:
            return hexToImVec4("#33cc33");
        case ToastTypeInfo:
            return hexToImVec4("#3399ff");
        case ToastTypeWarning:
            return hexToImVec4("#ffcc00");
        case ToastTypeError:
            return hexToImVec4("#ff3300");
        default:
            return hexToImVec4("#ffffff");
    }
}

static const char* toastNotificationIcon(ToastTypeNotification type) {
    switch (type) {
        case ToastTypeSuccess:
            return ICON_FA_CIRCLE_CHECK;
        case ToastTypeInfo:
            return ICON_FA_CIRCLE_INFO;
        case ToastTypeWarning:
            return ICON_FA_TRIANGLE_EXCLAMATION;
        case ToastTypeError:
            return ICON_FA_CIRCLE_XMARK;
        default:
            return "";
    }
}

class ToastNotificationManager {
    public:
        void addToastNotification(const std::string& message, ToastTypeNotification type, double duration);
        void renderToastNotifications();

    private:
        std::vector<ToastNotification> notifications;
        std::vector<ToastNotification> toastNotifications;
        int toastNotificationMaxCount = 5;
        int toastNotificationSpacing = 10;
        int toastNotificationPadding = 10;
        int toastNotificationWidth = 400;
        int toastNotificationHeight = 50;
        ImGuiWindowFlags toastNotificationWindowFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
};

#endif // SAS_TOAST_NOTIFICATIONS_H