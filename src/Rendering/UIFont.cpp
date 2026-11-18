#include "NoxUI/Rendering/UIFont.h"

#include <algorithm>
#include <fstream>
#include <string>

#include "NoxUI/Core/UILog.h"
#include "Rendering/UIMSDFData.h"

namespace NoxUI
{
    namespace
    {
        template <typename T, typename S, int N, msdf_atlas::GeneratorFunction<S, N> GenFunc>
        void GenerateMSDFAtlasRGBA8(
            const std::vector<msdf_atlas::GlyphGeometry>& glyphs,
            uint32_t width,
            uint32_t height,
            std::vector<uint8_t>& outPixels)
        {
            msdf_atlas::GeneratorAttributes attributes;
            attributes.config.overlapSupport = true;
            attributes.scanlinePass = true;

            msdf_atlas::ImmediateAtlasGenerator<S, N, GenFunc, msdf_atlas::BitmapAtlasStorage<T, N>> generator(
                static_cast<int>(width), static_cast<int>(height));
            generator.setAttributes(attributes);
            generator.setThreadCount(8);
            generator.generate(glyphs.data(), static_cast<int>(glyphs.size()));

            const msdfgen::BitmapConstRef<T, N> bitmap =
                static_cast<msdfgen::BitmapConstRef<T, N>>(generator.atlasStorage());

            const size_t pixelCount = static_cast<size_t>(bitmap.width) * static_cast<size_t>(bitmap.height);
            outPixels.resize(pixelCount * 4u);
            const uint8_t* src = reinterpret_cast<const uint8_t*>(bitmap.pixels);
            for (size_t i = 0; i < pixelCount; ++i)
            {
                outPixels[i * 4u + 0u] = src[i * 3u + 0u];
                outPixels[i * 4u + 1u] = src[i * 3u + 1u];
                outPixels[i * 4u + 2u] = src[i * 3u + 2u];
                outPixels[i * 4u + 3u] = 255u;
            }
        }
    }

    UIFont::UIFont(const std::filesystem::path& fontPath)
        : m_Data(std::make_unique<UIMSDFData>())
    {
        std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            LogMessage(LogLevel::Error, "UIFont: Failed to open font '" + fontPath.string() + "'");
            return;
        }

        const std::streamsize size = file.tellg();
        if (size <= 0)
        {
            LogMessage(LogLevel::Error, "UIFont: Font file is empty '" + fontPath.string() + "'");
            return;
        }
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> fontData(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(fontData.data()), size))
        {
            LogMessage(LogLevel::Error, "UIFont: Failed to read font '" + fontPath.string() + "'");
            return;
        }

        Load(fontData, fontPath.string());
    }

    UIFont::UIFont(const std::vector<uint8_t>& fontData, const std::string& name)
        : m_Data(std::make_unique<UIMSDFData>())
    {
        Load(fontData, name);
    }

    UIFont::~UIFont() = default;

    void UIFont::Load(const std::vector<uint8_t>& fontData, const std::string& name)
    {
        msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
        if (!ft)
        {
            LogMessage(LogLevel::Error, "UIFont: FreeType initialization failed");
            return;
        }

        msdfgen::FontHandle* font = fontData.empty() ? nullptr
            : msdfgen::loadFontData(ft, reinterpret_cast<const msdfgen::byte*>(fontData.data()), static_cast<int>(fontData.size()));
        if (!font)
        {
            LogMessage(LogLevel::Error, "UIFont: Failed to load font '" + name + "'");
            msdfgen::deinitializeFreetype(ft);
            return;
        }

        struct CharsetRange
        {
            uint32_t Begin, End;
        };

        static constexpr CharsetRange kCharsetRanges[] = {
            { 0x0020u, 0x00FFu } // Basic Latin + Latin Supplement
        };

        msdf_atlas::Charset charset;
        for (const CharsetRange& range : kCharsetRanges)
        {
            for (uint32_t c = range.Begin; c <= range.End; ++c)
                charset.add(c);
        }

        constexpr double fontScale = 1.0;
        m_Data->FontGeometry = msdf_atlas::FontGeometry(&m_Data->Glyphs);
        const int glyphsLoaded = m_Data->FontGeometry.loadCharset(font, fontScale, charset);
        LogMessage(LogLevel::Info, "UIFont: Loaded " + std::to_string(glyphsLoaded) + " glyphs from '" + name + "'");

        double emSize = 40.0;
        msdf_atlas::TightAtlasPacker atlasPacker;
        atlasPacker.setPixelRange(2.0);
        atlasPacker.setMiterLimit(1.0);
        atlasPacker.setScale(emSize);
        const int remaining = atlasPacker.pack(m_Data->Glyphs.data(), static_cast<int>(m_Data->Glyphs.size()));
        if (remaining != 0)
        {
            LogMessage(LogLevel::Error, "UIFont: Failed to pack all glyphs into atlas (" + std::to_string(remaining) + " remaining)");
            msdfgen::destroyFont(font);
            msdfgen::deinitializeFreetype(ft);
            return;
        }

        int width = 0;
        int height = 0;
        atlasPacker.getDimensions(width, height);
        m_AtlasWidth = static_cast<uint32_t>(std::max(1, width));
        m_AtlasHeight = static_cast<uint32_t>(std::max(1, height));

        constexpr float kDefaultAngleThreshold = 3.0f;
        constexpr uint64_t kLcgMultiplier = 6364136223846793005ULL;
        uint64_t glyphSeed = 0;
        for (msdf_atlas::GlyphGeometry& glyph : m_Data->Glyphs)
        {
            glyphSeed *= kLcgMultiplier;
            glyph.edgeColoring(msdfgen::edgeColoringInkTrap, kDefaultAngleThreshold, glyphSeed);
        }

        GenerateMSDFAtlasRGBA8<uint8_t, float, 3, msdf_atlas::msdfGenerator>(
            m_Data->Glyphs, m_AtlasWidth, m_AtlasHeight, m_AtlasPixels);

        if (s_UploadCallback)
            m_AtlasTextureId = s_UploadCallback(m_AtlasWidth, m_AtlasHeight, m_AtlasPixels.data());

        msdfgen::destroyFont(font);
        msdfgen::deinitializeFreetype(ft);
    }

    void UIFont::SetDefaultFontPath(const std::filesystem::path& path)
    {
        s_DefaultFontPath = path;
    }

    const std::filesystem::path& UIFont::GetDefaultFontPath()
    {
        return s_DefaultFontPath;
    }

    void UIFont::SetDefault(const Ref<UIFont>& font)
    {
        s_DefaultFont = font;
    }

    Ref<UIFont> UIFont::GetDefault()
    {
        if (!s_DefaultFont && !s_DefaultFontPath.empty() && std::filesystem::exists(s_DefaultFontPath))
            s_DefaultFont = CreateRef<UIFont>(s_DefaultFontPath);
        return s_DefaultFont;
    }

    void UIFont::ReleaseDefault()
    {
        s_DefaultFont = nullptr;
    }

    void UIFont::SetAtlasUploadCallback(AtlasUploadCallback callback)
    {
        s_UploadCallback = std::move(callback);
        if (s_UploadCallback && s_DefaultFont && s_DefaultFont->IsValid() &&
            s_DefaultFont->GetAtlasTextureID() == kInvalidTextureID)
        {
            s_DefaultFont->SetAtlasTextureID(s_UploadCallback(
                s_DefaultFont->GetAtlasWidth(),
                s_DefaultFont->GetAtlasHeight(),
                s_DefaultFont->GetAtlasPixelsRGBA8().data()));
        }
    }
}
