#pragma once

#include <string>

#include "NoxUI/Core/UIRef.h"
#include "NoxUI/Core/UIElement.h"

namespace NoxUI
{
    class UIFont;
    // What a single line of text does when it is wider than the space it was given.
    enum class UITextElide : uint8_t
    {
        None = 0, // drawn in full (it leaves its bounds; the parent's clip rect cuts it)
        End,      // "A long property na..."
        Middle    // "bistro_LMBR_00...Glass_Wine.nmat": keeps a file name's start and its end
    };

    class UILabel : public UIElement
    {
    public:
        NOXUI_STATE_HASH(UILabel)
        explicit UILabel(std::string text = {}, float fontSize = 0.0f);
        ~UILabel() override;

        std::string Text;
        float FontSizeOverride = 0.0f; // 0 = use theme style font_size
        bool UseColorOverride = false;
        bool WordWrap = false;
        // A line that does not fit never leaves the label: it is shortened with "..." (wrapped labels wrap instead).
        UITextElide Elide = UITextElide::End;
        UIColor ColorOverride{ 1.0f, 1.0f, 1.0f, 1.0f };
        Ref<UIFont> FontAsset;

        static UIVec2 MeasureText(std::string_view text, const Ref<UIFont>& fontAsset, float fontSize);
        static std::string WrapText(std::string_view text, const Ref<UIFont>& fontAsset, float fontSize, float maxWidth);
        // The text itself when it fits in maxWidth, else its longest shortening with "..." that does.
        static std::string ElideText(std::string_view text, const Ref<UIFont>& fontAsset, float fontSize, float maxWidth,
                                     UITextElide mode = UITextElide::End);

        // Whether the last paint had to shorten the text (a button shows the full text as its tooltip then).
        bool IsElided() const { return m_Elided; }

    protected:
        float ComputeHeightForWidth(float width) override;
        UIVec2 ComputeDesiredSize() override;
        void OnPaint(UIDrawList& drawList, const UITheme& theme) override;

    private:
        // The last word wrap (layout's height queries and paint share it), kept while text, width, size and font stay the same.
        struct WrapCache
        {
            std::string Source;
            std::string Text;
            float Width = -1.0f;
            float FontSize = 0.0f;
            float Height = 0.0f;
            const UIFont* Font = nullptr;
            bool Valid = false;
        };
        const WrapCache& GetWrapped(float width, float fontSize);
        WrapCache m_WrapCache;

        // The shortened text of the last paint, kept while text, width and size stay the same.
        std::string m_ElideSource;
        std::string m_ElideResult;
        float m_ElideWidth = -1.0f;
        float m_ElideFontSize = 0.0f;
        UITextElide m_ElideMode = UITextElide::None;
        bool m_Elided = false;
    };
}
