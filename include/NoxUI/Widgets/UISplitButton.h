#pragma once

#include <functional>
#include <string>
#include <vector>

#include "NoxUI/Layout/UILayout.h"
#include "NoxUI/Widgets/UIMenu.h"

namespace NoxUI
{
    class UIContext;

    // Primary action button paired with an attached dropdown menu of secondary actions.
    class UISplitButton : public UIStackBox
    {
    public:
        NOXUI_STATE_HASH(UISplitButton)
        UISplitButton(
            UIContext& context,
            std::string primaryLabel,
            std::function<void()> onPrimaryClick,
            std::vector<UIMenuItem> secondaryItems);
    };
}
