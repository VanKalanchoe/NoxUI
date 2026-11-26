#pragma once

#include <functional>
#include <string>
#include <vector>

#include "NoxUI/Core/UIElement.h"

namespace NoxUI
{
    class UIContext;
    class UILabel;

    // Selectable dropdown (combo box) that opens a themed overlay list below the trigger.
    class UIDropdown : public UIElement
    {
    public:
        NOXUI_STATE_HASH(UIDropdown)
        explicit UIDropdown(
            UIContext* context = nullptr,
            std::vector<std::string> items = {},
            int selectedIndex = 0,
            std::function<void(int)> onSelectionChanged = {});

        void SetContext(UIContext* context) { m_Context = context; }
        void SetItems(std::vector<std::string> items);
        const std::vector<std::string>& GetItems() const { return m_Items; }

        void SetSelectedIndex(int index, bool notify = false);
        int GetSelectedIndex() const { return m_SelectedIndex; }
        std::string GetSelectedText() const;

        std::function<void(int)> OnSelectionChanged;

        bool OnPointerUp(const UIPointerEvent& e) override;

    protected:
        UIVec2 ComputeDesiredSize() override;
        void ArrangeChildren(const UIRect& contentRect) override;
        void OnPaint(UIDrawList& drawList, const UITheme& theme) override;

    private:
        void SyncLabel();
        void OpenPopup();

        UIContext* m_Context = nullptr;
        std::vector<std::string> m_Items;
        int m_SelectedIndex = -1;
        Ref<UILabel> m_Label;
    };
}
