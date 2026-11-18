#pragma once

#include <cstdint>

#include "NoxUI/Core/UIElement.h"

namespace NoxUI
{
    // Retained textured quad widget displaying a backend texture handle/slot with optional corner rounding & tint.
    class UIImage : public UIElement
    {
    public:
        explicit UIImage(UITextureID textureId = kInvalidTextureID, const UIVec2& desiredSize = { 64.0f, 64.0f });

        UITextureID TextureId = kInvalidTextureID;
        UIVec2 UVMin{ 0.0f, 0.0f };
        UIVec2 UVMax{ 1.0f, 1.0f };
        UIColor Tint{ 1.0f, 1.0f, 1.0f, 1.0f };
        UICornerRadius CornerRadius = UICornerRadius::All(4.0f);
        float AspectRatio = 0.0f; // > 0 preserves width / height ratio on auto-sizing and paint

    protected:
        float ComputeHeightForWidth(float width) override;
        UIVec2 ComputeDesiredSize() override;
        void OnPaint(UIDrawList& drawList, const UITheme& theme) override;

    private:
        UIVec2 m_Desired{ 64.0f, 64.0f };
    };
}
