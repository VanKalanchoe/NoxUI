#include "NoxUI/Rendering/UIDrawList.h"
#include "NoxUI/Rendering/UIGlyphCache.h"

#include <algorithm>
#include <cmath>

#include "NoxUI/Core/UILog.h"
#include "NoxUI/Rendering/UIFont.h"
#include "Rendering/UIMSDFData.h"

namespace NoxUI
{
    UIDrawList::UIDrawList()
    {
        Clear(glm::vec2(1.0f, 1.0f));
    }

    void UIDrawList::Clear(const glm::vec2& viewportSize)
    {
        Touch();
        m_ViewportSize = glm::max(viewportSize, glm::vec2(1.0f));
        m_Primitives.clear();
        m_ClipRects.clear();
        m_ClipStack.clear();

        UIClipRect rootClip{};
        rootClip.rect = glm::vec4(0.0f, 0.0f, m_ViewportSize.x, m_ViewportSize.y);
        rootClip.cornerRadii = glm::vec4(0.0f);
        m_ClipRects.push_back(rootClip);
        m_ClipStack.push_back(0u);
    }

    void UIDrawList::PushClipRect(const glm::vec2& min, const glm::vec2& max, const glm::vec4& cornerRadii)
    {
        Touch();
        const UIClipRect& parent = m_ClipRects[CurrentClipIndex()];
        glm::vec2 clampedMin = glm::max(min, glm::vec2(parent.rect.x, parent.rect.y));
        glm::vec2 clampedMax = glm::min(max, glm::vec2(parent.rect.z, parent.rect.w));
        clampedMax = glm::max(clampedMax, clampedMin);

        UIClipRect clip{};
        clip.rect = glm::vec4(clampedMin.x, clampedMin.y, clampedMax.x, clampedMax.y);
        clip.cornerRadii = cornerRadii;

        const uint32_t index = static_cast<uint32_t>(m_ClipRects.size());
        m_ClipRects.push_back(clip);
        m_ClipStack.push_back(index);
    }

    void UIDrawList::PopClipRect()
    {
        Touch();
        if (m_ClipStack.size() <= 1)
        {
            LogMessage(LogLevel::Warn, "UIDrawList::PopClipRect called with empty user clip stack");
            return;
        }
        m_ClipStack.pop_back();
    }

    void UIDrawList::AddRoundedRect(const glm::vec2& min, const glm::vec2& max, const glm::vec4& color,
                                    const glm::vec4& cornerRadii, float borderThickness, const glm::vec4& borderColor,
                                    const glm::vec4& shadowColor, const glm::vec2& shadowOffset, float shadowBlur)
    {
        Touch();
        if (max.x <= min.x || max.y <= min.y)
            return;

        UIPrimitive prim{};
        prim.bounds = glm::vec4(min.x, min.y, max.x, max.y);
        prim.color0 = color;
        prim.color1 = color;
        prim.borderColor = borderColor;
        prim.shadowColor = shadowColor;
        prim.cornerRadii = cornerRadii;
        prim.shadowOffset = shadowOffset;
        prim.shadowBlur = std::max(0.0f, shadowBlur);
        prim.borderThickness = std::max(0.0f, borderThickness);
        prim.type = UI_PRIM_ROUNDED_RECT;
        prim.clipIndex = CurrentClipIndex();
        prim.flags = 0u;
        m_Primitives.push_back(prim);
    }

    void UIDrawList::AddRoundedRectGradient(const glm::vec2& min, const glm::vec2& max,
                                            const glm::vec4& topColor, const glm::vec4& bottomColor,
                                            const glm::vec4& cornerRadii, float borderThickness, const glm::vec4& borderColor,
                                            const glm::vec4& shadowColor, const glm::vec2& shadowOffset, float shadowBlur)
    {
        Touch();
        if (max.x <= min.x || max.y <= min.y)
            return;

        UIPrimitive prim{};
        prim.bounds = glm::vec4(min.x, min.y, max.x, max.y);
        prim.color0 = topColor;
        prim.color1 = bottomColor;
        prim.borderColor = borderColor;
        prim.shadowColor = shadowColor;
        prim.cornerRadii = cornerRadii;
        prim.shadowOffset = shadowOffset;
        prim.shadowBlur = std::max(0.0f, shadowBlur);
        prim.borderThickness = std::max(0.0f, borderThickness);
        prim.type = UI_PRIM_ROUNDED_RECT;
        prim.clipIndex = CurrentClipIndex();
        prim.flags = 1u;
        m_Primitives.push_back(prim);
    }

    void UIDrawList::AddTexturedQuad(const glm::vec2& min, const glm::vec2& max, UITextureID textureId,
                                     const glm::vec2& uvMin, const glm::vec2& uvMax,
                                     const glm::vec4& tint, const glm::vec4& cornerRadii)
    {
        Touch();
        if (max.x <= min.x || max.y <= min.y || textureId == kInvalidTextureID)
            return;

        UIPrimitive prim{};
        prim.bounds = glm::vec4(min.x, min.y, max.x, max.y);
        prim.color0 = tint;
        prim.cornerRadii = cornerRadii;
        prim.uvOrP0P1 = glm::vec4(uvMin.x, uvMin.y, uvMax.x, uvMax.y);
        prim.type = UI_PRIM_TEXTURED_QUAD;
        prim.clipIndex = CurrentClipIndex();
        prim.textureIndex = static_cast<uint32_t>(textureId);
        m_Primitives.push_back(prim);
    }

    bool UIDrawList::DrawCachedGlyph(UIGlyphCache& cache, const UIFont& font, float fontSizePx, uint32_t codepoint, double penX, double baselineY,
                                     const glm::vec4& color)
    {
        // The pen on a whole pixel plus a quarter-pixel phase the glyph was rasterized with; the baseline on a whole pixel.
        double whole = std::floor(penX);
        int phase = static_cast<int>(std::lround((penX - whole) * UIGlyphCache::kSubpixelPhases));
        if (phase >= UIGlyphCache::kSubpixelPhases)
        {
            whole += 1.0;
            phase = 0;
        }
        const UIGlyphCache::Glyph* glyph = cache.Find(font, fontSizePx, codepoint, phase);
        if (!glyph)
            return false;
        if (glyph->W == 0 || glyph->H == 0)
            return true; // (nothing to draw)
        const float x0 = static_cast<float>(whole) + static_cast<float>(glyph->X0);
        const float y0 = static_cast<float>(std::round(baselineY)) + static_cast<float>(glyph->Y0);
        UIPrimitive prim{};
        prim.bounds = glm::vec4(x0, y0, x0 + static_cast<float>(glyph->W), y0 + static_cast<float>(glyph->H));
        prim.color0 = color;
        prim.uvOrP0P1 = glyph->UV;
        prim.type = UI_PRIM_GLYPH_BITMAP;
        prim.clipIndex = CurrentClipIndex();
        prim.textureIndex = static_cast<uint32_t>(cache.GetTextureID());
        m_Primitives.push_back(prim);
        return true;
    }

    void UIDrawList::AddText(std::string_view text, const Ref<UIFont>& fontIn, const glm::vec2& topLeftPos,
                             float fontSizePx, const glm::vec4& color)
    {
        Touch();
        if (text.empty() || fontSizePx <= 0.0f)
            return;

        Ref<UIFont> font = fontIn ? fontIn : UIFont::GetDefault();
        if (!font || !font->IsValid() || !font->GetMSDFData())
            return;

        const UITextureID atlasTexId = font->GetAtlasTextureID();
        if (atlasTexId == kInvalidTextureID)
            return;

        const auto& fontGeometry = font->GetMSDFData()->FontGeometry;
        const auto& metrics = fontGeometry.getMetrics();
        const double scale = static_cast<double>(fontSizePx) / (metrics.ascenderY - metrics.descenderY);

        const float texelW = 1.0f / static_cast<float>(std::max(font->GetAtlasWidth(), 1u));
        const float texelH = 1.0f / static_cast<float>(std::max(font->GetAtlasHeight(), 1u));
        const uint32_t atlasSlot = static_cast<uint32_t>(atlasTexId);

        double x = static_cast<double>(topLeftPos.x);
        double baselineY = static_cast<double>(topLeftPos.y) + metrics.ascenderY * scale;
        const double lineHeight = metrics.lineHeight * scale;

        // Up to UIGlyphCache::kMaxPixelSize: glyphs rasterized at this size from the glyph cache, on whole pixels (the layout -- advances,
        // kerning, line height -- stays the distance field's, so measuring and drawing agree). Larger text, and a glyph the cache cannot
        // give this frame, from the distance field.
        UIGlyphCache& glyphCache = UIGlyphCache::Get();
        const bool useGlyphCache = fontSizePx <= UIGlyphCache::kMaxPixelSize && glyphCache.GetTextureID() != kInvalidTextureID;

        const UIMSDFData& msdf = *font->GetMSDFData();
        const double spaceAdvance = msdf.SpaceAdvance;

        for (size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[i];
            if (c == '\r')
                continue;
            if (c == '\n')
            {
                x = static_cast<double>(topLeftPos.x);
                baselineY += lineHeight;
                continue;
            }
            if (c == ' ')
            {
                double adv = spaceAdvance;
                if (i + 1 < text.size())
                    msdf.PairAdvance(adv, c, text[i + 1]);
                x += adv * scale;
                continue;
            }
            if (c == '\t')
            {
                x += 4.0 * spaceAdvance * scale;
                continue;
            }

            const auto* glyph = msdf.Glyph(c); // ('?' for a byte the font lacks)
            if (!glyph)
                continue;

            if (useGlyphCache && m_FingerprintOnly)
            {
                // (fingerprinting: the glyph as what decides its look -- codepoint, pen, size, colour -- without the glyph cache, which
                // would rasterize it now and give later glyphs other places in the atlas than drawing would)
                UIPrimitive prim{};
                prim.bounds = glm::vec4(static_cast<float>(x), static_cast<float>(baselineY), fontSizePx, static_cast<float>(glyph->getCodepoint()));
                prim.color0 = color;
                prim.type = UI_PRIM_GLYPH_BITMAP;
                prim.clipIndex = CurrentClipIndex();
                m_Primitives.push_back(prim);
                double advance = glyph->getAdvance();
                if (i + 1 < text.size())
                    msdf.PairAdvance(advance, c, text[i + 1]);
                x += advance * scale;
                continue;
            }
            if (useGlyphCache && DrawCachedGlyph(glyphCache, *font, fontSizePx, glyph->getCodepoint(), x, baselineY, color))
            {
                double advance = glyph->getAdvance();
                if (i + 1 < text.size())
                    msdf.PairAdvance(advance, c, text[i + 1]);
                x += advance * scale;
                continue;
            }

            double al = 0.0, ab = 0.0, ar = 0.0, at = 0.0;
            glyph->getQuadAtlasBounds(al, ab, ar, at);

            double pl = 0.0, pb = 0.0, pr = 0.0, pt = 0.0;
            glyph->getQuadPlaneBounds(pl, pb, pr, pt);

            if (pr > pl && pt > pb)
            {
                const float x0 = static_cast<float>(x + pl * scale);
                const float x1 = static_cast<float>(x + pr * scale);
                const float y0 = static_cast<float>(baselineY - pt * scale);
                const float y1 = static_cast<float>(baselineY - pb * scale);

                UIPrimitive prim{};
                prim.bounds = glm::vec4(x0, y0, x1, y1);
                prim.color0 = color;
                prim.uvOrP0P1 = glm::vec4(static_cast<float>(al) * texelW, static_cast<float>(at) * texelH,
                                          static_cast<float>(ar) * texelW, static_cast<float>(ab) * texelH);
                prim.type = UI_PRIM_MSDF_GLYPH;
                prim.clipIndex = CurrentClipIndex();
                prim.textureIndex = atlasSlot;
                m_Primitives.push_back(prim);
            }

            double advance = glyph->getAdvance();
            if (i + 1 < text.size())
                msdf.PairAdvance(advance, c, text[i + 1]);
            x += advance * scale;
        }
    }

    void UIDrawList::AddBezier(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3,
                               float thickness, const glm::vec4& color)
    {
        Touch();
        if (thickness <= 0.0f || color.a <= 0.0f)
            return;

        const glm::vec2 minP = glm::min(glm::min(p0, p1), glm::min(p2, p3));
        const glm::vec2 maxP = glm::max(glm::max(p0, p1), glm::max(p2, p3));

        UIPrimitive prim{};
        prim.bounds = glm::vec4(minP.x, minP.y, maxP.x, maxP.y);
        prim.color0 = color;
        prim.uvOrP0P1 = glm::vec4(p0.x, p0.y, p1.x, p1.y);
        prim.extraOrP2P3 = glm::vec4(p2.x, p2.y, p3.x, p3.y);
        prim.borderThickness = thickness;
        prim.type = UI_PRIM_CUBIC_BEZIER;
        prim.clipIndex = CurrentClipIndex();
        m_Primitives.push_back(prim);
    }
}
