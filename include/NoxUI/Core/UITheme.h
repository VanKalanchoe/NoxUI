#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

#include "NoxUI/Core/UIRef.h"
#include "NoxUI/Core/UITypes.h"

namespace NoxUI
{
    struct UIStyleBox
    {
        UIColor FillColor{ 0.094f, 0.110f, 0.149f, 1.0f };
        UIColor FillColorBottom{ 0.094f, 0.110f, 0.149f, 1.0f };
        bool UseGradient = false;

        UIColor BorderColor{ 0.200f, 0.235f, 0.322f, 1.0f };
        float BorderThickness = 0.0f;

        UIColor ShadowColor{ 0.0f, 0.0f, 0.0f, 0.0f };
        UIVec2 ShadowOffset{ 0.0f, 4.0f };
        float ShadowBlur = 0.0f;

        UICornerRadius CornerRadius = UICornerRadius::All(3.0f);
        UIThickness Padding = UIThickness::All(8.0f);

        UIColor TextColor{ 0.92f, 0.94f, 0.98f, 1.0f };
        float FontSize = 18.0f;

        float TransitionSeconds = 0.10f;

        static UIStyleBox Lerp(const UIStyleBox& a, const UIStyleBox& b, float t);
        // Every field the same value (floats compared as values).
        static bool Equal(const UIStyleBox& a, const UIStyleBox& b)
        {
            return a.FillColor == b.FillColor && a.FillColorBottom == b.FillColorBottom && a.UseGradient == b.UseGradient &&
                   a.BorderColor == b.BorderColor && a.BorderThickness == b.BorderThickness && a.ShadowColor == b.ShadowColor &&
                   a.ShadowOffset == b.ShadowOffset && a.ShadowBlur == b.ShadowBlur && a.CornerRadius.TopLeft == b.CornerRadius.TopLeft &&
                   a.CornerRadius.TopRight == b.CornerRadius.TopRight && a.CornerRadius.BottomRight == b.CornerRadius.BottomRight &&
                   a.CornerRadius.BottomLeft == b.CornerRadius.BottomLeft && a.Padding.Left == b.Padding.Left && a.Padding.Top == b.Padding.Top &&
                   a.Padding.Right == b.Padding.Right && a.Padding.Bottom == b.Padding.Bottom && a.TextColor == b.TextColor &&
                   a.FontSize == b.FontSize && a.TransitionSeconds == b.TransitionSeconds;
        }
    };

    class UITheme : public RefCounted
    {
    public:
        UITheme() = default;

        static Ref<UITheme> CreateDefault();

        // Loads a .nstyle YAML file. Falls back safely to default values on parse errors.
        bool LoadFromFile(const std::filesystem::path& path);

        // Associates a .nstyle file path and flags a pending hot-reload request.
        void SetWatchedFile(const std::filesystem::path& path) { m_WatchedPath = path; }
        void RequestHotReload() { m_PendingReload = true; }

        // Polls any pending hot reload on the main thread. Returns true if reloaded.
        bool PollHotReload();
        // Changes whenever a token or a style may have changed (SetToken, SetStyle, LoadFromFile): what was drawn may look different.
        uint64_t GetVersion() const { return m_Version; }

        void SetToken(const std::string& name, const UIColor& color);
        UIColor GetToken(std::string_view name, const UIColor& fallback = { 1.0f, 1.0f, 1.0f, 1.0f }) const;

        void SetStyle(const std::string& selector, const UIStyleBox& style);

        // Resolves style for (className, variant, state), layering:
        // Base -> className -> className.variant -> :hover/:pressed/:focused/:disabled.
        UIStyleBox Resolve(std::string_view className, std::string_view variant, UIState state) const;

    private:
        std::unordered_map<std::string, UIColor> m_Tokens;
        std::unordered_map<std::string, UIStyleBox> m_Styles;
        std::filesystem::path m_WatchedPath;
        bool m_PendingReload = false;
        uint64_t m_Version = 0;
    };
}
