#pragma once

#include <functional>
#include <string>

#include "NoxUI/Core/UIElement.h"
#include "NoxUI/Widgets/UILabel.h"

namespace NoxUI
{

    class UIButton : public UIElement
    {
    public:
        explicit UIButton(std::string text = {}, std::function<void()> onClick = {});
        ~UIButton() override;

        void SetText(const std::string& text);
        const std::string& GetText() const;

        std::function<void()> OnClick;
        // How a text wider than the button is shortened (Middle for file and asset names). While it is shortened and the
        // button has no tooltip of its own, the full text is its tooltip.
        UITextElide TextElide = UITextElide::End;

        bool OnPointerUp(const UIPointerEvent& e) override;

    protected:
        UIVec2 ComputeDesiredSize() override;
        void ArrangeChildren(const UIRect& contentRect) override;

    private:
        Ref<UILabel> m_Label;
        bool m_AutoTooltip = false;
    };
}
