#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "NoxUI/Core/UIContext.h"
#include "NoxUI/Rendering/UIDrawList.h"

namespace NoxUI
{
    class UIDockSpace;

    struct UIViewportOSWindow
    {
        void* WindowHandle = nullptr;     // e.g. SDL_Window*
        void* NativeRawHandle = nullptr;  // e.g. HWND on Win32
        uint32_t WindowID = 0;
    };

    struct UIViewportPlatformBackend
    {
        void* MainWindowHandle = nullptr;
        uint32_t MainWindowID = 0;

        std::function<UIViewportOSWindow(const std::string& title, int x, int y, uint32_t w, uint32_t h)> CreateWindow;
        std::function<void(void* windowHandle)> DestroyWindow;
        std::function<void(void* windowHandle, int x, int y)> SetWindowPosition;
        std::function<void(void* windowHandle, int& outX, int& outY)> GetWindowPosition;
        std::function<void(void* windowHandle, int w, int h)> SetWindowSize;
        std::function<void(void* windowHandle, int& outW, int& outH)> GetWindowSize;
        std::function<void(void* windowHandle, int& outPixelW, int& outPixelH)> GetWindowPixelSize;
        std::function<float(void* windowHandle)> GetWindowContentScale;
        std::function<void(float& outGlobalX, float& outGlobalY, bool& outLeftDown)> GetGlobalMouseState;
    };

    struct UIViewportRendererBackend
    {
        std::function<uint64_t(void* nativeRawHandle, uint32_t pixelW, uint32_t pixelH)> CreateViewport;
        std::function<void(uint64_t rendererHandle)> DestroyViewport;
        std::function<void(uint64_t rendererHandle, uint32_t pixelW, uint32_t pixelH)> ResizeViewport;
        std::function<void(uint64_t rendererHandle, const UIDrawList& drawList, uint32_t pixelW, uint32_t pixelH)> RenderViewport;
        std::function<void(uint64_t rendererHandle)> PresentViewport;
        std::function<void()> WaitDeviceIdle;
    };

    struct UIViewport
    {
        uint32_t WindowID = 0;
        void* WindowHandle = nullptr;
        void* NativeRawHandle = nullptr;
        uint64_t RendererHandle = 0;

        UIContext Context;
        UIDrawList DrawList;
        std::string Title;
        Ref<UIElement> Content;
        Ref<UIElement> ContentHost;

        uint32_t PixelWidth = 0;
        uint32_t PixelHeight = 0;
        bool PendingClose = false;
        bool NeedsSwapchainResize = false;
        std::chrono::steady_clock::time_point LastResizeRequestTime{};
    };

    // Manages secondary OS windows + swapchains via pluggable platform/renderer backends
    // for ImGui-style multi-viewport tearing & re-docking across monitors.
    class UIViewportManager
    {
    public:
        explicit UIViewportManager(UIContext& mainContext, UIDockSpace* dockSpace = nullptr);
        ~UIViewportManager();

        void SetDockSpace(UIDockSpace* dockSpace) { m_DockSpace = dockSpace; }
        UIDockSpace* GetDockSpace() const { return m_DockSpace; }
        void SetPlatformBackend(UIViewportPlatformBackend backend) { m_Platform = std::move(backend); }
        void SetRendererBackend(UIViewportRendererBackend backend) { m_Renderer = std::move(backend); }

        const UIViewportPlatformBackend& GetPlatformBackend() const { return m_Platform; }
        const UIViewportRendererBackend& GetRendererBackend() const { return m_Renderer; }

        // Promotes a panel/floating window into its own OS window at the global cursor position.
        UIViewport* PromoteToViewport(
            const std::string& title,
            const Ref<UIElement>& content,
            UIVec2 size,
            UIVec2 grabOffset,
            bool startDragging = true);

        void BeginDragViewport(UIViewport* viewport, UIVec2 grabOffset);
        void BeginResizeViewport(UIViewport* viewport);

        UIViewport* FindByWindowID(uint32_t windowID) const;

        // Syncs active OS-window drag position before frame rendering for responsive cursor tracking.
        void UpdateActiveDrag();

        // Updates active OS-window drags/resizes, updates UIContexts, and renders secondary swapchains.
        void UpdateAndRender(float dt);

        void Shutdown();
        // The user asked to close a torn-off window (its title bar's X, the OS's close button): closed unless the dock space's
        // CanClosePanel keeps it open (an editor with unsaved changes asks first, then calls ClosePanel itself).
        void RequestClose(UIViewport& viewport);
        // Closes the torn-off window showing `title` (no question asked: the caller decided); false when there is none.
        bool ClosePanel(const std::string& title);
        // A torn-off window shows `title` (and is not closing).
        bool HasPanel(const std::string& title) const;

        size_t GetViewportCount() const { return m_Viewports.size(); }

        static UIViewportManager* GetActive() { return s_Active; }

    private:
        UIContext& m_MainContext;
        UIDockSpace* m_DockSpace = nullptr;
        UIViewportPlatformBackend m_Platform{};
        UIViewportRendererBackend m_Renderer{};

        std::vector<std::unique_ptr<UIViewport>> m_Viewports;
        UIViewport* m_ActiveDragViewport = nullptr;
        UIVec2 m_DragGrabOffset{ 0.0f, 0.0f };
        UIViewport* m_ActiveResizeViewport = nullptr;

        inline static UIViewportManager* s_Active = nullptr;
    };
}
