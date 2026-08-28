#ifndef SAS_RAII_GUARDS_H
#define SAS_RAII_GUARDS_H

#include "imgui.h"

struct ChildScope {
        explicit ChildScope(const char* id, const ImVec2& sz, bool border, ImGuiWindowFlags f = 0) {
            open = ImGui::BeginChild(id, sz, border, f);
        }
        ~ChildScope() noexcept {
            ImGui::EndChild();
        }
        ChildScope(const ChildScope&) = delete;
        ChildScope& operator=(const ChildScope&) = delete;
        bool open = false;
};
#endif // SAS_RAII_GUARDS_H