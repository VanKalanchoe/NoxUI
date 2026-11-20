#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "NoxUI/Core/UIRef.h"
#include "NoxUI/Core/UITypes.h"

namespace NoxUI
{
    struct UIMSDFData;

    // A font's glyphs as a multi-channel signed distance field atlas (msdf-atlas-gen).
    // Generates an RGBA8 CPU atlas bitmap; renderer backends (e.g. noxui_impl_noxrhi)
    // upload it to a GPU texture and bind its UITextureID via SetAtlasTextureID().
    class UIFont : public RefCounted
    {
    public:
        explicit UIFont(const std::filesystem::path& fontPath);
        UIFont(const std::vector<uint8_t>& fontData, const std::string& name);
        ~UIFont() override;

        const UIMSDFData* GetMSDFData() const { return m_Data.get(); }

        uint32_t GetAtlasWidth() const { return m_AtlasWidth; }
        uint32_t GetAtlasHeight() const { return m_AtlasHeight; }
        const std::vector<uint8_t>& GetAtlasPixelsRGBA8() const { return m_AtlasPixels; }

        void SetAtlasTextureID(UITextureID textureId) { m_AtlasTextureId = textureId; }
        UITextureID GetAtlasTextureID() const { return m_AtlasTextureId; }

        bool IsValid() const { return m_Data != nullptr && m_AtlasWidth > 0 && m_AtlasHeight > 0; }
        // The font file itself (UIGlyphCache rasterizes small text from it at the exact size shown) and an id no other font object
        // ever has (a freed font's address can come back for another).
        const std::vector<uint8_t>& GetFontBytes() const { return m_FontBytes; }
        uint64_t GetCacheId() const { return m_CacheId; }

        // Optional global default font path & upload hook used by UIFont::GetDefault().
        static void SetDefaultFontPath(const std::filesystem::path& path);
        static const std::filesystem::path& GetDefaultFontPath();
        static void SetDefault(const Ref<UIFont>& font);
        static Ref<UIFont> GetDefault();
        static void ReleaseDefault();

        using AtlasUploadCallback = std::function<UITextureID(uint32_t width, uint32_t height, const void* rgba8Pixels)>;
        static void SetAtlasUploadCallback(AtlasUploadCallback callback);

    private:
        void Load(const std::vector<uint8_t>& fontData, const std::string& name);

        std::unique_ptr<UIMSDFData> m_Data;
        std::vector<uint8_t> m_AtlasPixels;
        std::vector<uint8_t> m_FontBytes;
        uint64_t m_CacheId = 0;
        uint32_t m_AtlasWidth = 0;
        uint32_t m_AtlasHeight = 0;
        UITextureID m_AtlasTextureId = kInvalidTextureID;

        inline static std::filesystem::path s_DefaultFontPath = "assets/fonts/OpenSans-Regular.ttf";
        inline static Ref<UIFont> s_DefaultFont;
        inline static AtlasUploadCallback s_UploadCallback;
    };
}
