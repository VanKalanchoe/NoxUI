#pragma once

#include <vector>

#undef INFINITE
#include "msdf-atlas-gen/msdf-atlas-gen.h"

namespace NoxUI
{
    struct UIMSDFData
    {
        std::vector<msdf_atlas::GlyphGeometry> Glyphs;
        msdf_atlas::FontGeometry FontGeometry;
    };
}
