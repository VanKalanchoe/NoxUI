#pragma once

#include <functional>

#include "NoxUI/Core/UIElement.h"

namespace NoxUI
{
    // Dual-handle min/max range slider (useful for LOD ranges, attenuation, lifetimes).
    class UIRangeSlider : public UIElement
    {
    public:
        NOXUI_STATE_HASH(UIRangeSlider)
        explicit UIRangeSlider(
            float lowValue = 0.2f,
            float highValue = 0.8f,
            float minLimit = 0.0f,
            float maxLimit = 1.0f,
            std::function<void(float, float)> onRangeChanged = {});

        void SetRange(float low, float high, bool notify = false);
        float GetLow() const { return m_Low; }
        float GetHigh() const { return m_High; }

        float MinLimit = 0.0f;
        float MaxLimit = 1.0f;
        std::function<void(float, float)> OnRangeChanged;

        bool OnPointerDown(const UIPointerEvent& e) override;
        bool OnPointerMove(const UIPointerEvent& e) override;
        bool OnPointerUp(const UIPointerEvent& e) override;

    protected:
        UIVec2 ComputeDesiredSize() override;
        void OnPaint(UIDrawList& drawList, const UITheme& theme) override;

    private:
        enum class DragHandle : uint8_t
        {
            None = 0,
            Low,
            High
        };

        float ValueFromX(float x) const;

        float m_Low = 0.2f;
        float m_High = 0.8f;
        DragHandle m_Dragging = DragHandle::None;
    };
}
