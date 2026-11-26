#pragma once

#include <functional>
#include <string>

#include "NoxUI/Core/UIElement.h"

namespace NoxUI
{
    class UILabel;

    // Animated pill-shaped toggle switch with optional label.
    class UIToggleSwitch : public UIElement
    {
    public:
        NOXUI_STATE_HASH(UIToggleSwitch)
        explicit UIToggleSwitch(
            std::string label = {},
            bool checked = false,
            std::function<void(bool)> onToggled = {});

        void SetChecked(bool checked, bool notify = false);
        bool IsChecked() const { return m_Checked; }

        UIColor OnColor{ 0.24f, 0.56f, 0.96f, 1.0f };
        std::function<void(bool)> OnToggled;

        bool OnPointerUp(const UIPointerEvent& e) override;

    protected:
        void OnUpdate(float dt) override;
        UIVec2 ComputeDesiredSize() override;
        void ArrangeChildren(const UIRect& contentRect) override;
        void OnPaint(UIDrawList& drawList, const UITheme& theme) override;

    private:
        bool m_Checked = false;
        float m_AnimT = 0.0f;
        Ref<UILabel> m_Label;
    };
}
