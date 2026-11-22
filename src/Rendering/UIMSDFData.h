#pragma once

#include <array>
#include <vector>

#undef INFINITE
#include "msdf-atlas-gen/msdf-atlas-gen.h"

namespace NoxUI
{
    struct UIMSDFData
    {
        std::vector<msdf_atlas::GlyphGeometry> Glyphs;
        msdf_atlas::FontGeometry FontGeometry;

        // The glyph of each byte (the font's charset is Latin-1: one byte is one codepoint), with '?' for a byte the font lacks, and the
        // space's advance -- looked up once at load: FontGeometry::getGlyph searches a std::map, which every measured or drawn character
        // used to pay (the editor-widgets demo spent most of its 3.4 ms per frame there). Pointers into Glyphs, which no longer changes.
        std::array<const msdf_atlas::GlyphGeometry*, 256> ByteGlyphs{};
        double SpaceAdvance = 0.25;

        void BuildByteGlyphs()
        {
            const msdf_atlas::GlyphGeometry* fallback = FontGeometry.getGlyph('?');
            for (uint32_t c = 0; c < 256; ++c)
            {
                const msdf_atlas::GlyphGeometry* glyph = FontGeometry.getGlyph(c);
                ByteGlyphs[c] = glyph ? glyph : fallback;
            }
            if (const msdf_atlas::GlyphGeometry* space = FontGeometry.getGlyph(' '))
                SpaceAdvance = space->getAdvance();
        }

        const msdf_atlas::GlyphGeometry* Glyph(char c) const { return ByteGlyphs[static_cast<unsigned char>(c)]; }
    };
}
