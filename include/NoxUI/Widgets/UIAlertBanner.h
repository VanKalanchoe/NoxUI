#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "NoxUI/Layout/UILayout.h"

namespace NoxUI
{
    enum class UIAlertSeverity : uint8_t
    {
        Info = 0,
        Success,
        Warning,
        Error
    };

    // Dismissible inline alert banner with colored accent bar, message, optional action, and close [X].
    class UIAlertBanner : public UIStackBox
    {
    public:
        UIAlertBanner(
            UIAlertSeverity severity,
            std::string message,
            std::string actionLabel = {},
            std::function<void()> onAction = {},
            bool dismissible = true);

    protected:
        void OnPaint(UIDrawList& drawList, const UITheme& theme) override;

    private:
        UIColor m_Accent{ 0.24f, 0.56f, 0.96f, 1.0f };
    };
}
