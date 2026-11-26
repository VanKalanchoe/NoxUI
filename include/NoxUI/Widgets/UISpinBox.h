#pragma once

#include <functional>

#include "NoxUI/Layout/UILayout.h"

namespace NoxUI
{
    class UIDragScalar;

    // Numeric stepper combining [-] button, draggable scalar readout, and [+] button.
    class UISpinBox : public UIStackBox
    {
    public:
        NOXUI_STATE_HASH(UISpinBox)
        explicit UISpinBox(
            float initialValue = 0.0f,
            float step = 1.0f,
            float minValue = -1e6f,
            float maxValue = 1e6f,
            std::function<void(float)> onChanged = {});

        void SetValue(float value, bool notify = false);
        float GetValue() const;

        float Step = 1.0f;
        float MinValue = -1e6f;
        float MaxValue = 1e6f;
        std::function<void(float)> OnChanged;

    private:
        Ref<UIDragScalar> m_Scalar;
    };
}
