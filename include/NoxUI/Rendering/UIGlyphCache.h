#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "NoxUI/Core/UITypes.h"

namespace NoxUI
{
    class UIFont;

    // Screen text at the size it is shown (Unreal Slate's font cache, Dear ImGui 1.92's dynamic fonts): each glyph is rasterized once
    // per pixel size and sub-pixel phase by FreeType into one 8-bit coverage atlas, and drawn texel for pixel (UI_PRIM_GLYPH_BITMAP).
    // A multi-channel distance field shrunk from a large atlas cannot do this: below its size the distance range covers less than a
    // pixel and edges go wavy, and nothing snaps to the pixel grid. Text above kMaxPixelSize keeps the distance field (it scales).
    //
    // Sizes are cached in steps of 1/2 px and the pen's x in steps of 1/4 px (the glyph is rasterized with that offset, so letters
    // keep their spacing while every quad sits on whole pixels); baselines snap to whole pixels. Layout (advances, kerning, line
    // height) stays the font's metrics, so measuring and drawing agree. Glyphs go into shelves of the atlas; a renderer backend uploads
    // the rectangles they changed before the frame that draws them (TakePendingRegions) -- noxui_impl_noxrhi does this in
    // NoxUI_ImplNoxRHI_CopyData. A glyph the atlas cannot hold this frame is drawn from the distance field, and the atlas is emptied
    // once the frame's uploads are recorded (ResetRequested / Reset).
    //
    // Main thread only. Without a backend that sets the texture (SetTextureID) all text uses the distance field.
    class UIGlyphCache
    {
    public:
        static UIGlyphCache& Get();
        ~UIGlyphCache();

        static constexpr uint32_t kAtlasSize = 2048;   // texels per side (R8: 4 MB)
        static constexpr float kMaxPixelSize = 48.0f;  // larger text is drawn from the distance field
        static constexpr int kSubpixelPhases = 4;      // pen x positions per pixel

        struct Glyph
        {
            int16_t X0 = 0, Y0 = 0; // the bitmap's top-left from the snapped pen (x) and baseline (y), whole pixels (y down)
            uint16_t W = 0, H = 0;  // 0: nothing to draw (a space)
            glm::vec4 UV{ 0.0f };   // min u, min v, max u, max v
        };

        // The glyph of `codepoint` at `pixelSize` (the size the UI uses: ascender to descender) with the pen `phase` quarters of a pixel
        // right of a whole pixel; rasterized now when new. Null: draw it from the distance field (no atlas texture, a font FreeType
        // cannot read, or the atlas is full until the frame's uploads are recorded).
        const Glyph* Find(const UIFont& font, float pixelSize, uint32_t codepoint, int phase);
        // The size glyphs are rasterized at for a requested size (steps of 1/2 px).
        static float QuantizeSize(float pixelSize);

        // The atlas texture (an R8 texture of kAtlasSize^2 the backend made); kInvalidTextureID: none, all text from the distance field.
        UITextureID GetTextureID() const { return m_TextureId; }
        void SetTextureID(UITextureID texture) { m_TextureId = texture; }

        // For the backend: the rectangles of the atlas written since the last call, and the atlas's pixels (rows of kAtlasSize bytes)
        // to copy them from. Each is a run of glyphs placed side by side on one shelf: it holds only them and texels no glyph uses, so
        // copying it never touches what an earlier frame still reads.
        struct Region
        {
            uint32_t X = 0, Y = 0, W = 0, H = 0;
        };
        std::vector<Region> TakePendingRegions();
        const uint8_t* GetPixels() const { return m_Pixels.data(); }
        // The atlas ran out of room this frame: Reset once the frame's uploads are recorded (the glyphs drawn this frame keep their
        // texels until later uploads, which the GPU runs after this frame's draws, overwrite them).
        bool ResetRequested() const { return m_ResetRequested; }
        void Reset();
        // Drops every face and glyph (the backend shut down, or fonts are being reloaded).
        void Clear();

    private:
        UIGlyphCache();
        struct Library;
        struct FontFace;
        FontFace* FaceOf(const UIFont& font);

        std::unique_ptr<Library> m_Library;
        std::unordered_map<uint64_t, std::unique_ptr<FontFace>> m_Faces; // by UIFont::GetCacheId
        std::unordered_map<uint64_t, Glyph> m_Glyphs;                    // by face, size step, phase, codepoint
        std::vector<uint8_t> m_Pixels;
        uint32_t m_ShelfY = 0, m_ShelfHeight = 0, m_CursorX = 0;
        std::vector<Region> m_Pending;
        bool m_ResetRequested = false;
        UITextureID m_TextureId = kInvalidTextureID;
    };
}
