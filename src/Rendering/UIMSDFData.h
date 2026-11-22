#pragma once

#include <algorithm>
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
        // The pen advance from one byte to the next with kerning: what FontGeometry::getAdvance(advance, c1, c2) gives for the two chars
        // (computed with that function at load, for every pair: the same values, the same char-to-codepoint conversion, the same
        // pairs it fails on), as an index into PairAdvances -- 0: the call fails and leaves the advance as it was.
        std::vector<uint16_t> PairAdvanceIndex;  // 256 x 256
        std::vector<double> PairAdvances{ 0.0 }; // ([0] unused)

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

            PairAdvanceIndex.assign(256u * 256u, 0);
            PairAdvances.assign(1, 0.0);
            for (uint32_t first = 0; first < 256; ++first)
            {
                if (!FontGeometry.getGlyph(static_cast<char>(first))) // (getAdvance fails for every pair of this row)
                    continue;
                for (uint32_t second = 0; second < 256; ++second)
                {
                    const char c1 = static_cast<char>(first), c2 = static_cast<char>(second);
                    double advance = 0.0;
                    if (!FontGeometry.getAdvance(advance, c1, c2)) // (as text drawing calls it: chars to codepoints)
                        continue;
                    auto found = std::find(PairAdvances.begin() + 1, PairAdvances.end(), advance);
                    if (found == PairAdvances.end())
                    {
                        PairAdvances.push_back(advance);
                        found = PairAdvances.end() - 1;
                    }
                    PairAdvanceIndex[first * 256u + second] = static_cast<uint16_t>(found - PairAdvances.begin());
                }
            }
        }

        const msdf_atlas::GlyphGeometry* Glyph(char c) const { return ByteGlyphs[static_cast<unsigned char>(c)]; }
        // FontGeometry::getAdvance(advance, c1, c2) from the table: sets `advance` where that call succeeds, else leaves it.
        void PairAdvance(double& advance, char c1, char c2) const
        {
            if (PairAdvanceIndex.empty())
                return;
            const uint16_t index = PairAdvanceIndex[static_cast<unsigned char>(c1) * 256u + static_cast<unsigned char>(c2)];
            if (index != 0)
                advance = PairAdvances[index];
        }
    };
}
