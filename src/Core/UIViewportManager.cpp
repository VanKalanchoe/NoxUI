#include "NoxUI/Core/UIViewportManager.h"

#include <algorithm>

#include "NoxUI/Layout/UIDockSpace.h"
#include "NoxUI/Layout/UILayout.h"
#include "NoxUI/Widgets/UIButton.h"
#include "NoxUI/Widgets/UILabel.h"

namespace NoxUI
{
    namespace
    {
        class UIViewportChrome final : public UIElement
        {
        public:
            UIViewportChrome(
                UIViewportManager& manager,
                UIViewport& viewport,
                const std::string& title,
                const Ref<UIElement>& content)
                : m_Manager(manager), m_Viewport(viewport)
            {
                StyleClass = "Panel";
                DrawBackground = true;
                ClipChildren = true;
                HitTestMode = UIHitTestMode::Self;

                m_TitleLabel = Ref<UILabel>(new UILabel(title));
                m_TitleLabel->HitTestMode = UIHitTestMode::None;
                m_TitleLabel->VAlign = UIAlignment::Center;
                AddChild(m_TitleLabel);

                m_CloseBtn = Ref<UIButton>(new UIButton("X", [&viewport]()
                {
                    viewport.PendingClose = true;
                }));
                m_CloseBtn->StyleClass = "TitleClose";
                m_CloseBtn->WidthMode = UISizeMode::Fixed;
                m_CloseBtn->HeightMode = UISizeMode::Fixed;
                AddChild(m_CloseBtn);

                m_ContentHost = Ref<UIStackBox>(new UIStackBox(UIOrientation::Vertical, 0.0f));
                m_ContentHost->Padding = UIThickness::All(6.0f);
                if (content)
                {
                    content->Visible = true;
                    content->WidthMode = UISizeMode::Fill;
                    content->HeightMode = UISizeMode::Fill;
                    m_ContentHost->AddChild(content);
                }
                AddChild(m_ContentHost);
                viewport.ContentHost = m_ContentHost;
            }

            UIElement* HitTest(const UIVec2& pos) override
            {
                if (!Visible || HitTestMode == UIHitTestMode::None)
                    return nullptr;
                const UIRect& b = GetBounds();
                if (!b.Contains(pos))
                    return nullptr;
                const UIRect resizeRect{
                    { b.Max.x - kResizeGripSize, b.Max.y - kResizeGripSize },
                    b.Max
                };
                if (resizeRect.Contains(pos))
                    return this;
                return UIElement::HitTest(pos);
            }

            UICursorShape GetCursorShape(const UIVec2& pos) const override
            {
                const UIRect& b = GetBounds();
                const UIRect resizeRect{
                    { b.Max.x - kResizeGripSize, b.Max.y - kResizeGripSize },
                    b.Max
                };
                return resizeRect.Contains(pos) ? UICursorShape::ResizeNWSE : UICursorShape::Default;
            }

            bool OnPointerDown(const UIPointerEvent& e) override
            {
                if (e.Button != UIMouseButton::Left)
                    return false;
                const UIRect& b = GetBounds();
                const UIRect resizeRect{
                    { b.Max.x - kResizeGripSize, b.Max.y - kResizeGripSize },
                    b.Max
                };
                if (resizeRect.Contains(e.Position))
                {
                    m_Manager.BeginResizeViewport(&m_Viewport);
                    return true;
                }
                if (e.Position.y <= b.Min.y + m_TitleBarHeight)
                {
                    float scale = 1.0f;
                    const auto& plat = m_Manager.GetPlatformBackend();
                    if (plat.GetWindowContentScale && m_Viewport.WindowHandle)
                        scale = std::max(0.1f, plat.GetWindowContentScale(m_Viewport.WindowHandle));
                    m_Manager.BeginDragViewport(&m_Viewport, { e.Position.x / scale, e.Position.y / scale });
                    return true;
                }
                return false;
            }

        protected:
            UIVec2 ComputeDesiredSize() override
            {
                const UIVec2 titleSz = m_TitleLabel ? m_TitleLabel->Measure() : UIVec2{};
                if (m_CloseBtn)
                    m_CloseBtn->Measure();
                const UIVec2 contentSz = m_ContentHost ? m_ContentHost->Measure() : UIVec2{};
                m_TitleBarHeight = std::max(kTitleBarHeight, titleSz.y + 10.0f);
                return {
                    std::max(titleSz.x + m_TitleBarHeight + 20.0f, contentSz.x),
                    m_TitleBarHeight + contentSz.y
                };
            }

            void ArrangeChildren(const UIRect& /*contentRect*/) override
            {
                const UIRect& b = GetBounds();
                const UIVec2 titleSz = m_TitleLabel ? m_TitleLabel->GetDesiredSize() : UIVec2{};
                m_TitleBarHeight = std::max(kTitleBarHeight, titleSz.y + 10.0f);
                const float closeW = m_TitleBarHeight;
                if (m_TitleLabel)
                {
                    m_TitleLabel->Arrange({
                        { b.Min.x + 10.0f, b.Min.y },
                        { b.Max.x - closeW - 6.0f, b.Min.y + m_TitleBarHeight }
                    });
                }
                if (m_CloseBtn)
                {
                    m_CloseBtn->FixedSize = { closeW, m_TitleBarHeight };
                    m_CloseBtn->Arrange({
                        { b.Max.x - closeW, b.Min.y },
                        { b.Max.x, b.Min.y + m_TitleBarHeight }
                    });
                }
                if (m_ContentHost)
                {
                    m_ContentHost->Arrange({
                        { b.Min.x, b.Min.y + m_TitleBarHeight },
                        { b.Max.x, b.Max.y }
                    });
                }
            }

            void OnPaint(UIDrawList& drawList, const UITheme& theme) override
            {
                UIElement::OnPaint(drawList, theme);
                const UIRect& b = GetBounds();
                const UIColor border = theme.GetToken("Border", { 0.204f, 0.216f, 0.255f, 1.0f });
                const UIColor muted = theme.GetToken("TextMuted", { 0.580f, 0.616f, 0.686f, 1.0f });
                drawList.AddRoundedRect(
                    b.Min,
                    { b.Max.x, b.Min.y + m_TitleBarHeight },
                    theme.GetToken("Surface1", { 0.141f, 0.149f, 0.173f, 1.0f })
                );
                drawList.AddRoundedRect(
                    { b.Min.x, b.Min.y + m_TitleBarHeight - 1.0f },
                    { b.Max.x, b.Min.y + m_TitleBarHeight },
                    border
                );
                drawList.AddBezier(
                    { b.Max.x - 10.0f, b.Max.y - 3.0f },
                    { b.Max.x - 6.5f, b.Max.y - 6.5f },
                    { b.Max.x - 6.5f, b.Max.y - 6.5f },
                    { b.Max.x - 3.0f, b.Max.y - 10.0f },
                    1.5f,
                    { muted.r, muted.g, muted.b, 0.7f }
                );
            }

        private:
            static constexpr float kTitleBarHeight = 30.0f;
            static constexpr float kResizeGripSize = 14.0f;
            float m_TitleBarHeight = kTitleBarHeight;

            UIViewportManager& m_Manager;
            UIViewport& m_Viewport;
            Ref<UILabel> m_TitleLabel;
            Ref<UIButton> m_CloseBtn;
            Ref<UIStackBox> m_ContentHost;
        };
    }

    UIViewportManager::UIViewportManager(UIContext& mainContext, UIDockSpace* dockSpace)
        : m_MainContext(mainContext)
        , m_DockSpace(dockSpace)
    {
        s_Active = this;
    }

    UIViewportManager::~UIViewportManager()
    {
        Shutdown();
        if (s_Active == this)
            s_Active = nullptr;
    }

    void UIViewportManager::Shutdown()
    {
        m_ActiveDragViewport = nullptr;
        m_ActiveResizeViewport = nullptr;
        if (!m_Viewports.empty())
        {
            if (m_Renderer.WaitDeviceIdle)
                m_Renderer.WaitDeviceIdle();
            for (auto& vp : m_Viewports)
            {
                if (vp->RendererHandle != 0 && m_Renderer.DestroyViewport)
                    m_Renderer.DestroyViewport(vp->RendererHandle);
                if (vp->WindowHandle && m_Platform.DestroyWindow)
                    m_Platform.DestroyWindow(vp->WindowHandle);
            }
            m_Viewports.clear();
        }
    }

    UIViewport* UIViewportManager::FindByWindowID(uint32_t windowID) const
    {
        for (const auto& vp : m_Viewports)
        {
            if (vp->WindowID == windowID)
                return vp.get();
        }
        return nullptr;
    }

    UIViewport* UIViewportManager::PromoteToViewport(
        const std::string& title,
        const Ref<UIElement>& content,
        UIVec2 size,
        UIVec2 grabOffset,
        bool startDragging)
    {
        if (!m_Platform.CreateWindow)
            return nullptr;

        float gx = 0.0f;
        float gy = 0.0f;
        bool leftDown = false;
        if (m_Platform.GetGlobalMouseState)
            m_Platform.GetGlobalMouseState(gx, gy, leftDown);

        const uint32_t winW = static_cast<uint32_t>(std::max(220.0f, size.x));
        const uint32_t winH = static_cast<uint32_t>(std::max(140.0f, size.y));
        const int posX = static_cast<int>(gx - grabOffset.x);
        const int posY = static_cast<int>(gy - grabOffset.y);

        const UIViewportOSWindow osWin = m_Platform.CreateWindow(
            title.empty() ? "Floating Panel" : title, posX, posY, winW, winH);
        if (!osWin.WindowHandle)
            return nullptr;

        auto vp = std::make_unique<UIViewport>();
        vp->Title = title;
        vp->Content = content;
        vp->WindowHandle = osWin.WindowHandle;
        vp->NativeRawHandle = osWin.NativeRawHandle;
        vp->WindowID = osWin.WindowID;

        int pw = static_cast<int>(winW);
        int ph = static_cast<int>(winH);
        if (m_Platform.GetWindowPixelSize)
            m_Platform.GetWindowPixelSize(vp->WindowHandle, pw, ph);
        vp->PixelWidth = static_cast<uint32_t>(std::max(pw, 1));
        vp->PixelHeight = static_cast<uint32_t>(std::max(ph, 1));

        if (m_Renderer.CreateViewport && vp->NativeRawHandle)
            vp->RendererHandle = m_Renderer.CreateViewport(vp->NativeRawHandle, vp->PixelWidth, vp->PixelHeight);

        if (m_MainContext.GetTheme())
            vp->Context.SetTheme(m_MainContext.GetTheme());
        vp->Context.SetPlatformCallbacks(m_MainContext.GetPlatformCallbacks());

        Ref<UIViewportChrome> chrome(new UIViewportChrome(*this, *vp, title, content));
        vp->Context.SetRoot(chrome);

        UIViewport* raw = vp.get();
        m_Viewports.push_back(std::move(vp));

        if (startDragging)
            BeginDragViewport(raw, grabOffset);

        return raw;
    }

    void UIViewportManager::BeginDragViewport(UIViewport* viewport, UIVec2 grabOffset)
    {
        m_ActiveDragViewport = viewport;
        m_DragGrabOffset = grabOffset;
    }

    void UIViewportManager::BeginResizeViewport(UIViewport* viewport)
    {
        m_ActiveResizeViewport = viewport;
    }

    void UIViewportManager::UpdateActiveDrag()
    {
        if (!m_ActiveDragViewport || !m_ActiveDragViewport->WindowHandle)
            return;

        float gx = 0.0f;
        float gy = 0.0f;
        bool leftDown = true;
        if (m_Platform.GetGlobalMouseState)
            m_Platform.GetGlobalMouseState(gx, gy, leftDown);

        if (m_Platform.SetWindowPosition)
        {
            m_Platform.SetWindowPosition(
                m_ActiveDragViewport->WindowHandle,
                static_cast<int>(gx - m_DragGrabOffset.x),
                static_cast<int>(gy - m_DragGrabOffset.y));
        }

        if (m_DockSpace && m_Platform.MainWindowHandle && m_Platform.GetWindowPosition && m_Platform.GetWindowSize)
        {
            int mainX = 0, mainY = 0, mainW = 0, mainH = 0;
            m_Platform.GetWindowPosition(m_Platform.MainWindowHandle, mainX, mainY);
            m_Platform.GetWindowSize(m_Platform.MainWindowHandle, mainW, mainH);
            const bool overMain = (gx >= static_cast<float>(mainX) && gx <= static_cast<float>(mainX + mainW) &&
                                   gy >= static_cast<float>(mainY) && gy <= static_cast<float>(mainY + mainH));
            if (overMain)
            {
                const float scale = m_Platform.GetWindowContentScale ? m_Platform.GetWindowContentScale(m_Platform.MainWindowHandle) : 1.0f;
                const UIVec2 localPos{
                    (gx - static_cast<float>(mainX)) * scale,
                    (gy - static_cast<float>(mainY)) * scale
                };
                m_DockSpace->UpdateDockPreview(localPos);
            }
            else
            {
                m_DockSpace->ClearDockPreview();
            }
        }
    }

    void UIViewportManager::UpdateAndRender(float dt)
    {
        if (m_ActiveDragViewport && m_ActiveDragViewport->WindowHandle)
        {
            UpdateActiveDrag();
            float gx = 0.0f;
            float gy = 0.0f;
            bool leftDown = true;
            if (m_Platform.GetGlobalMouseState)
                m_Platform.GetGlobalMouseState(gx, gy, leftDown);
            if (!leftDown)
            {
                if (m_DockSpace && m_DockSpace->HasActiveDropZone())
                {
                    Ref<UIElement> content = m_ActiveDragViewport->Content;
                    if (m_ActiveDragViewport->ContentHost && content)
                        m_ActiveDragViewport->ContentHost->RemoveChild(content);
                    m_DockSpace->CommitDockDrop(m_ActiveDragViewport->Title, content);
                    m_ActiveDragViewport->PendingClose = true;
                }
                else if (m_DockSpace)
                {
                    m_DockSpace->ClearDockPreview();
                }
                m_ActiveDragViewport = nullptr;
            }
        }

        if (m_ActiveResizeViewport && m_ActiveResizeViewport->WindowHandle)
        {
            float gx = 0.0f;
            float gy = 0.0f;
            bool leftDown = true;
            if (m_Platform.GetGlobalMouseState)
                m_Platform.GetGlobalMouseState(gx, gy, leftDown);
            int wx = 0, wy = 0;
            if (m_Platform.GetWindowPosition)
                m_Platform.GetWindowPosition(m_ActiveResizeViewport->WindowHandle, wx, wy);
            const int newW = std::max(220, static_cast<int>(gx) - wx);
            const int newH = std::max(140, static_cast<int>(gy) - wy);
            if (m_Platform.SetWindowSize)
                m_Platform.SetWindowSize(m_ActiveResizeViewport->WindowHandle, newW, newH);
            m_ActiveResizeViewport->NeedsSwapchainResize = true;
            m_ActiveResizeViewport->LastResizeRequestTime = std::chrono::steady_clock::now();

            if (!leftDown)
                m_ActiveResizeViewport = nullptr;
        }

        bool anyClosing = false;
        for (const auto& vp : m_Viewports)
        {
            if (vp->PendingClose)
            {
                anyClosing = true;
                break;
            }
        }
        if (anyClosing)
        {
            if (m_Renderer.WaitDeviceIdle)
                m_Renderer.WaitDeviceIdle();
            std::erase_if(m_Viewports, [this](const std::unique_ptr<UIViewport>& vp)
            {
                if (!vp->PendingClose)
                    return false;
                if (m_ActiveDragViewport == vp.get())
                    m_ActiveDragViewport = nullptr;
                if (m_ActiveResizeViewport == vp.get())
                    m_ActiveResizeViewport = nullptr;
                if (vp->RendererHandle != 0 && m_Renderer.DestroyViewport)
                    m_Renderer.DestroyViewport(vp->RendererHandle);
                if (vp->WindowHandle && m_Platform.DestroyWindow)
                    m_Platform.DestroyWindow(vp->WindowHandle);
                return true;
            });
        }

        const auto now = std::chrono::steady_clock::now();
        for (const auto& vp : m_Viewports)
        {
            if (!vp->WindowHandle)
                continue;

            int pw = static_cast<int>(vp->PixelWidth);
            int ph = static_cast<int>(vp->PixelHeight);
            if (m_Platform.GetWindowPixelSize)
                m_Platform.GetWindowPixelSize(vp->WindowHandle, pw, ph);
            if (pw <= 0 || ph <= 0)
                continue;

            const uint32_t curW = static_cast<uint32_t>(pw);
            const uint32_t curH = static_cast<uint32_t>(ph);
            if (curW != vp->PixelWidth || curH != vp->PixelHeight)
            {
                vp->PixelWidth = curW;
                vp->PixelHeight = curH;
                vp->NeedsSwapchainResize = true;
                vp->LastResizeRequestTime = now;
            }

            if (vp->NeedsSwapchainResize)
            {
                const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - vp->LastResizeRequestTime).count();
                if (elapsedMs >= 16)
                {
                    if (m_Renderer.ResizeViewport && vp->RendererHandle != 0)
                        m_Renderer.ResizeViewport(vp->RendererHandle, vp->PixelWidth, vp->PixelHeight);
                    vp->NeedsSwapchainResize = false;
                }
            }

            vp->Context.UpdateAndRender(
                vp->DrawList,
                { static_cast<float>(vp->PixelWidth), static_cast<float>(vp->PixelHeight) },
                dt);

            if (vp->RendererHandle != 0 && m_Renderer.RenderViewport)
            {
                m_Renderer.RenderViewport(vp->RendererHandle, vp->DrawList, vp->PixelWidth, vp->PixelHeight);
                if (m_Renderer.PresentViewport)
                    m_Renderer.PresentViewport(vp->RendererHandle);
            }
        }
    }
}
