#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "NoxUI/Core/UIElement.h"
#include "NoxUI/Core/UIPlatform.h"
#include "NoxUI/Core/UIRef.h"
#include "NoxUI/Core/UITheme.h"
#include "NoxUI/Rendering/UIDrawList.h"

// (UIFrameReplay and tools test for it: SetVerifyMode exists)
#define NOXUI_HAS_VERIFY_MODE 1
// (UIContext::SetLayoutCache / SetPaintCache and their counts)
#define NOXUI_HAS_PARTIAL_FRAMES 1
// (UIContext::GetDirtyRects)
#define NOXUI_HAS_DIRTY_RECTS 1

namespace NoxUI
{
    class UIDrawList;

    // Owns a viewport's retained UIElement tree, routes input (hit-testing, capture, focus, bubbling),
    // and drives 2-pass layout + style transitions + painting.
    class UIContext
    {
    public:
        UIContext();
        ~UIContext();

        static UIContext* GetCurrent() { return s_Current; }

        void SetRoot(const Ref<UIElement>& root);
        const Ref<UIElement>& GetRoot() const { return m_Root; }

        void SetOverlay(const Ref<UIElement>& overlay);
        const Ref<UIElement>& GetOverlay() const { return m_Overlay; }
        void ClearOverlay() { SetOverlay(nullptr); }

        void PushToast(std::string message, float durationSeconds = 3.0f, const UIColor& accentColor = { 0.24f, 0.56f, 0.96f, 1.0f });
        // A toast with a severity (its icon and colour), a title and an optional detail line. A click on it dismisses it; the
        // pointer resting on it keeps it. At most five are shown, the newest at the bottom.
        void PushNotification(UINotification notification);
        // Space kept free under the toasts (a status bar at the bottom of the window).
        void SetToastBottomInset(float pixels) { m_ToastBottomInset = pixels; }

        void SetTheme(const Ref<UITheme>& theme);
        const Ref<UITheme>& GetTheme() const { return m_Theme; }

        void SetPlatformCallbacks(UIPlatformCallbacks callbacks) { m_PlatformCallbacks = std::move(callbacks); }
        const UIPlatformCallbacks& GetPlatformCallbacks() const { return m_PlatformCallbacks; }
        void CaptureMouse(bool capture) const
        {
            if (m_PlatformCallbacks.CaptureMouse)
                m_PlatformCallbacks.CaptureMouse(capture);
        }

        // Platform-agnostic input injection (called by noxui_impl_sdl3 or custom platform loop)
        bool OnPointerMove(const UIVec2& pos);
        bool OnPointerDown(const UIVec2& pos, int button);
        bool OnPointerDown(int button) { return OnPointerDown(m_LastMousePos, button); }
        bool OnPointerUp(const UIVec2& pos, int button);
        bool OnPointerUp(int button) { return OnPointerUp(m_LastMousePos, button); }
        bool OnScroll(const UIVec2& scrollDelta);
        bool OnKeyDown(UIKey key, uint16_t mods, bool repeat = false) { return OnKeyDown(static_cast<int>(key), mods, repeat); }
        bool OnKeyDown(int keyCode, uint16_t mods, bool repeat = false);
        void OnKeyUp(UIKey key, uint16_t mods)
        {
            (void)key;
            m_Modifiers = mods;
            s_Modifiers = mods;
            m_ChangedSinceFrame = true;
        }
        void SetModifiers(uint16_t mods)
        {
            if (mods != m_Modifiers)
                m_ChangedSinceFrame = true;
            m_Modifiers = mods;
            s_Modifiers = mods;
        }
        uint16_t GetModifiers() const { return m_Modifiers; }
        bool OnTextInput(std::string_view text);

        void UpdateAndRender(UIDrawList& drawList, const UIVec2& viewportSize, float dt);

        // Idle frames. Each frame runs the OnSync hooks, updates the hover against what is on screen (the last layout, as pointer
        // events do), runs the style pass (OnUpdate: animations, transitions) and then keys every visible element (UIElement::HashState,
        // with what it reads outside itself) -- with the context's own state (focus, pointer, toasts, theme, viewport) that is the UI's
        // fingerprint. When no input came since the last frame, the fingerprint equals the last frame's and `drawList` is still the list
        // it drew (UIDrawList::GetRevision), laying out and painting would draw the same: they are skipped and `drawList` keeps what it
        // holds. An element whose class has no complete fingerprint keeps every frame drawn. A callback that draws from state outside
        // the UI (an icon painter reading a game value) calls RequestRedraw when it changes.
        void SetIncremental(bool enabled) { m_Incremental = enabled; }
        bool IsIncremental() const { return m_Incremental; }
        void RequestRedraw() { m_ChangedSinceFrame = true; }
        // Whether the last UpdateAndRender kept the previous draw list (a backend may skip its upload: the revision did not change).
        bool WasLastFrameSkipped() const { return m_LastFrameSkipped; }
        uint64_t GetSkippedFrameCount() const { return m_SkippedFrames; }
        // Drawn frames do only what changed (with SetIncremental). Layout cache: an element whose state and the state of everything
        // inside it are what they were when it was last measured (arranged, in the same rect) keeps its size (its bounds and those
        // inside) without measuring (arranging) again. Paint cache: an element whose state, bounds and clip, and those of everything
        // inside it, are last frame's is copied from last frame's draw list -- one level meter moving repaints the meter and the
        // containers around it, not the other 2,500 elements. Both are keyed by the fingerprints (UIElement::HashState, plus
        // HashPaintDependencies for what an element reads outside itself). Layout must not change what paint reads other than the
        // bounds (a parent may set a child's FixedSize or Visible while it lays it out; a child's own properties are checked again).
        void SetLayoutCache(bool enabled) { m_LayoutCache = enabled; }
        bool IsLayoutCacheEnabled() const { return m_LayoutCache; }
        void SetPaintCache(bool enabled) { m_PaintCache = enabled; }
        bool IsPaintCacheEnabled() const { return m_PaintCache; }
        // Partial redraw, for a backend that keeps the UI's pixels (UIDemo's UI cache): the parts of the viewport the last drawn frame
        // changed, compared with the frame drawn before it -- whose list had revision GetDirtyBaseRevision(). A backend whose pixels show
        // the list at that revision redraws only these rects (clears each and draws the list scissored to it); otherwise, or when
        // IsDirtyFull() (no paint cache, a new viewport, children reordered, half the viewport or more changed), all of it. An idle frame
        // leaves them as they are (and the list's revision too: nothing to redraw). Rects in whole pixels, disjoint, at most 16.
        const std::vector<UIRect>& GetDirtyRects() const { return m_DirtyRects; }
        bool IsDirtyFull() const { return m_DirtyFull; }
        uint64_t GetDirtyBaseRevision() const { return m_DirtyBaseRevision; }
        // The last drawn frame: elements measured and arranged (not reused), elements painted, and elements copied from the frame
        // before (each with everything inside it).
        uint32_t GetLastMeasuredCount() const { return m_LastMeasured; }
        uint32_t GetLastArrangedCount() const { return m_LastArranged; }
        uint32_t GetLastPaintedCount() const { return m_LastPainted; }
        uint32_t GetLastReplayedCount() const { return m_LastReplayed; }
        // Verify mode (tests, debugging): a frame that would be skipped is drawn in full anyway and compared with what it would have
        // kept -- the draw list byte for byte and the fingerprint; a difference is an error (a state some HashState misses). A drawn
        // frame that copied elements from the last one (SetPaintCache) is painted again in full and compared the same way.
        void SetVerifyMode(bool enabled) { m_VerifyMode = enabled; }
        uint64_t GetVerifyFailureCount() const { return m_VerifyFailures; }
        uint64_t GetVerifiedFrameCount() const { return m_VerifiedFrames; }
        // Why the last frame was drawn in full (Idle when it was not): to find what keeps a UI from being idle.
        enum class DrawReason : uint8_t
        {
            Idle,           // not drawn: the last draw list was kept
            Disabled,       // SetIncremental(false)
            Changed,        // input, a deferred action, a theme reload, RequestRedraw, a new root / overlay / theme / toast
            Incomplete,     // an element whose class has no complete fingerprint (GetIncompleteClassName)
            StateChanged,   // the fingerprint differs from the last frame's (a value, an animation, a timer moved)
            DrawListChanged // `drawList` is not the list drawn last, or something changed it since
        };
        DrawReason GetLastDrawReason() const { return m_LastDrawReason; }
        uint64_t GetDrawReasonCount(DrawReason reason) const { return m_DrawReasonCounts[static_cast<size_t>(reason)]; }
        // The class of the element that made the last fingerprint incomplete (typeid name; empty when complete).
        const char* GetIncompleteClassName() const { return m_IncompleteClassName.c_str(); }
        // Debugging: each frame, the elements whose own fingerprint changed since the last frame are logged (class, StyleClass,
        // Name) -- what keeps the UI from being idle. Slow; not for shipping.
        void SetTraceStateChanges(bool enabled) { m_TraceStateChanges = enabled; }

        UIElement* GetHovered() const { return m_Hovered.get(); }
        UIElement* GetFocused() const { return m_Focused.get(); }
        void SetFocus(UIElement* element);
        void SetPointerCapture(const Ref<UIElement>& element);
        void ReleasePointerCapture();
        // The press just dispatched is the element's own drag (a scroll bar's thumb, a number field's scrub): it must not
        // turn into a drag-and-drop of an ancestor's payload once the pointer moved far enough.
        void CancelPendingDrag() { m_DragPending = false; }
        // The size of the viewport the context was last laid out in (popups clamp to it).
        const UIVec2& GetViewportSize() const { return m_ViewportSize; }
        const UIVec2& GetLastMousePos() const { return m_LastMousePos; }
        bool IsDragging() const { return m_DragActive; }

        // Runs `action` at the start of this context's next frame, after the input of this one: what must not happen inside the
        // callback that asks for it (a list rebuilt from one of its own rows' clicks, a popup shown again from its own button).
        void Defer(std::function<void()> action)
        {
            m_Deferred.push_back(std::move(action));
            m_ChangedSinceFrame = true;
        }

        // The input state of the last event any context saw (every OS window's): the UIMod bits held, and whether a mouse button
        // is held (0: any; else a UIMouseButton). For code outside an event (a finished drag becomes one undo step).
        static uint16_t GetCurrentModifiers() { return s_Modifiers; }
        static bool IsMouseButtonHeld(int button = 0) { return button == 0 ? s_ButtonsHeld != 0 : (s_ButtonsHeld & (1u << button)) != 0; }
        const UIDragPayload& GetDragPayload() const { return m_ActivePayload; }

    private:
        uint64_t HashFrame(const UIVec2& viewportSize, float dt, bool& complete);
        void AdvanceHoverTime(float dt);
        bool TooltipVisibleThisFrame(float dt) const;
        static constexpr float kTooltipDelay = 0.45f; // (seconds the pointer rests before a tooltip shows)
        bool IsInTree(const UIElement* element) const;
        void DropDetachedInput();
        void UpdateHover(const UIVec2& pos);
        void UpdateSystemCursor(const UIVec2& pos) const;
        bool DispatchPointerMove(const UIVec2& pos);
        bool DispatchPointerDown(const UIVec2& pos, int button);
        bool DispatchPointerUp(const UIVec2& pos, int button);
        bool DispatchScroll(const UIVec2& scrollDelta);
        bool DispatchKeyDown(int keyCode, uint16_t mods, bool repeat);
        bool DispatchTextInput(std::string_view text);

        Ref<UIElement> m_Root;
        Ref<UIElement> m_Overlay;
        Ref<UITheme> m_Theme;
        UIPlatformCallbacks m_PlatformCallbacks{};

        inline static UIContext* s_Current = nullptr;
        inline static UIContext* s_LastDrawn = nullptr; // (ThemeColor outside a frame or an event)
        friend UIColor ThemeColor(std::string_view token, const UIColor& fallback);
        inline static uint16_t s_Modifiers = 0;
        inline static uint32_t s_ButtonsHeld = 0;
        std::vector<std::function<void()>> m_Deferred;

        Ref<UIElement> m_Hovered;
        Ref<UIElement> m_Captured;
        Ref<UIElement> m_Focused;

        UIVec2 m_LastMousePos{ 0.0f, 0.0f };
        UIVec2 m_ViewportSize{ 0.0f, 0.0f };
        uint16_t m_Modifiers = 0;
        float m_HoverTime = 0.0f;

        bool m_DragPending = false;
        bool m_DragActive = false;
        UIVec2 m_DragOrigin{ 0.0f, 0.0f };
        UIDragPayload m_ActivePayload{};

        struct ToastItem
        {
            UINotification Note;
            float Remaining = 3.0f;
            float Age = 0.0f;     // seconds shown (fade / slide in)
            bool HasAccent = false;
            UIColor Accent{ 0.24f, 0.56f, 0.96f, 1.0f };
            UIRect Bounds{};      // where it was drawn last frame (clicks)
        };
        void PaintToasts(UIDrawList& drawList, const UIVec2& viewportSize, float dt);
        bool DismissToastAt(const UIVec2& pos);
        std::vector<ToastItem> m_Toasts;
        float m_ToastBottomInset = 0.0f;

        // Idle frames (SetIncremental): input or a change the fingerprint does not cover came since the last frame; the fingerprint
        // the last full frame left and whether that frame changed nothing; the draw list it drew and its revision then.
        bool m_Incremental = true;
        bool m_ChangedSinceFrame = true;
        bool m_LastFrameStationary = false;
        bool m_LastFrameSkipped = false;
        uint64_t m_LastStateHash = 0;
        const UIDrawList* m_LastOutputList = nullptr;
        uint64_t m_LastOutputRevision = 0;
        uint64_t m_SkippedFrames = 0;
        bool m_VerifyMode = false;
        uint64_t m_VerifyFailures = 0;
        uint64_t m_VerifiedFrames = 0;
        // Layout and paint caches: last frame's list (swapped out of `drawList`, so nothing is copied), and the generation the
        // elements' paint records must carry to be read from it (a new one whenever it does not hold the last frame's output).
        bool m_LayoutCache = true;
        bool m_PaintCache = true;
        UIDrawList m_PreviousList;
        uint64_t m_PaintGeneration = 0;
        // Dirty rects (GetDirtyRects): the ranges the paint copied, the primitives the trees drew last frame (the rest is the
        // context's: tooltip, drag, toasts).
        void UpdateDirtyRects(const UIDrawList& drawList, bool haveBase, uint32_t treePrimCount);
        std::vector<UIElement::PaintPass::Segment> m_DirtySegments;
        std::vector<UIRect> m_DirtyRects;
        bool m_DirtyFull = true;
        uint64_t m_DirtyBaseRevision = 0;
        uint32_t m_LastTreePrimCount = 0;
        uint32_t m_LastMeasured = 0;
        uint32_t m_LastArranged = 0;
        uint32_t m_LastPainted = 0;
        uint32_t m_LastReplayed = 0;
        DrawReason m_LastDrawReason = DrawReason::Changed;
        uint64_t m_DrawReasonCounts[6]{};
        const std::type_info* m_IncompleteType = nullptr;
        std::string m_IncompleteClassName;
        bool m_TraceStateChanges = false;
        std::vector<std::pair<const void*, uint64_t>> m_TraceLast, m_TraceNow;
    };

    // A theme token's colour from the theme in use -- the context handling the current event or frame, else the one drawn last (a 3D
    // overlay painted outside the UI pass) -- or `fallback` when there is none or the theme does not name the token. For colours chosen
    // in code: nothing looks different without a theme entry, and a theme can change every one of them.
    UIColor ThemeColor(std::string_view token, const UIColor& fallback);

    // A named colour constant read from the theme each time it is used (ThemeColor): `constexpr UIThemeToken kWarn{ "Warn", {...} };`
    // then pass kWarn where a UIColor goes.
    struct UIThemeToken
    {
        std::string_view Name;
        UIColor Fallback;
        UIColor Get() const { return ThemeColor(Name, Fallback); }
        operator UIColor() const { return Get(); }
    };
}
