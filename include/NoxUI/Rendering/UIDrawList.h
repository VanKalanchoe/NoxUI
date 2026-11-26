#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "NoxUI/Core/UIRef.h"
#include "NoxUI/Core/UITypes.h"

namespace NoxUI
{
    class UIFont;
    class UIGlyphCache;

    enum UIPrimitiveType : uint32_t
    {
        UI_PRIM_ROUNDED_RECT  = 0u,
        UI_PRIM_TEXTURED_QUAD = 1u,
        UI_PRIM_MSDF_GLYPH    = 2u,
        UI_PRIM_CUBIC_BEZIER  = 3u,
        UI_PRIM_GLYPH_BITMAP  = 4u  // a glyph from UIGlyphCache: 8-bit coverage, drawn texel for pixel
    };

    struct UIClipRect
    {
        glm::vec4 rect;        // (minX, minY, maxX, maxY) in pixels
        glm::vec4 cornerRadii; // (topLeft, topRight, bottomRight, bottomLeft)
    };

    struct UIPrimitive
    {
        glm::vec4 bounds;          // (minX, minY, maxX, maxY) in pixels
        glm::vec4 color0;          // primary fill / tint
        glm::vec4 color1;          // secondary gradient color / MSDF outline color
        glm::vec4 borderColor;     // border RGBA
        glm::vec4 shadowColor;     // drop shadow RGBA
        glm::vec4 cornerRadii;     // (topLeft, topRight, bottomRight, bottomLeft)
        glm::vec4 uvOrP0P1;        // Textured/MSDF: (uMin, vMin, uMax, vMax); Bezier: (p0.xy, p1.xy)
        glm::vec4 extraOrP2P3;     // Bezier: (p2.xy, p3.xy); RoundedRect: (gradientAngle, strokeWidth, 0, 0)
        glm::vec2 shadowOffset;    // drop shadow offset in pixels
        float shadowBlur;          // drop shadow blur radius in pixels (0 = disabled)
        float borderThickness;
        uint32_t type;             // UIPrimitiveType
        uint32_t clipIndex;        // index into UIClipRect buffer
        uint32_t textureIndex;     // backend texture descriptor slot / ID
        uint32_t flags;            // bit 0: gradient enabled, bit 1: MSDF outline enabled
    };

    // Collects resolution-independent 2D SDF UI primitives for one viewport frame.
    // Coordinates are in output pixels with origin at top-left (x right, y down).
    // What an element painted last for its fingerprint (UIElement::HashPaintOutput): compared with what it paints now, and a version
    // that changes when they differ -- comparing is exact, and cheaper than hashing every primitive every frame.
    struct UIPaintMemo
    {
        std::vector<UIPrimitive> Primitives;
        std::vector<UIClipRect> ClipRects;
        uint64_t Version = 0;
    };

    class UIDrawList
    {
    public:
        UIDrawList();

        void Clear(const glm::vec2& viewportSize);

        void PushClipRect(const glm::vec2& min, const glm::vec2& max, const glm::vec4& cornerRadii = glm::vec4(0.0f));
        void PopClipRect();

        void AddRoundedRect(const glm::vec2& min, const glm::vec2& max, const glm::vec4& color,
                            const glm::vec4& cornerRadii = glm::vec4(0.0f),
                            float borderThickness = 0.0f, const glm::vec4& borderColor = glm::vec4(0.0f),
                            const glm::vec4& shadowColor = glm::vec4(0.0f),
                            const glm::vec2& shadowOffset = glm::vec2(0.0f), float shadowBlur = 0.0f);

        void AddRoundedRectGradient(const glm::vec2& min, const glm::vec2& max,
                                    const glm::vec4& topColor, const glm::vec4& bottomColor,
                                    const glm::vec4& cornerRadii = glm::vec4(0.0f),
                                    float borderThickness = 0.0f, const glm::vec4& borderColor = glm::vec4(0.0f),
                                    const glm::vec4& shadowColor = glm::vec4(0.0f),
                                    const glm::vec2& shadowOffset = glm::vec2(0.0f), float shadowBlur = 0.0f);

        void AddTexturedQuad(const glm::vec2& min, const glm::vec2& max, UITextureID textureId,
                             const glm::vec2& uvMin = glm::vec2(0.0f, 0.0f),
                             const glm::vec2& uvMax = glm::vec2(1.0f, 1.0f),
                             const glm::vec4& tint = glm::vec4(1.0f),
                             const glm::vec4& cornerRadii = glm::vec4(0.0f));

        void AddText(std::string_view text, const Ref<UIFont>& font, const glm::vec2& topLeftPos,
                     float fontSizePx, const glm::vec4& color);

        void AddBezier(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3,
                       float thickness, const glm::vec4& color);

        const std::vector<UIPrimitive>& GetPrimitives() const { return m_Primitives; }
        const std::vector<UIClipRect>& GetClipRects() const { return m_ClipRects; }
        const glm::vec2& GetViewportSize() const { return m_ViewportSize; }
        bool IsEmpty() const { return m_Primitives.empty(); }
        // Changes with every change to the list (a copy keeps it: the same revision, the same content). UIContext leaves a list it
        // drew untouched on an idle frame only when its revision is still the one it left; a backend can skip an upload the same way.
        uint64_t GetRevision() const { return m_Revision; }
        // A list painted only to fingerprint what would be drawn (UIElement::HashPaintOutput): text records its glyphs (codepoint,
        // pen, size, colour) without asking the glyph cache, which must not rasterize or place glyphs for a list nobody draws.
        void SetFingerprintOnly(bool enabled) { m_FingerprintOnly = enabled; }

    private:
        void Touch() { m_Revision = ++s_RevisionCounter; }
        uint32_t CurrentClipIndex() const { return m_ClipStack.empty() ? 0u : m_ClipStack.back(); }
        // One glyph from the glyph cache at the pen (snapped to the pixel grid); false: draw it from the distance field.
        bool DrawCachedGlyph(UIGlyphCache& cache, const UIFont& font, float fontSizePx, uint32_t codepoint, double penX, double baselineY,
                             const glm::vec4& color);

        std::vector<UIPrimitive> m_Primitives;
        std::vector<UIClipRect> m_ClipRects;
        std::vector<uint32_t> m_ClipStack;
        glm::vec2 m_ViewportSize{ 1.0f, 1.0f };
        uint64_t m_Revision = 0;
        bool m_FingerprintOnly = false;
        inline static uint64_t s_RevisionCounter = 0;
    };
}
