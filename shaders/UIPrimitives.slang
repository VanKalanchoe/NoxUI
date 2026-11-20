// ============================================================================
// NoxUI Self-Contained Cross-API SDF UI Shader (Vulkan BDA + D3D12 Bindless)
// ============================================================================

enum UIPrimitiveType : uint
{
    UI_PRIM_ROUNDED_RECT  = 0u,
    UI_PRIM_TEXTURED_QUAD = 1u,
    UI_PRIM_MSDF_GLYPH    = 2u,
    UI_PRIM_CUBIC_BEZIER  = 3u,
    UI_PRIM_GLYPH_BITMAP  = 4u  // a glyph from UIGlyphCache: 8-bit coverage, drawn texel for pixel
};

struct UIClipRect
{
    float4 rect;        // (minX, minY, maxX, maxY) in pixels
    float4 cornerRadii; // (topLeft, topRight, bottomRight, bottomLeft)
};

struct UIPrimitive
{
    float4 bounds;          // (minX, minY, maxX, maxY) in pixels
    float4 color0;          // primary fill / tint
    float4 color1;          // secondary gradient color / MSDF outline color
    float4 borderColor;     // border RGBA
    float4 shadowColor;     // drop shadow RGBA
    float4 cornerRadii;     // (topLeft, topRight, bottomRight, bottomLeft)
    float4 uvOrP0P1;        // Textured/MSDF: (uMin, vMin, uMax, vMax); Bezier: (p0.xy, p1.xy)
    float4 extraOrP2P3;     // Bezier: (p2.xy, p3.xy); RoundedRect: (gradientAngle, strokeWidth, 0, 0)
    float2 shadowOffset;    // drop shadow offset in pixels
    float shadowBlur;       // drop shadow blur radius in pixels (0 = disabled)
    float borderThickness;
    uint type;              // UIPrimitiveType
    uint clipIndex;         // index into UIClipRect buffer
    uint textureIndex;      // bindless slot relative to imageHeapIndexOffset
    uint flags;             // bit 0: gradient enabled, bit 1: MSDF outline enabled
};

struct UIPushConstants
{
    uint64_t primitiveAddress;
    uint64_t clipRectAddress;
    uint primitiveSlot;
    uint clipRectSlot;
    uint imageHeapIndexOffset;
    uint samplerSlot;
    float2 screenSize;
    uint primitiveCount;
    uint linearColorOutput;
};

UIPrimitive loadPrimitive(UIPushConstants pc, uint index)
{
#ifdef NOX_VULKAN
    UIPrimitive* prims = (UIPrimitive*)pc.primitiveAddress;
    return prims[index];
#else
    ByteAddressBuffer buf = ByteAddressBuffer.Handle(uint2(pc.primitiveSlot, 0));
    uint base = index * 160u;
    UIPrimitive p;
    p.bounds      = asfloat(buf.Load4(base + 0u));
    p.color0      = asfloat(buf.Load4(base + 16u));
    p.color1      = asfloat(buf.Load4(base + 32u));
    p.borderColor = asfloat(buf.Load4(base + 48u));
    p.shadowColor = asfloat(buf.Load4(base + 64u));
    p.cornerRadii = asfloat(buf.Load4(base + 80u));
    p.uvOrP0P1    = asfloat(buf.Load4(base + 96u));
    p.extraOrP2P3 = asfloat(buf.Load4(base + 112u));
    float4 w8     = asfloat(buf.Load4(base + 128u));
    p.shadowOffset    = w8.xy;
    p.shadowBlur      = w8.z;
    p.borderThickness = w8.w;
    uint4 w9      = buf.Load4(base + 144u);
    p.type         = w9.x;
    p.clipIndex    = w9.y;
    p.textureIndex = w9.z;
    p.flags        = w9.w;
    return p;
#endif
}

UIClipRect loadClipRect(UIPushConstants pc, uint index)
{
#ifdef NOX_VULKAN
    UIClipRect* clips = (UIClipRect*)pc.clipRectAddress;
    return clips[index];
#else
    ByteAddressBuffer buf = ByteAddressBuffer.Handle(uint2(pc.clipRectSlot, 0));
    uint base = index * 32u;
    UIClipRect c;
    c.rect        = asfloat(buf.Load4(base + 0u));
    c.cornerRadii = asfloat(buf.Load4(base + 16u));
    return c;
#endif
}

struct UIVSOutput
{
    float4 pos : SV_Position;
    float2 pixelPos : TEXCOORD0;
    float2 uv : TEXCOORD1;
    nointerpolation uint primIndex : TEXCOORD2;
};

[shader("vertex")]
UIVSOutput vertMain(uint vertexId : SV_VertexID, uniform UIPushConstants pc)
{
    const uint primIndex = vertexId / 6u;
    const uint cornerIdx = vertexId % 6u;
    const UIPrimitive prim = loadPrimitive(pc, primIndex);

    // Expand quad bounds to cover soft drop shadows or Bezier stroke thickness.
    float2 padMin = float2(0.0, 0.0);
    float2 padMax = float2(0.0, 0.0);
    if (prim.type == uint(UIPrimitiveType.UI_PRIM_ROUNDED_RECT) && prim.shadowBlur > 0.0)
    {
        const float expand = prim.shadowBlur * 1.5;
        padMin = max(float2(0.0, 0.0), float2(expand, expand) - prim.shadowOffset);
        padMax = max(float2(0.0, 0.0), float2(expand, expand) + prim.shadowOffset);
    }
    else if (prim.type == uint(UIPrimitiveType.UI_PRIM_CUBIC_BEZIER))
    {
        const float strokePad = prim.borderThickness * 0.5 + 2.0;
        padMin = float2(strokePad, strokePad);
        padMax = float2(strokePad, strokePad);
    }

    const float2 pMin = prim.bounds.xy - padMin;
    const float2 pMax = prim.bounds.zw + padMax;
    const float2 invScreen = 1.0 / max(pc.screenSize, float2(1.0, 1.0));

    // 0: top-left, 1: top-right, 2: bottom-right, 3: bottom-left (pixel coords: origin top-left, y down)
    const float2 corners[4] = {
        float2(pMin.x, pMin.y),
        float2(pMax.x, pMin.y),
        float2(pMax.x, pMax.y),
        float2(pMin.x, pMax.y)
    };
    const float2 uvs[4] = {
        float2(prim.uvOrP0P1.x, prim.uvOrP0P1.y),
        float2(prim.uvOrP0P1.z, prim.uvOrP0P1.y),
        float2(prim.uvOrP0P1.z, prim.uvOrP0P1.w),
        float2(prim.uvOrP0P1.x, prim.uvOrP0P1.w)
    };

    static const uint kQuadIndices[6] = { 0u, 3u, 2u, 0u, 2u, 1u };
    const uint c = kQuadIndices[cornerIdx];
    const float2 px = corners[c];

    // With ViewportBounds(0, fbHeight, fbWidth, -fbHeight), both Vulkan and D3D12 map
    // top-left (0, 0) -> NDC (-1, +1) and bottom-right (W, H) -> NDC (+1, -1).
    const float2 ndc = float2(px.x * invScreen.x * 2.0 - 1.0,
                              1.0 - px.y * invScreen.y * 2.0);

    UIVSOutput outv;
    outv.pos = float4(ndc, 0.0, 1.0);
    outv.pixelPos = px;
    outv.uv = uvs[c];
    outv.primIndex = primIndex;
    return outv;
}

float median3(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

// Signed distance to a box centered at origin with per-corner radii:
// radii = (topLeft, topRight, bottomRight, bottomLeft) in screen space (y down).
float sdRoundedBox(float2 p, float2 halfSize, float4 radii)
{
    float2 r = (p.x > 0.0) ? float2(radii.y, radii.z) : float2(radii.x, radii.w);
    float radius = (p.y > 0.0) ? r.y : r.x;
    radius = min(radius, min(halfSize.x, halfSize.y));
    float2 q = abs(p) - halfSize + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

float sdSegment(float2 p, float2 a, float2 b)
{
    float2 pa = p - a;
    float2 ba = b - a;
    float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-6));
    return length(pa - ba * h);
}

float sdCubicBezierApprox(float2 p, float2 p0, float2 p1, float2 p2, float2 p3)
{
    const int STEPS = 20;
    float minDist = 1e9;
    float2 prev = p0;
    for (int i = 1; i <= STEPS; ++i)
    {
        float t = float(i) / float(STEPS);
        float u = 1.0 - t;
        float2 pt = (u * u * u) * p0
                  + (3.0 * u * u * t) * p1
                  + (3.0 * u * t * t) * p2
                  + (t * t * t) * p3;
        minDist = min(minDist, sdSegment(p, prev, pt));
        prev = pt;
    }
    return minDist;
}

float screenPxRange(Texture2D atlas, float2 uv)
{
    uint w, h;
    atlas.GetDimensions(w, h);
    const float pxRange = 2.0;
    float2 unitRange = float2(pxRange) / float2(max(w, 1u), max(h, 1u));
    float2 screenTexSize = float2(1.0) / max(fwidth(uv), float2(1e-6));
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

float3 srgbToLinear(float3 c)
{
    c = max(c, 0.0);
    float3 low = c / 12.92;
    float3 high = pow((c + 0.055) / 1.055, 2.4);
    return lerp(high, low, step(c, 0.04045));
}

float4 blendOver(float4 fg, float4 bg)
{
    float outA = fg.a + bg.a * (1.0 - fg.a);
    float3 outRGB = (fg.rgb * fg.a + bg.rgb * bg.a * (1.0 - fg.a)) / max(outA, 1e-6);
    return float4(outRGB, outA);
}

[shader("fragment")]
float4 fragMain(UIVSOutput input, uniform UIPushConstants pc) : SV_TARGET0
{
    const UIPrimitive prim = loadPrimitive(pc, input.primIndex);
    const UIClipRect clip = loadClipRect(pc, prim.clipIndex);

    // Clip rect evaluation (supports optional rounded clip corners)
    const float2 clipCenter = 0.5 * (clip.rect.xy + clip.rect.zw);
    const float2 clipHalf = 0.5 * (clip.rect.zw - clip.rect.xy);
    const float clipDist = sdRoundedBox(input.pixelPos - clipCenter, clipHalf, clip.cornerRadii);
    const float clipAlpha = 1.0 - smoothstep(-0.5, 0.5, clipDist);
    if (clipAlpha <= 0.001)
        discard;

    float4 outColor = float4(0.0);

    if (prim.type == uint(UIPrimitiveType.UI_PRIM_ROUNDED_RECT))
    {
        const float2 center = 0.5 * (prim.bounds.xy + prim.bounds.zw);
        const float2 halfSize = 0.5 * (prim.bounds.zw - prim.bounds.xy);
        const float2 localP = input.pixelPos - center;
        const float d = sdRoundedBox(localP, halfSize, prim.cornerRadii);
        const float aa = max(fwidth(d), 0.75);

        // Soft drop shadow behind the box
        if (prim.shadowBlur > 0.0 && prim.shadowColor.a > 0.0)
        {
            const float sdShadow = sdRoundedBox(localP - prim.shadowOffset, halfSize, prim.cornerRadii);
            const float shadowA = 1.0 - smoothstep(-prim.shadowBlur * 0.35, prim.shadowBlur, sdShadow);
            outColor = float4(prim.shadowColor.rgb, prim.shadowColor.a * shadowA);
        }

        // Fill (solid or linear gradient)
        float4 fillColor = prim.color0;
        if ((prim.flags & 1u) != 0u)
        {
            float2 uvBox = (input.pixelPos - prim.bounds.xy) / max(prim.bounds.zw - prim.bounds.xy, float2(1.0));
            float t = saturate(uvBox.y);
            fillColor = lerp(prim.color0, prim.color1, t);
        }

        const float fillAlpha = 1.0 - smoothstep(-aa * 0.5, aa * 0.5, d);
        if (fillAlpha > 0.0)
            outColor = blendOver(float4(fillColor.rgb, fillColor.a * fillAlpha), outColor);

        // Crisp inner/centered border
        if (prim.borderThickness > 0.0 && prim.borderColor.a > 0.0)
        {
            const float halfBorder = prim.borderThickness * 0.5;
            const float borderDist = abs(d + halfBorder) - halfBorder;
            const float borderAlpha = 1.0 - smoothstep(-aa * 0.5, aa * 0.5, borderDist);
            if (borderAlpha > 0.0)
                outColor = blendOver(float4(prim.borderColor.rgb, prim.borderColor.a * borderAlpha), outColor);
        }
    }
    else if (prim.type == uint(UIPrimitiveType.UI_PRIM_TEXTURED_QUAD))
    {
        Texture2D tex = Texture2D.Handle(uint2(pc.imageHeapIndexOffset + prim.textureIndex, 0));
        SamplerState samp = SamplerState.Handle(uint2(pc.samplerSlot, 0));
        float4 sampled = tex.Sample(samp, input.uv) * prim.color0;

        const float2 center = 0.5 * (prim.bounds.xy + prim.bounds.zw);
        const float2 halfSize = 0.5 * (prim.bounds.zw - prim.bounds.xy);
        const float d = sdRoundedBox(input.pixelPos - center, halfSize, prim.cornerRadii);
        const float aa = max(fwidth(d), 0.75);
        const float mask = 1.0 - smoothstep(-aa * 0.5, aa * 0.5, d);
        outColor = float4(sampled.rgb, sampled.a * mask);
    }
    else if (prim.type == uint(UIPrimitiveType.UI_PRIM_MSDF_GLYPH))
    {
        Texture2D atlas = Texture2D.Handle(uint2(pc.imageHeapIndexOffset + prim.textureIndex, 0));
        SamplerState samp = SamplerState.Handle(uint2(pc.samplerSlot, 0));
        const float3 msd = atlas.Sample(samp, input.uv).rgb;
        const float sd = median3(msd.r, msd.g, msd.b);
        const float pxRange = screenPxRange(atlas, input.uv);
        const float opacity = clamp(pxRange * (sd - 0.5) + 0.5, 0.0, 1.0);
        outColor = float4(prim.color0.rgb, prim.color0.a * opacity);
    }
    else if (prim.type == uint(UIPrimitiveType.UI_PRIM_GLYPH_BITMAP))
    {
        // The glyph rasterized at this size and pen offset, its quad on whole pixels: each pixel reads its own texel's coverage.
        Texture2D atlas = Texture2D.Handle(uint2(pc.imageHeapIndexOffset + prim.textureIndex, 0));
        SamplerState samp = SamplerState.Handle(uint2(pc.samplerSlot, 0));
        const float coverage = atlas.Sample(samp, input.uv).r;
        outColor = float4(prim.color0.rgb, prim.color0.a * coverage);
    }
    else if (prim.type == uint(UIPrimitiveType.UI_PRIM_CUBIC_BEZIER))
    {
        const float d = sdCubicBezierApprox(input.pixelPos,
                                            prim.uvOrP0P1.xy, prim.uvOrP0P1.zw,
                                            prim.extraOrP2P3.xy, prim.extraOrP2P3.zw);
        const float halfW = prim.borderThickness * 0.5;
        const float aa = max(fwidth(d), 0.75);
        const float alpha = 1.0 - smoothstep(halfW - aa * 0.5, halfW + aa * 0.5, d);
        outColor = float4(prim.color0.rgb, prim.color0.a * alpha);
    }

    outColor.a *= clipAlpha;
    if (outColor.a <= 0.001)
        discard;

    if (pc.linearColorOutput != 0u && prim.type != uint(UIPrimitiveType.UI_PRIM_TEXTURED_QUAD))
        outColor.rgb = srgbToLinear(outColor.rgb);

    return outColor;
}
