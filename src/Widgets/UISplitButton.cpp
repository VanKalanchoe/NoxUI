#include "NoxUI/Widgets/UISplitButton.h"

#include "NoxUI/Core/UIContext.h"
#include "NoxUI/Widgets/UIButton.h"

namespace NoxUI
{
    UISplitButton::UISplitButton(
        UIContext& context,
        std::string primaryLabel,
        std::function<void()> onPrimaryClick,
        std::vector<UIMenuItem> secondaryItems)
        : UIStackBox(UIOrientation::Horizontal, 2.0f)
    {
        auto mainBtn = Ref<UIButton>(new UIButton(std::move(primaryLabel), std::move(onPrimaryClick)));
        mainBtn->StyleVariant = "primary";

        auto dropBtn = Ref<UIButton>(new UIButton("v"));
        dropBtn->StyleVariant = "primary";
        UIButton* mainPtr = mainBtn.get();
        dropBtn->OnClick = [&context, mainPtr, items = std::move(secondaryItems)]()
        {
            const UIRect& b = mainPtr->GetBounds();
            UIMenuPopup::ShowContextMenu(context, { b.Min.x, b.Max.y + 3.0f }, items);
        };

        AddChild(mainBtn);
        AddChild(dropBtn);
    }

    void UISplitButton::HashState(UIStateHasher& h) const
    {
        UIStackBox::HashState(h);
    }
}
