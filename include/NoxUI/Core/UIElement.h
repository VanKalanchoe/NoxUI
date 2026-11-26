#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include "NoxUI/Core/UIRef.h"
#include "NoxUI/Core/UIStateHash.h"
#include "NoxUI/Core/UITheme.h"
#include "NoxUI/Core/UITypes.h"

namespace NoxUI
{
    class UIContext;
    class UIDrawList;
    struct UIPaintMemo;

    class UIElement : public RefCounted
    {
    public:
        UIElement();
        ~UIElement() override;

        uint64_t GetId() const { return m_Id; }

        // Tree management
        void AddChild(const Ref<UIElement>& child);
        void RemoveChild(const Ref<UIElement>& child);
        void ClearChildren();

        UIElement* GetParent() const { return m_Parent; }
        const std::vector<Ref<UIElement>>& GetChildren() const { return m_Children; }
        // A visible child is under `pos`. A popup's backdrop asks this before it dismisses: a press on the popup's own rows
        // bubbles up to the backdrop too, and must not close it before the row sees the release.
        bool AnyChildContains(const UIVec2& pos) const
        {
            for (const auto& child : m_Children)
            {
                if (child && child->Visible && child->GetBounds().Contains(pos))
                    return true;
            }
            return false;
        }

        // Layout pass (Pass 1: bottom-up Measure, Pass 2: top-down Arrange)
        UIVec2 Measure();
        // The height this element needs at `width` (text wraps, a grid adds rows): override ComputeHeightForWidth, call HeightForWidth.
        // HeightForWidth remembers its answers during a layout pass (UIContext marks it): containers ask each child several times
        // (measuring a slot, then arranging it, then the child asks its own children), which nested stacks multiplied per level.
        // With the layout cache (UIContext::SetLayoutCache) it keeps them across frames while nothing inside the element changed.
        virtual float ComputeHeightForWidth(float /*width*/) { return m_DesiredSize.y; }
        float HeightForWidth(float width);
        void Arrange(const UIRect& allottedRect);
        // UIContext: the layout pass begins (HeightForWidth remembers answers until it ends) and ends.
        static void BeginLayoutPass() { s_LayoutPass = ++s_LayoutPassCounter; }
        static void EndLayoutPass() { s_LayoutPass = 0; }

        const UIRect& GetBounds() const { return m_Bounds; }
        const UIVec2& GetDesiredSize() const { return m_DesiredSize; }

        // Style & Paint
        void UpdateStylesRecursive(const UITheme& theme, float dt);
        void PaintRecursive(UIDrawList& drawList, const UITheme& theme);

        // Fingerprint (UIStateHash.h): HashState adds everything this element's own layout and paint read -- its fields, its
        // interaction state (hover, drag, caret, scroll), its animation state, values read through callbacks -- and not its children
        // (the context keys each element after the style pass with it, and combines the keys of the ones inside). A class that
        // overrides it also overrides StateHashClass to return its own typeid: an element whose class does not (a subclass with fields
        // of its own) is never skipped, reused or copied, and keeps the frames it is in drawn in full.
        virtual void HashState(UIStateHasher& h) const;
        virtual const std::type_info& StateHashClass() const { return typeid(UIElement); }
        // This element and, when visible, everything inside it, in one hasher (tests and tools; the context keys elements one by one).
        void HashTree(UIStateHasher& h) const;
        // For the layout and paint caches (UIContext::SetLayoutCache / SetPaintCache), which key each element on its own: state
        // this element's layout or paint reads from outside itself and the elements inside it -- an owner it paints from (a dock
        // space's splitters), a model a callback reads. The parent's own state and bounds are always part of the key, and so is
        // the pointer (position, buttons, modifiers) for the element under it, the captured one and the focused one.
        virtual void HashPaintDependencies(UIStateHasher& /*h*/) const {}

        // Hit-testing (reverse paint order)
        virtual UIElement* HitTest(const UIVec2& pos);
        virtual UICursorShape GetCursorShape(const UIVec2& /*pos*/) const { return UICursorShape::Default; }

        // State helpers (managed by UIContext)
        UIState GetState() const { return m_State; }
        void SetStateFlag(UIState flag, bool enabled);
        bool IsEnabled() const { return !HasState(m_State, UIState::Disabled); }
        void SetEnabled(bool enabled) { SetStateFlag(UIState::Disabled, !enabled); }

        // Public layout / visual properties
        std::string Name;
        std::string StyleClass = "Panel";
        std::string StyleVariant;
        std::string Tooltip;

        bool Visible = true;
        bool ClipChildren = false;
        bool DrawBackground = false;
        bool Focusable = false;
        // Tab goes to this element while it has the focus (a console line completes with it) instead of moving the focus on.
        bool CapturesTab = false;
        UIHitTestMode HitTestMode = UIHitTestMode::PassThrough;

        UISizeMode WidthMode = UISizeMode::Auto;
        UISizeMode HeightMode = UISizeMode::Auto;
        UIVec2 FixedSize{ 0.0f, 0.0f };
        UIVec2 MinSize{ 0.0f, 0.0f };
        UIVec2 MaxSize{ 1e6f, 1e6f };
        float FlexWeight = 1.0f;
        // Disabled with children: drawn under a veil (theme token DisabledVeil), for containers whose children draw themselves
        // as enabled (a settings row and its checkbox).
        bool VeilWhenDisabled = false;

        UIAlignment HAlign = UIAlignment::Stretch;
        UIAlignment VAlign = UIAlignment::Stretch;
        UIThickness Margin{};
        UIThickness Padding{}; // overrides or adds inner spacing

        // Godot-style anchors (used by UIAnchorBox)
        UIVec2 AnchorMin{ 0.0f, 0.0f };
        UIVec2 AnchorMax{ 0.0f, 0.0f };
        UIThickness AnchorOffsets{};

        // Events (return true if handled to stop bubbling)
        virtual bool OnPointerEnter(const UIPointerEvent&) { return false; }
        virtual bool OnPointerLeave(const UIPointerEvent&) { return false; }
        virtual bool OnPointerDown(const UIPointerEvent& e) { return OnMouseDown ? OnMouseDown(e) : false; }
        virtual bool OnPointerUp(const UIPointerEvent& e) { return OnMouseUp ? OnMouseUp(e) : false; }
        virtual bool OnPointerMove(const UIPointerEvent&) { return false; }
        virtual bool OnMouseWheel(const UIPointerEvent&) { return false; }
        virtual bool OnKeyDown(const UIKeyEvent& e) { return OnKey ? OnKey(e) : false; }
        virtual bool OnTextInput(std::string_view) { return false; }
        // The keyboard focus came to the element or left it (a field applies what was typed when it leaves).
        virtual void OnFocusChanged(bool /*focused*/) {}

        std::function<bool(const UIPointerEvent&)> OnMouseDown;
        std::function<bool(const UIPointerEvent&)> OnMouseUp;
        // A key pressed while the element or one inside it has the keyboard (an element that overrides OnKeyDown decides itself):
        // a window's own shortcuts (Ctrl+S, Ctrl+Z) without a subclass.
        std::function<bool(const UIKeyEvent&)> OnKey;
        // Run once a frame by the context while the element is in its tree -- hidden ones too, so a sync can show its element again --
        // before layout: an element following a value (a field showing a setting changed elsewhere, a button enabled while something
        // holds). No list of updates to keep and no stale ones after the element is gone. It must not add or remove elements (use
        // UIContext::Defer for that).
        std::function<void()> OnSync;
        // OnSync of this element and everything inside it (the context calls it on its root and overlay each frame).
        void SyncRecursive();
        // Drops every OnSync in this element and below (what they read is going away before the elements do: an editor closed
        // while its window still shows for a frame).
        void ClearSyncsRecursive();

        // Drag & drop callbacks
        std::function<std::optional<UIDragPayload>()> OnDragStart;
        std::string AcceptedDropType;
        std::function<bool(const UIDragPayload&)> OnDrop;

    protected:
        // Values the element reads from outside the UI every frame (a level meter's Source, a property grid's model): run with the
        // OnSync hooks (after this element's own), before the frame is fingerprinted -- so an idle frame cannot miss a change there.
        virtual void OnPoll() {}
        virtual void OnUpdate(float dt) {}
        virtual UIVec2 ComputeDesiredSize();
        virtual void ArrangeChildren(const UIRect& contentRect);
        virtual void OnPaint(UIDrawList& drawList, const UITheme& theme);
        // Painted after the children and outside their clip rect: what must stay on top of the content (a scroll bar).
        virtual void OnPaintOverlay(UIDrawList& /*drawList*/, const UITheme& /*theme*/) {}
        // The rect children are clipped to when ClipChildren is set (the bounds; a scroll view leaves out its scroll bar's lane).
        virtual UIRect GetChildClipRect() const { return m_Bounds; }

        const UIStyleBox& GetResolvedStyle() const { return m_CurrentStyle; }
        // For HashState of a widget that paints what callbacks give (curve samples, an icon painter, values read live): paints the
        // element (OnPaint, at its last bounds, with the current context's theme) into a scratch list and hashes the primitives --
        // exactly what it would draw now, whatever the callbacks read. Its measure must not depend on them.
        void HashPaintOutput(UIStateHasher& h, UIPaintMemo& memo) const;

    private:
        friend class UIContext;

        // ---- Layout and paint caches (UIContext::SetLayoutCache / SetPaintCache) ----
        // Every element is keyed on its own: its own fingerprint (HashState, HashPaintDependencies) and the keys of what is inside it.
        // Layout: the style pass keys each visible element after its children (UpdateStylesRecursive) -- the state layout reads.
        // Measure and Arrange then reuse what they found last time while the key, the bounds inside, the rect and the parent's own
        // state are what they were. Paint: keyed again after layout and hover (KeyPaintTree), with the bounds; an element whose key and
        // clip are last frame's is copied from last frame's list (PaintCached), the elements inside it with it.
        struct PaintPass
        {
            UIDrawList* Out = nullptr;
            const UIDrawList* Old = nullptr; // (last frame's list; its records carry Generation)
            const UITheme* Theme = nullptr;
            uint64_t Generation = 0;
            bool Replay = true;
            uint32_t Painted = 0, Replayed = 0;
        };
        struct KeyContext
        {
            const UIElement* Hovered = nullptr;
            const UIElement* Captured = nullptr;
            const UIElement* Focused = nullptr;
            uint64_t PointerHash = 0;
        };
        static constexpr uint32_t kParentOuterClip = 0xFFFFFFFFu;
        // The fingerprint of this element's own state with the frame's epoch (theme, fonts, glyph atlas, viewport); false: its class
        // does not fingerprint everything it has.
        bool HashOwnKey(UIStateHasher& h) const;
        // The keys of this element and the visible ones inside (UIContext, after the style pass).
        void KeyTree();
        // Verify mode: every visible element's own state is still what the style pass keyed (an idle frame changed nothing).
        bool OwnKeysHold() const;
        UIStyleBox ResolveTarget(const UITheme& theme);
        // This element's own state is still what the style pass keyed (a parent may change a child's properties while it lays it out).
        bool OwnLayoutKeyHolds();
        // The key Measure and HeightForWidth reuse their answers under (0: nothing to reuse this pass).
        uint64_t MeasureKey();
        // The parent's own state and bounds for the layout keys (0: not keyed this pass, nothing to reuse).
        uint64_t ParentLayoutKey() const;
        // The veil of a disabled container and OnPaintOverlay: what PaintRecursive paints after the children.
        void PaintAfterChildren(UIDrawList& drawList, const UITheme& theme);
        void UpdateLayoutHash();
        // This element and the ones above it measure and arrange again: one inside them was measured or arranged outside their own
        // measure (arrange) -- a popup sized from an event -- so the sizes and bounds they remember may not hold.
        void ForgetLayoutUpward();
        void InvalidateCaches();
        uint64_t KeyPaintTree(const UIElement* parent, const KeyContext& context);
        void PaintCached(PaintPass& pass, uint32_t parentPrim, uint32_t parentClip, uint32_t parentOuter, bool parentOldValid, uint32_t parentOldPrim,
                         uint32_t parentOldClip, uint32_t parentOldOuter);
        // Set by UIContext for its frame (one context lays out and paints at a time).
        inline static uint64_t s_CacheEpoch = 0;
        inline static uint64_t s_KeyPass = 0;
        inline static bool s_LayoutCache = false;
        inline static const std::type_info* s_FirstIncomplete = nullptr;
        inline static std::vector<std::pair<const void*, uint64_t>>* s_Trace = nullptr;
        // Counts UpdateAndRender calls (after the OnSync hooks): HashPaintOutput paints an element once a frame.
        inline static uint64_t s_FrameSerial = 0;
        // Counted for UIContext's statistics: the elements a frame measured and arranged (not reused).
        inline static uint32_t s_MeasuredCount = 0;
        inline static uint32_t s_ArrangedCount = 0;

        // Keys (style pass): own, and with the ones inside.
        uint64_t m_KeyPass = 0;
        uint64_t m_OwnKey = 0;
        uint64_t m_TreeKey = 0;
        bool m_OwnComplete = false;
        bool m_TreeKeyed = false;
        bool m_OwnKeyStale = false;
        // The style the current one settled on was resolved for these (UpdateStylesRecursive resolves only when one changes).
        const UITheme* m_ResolvedTheme = nullptr;
        uint64_t m_ResolvedThemeVersion = 0;
        UIState m_ResolvedState = UIState::Normal;
        std::string m_ResolvedClass, m_ResolvedVariant;
        // The bounds of this element and the visible ones inside it after its last Arrange.
        uint64_t m_LayoutHash = 0;
        // Measure
        uint64_t m_MeasureKeyPass = 0;
        uint64_t m_MeasureKeyThisPass = 0;
        uint64_t m_MeasureKey = 0;
        bool m_MeasureValid = false;
        uint64_t m_MeasureSerial = 0; // (counts the measures that found a size: the sizes inside changed when it did)
        // Arrange
        UIRect m_ArrangeRect{};
        uint64_t m_ArrangeTreeKey = 0;
        uint64_t m_ArrangeMeasureSerial = 0;
        uint64_t m_ArrangeParentKey = 0;
        uint64_t m_ArrangeLayoutHash = 0;
        bool m_ArrangeValid = false;
        bool m_InMeasure = false;
        bool m_InArrange = false;
        // Paint keys (after layout) and what this element and the ones inside it added to last frame's list, relative to its parent's
        // first primitive and clip rect then.
        uint64_t m_PaintStateKey = 0; // (own state, bounds and clip: what the children's keys take of their parent)
        uint64_t m_PaintOwnKey = 0;
        uint64_t m_PaintTreeKey = 0;
        bool m_PaintKeyed = false;
        struct PaintRecord
        {
            uint64_t Key = 0;
            uint64_t Generation = 0;
            glm::vec4 OuterRect{ 0.0f }, OuterRadii{ 0.0f }; // (the clip it was painted under: clips it pushed were cut by it)
            uint32_t PrimOffset = 0, PrimCount = 0, ClipOffset = 0, ClipCount = 0;
            uint32_t OuterRel = 0; // (kParentOuterClip: the clip the parent paints under; else a clip the parent pushed, from its first)
            bool Keyed = false;
            bool Valid = false;
        } m_PaintRecord;

        uint64_t m_Id = 0;
        UIElement* m_Parent = nullptr;
        std::vector<Ref<UIElement>> m_Children;

        UIVec2 m_DesiredSize{ 0.0f, 0.0f };
        UIRect m_Bounds{};

        UIState m_State = UIState::Normal;
        UIStyleBox m_CurrentStyle{};
        bool m_HasResolvedStyle = false;
        // The current style is the theme's style for StyleClass / StyleVariant / the state, exactly (no transition left): the
        // fingerprint then needs only those (theme version, class, variant, state), not the box.
        bool m_StyleSettled = false;

        // HeightForWidth's answers in the current layout pass (two widths: a row asks its label and value at theirs, a stack at its own)
        // -- with the layout cache, kept while MeasureKey stays (and the desired size they were found with).
        uint64_t m_HeightForWidthPass = 0;
        uint64_t m_HeightForWidthCacheKey = 0;
        float m_HeightForWidthKey[2]{};
        float m_HeightForWidthValue[2]{};
        UIVec2 m_HeightForWidthDesired[2]{};
        uint8_t m_HeightForWidthCount = 0;
        uint8_t m_HeightForWidthNext = 0;
        inline static uint64_t s_LayoutPass = 0;
        inline static uint64_t s_LayoutPassCounter = 0;
    };

    // Adds `sync` to the element's OnSync, after what it runs already. The function lives in the element: capture the element by raw
    // pointer, never by Ref (a Ref to itself would keep it alive for ever).
    inline void AppendSync(UIElement& element, std::function<void()> sync)
    {
        if (!element.OnSync)
        {
            element.OnSync = std::move(sync);
            return;
        }
        element.OnSync = [first = std::move(element.OnSync), second = std::move(sync)]() { first(); second(); };
    }

    template <typename T, typename... Args>
    Ref<T> MakeUI(Args&&... args)
    {
        return Ref<T>(new T(std::forward<Args>(args)...));
    }
}
