#pragma once

#include <functional>
#include <string>
#include <vector>

#include "NoxUI/Layout/UILayout.h"

namespace NoxUI
{
    class UIButton;

    // Multi-section accordion container; optionally enforces only one expanded section at a time.
    class UIAccordion : public UIStackBox
    {
    public:
        NOXUI_STATE_HASH(UIAccordion)
        explicit UIAccordion(bool allowMultipleOpen = false);

        void AddSection(std::string title, const Ref<UIElement>& content, bool expanded = false);
        void ExpandSection(int index);

        bool AllowMultipleOpen = false;

    private:
        struct Section
        {
            std::string Title;
            Ref<UIButton> HeaderBtn;
            Ref<UIElement> Content;
            bool Expanded = false;
        };

        void SyncSections();
        std::vector<Section> m_Sections;
    };
}
