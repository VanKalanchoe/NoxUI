#pragma once

// NoxRHI Renderer Backend for NoxUI (Vulkan, D3D12, and DeviceNone).
// Modeled directly on NoxRHI's imgui_impl_noxrhi.h.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <NRIHPP/NRI.hpp>
#include <NRIHPP/NRIExtensions.hpp>

#include "NoxUI/Core/UIViewportManager.h"
#include "NoxUI/Rendering/UIDrawList.h"
#include "NoxUI/Rendering/UIFont.h"

static_assert(sizeof(NoxUI::UIPrimitive) == 160, "NoxUI::UIPrimitive must be 160 bytes to match UIPrimitives.slang");
static_assert(sizeof(NoxUI::UIClipRect) == 32, "NoxUI::UIClipRect must be 32 bytes to match UIPrimitives.slang");

struct NoxUI_ImplNoxRHI_InitInfo
{
    nri::Device* device = nullptr;
    nri::DescriptorHeap* resourceHeap = nullptr; // Required shared resource heap
    nri::DescriptorHeap* samplerHeap = nullptr;  // Required shared sampler heap
    uint32_t samplerSlot = 0;
    NriImageFormat colorFormat = NRI_FORMAT_SURFACE;
    uint32_t framesInFlight = 2;
    bool linearColorOutput = true;
    std::string shaderPath; // Optional path to UIPrimitives.slang; falls back to embedded source
    NoxUI::UIViewportManager* viewportManager = nullptr;
};

struct NoxUI_NoxRHIDrawData
{
    const NoxUI::UIPrimitive* primitives = nullptr;
    uint32_t primitiveCount = 0;
    const NoxUI::UIClipRect* clipRects = nullptr;
    uint32_t clipRectCount = 0;
    float displaySize[2]{ 0.0f, 0.0f };
    uint32_t frameIndex = 0;
    uint32_t viewportSlot = 0;
};

namespace NoxUI::Detail
{
    inline constexpr const char* kEmbeddedUIShaderSource = R"SLANG(
enum UIPrimitiveType : uint
{
    UI_PRIM_ROUNDED_RECT  = 0u,
    UI_PRIM_TEXTURED_QUAD = 1u,
    UI_PRIM_MSDF_GLYPH    = 2u,
    UI_PRIM_CUBIC_BEZIER  = 3u
};

struct UIClipRect
{
    float4 rect;
    float4 cornerRadii;
};

struct UIPrimitive
{
    float4 bounds;
    float4 color0;
    float4 color1;
    float4 borderColor;
    float4 shadowColor;
    float4 cornerRadii;
    float4 uvOrP0P1;
    float4 extraOrP2P3;
    float2 shadowOffset;
    float shadowBlur;
    float borderThickness;
    uint type;
    uint clipIndex;
    uint textureIndex;
    uint flags;
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

        if (prim.shadowBlur > 0.0 && prim.shadowColor.a > 0.0)
        {
            const float sdShadow = sdRoundedBox(localP - prim.shadowOffset, halfSize, prim.cornerRadii);
            const float shadowA = 1.0 - smoothstep(-prim.shadowBlur * 0.35, prim.shadowBlur, sdShadow);
            outColor = float4(prim.shadowColor.rgb, prim.shadowColor.a * shadowA);
        }

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
)SLANG";

    struct UIPushConstantsHost
    {
        uint64_t primitiveAddress = 0;
        uint64_t clipRectAddress = 0;
        uint32_t primitiveSlot = 0;
        uint32_t clipRectSlot = 0;
        uint32_t imageHeapIndexOffset = 0;
        uint32_t samplerSlot = 0;
        float screenSize[2]{ 1.0f, 1.0f };
        uint32_t primitiveCount = 0;
        uint32_t linearColorOutput = 1;
    };
    static_assert(sizeof(UIPushConstantsHost) == 48, "UIPushConstantsHost layout mismatch");

    struct FrameGeometry
    {
        nri::Buffer primStaging;
        nri::Buffer primGpu;
        uint64_t primCapacityBytes = 0;
        uint32_t primSlot = 0;
        uint32_t activePrimCount = 0;

        nri::Buffer clipStaging;
        nri::Buffer clipGpu;
        uint64_t clipCapacityBytes = 0;
        uint32_t clipSlot = 0;
        uint32_t activeClipCount = 0;
    };

    struct ManagedTexture
    {
        nri::Texture texture;
        uint32_t descriptorSlot = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct ViewportSwapchainContext
    {
        uint32_t slot = 0;
        nri::NativeWindowHandle window{};
        uint32_t width = 0;
        uint32_t height = 0;
        bool needsResize = false;
        bool skipPresent = true;
        uint32_t frameCounter = 0;
        uint32_t lastImageIndex = 0;
        nri::Swapchain swapchain;
        std::vector<nri::CommandAllocator> allocators;
        std::vector<nri::CommandBuffer> commandBuffers;
    };

    struct NoxRHIBackendData
    {
        nri::Device* device = nullptr;
        nri::DescriptorHeap* resourceHeap = nullptr;
        nri::DescriptorHeap* samplerHeap = nullptr;
        uint32_t samplerSlot = 0;
        NriImageFormat colorFormat = NRI_FORMAT_SURFACE;
        uint32_t framesInFlight = 2;
        bool linearColorOutput = true;

        nri::ShaderCompiler compiler;
        nri::Pipeline pipeline;
        nri::CommandAllocator uploadAllocator;
        nri::CommandBuffer uploadCmd;

        std::mutex mutex;
        std::unordered_map<uint64_t, FrameGeometry> geometry;
        std::unordered_map<UITextureID, ManagedTexture> textures;
        std::vector<std::pair<nri::Buffer, nri::Buffer>> deferredBuffers;

        uint32_t nextViewportSlot = 1;
        uint64_t nextViewportHandle = 1;
        std::unordered_map<uint64_t, std::unique_ptr<ViewportSwapchainContext>> viewports;
    };

    inline NoxRHIBackendData*& GetBackendDataPtr()
    {
        static NoxRHIBackendData* s_Data = nullptr;
        return s_Data;
    }

    inline uint64_t MakeGeometryKey(uint32_t viewportSlot, uint32_t frameSlot)
    {
        return (static_cast<uint64_t>(viewportSlot) << 32) | frameSlot;
    }

    inline void ApplyUIDynamicState(nri::CommandBuffer& cmd)
    {
        cmd.setVertexInput();
        cmd.setPrimitiveTopology(NRI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        cmd.setPrimitiveRestartEnable(false);
        cmd.setRasterizerDiscardEnable(false);
        cmd.setPolygonMode(NRI_POLYGON_MODE_FILL);
        cmd.setCullMode(NRI_CULL_MODE_NONE);
        cmd.setFrontFace(NRI_FRONT_FACE_COUNTER_CLOCKWISE);
        cmd.setDepthBiasEnable(false);
        cmd.setDepthClampEnable(false);
        cmd.setLineWidth(1.0f);

        cmd.setRasterizationSamples(1);
        cmd.setSampleMask(1, 0xFFFFFFFFu);
        cmd.setAlphaToCoverageEnable(false);
        cmd.setAlphaToOneEnable(false);

        cmd.setDepthTestEnable(false);
        cmd.setDepthWriteEnable(false);
        cmd.setDepthCompareOp(NRI_COMPARE_ALWAYS);
        cmd.setDepthBoundsTestEnable(false);
        cmd.setStencilTestEnable(false);

        const nri::ColorBlendEquation blend{
            .srcColorBlendFactor = NRI_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = NRI_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = NRI_BLEND_OP_ADD,
            .srcAlphaBlendFactor = NRI_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = NRI_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp = NRI_BLEND_OP_ADD
        };
        cmd.setColorBlendEnable(0, true);
        cmd.setColorBlendEquation(0, blend);
        cmd.setColorWriteMask(0, nri::ColorComponent::All);
        cmd.setLogicOpEnable(false);
    }

    inline std::string EnsureShaderFile(const std::string& requestedPath)
    {
        std::error_code ec;
        if (!requestedPath.empty() && std::filesystem::exists(requestedPath, ec))
            return requestedPath;

        if (std::filesystem::exists("shaders/UIPrimitives.slang", ec))
            return "shaders/UIPrimitives.slang";

        std::filesystem::path folder = std::filesystem::path("shader_cache_noxui");
        std::filesystem::create_directories(folder, ec);
        std::filesystem::path shaderPath = folder / "noxui_primitives_builtin.slang";

        const size_t sourceLen = std::strlen(kEmbeddedUIShaderSource);
        bool needWrite = !std::filesystem::exists(shaderPath, ec);
        if (!needWrite)
        {
            std::ifstream in(shaderPath, std::ios::binary | std::ios::ate);
            if (!in || static_cast<size_t>(in.tellg()) != sourceLen)
            {
                needWrite = true;
            }
            else
            {
                std::string existing(sourceLen, '\0');
                in.seekg(0);
                in.read(existing.data(), static_cast<std::streamsize>(sourceLen));
                if (existing != kEmbeddedUIShaderSource)
                    needWrite = true;
            }
        }
        if (needWrite)
        {
            std::ofstream out(shaderPath, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                folder = std::filesystem::temp_directory_path() / "noxui_shader_cache";
                std::filesystem::create_directories(folder, ec);
                shaderPath = folder / "noxui_primitives_builtin.slang";
                out.open(shaderPath, std::ios::binary | std::ios::trunc);
                if (!out)
                    throw std::runtime_error("noxui_impl_noxrhi failed to write built-in shader: " + shaderPath.string());
            }
            out.write(kEmbeddedUIShaderSource, static_cast<std::streamsize>(sourceLen));
        }
        return shaderPath.string();
    }

    inline void EnsureGeometryCapacity(NoxRHIBackendData& bd, FrameGeometry& geom, uint64_t neededPrimBytes, uint64_t neededClipBytes)
    {
        constexpr uint64_t kMinPrimBytes = 128ull * 1024ull;
        constexpr uint64_t kMinClipBytes = 16ull * 1024ull;

        if (neededPrimBytes > geom.primCapacityBytes)
        {
            if (geom.primStaging || geom.primGpu)
                bd.deferredBuffers.emplace_back(std::move(geom.primStaging), std::move(geom.primGpu));

            const uint64_t newCap = std::max({ neededPrimBytes, geom.primCapacityBytes * 2ull, kMinPrimBytes });
            geom.primStaging = bd.device->createBuffer({
                .size = newCap,
                .usage = NRI_BUFFER_USAGE_STAGING
            });
            geom.primGpu = bd.device->createBuffer({
                .size = newCap,
                .usage = NRI_BUFFER_USAGE_STORAGE
            });
            geom.primCapacityBytes = newCap;
            geom.primSlot = bd.resourceHeap->registerBuffer(geom.primGpu, newCap);
        }

        if (neededClipBytes > geom.clipCapacityBytes)
        {
            if (geom.clipStaging || geom.clipGpu)
                bd.deferredBuffers.emplace_back(std::move(geom.clipStaging), std::move(geom.clipGpu));

            const uint64_t newCap = std::max({ neededClipBytes, geom.clipCapacityBytes * 2ull, kMinClipBytes });
            geom.clipStaging = bd.device->createBuffer({
                .size = newCap,
                .usage = NRI_BUFFER_USAGE_STAGING
            });
            geom.clipGpu = bd.device->createBuffer({
                .size = newCap,
                .usage = NRI_BUFFER_USAGE_STORAGE
            });
            geom.clipCapacityBytes = newCap;
            geom.clipSlot = bd.resourceHeap->registerBuffer(geom.clipGpu, newCap);
        }
    }
}

inline NoxUI::UITextureID NoxUI_ImplNoxRHI_CreateTexture(uint32_t width, uint32_t height, const void* rgba8Pixels, bool srgb = false)
{
    auto* bd = NoxUI::Detail::GetBackendDataPtr();
    if (!bd || !bd->device || !bd->resourceHeap || width == 0 || height == 0 || !rgba8Pixels)
        return NoxUI::kInvalidTextureID;

    bd->device->waitIdle();

    nri::Texture tex = bd->device->createTexture({
        .width = width,
        .height = height,
        .arrayLayers = 1,
        .mipLevels = 1,
        .sampleCount = 1,
        .usage = NRI_TEXTURE_USAGE_SHADER_RESOURCE,
        .format = srgb ? NRI_FORMAT_SRGBA8 : NRI_FORMAT_RGBA8,
        .sharedAcrossQueues = true
    });

    const uint64_t byteSize = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ull;
    nri::Buffer staging = bd->device->createBuffer({
        .size = byteSize,
        .usage = NRI_BUFFER_USAGE_STAGING
    });
    void* mapped = staging.map(0, byteSize);
    std::memcpy(mapped, rgba8Pixels, static_cast<size_t>(byteSize));
    staging.unmap();

    bd->uploadAllocator.resetPool();
    bd->uploadCmd.begin(0, true);
    const size_t mipOffset = 0;
    tex.recordUpload(bd->uploadCmd.handle(), staging.handle(), 0, { &mipOffset, 1 });
    bd->uploadCmd.end(0);
    bd->device->submitAndWait(bd->uploadCmd, 0);

    bd->resourceHeap->registerTexture(tex);
    const uint32_t slot = tex.getDescriptorSlot();
    const NoxUI::UITextureID id = static_cast<NoxUI::UITextureID>(slot);

    std::scoped_lock lock(bd->mutex);
    bd->textures[id] = NoxUI::Detail::ManagedTexture{
        .texture = std::move(tex),
        .descriptorSlot = slot,
        .width = width,
        .height = height
    };
    return id;
}

inline void NoxUI_ImplNoxRHI_DestroyTexture(NoxUI::UITextureID textureId)
{
    auto* bd = NoxUI::Detail::GetBackendDataPtr();
    if (!bd || textureId == NoxUI::kInvalidTextureID)
        return;

    bd->device->waitIdle();
    std::scoped_lock lock(bd->mutex);
    bd->textures.erase(textureId);
}

inline NoxUI::UITextureID NoxUI_ImplNoxRHI_CreateFontTexture(NoxUI::UIFont& font)
{
    if (!font.IsValid() || font.GetAtlasPixelsRGBA8().empty())
        return NoxUI::kInvalidTextureID;

    const NoxUI::UITextureID texId = NoxUI_ImplNoxRHI_CreateTexture(
        font.GetAtlasWidth(),
        font.GetAtlasHeight(),
        font.GetAtlasPixelsRGBA8().data(),
        false);
    font.SetAtlasTextureID(texId);
    return texId;
}

inline NoxUI_NoxRHIDrawData NoxUI_ImplNoxRHI_PrepareDrawData(
    const NoxUI::UIDrawList& drawList,
    uint32_t frameSlot,
    uint32_t viewportSlot = 0)
{
    const auto& prims = drawList.GetPrimitives();
    const auto& clips = drawList.GetClipRects();
    const glm::vec2 vpSize = drawList.GetViewportSize();

    return NoxUI_NoxRHIDrawData{
        .primitives = prims.empty() ? nullptr : prims.data(),
        .primitiveCount = static_cast<uint32_t>(prims.size()),
        .clipRects = clips.empty() ? nullptr : clips.data(),
        .clipRectCount = static_cast<uint32_t>(clips.size()),
        .displaySize = { vpSize.x, vpSize.y },
        .frameIndex = frameSlot,
        .viewportSlot = viewportSlot
    };
}

inline void NoxUI_ImplNoxRHI_CopyData(nri::CommandBuffer& cmd, const NoxUI_NoxRHIDrawData& drawData)
{
    auto* bd = NoxUI::Detail::GetBackendDataPtr();
    if (!bd)
        return;

    const uint64_t key = NoxUI::Detail::MakeGeometryKey(drawData.viewportSlot, drawData.frameIndex % bd->framesInFlight);
    auto& geom = bd->geometry[key];

    geom.activePrimCount = drawData.primitiveCount;
    geom.activeClipCount = drawData.clipRectCount;
    if (drawData.primitiveCount == 0 || drawData.clipRectCount == 0 || !drawData.primitives || !drawData.clipRects)
        return;

    const uint64_t primBytes = static_cast<uint64_t>(drawData.primitiveCount) * sizeof(NoxUI::UIPrimitive);
    const uint64_t clipBytes = static_cast<uint64_t>(drawData.clipRectCount) * sizeof(NoxUI::UIClipRect);
    NoxUI::Detail::EnsureGeometryCapacity(*bd, geom, primBytes, clipBytes);

    void* primDst = geom.primStaging.map(0, primBytes);
    std::memcpy(primDst, drawData.primitives, static_cast<size_t>(primBytes));
    geom.primStaging.unmap();

    void* clipDst = geom.clipStaging.map(0, clipBytes);
    std::memcpy(clipDst, drawData.clipRects, static_cast<size_t>(clipBytes));
    geom.clipStaging.unmap();

    cmd.copyBuffer(geom.primStaging, geom.primGpu, nri::BufferCopyRegion{ 0, 0, primBytes });
    cmd.copyBuffer(geom.clipStaging, geom.clipGpu, nri::BufferCopyRegion{ 0, 0, clipBytes });

    const nri::BufferBarrierDesc barriers[] = {
        {
            .buffer = geom.primGpu.handle(),
            .before = { nri::AccessBits::TransferWrite, nri::StageBits::Transfer },
            .after = { nri::AccessBits::ShaderRead, nri::StageBits::Fragment | nri::StageBits::Compute | nri::StageBits::Task | nri::StageBits::Mesh }
        },
        {
            .buffer = geom.clipGpu.handle(),
            .before = { nri::AccessBits::TransferWrite, nri::StageBits::Transfer },
            .after = { nri::AccessBits::ShaderRead, nri::StageBits::Fragment | nri::StageBits::Compute | nri::StageBits::Task | nri::StageBits::Mesh }
        }
    };
    cmd.resourceBarriers({}, barriers);
    cmd.executionBarrier();
}

inline void NoxUI_ImplNoxRHI_RenderDrawData(nri::CommandBuffer& cmd, const NoxUI_NoxRHIDrawData& drawData)
{
    auto* bd = NoxUI::Detail::GetBackendDataPtr();
    if (!bd || drawData.primitiveCount == 0)
        return;

    const float fbWidth = drawData.displaySize[0];
    const float fbHeight = drawData.displaySize[1];
    if (fbWidth <= 0.0f || fbHeight <= 0.0f)
        return;

    const uint64_t key = NoxUI::Detail::MakeGeometryKey(drawData.viewportSlot, drawData.frameIndex % bd->framesInFlight);
    auto it = bd->geometry.find(key);
    if (it == bd->geometry.end())
        return;

    const auto& geom = it->second;
    if (geom.activePrimCount == 0 || geom.activeClipCount == 0 || !geom.primGpu || !geom.clipGpu)
        return;

    cmd.bindDescriptorHeaps(bd->resourceHeap, bd->samplerHeap);
    cmd.bindPipeline(NRI_PIPELINE_BIND_POINT_GRAPHICS, bd->pipeline);
    NoxUI::Detail::ApplyUIDynamicState(cmd);

    const nri::ViewportBounds viewport{ 0.0f, fbHeight, fbWidth, -fbHeight };
    cmd.setViewportWithCount(viewport, 0.0f, 1.0f);
    cmd.setScissorRect(0, 0, static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight));

    NoxUI::Detail::UIPushConstantsHost pc{};
    pc.primitiveAddress = geom.primGpu.getDeviceAddress();
    pc.clipRectAddress = geom.clipGpu.getDeviceAddress();
    pc.primitiveSlot = geom.primSlot;
    pc.clipRectSlot = geom.clipSlot;
    pc.imageHeapIndexOffset = bd->resourceHeap->getImageHeapIndexOffset();
    pc.samplerSlot = bd->samplerSlot;
    pc.screenSize[0] = fbWidth;
    pc.screenSize[1] = fbHeight;
    pc.primitiveCount = geom.activePrimCount;
    pc.linearColorOutput = bd->linearColorOutput ? 1u : 0u;

    cmd.pushData(&pc, sizeof(pc));
    cmd.draw(geom.activePrimCount * 6u, 1u, 0u, 0u);
}

inline bool NoxUI_ImplNoxRHI_Init(const NoxUI_ImplNoxRHI_InitInfo& info)
{
    if (!info.device || !info.device->handle() || !info.resourceHeap || !info.samplerHeap)
        return false;

    auto*& bdPtr = NoxUI::Detail::GetBackendDataPtr();
    if (bdPtr != nullptr)
        return false;

    auto* bd = new NoxUI::Detail::NoxRHIBackendData();
    bd->device = info.device;
    bd->resourceHeap = info.resourceHeap;
    bd->samplerHeap = info.samplerHeap;
    bd->samplerSlot = info.samplerSlot;
    bd->colorFormat = info.colorFormat != NRI_FORMAT_NONE ? info.colorFormat : NRI_FORMAT_SURFACE;
    bd->framesInFlight = info.framesInFlight > 0 ? info.framesInFlight : 2;
    bd->linearColorOutput = info.linearColorOutput;

    bd->uploadAllocator = bd->device->createCommandAllocator(NRI_COMMAND_BUFFER_RESET_WITH_ALLOCATOR, NRI_QUEUE_GRAPHICS);
    bd->uploadCmd = bd->uploadAllocator.allocateCommandBuffer(1);

    const NriGraphicsAPI api = bd->device->getAPI();
    if (api != NRI_GRAPHICS_API_NONE)
    {
        bd->compiler = nri::createSlangCompiler();
        bd->compiler.setTargetAPI(api);
    }

    const std::string shaderPath = NoxUI::Detail::EnsureShaderFile(info.shaderPath);
    bd->pipeline = bd->device->createPipeline({
        .type = NRI_PIPELINE_GRAPHICS,
        .shaders = {
            { NRI_SHADER_STAGE_VERTEX,   "vertMain", shaderPath },
            { NRI_SHADER_STAGE_FRAGMENT, "fragMain", shaderPath }
        },
        .colorFormats = { bd->colorFormat },
        .buildNow = true
    }, bd->compiler.handle());
    bd->device->finishPipelineBuilds();

    bdPtr = bd;

    // Automatically upload MSDF font atlases when UIFont::GetDefault() or a new UIFont is created
    NoxUI::UIFont::SetAtlasUploadCallback([](uint32_t width, uint32_t height, const void* rgba8Pixels) -> NoxUI::UITextureID
    {
        return NoxUI_ImplNoxRHI_CreateTexture(width, height, rgba8Pixels, false);
    });

    if (info.viewportManager)
    {
        NoxUI::UIViewportRendererBackend vpRend{};
        vpRend.CreateViewport = [](void* nativeRawHandle, uint32_t pixelW, uint32_t pixelH) -> uint64_t
        {
            auto* b = NoxUI::Detail::GetBackendDataPtr();
            if (!b || !nativeRawHandle)
                return 0;

            auto vp = std::make_unique<NoxUI::Detail::ViewportSwapchainContext>();
            vp->slot = b->nextViewportSlot++;
            vp->width = std::max(1u, pixelW);
            vp->height = std::max(1u, pixelH);
#ifdef _WIN32
            vp->window.type = NRI_WINDOW_SYSTEM_WIN32;
            vp->window.hwnd = nativeRawHandle;
#endif
            vp->swapchain = nri::createSwapchain(*b->device, {
                .width = vp->width,
                .height = vp->height,
                .vsync = false,
                .window = vp->window
            });

            vp->allocators.resize(b->framesInFlight);
            vp->commandBuffers.resize(b->framesInFlight);
            for (uint32_t i = 0; i < b->framesInFlight; ++i)
            {
                vp->allocators[i] = b->device->createCommandAllocator(NRI_COMMAND_BUFFER_RESET_WITH_ALLOCATOR, NRI_QUEUE_GRAPHICS);
                vp->commandBuffers[i] = vp->allocators[i].allocateCommandBuffer(1);
            }

            const uint64_t handle = b->nextViewportHandle++;
            b->viewports.emplace(handle, std::move(vp));
            return handle;
        };
        vpRend.DestroyViewport = [](uint64_t rendererHandle)
        {
            auto* b = NoxUI::Detail::GetBackendDataPtr();
            if (!b || rendererHandle == 0)
                return;
            auto it = b->viewports.find(rendererHandle);
            if (it == b->viewports.end())
                return;
            b->device->waitIdle();
            for (uint32_t f = 0; f < b->framesInFlight; ++f)
                b->geometry.erase(NoxUI::Detail::MakeGeometryKey(it->second->slot, f));
            b->viewports.erase(it);
        };
        vpRend.ResizeViewport = [](uint64_t rendererHandle, uint32_t pixelW, uint32_t pixelH)
        {
            auto* b = NoxUI::Detail::GetBackendDataPtr();
            if (!b || rendererHandle == 0)
                return;
            auto it = b->viewports.find(rendererHandle);
            if (it == b->viewports.end())
                return;
            it->second->width = pixelW;
            it->second->height = pixelH;
            it->second->needsResize = true;
        };
        vpRend.RenderViewport = [](uint64_t rendererHandle, const NoxUI::UIDrawList& drawList, uint32_t pixelW, uint32_t pixelH)
        {
            auto* b = NoxUI::Detail::GetBackendDataPtr();
            if (!b || rendererHandle == 0)
                return;
            auto it = b->viewports.find(rendererHandle);
            if (it == b->viewports.end())
                return;

            auto& vp = *it->second;
            vp.skipPresent = true;
            if (pixelW == 0 || pixelH == 0)
                return;

            if (vp.needsResize || !vp.swapchain)
            {
                b->device->waitIdle();
                vp.swapchain.reset();
                vp.width = pixelW;
                vp.height = pixelH;
                vp.swapchain = nri::createSwapchain(*b->device, {
                    .width = vp.width,
                    .height = vp.height,
                    .vsync = false,
                    .window = vp.window
                });
                vp.needsResize = false;
            }

            const uint32_t frameSlot = vp.frameCounter % b->framesInFlight;
            uint32_t imageIndex = 0;
            if (vp.swapchain.acquireNextImage(frameSlot, imageIndex) == NRI_FRAME_RESULT_RESIZE_REQUIRED)
            {
                vp.needsResize = true;
                return;
            }
            vp.lastImageIndex = imageIndex;

            const NoxUI_NoxRHIDrawData dd = NoxUI_ImplNoxRHI_PrepareDrawData(drawList, frameSlot, vp.slot);

            vp.allocators[frameSlot].resetPool();
            nri::CommandBuffer& cmd = vp.commandBuffers[frameSlot];
            cmd.begin(0, true);
            NoxUI_ImplNoxRHI_CopyData(cmd, dd);

            cmd.transitionSwapchainLayout(vp.swapchain.handle(), imageIndex, NRI_TEXTURE_LAYOUT_UNDEFINED, NRI_TEXTURE_LAYOUT_COLOR_ATTACHMENT);

            nri::RenderDesc renderDesc{};
            renderDesc.renderArea = vp.swapchain.getExtent();
            renderDesc.isSwapchainPass = true;
            renderDesc.colorAttachments = {
                nri::RenderAttachDesc{
                    .attachmentSwapchain = vp.swapchain.handle(),
                    .resolveImageIndex = imageIndex,
                    .loadOp = NRI_LOAD_OP_CLEAR,
                    .storeOp = NRI_STORE_OP_STORE,
                    .clearColor = { 0.063f, 0.075f, 0.106f, 1.0f }
                }
            };
            cmd.beginRendering(renderDesc);
            NoxUI_ImplNoxRHI_RenderDrawData(cmd, dd);
            cmd.endRendering();

            cmd.transitionSwapchainLayout(vp.swapchain.handle(), imageIndex, NRI_TEXTURE_LAYOUT_COLOR_ATTACHMENT, NRI_TEXTURE_LAYOUT_PRESENT);
            cmd.end(0);

            const NriCommandBuffer rawCmds[] = { cmd.handle() };
            b->device->submitCommandBuffers(rawCmds, vp.swapchain.handle(), frameSlot, imageIndex);
            vp.skipPresent = false;
        };
        vpRend.PresentViewport = [](uint64_t rendererHandle)
        {
            auto* b = NoxUI::Detail::GetBackendDataPtr();
            if (!b || rendererHandle == 0)
                return;
            auto it = b->viewports.find(rendererHandle);
            if (it == b->viewports.end())
                return;
            auto& vp = *it->second;
            if (vp.skipPresent || !vp.swapchain)
                return;
            const uint32_t frameSlot = vp.frameCounter % b->framesInFlight;
            if (vp.swapchain.present(frameSlot, vp.lastImageIndex) == NRI_FRAME_RESULT_RESIZE_REQUIRED)
                vp.needsResize = true;
            ++vp.frameCounter;
        };
        vpRend.WaitDeviceIdle = []()
        {
            if (auto* b = NoxUI::Detail::GetBackendDataPtr(); b && b->device && b->device->handle())
                b->device->waitIdle();
        };
        info.viewportManager->SetRendererBackend(std::move(vpRend));
    }

    return true;
}

inline void NoxUI_ImplNoxRHI_Shutdown()
{
    auto*& bdPtr = NoxUI::Detail::GetBackendDataPtr();
    if (!bdPtr)
        return;

    NoxUI::UIFont::SetAtlasUploadCallback({});
    NoxUI::UIFont::ReleaseDefault();

    if (bdPtr->device && bdPtr->device->handle())
        bdPtr->device->waitIdle();

    bdPtr->viewports.clear();
    bdPtr->geometry.clear();
    bdPtr->textures.clear();
    bdPtr->deferredBuffers.clear();
    bdPtr->pipeline.reset();
    bdPtr->compiler.reset();
    bdPtr->uploadCmd.reset();
    bdPtr->uploadAllocator.reset();

    delete bdPtr;
    bdPtr = nullptr;
}
