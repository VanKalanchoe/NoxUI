#pragma once

#include <functional>
#include <string>

#include "NoxUI/Core/UIElement.h"

namespace NoxUI
{
    class UITextInput : public UIElement
    {
    public:
        NOXUI_STATE_HASH(UITextInput)
        explicit UITextInput(std::string text = {}, std::string placeholder = {});

        std::string Text;
        std::string Placeholder;
        bool ReadOnly = false;

        std::function<void(const std::string&)> OnChanged;
        std::function<void(const std::string&)> OnSubmit;

        bool OnPointerDown(const UIPointerEvent& e) override;
        bool OnKeyDown(const UIKeyEvent& e) override;
        bool OnTextInput(std::string_view text) override;

        void SelectAll();

    protected:
        void OnUpdate(float dt) override;
        UIVec2 ComputeDesiredSize() override;
        void OnPaint(UIDrawList& drawList, const UITheme& theme) override;

    private:
        size_t m_Cursor = 0;
        bool m_SelectAll = false;
        bool m_WasFocusedOnPress = false;
        float m_BlinkTimer = 0.0f;
        uint64_t m_LastClickTicks = 0;
    };
}
