#include "NoxUI/Core/UIElement.h"
#include "NoxUI/Core/UIContext.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "NoxUI/Core/UILog.h"
#include "NoxUI/Rendering/UIDrawList.h"

namespace NoxUI
{
    namespace
    {
        std::atomic<uint64_t> s_NextElementId{ 1 };

        UIVec2 SanitizeSize(const UIVec2& v)
        {
            return {
                std::isfinite(v.x) ? std::max(0.0f, v.x) : 0.0f,
                std::isfinite(v.y) ? std::max(0.0f, v.y) : 0.0f
            };
        }
    }

    UIElement::UIElement()
        : m_Id(s_NextElementId.fetch_add(1, std::memory_order_relaxed))
    {
    }

    UIElement::~UIElement()
    {
        for (const auto& child : m_Children)
        {
            if (child && child->m_Parent == this)
                child->m_Parent = nullptr;
        }
    }

    void UIElement::AddChild(const Ref<UIElement>& child)
    {
        if (!child || child.get() == this)
            return;

        // Guard against cyclic parenting
        for (UIElement* p = m_Parent; p != nullptr; p = p->m_Parent)
        {
            if (p == child.get())
            {
                LogMessage(LogLevel::Error, "UIElement::AddChild: cyclic hierarchy detected");
                return;
            }
        }

        if (child->m_Parent)
            child->m_Parent->RemoveChild(child);

        child->m_Parent = this;
        child->InvalidateCaches(); // (what it remembers was laid out and painted under another parent)
        m_Children.push_back(child);
    }

    void UIElement::RemoveChild(const Ref<UIElement>& child)
    {
        if (!child)
            return;
        auto it = std::find(m_Children.begin(), m_Children.end(), child);
        if (it != m_Children.end())
        {
            (*it)->m_Parent = nullptr;
            (*it)->InvalidateCaches();
            m_Children.erase(it);
        }
    }

    void UIElement::ClearChildren()
    {
        for (auto& c : m_Children)
        {
            c->m_Parent = nullptr;
            c->InvalidateCaches();
        }
        m_Children.clear();
    }

    UIVec2 UIElement::Measure()
    {
        if (!Visible)
        {
            m_DesiredSize = { 0.0f, 0.0f };
            return m_DesiredSize;
        }

        // Layout cache: nothing inside changed since the size was found (MeasureKey), so it is the same.
        const uint64_t key = MeasureKey();
        if (key != 0 && m_MeasureValid && key == m_MeasureKey)
            return m_DesiredSize;

        ++s_MeasuredCount;
        m_InMeasure = true;
        UIVec2 desired = ComputeDesiredSize();
        m_InMeasure = false;
        if (WidthMode == UISizeMode::Fixed)
            desired.x = FixedSize.x;
        if (HeightMode == UISizeMode::Fixed)
            desired.y = FixedSize.y;

        desired = glm::clamp(SanitizeSize(desired), MinSize, MaxSize);
        m_DesiredSize = {
            desired.x + Margin.Horizontal(),
            desired.y + Margin.Vertical()
        };
        m_MeasureKey = key;
        m_MeasureValid = key != 0;
        ++m_MeasureSerial;
        if (m_Parent && !m_Parent->m_InMeasure)
            m_Parent->ForgetLayoutUpward(); // (measured outside its parent's measure: what the ones above found may not hold)
        return m_DesiredSize;
    }

    void UIElement::ForgetLayoutUpward()
    {
        for (UIElement* p = this; p != nullptr; p = p->m_Parent)
        {
            p->m_MeasureValid = false;
            p->m_ArrangeValid = false;
            p->m_HeightForWidthCacheKey = 0;
        }
    }

    bool UIElement::HashOwnKey(UIStateHasher& h) const
    {
        h.Add(s_CacheEpoch);
        HashState(h);
        HashPaintDependencies(h);
        return typeid(*this) == StateHashClass();
    }

    bool UIElement::OwnKeysHold() const
    {
        if (!Visible)
            return true;
        if (m_KeyPass == s_KeyPass)
        {
            UIStateHasher h;
            HashOwnKey(h);
            if (h.Value() != m_OwnKey)
                return false;
        }
        for (const auto& child : m_Children)
        {
            if (!child->OwnKeysHold())
                return false;
        }
        return true;
    }

    bool UIElement::OwnLayoutKeyHolds()
    {
        if (!s_LayoutCache || s_LayoutPass == 0 || m_KeyPass != s_KeyPass || !m_TreeKeyed)
            return false;
        // (checked again where it is used: a parent sets a child's FixedSize or Visible while it lays it out)
        UIStateHasher h;
        HashOwnKey(h);
        return h.Value() == m_OwnKey;
    }

    uint64_t UIElement::ParentLayoutKey() const
    {
        // (the parent's own state and bounds: a layout reading its parent, a grid box its available width)
        if (!m_Parent)
            return 1;
        if (m_Parent->m_KeyPass != s_KeyPass || !m_Parent->m_OwnComplete)
            return 0;
        UIStateHasher h;
        h(m_Parent->m_OwnKey, m_Parent->m_Bounds);
        return h.Value() | 1u;
    }

    uint64_t UIElement::MeasureKey()
    {
        if (m_MeasureKeyPass == s_LayoutPass)
            return m_MeasureKeyThisPass;
        m_MeasureKeyPass = s_LayoutPass;
        m_MeasureKeyThisPass = 0;
        if (s_LayoutPass == 0 || !OwnLayoutKeyHolds())
            return 0;
        const uint64_t parentKey = ParentLayoutKey();
        if (parentKey == 0)
            return 0;
        // Everything inside (its state), the bounds inside as the last arrange left them (a wrap box measures at its width) and
        // the parent.
        UIStateHasher h;
        h(m_TreeKey, m_LayoutHash, parentKey);
        m_MeasureKeyThisPass = h.Value() | 1u;
        return m_MeasureKeyThisPass;
    }

    void UIElement::UpdateLayoutHash()
    {
        uint64_t h = HashCombineFloat(HashCombineFloat(HashCombineFloat(HashCombineFloat(static_cast<uint64_t>(m_Children.size()), m_Bounds.Min.x),
                                                                        m_Bounds.Min.y), m_Bounds.Max.x), m_Bounds.Max.y);
        for (const auto& child : m_Children)
        {
            h = HashCombinePointer(h, child.get());
            h = HashCombine(h, child->Visible ? child->m_LayoutHash : uint64_t{ 0x48 });
        }
        m_LayoutHash = h;
    }

    void UIElement::InvalidateCaches()
    {
        m_KeyPass = 0;
        m_TreeKeyed = false;
        m_MeasureValid = false;
        m_MeasureKeyPass = ~0ull;
        m_ArrangeValid = false;
        m_HeightForWidthCacheKey = 0;
        m_HeightForWidthPass = ~0ull;
        m_PaintRecord.Valid = false;
        m_PaintKeyed = false;
    }

    void UIElement::HashState(UIStateHasher& h) const
    {
        h(Name, StyleClass, StyleVariant, Tooltip, Visible, ClipChildren, DrawBackground, Focusable, CapturesTab, HitTestMode, WidthMode,
          HeightMode, FixedSize, MinSize, MaxSize, FlexWeight, VeilWhenDisabled, HAlign, VAlign, Margin, Padding, AnchorMin, AnchorMax,
          AnchorOffsets, AcceptedDropType);
        h(m_State, m_HasResolvedStyle, m_StyleSettled);
        if (!m_StyleSettled) // (a transition still running: the box itself; settled, it is the theme's for class / variant / state)
            h(m_CurrentStyle);
    }

    void UIElement::HashPaintOutput(UIStateHasher& h, UIPaintMemo& memo) const
    {
        const UIContext* context = UIContext::GetCurrent();
        if (!context || !context->GetTheme())
        {
            h.Complete = false; // (nothing to paint with: no proof)
            return;
        }
        if (s_FrameSerial != 0 && memo.Frame == s_FrameSerial && std::memcmp(&memo.Bounds, &m_Bounds, sizeof(UIRect)) == 0)
        {
            h.Add(memo.Version); // (painted already this frame at these bounds)
            return;
        }
        memo.Frame = s_FrameSerial;
        memo.Bounds = m_Bounds;
        thread_local UIDrawList scratch;
        scratch.SetFingerprintOnly(true); // (no glyph rasterized for it)
        scratch.Clear(context->GetViewportSize());
        const_cast<UIElement*>(this)->OnPaint(scratch, *context->GetTheme()); // (painting changes nothing but caches of what it paints)
        const auto& prims = scratch.GetPrimitives();
        const auto& clips = scratch.GetClipRects();
        const bool same = prims.size() == memo.Primitives.size() && clips.size() == memo.ClipRects.size() &&
                          (prims.empty() || std::memcmp(prims.data(), memo.Primitives.data(), prims.size() * sizeof(UIPrimitive)) == 0) &&
                          (clips.empty() || std::memcmp(clips.data(), memo.ClipRects.data(), clips.size() * sizeof(UIClipRect)) == 0);
        if (!same)
        {
            memo.Primitives = prims;
            memo.ClipRects = clips;
            ++memo.Version;
        }
        h.Add(memo.Version);
    }

    void UIElement::HashTree(UIStateHasher& h) const
    {
        h.Add(static_cast<const void*>(this));
        if (!Visible) // (nothing inside a hidden element is drawn; showing it again changes Visible)
        {
            h.Add(false);
            return;
        }
        if (typeid(*this) != StateHashClass())
        {
            if (h.Complete)
                h.IncompleteType = &typeid(*this);
            h.Complete = false;
        }
        if (h.Trace)
        {
            UIStateHasher own;
            HashState(own);
            h.Trace->emplace_back(this, own.Value());
        }
        HashState(h);
        h.Add(m_Children.size());
        for (const auto& child : m_Children)
            child->HashTree(h);
    }

    float UIElement::HeightForWidth(float width)
    {
        if (s_LayoutPass == 0) // (outside a layout pass: nothing to remember it for)
            return ComputeHeightForWidth(width);
        if (m_HeightForWidthPass != s_LayoutPass)
        {
            m_HeightForWidthPass = s_LayoutPass;
            // (the layout cache keeps the answers while nothing inside changed: the same key as Measure's)
            const uint64_t key = MeasureKey();
            if (key == 0 || key != m_HeightForWidthCacheKey)
            {
                m_HeightForWidthCount = 0;
                m_HeightForWidthNext = 0;
            }
            m_HeightForWidthCacheKey = key;
        }
        for (uint8_t i = 0; i < m_HeightForWidthCount; ++i)
        {
            // (and found with the desired size it has now: the default answer is the desired height)
            if (m_HeightForWidthKey[i] == width && m_HeightForWidthDesired[i] == m_DesiredSize)
                return m_HeightForWidthValue[i];
        }
        const float height = ComputeHeightForWidth(width);
        const uint8_t slot = m_HeightForWidthNext;
        m_HeightForWidthKey[slot] = width;
        m_HeightForWidthValue[slot] = height;
        m_HeightForWidthDesired[slot] = m_DesiredSize;
        m_HeightForWidthNext = static_cast<uint8_t>((slot + 1) % 2);
        m_HeightForWidthCount = static_cast<uint8_t>(std::min<int>(m_HeightForWidthCount + 1, 2));
        return height;
    }

    void UIElement::Arrange(const UIRect& allottedRect)
    {
        if (!Visible)
        {
            m_Bounds = {};
            m_LayoutHash = 0;
            m_ArrangeValid = false;
            return;
        }

        // Layout cache: the same rect, nothing inside changed (its state, its sizes: Measure found nothing new since), the bounds
        // inside are the ones this left, and the parent is the same -- every bound inside is where it is.
        const uint64_t parentKey = s_LayoutCache && s_LayoutPass != 0 ? ParentLayoutKey() : 0;
        if (parentKey != 0 && m_ArrangeValid && std::memcmp(&allottedRect, &m_ArrangeRect, sizeof(UIRect)) == 0 &&
            m_TreeKey == m_ArrangeTreeKey && m_MeasureSerial == m_ArrangeMeasureSerial && m_LayoutHash == m_ArrangeLayoutHash &&
            parentKey == m_ArrangeParentKey && OwnLayoutKeyHolds())
            return;

        ++s_ArrangedCount;
        m_Bounds = allottedRect.Inset(Margin);
        const UIRect contentRect = m_Bounds.Inset(Padding);
        m_InArrange = true;
        ArrangeChildren(contentRect);
        m_InArrange = false;
        if (m_Parent && !m_Parent->m_InArrange)
            m_Parent->ForgetLayoutUpward(); // (arranged outside its parent's arrange: the bounds the ones above remember changed)

        m_ArrangeValid = false;
        if (s_LayoutCache)
        {
            UpdateLayoutHash();
            m_ArrangeValid = parentKey != 0 && OwnLayoutKeyHolds();
            m_ArrangeRect = allottedRect;
            m_ArrangeTreeKey = m_TreeKey;
            m_ArrangeMeasureSerial = m_MeasureSerial;
            m_ArrangeLayoutHash = m_LayoutHash;
            m_ArrangeParentKey = parentKey;
        }
    }

    UIVec2 UIElement::ComputeDesiredSize()
    {
        UIVec2 maxChild{ 0.0f, 0.0f };
        for (const auto& child : m_Children)
            maxChild = glm::max(maxChild, child->Measure());

        return {
            maxChild.x + Padding.Horizontal(),
            maxChild.y + Padding.Vertical()
        };
    }

    void UIElement::ArrangeChildren(const UIRect& contentRect)
    {
        for (const auto& child : m_Children)
        {
            if (!child->Visible)
                continue;

            const UIVec2 desired = child->GetDesiredSize();
            UIRect childRect = contentRect;

            if (child->HAlign != UIAlignment::Stretch && child->WidthMode != UISizeMode::Fill)
            {
                const float w = std::min(desired.x, contentRect.Width());
                if (child->HAlign == UIAlignment::Center)
                    childRect.Min.x = contentRect.Min.x + (contentRect.Width() - w) * 0.5f;
                else if (child->HAlign == UIAlignment::End)
                    childRect.Min.x = contentRect.Max.x - w;
                childRect.Max.x = childRect.Min.x + w;
            }

            if (child->VAlign != UIAlignment::Stretch && child->HeightMode != UISizeMode::Fill)
            {
                const float h = std::min(desired.y, contentRect.Height());
                if (child->VAlign == UIAlignment::Center)
                    childRect.Min.y = contentRect.Min.y + (contentRect.Height() - h) * 0.5f;
                else if (child->VAlign == UIAlignment::End)
                    childRect.Min.y = contentRect.Max.y - h;
                childRect.Max.y = childRect.Min.y + h;
            }

            child->Arrange(childRect);
        }
    }

    void UIElement::SyncRecursive()
    {
        if (OnSync)
            OnSync();
        OnPoll();
        // (a snapshot: a sync that breaks the rule and changes the children does not invalidate the walk -- kept in a buffer per
        // depth that is reused, not allocated for every element every frame)
        thread_local std::vector<std::vector<Ref<UIElement>>> s_Snapshots;
        thread_local size_t s_Depth = 0;
        if (s_Snapshots.size() <= s_Depth)
            s_Snapshots.resize(s_Depth + 1);
        const size_t depth = s_Depth;
        s_Snapshots[depth].assign(m_Children.begin(), m_Children.end());
        ++s_Depth;
        for (size_t i = 0; i < s_Snapshots[depth].size(); ++i) // (indexed each time: a deeper level may grow s_Snapshots)
        {
            const Ref<UIElement> child = s_Snapshots[depth][i];
            child->SyncRecursive();
        }
        --s_Depth;
        s_Snapshots[s_Depth].clear();
    }

    void UIElement::ClearSyncsRecursive()
    {
        OnSync = nullptr;
        for (const auto& child : m_Children)
            child->ClearSyncsRecursive();
    }

    void UIElement::UpdateStylesRecursive(const UITheme& theme, float dt)
    {
        if (!Visible)
            return;

        OnUpdate(dt);

        // Settled on the theme's style for the same class, variant, state and theme version: nothing to resolve or blend.
        const bool sameTarget = m_HasResolvedStyle && m_StyleSettled && m_ResolvedTheme == &theme && m_ResolvedThemeVersion == theme.GetVersion() &&
                                m_ResolvedState == m_State && m_ResolvedClass == StyleClass && m_ResolvedVariant == StyleVariant;
        if (sameTarget)
        {
        }
        else if (const UIStyleBox target = ResolveTarget(theme); !m_HasResolvedStyle || target.TransitionSeconds <= 0.0f)
        {
            m_CurrentStyle = target;
            m_HasResolvedStyle = true;
            m_StyleSettled = true;
        }
        else
        {
            const float alpha = 1.0f - std::exp(-dt / std::max(target.TransitionSeconds, 0.001f));
            m_CurrentStyle = UIStyleBox::Lerp(m_CurrentStyle, target, alpha);
            // Arrived: within a thousandth (a quarter of an 8-bit colour step, a thousandth of a pixel) the style is the target. An
            // exponential approach never lands on it exactly, and with a frame time that varies its last bits moved for ever -- the
            // element changed every frame, and no frame could be idle (the user's tab buttons). The toggle, drawer and scroll bar
            // fades snap the same way.
            if (UIStyleBox::Near(m_CurrentStyle, target, 1e-3f))
                m_CurrentStyle = target;
            m_StyleSettled = UIStyleBox::Equal(m_CurrentStyle, target);
        }

        for (const auto& child : m_Children)
            child->UpdateStylesRecursive(theme, dt);
    }

    void UIElement::KeyTree()
    {
        // After every element's OnUpdate (one may change another through a callback: a field committing its edit), the keys on the
        // way back up: this element's own state as the frame will read it (HashState, with what it reads outside:
        // HashPaintDependencies), and the keys of the ones inside -- the idle check, the layout cache and the paint cache all start
        // from these.
        if (!Visible)
            return;
        for (const auto& child : m_Children)
            child->KeyTree();
        {
            UIStateHasher own;
            m_OwnComplete = HashOwnKey(own);
            m_OwnKey = own.Value();
            m_OwnKeyStale = false;
            if (!m_OwnComplete && s_FirstIncomplete == nullptr)
                s_FirstIncomplete = &typeid(*this);
            if (s_Trace)
                s_Trace->emplace_back(this, m_OwnKey);
            bool keyed = m_OwnComplete;
            uint64_t tree = HashCombine(m_OwnKey, static_cast<uint64_t>(m_Children.size()));
            for (const auto& child : m_Children)
            {
                tree = HashCombinePointer(tree, child.get());
                if (!child->Visible)
                {
                    tree = HashCombine(tree, uint64_t{ 0x48 }); // (hidden)
                    continue;
                }
                tree = HashCombine(tree, child->m_TreeKey);
                keyed = keyed && child->m_TreeKeyed && child->m_KeyPass == s_KeyPass;
            }
            m_TreeKey = tree;
            m_TreeKeyed = keyed;
            m_KeyPass = s_KeyPass;
        }
    }

    UIStyleBox UIElement::ResolveTarget(const UITheme& theme)
    {
        m_ResolvedTheme = &theme;
        m_ResolvedThemeVersion = theme.GetVersion();
        m_ResolvedState = m_State;
        if (m_ResolvedClass != StyleClass)
            m_ResolvedClass = StyleClass;
        if (m_ResolvedVariant != StyleVariant)
            m_ResolvedVariant = StyleVariant;
        return theme.Resolve(StyleClass, StyleVariant, m_State);
    }

    void UIElement::PaintRecursive(UIDrawList& drawList, const UITheme& theme)
    {
        if (!Visible)
            return;

        OnPaint(drawList, theme);

        if (ClipChildren)
        {
            const UIRect clip = GetChildClipRect();
            drawList.PushClipRect(clip.Min, clip.Max, m_CurrentStyle.CornerRadius.ToVec4());
        }

        for (const auto& child : m_Children)
            child->PaintRecursive(drawList, theme);

        if (ClipChildren)
            drawList.PopClipRect();

        PaintAfterChildren(drawList, theme);
    }

    uint64_t UIElement::KeyPaintTree(const UIElement* parent, const KeyContext& context)
    {
        // Own: the state the style pass keyed (fingerprinted again when hover changed it since), its bounds and clip, the parent's
        // state (a child painted from its parent's: the corner handle, the dock's splitters), and the pointer for the elements that
        // read it. Layout changes nothing paint reads but the bounds, or does it from the state inside (a scroll clamped to its
        // content): the same keys and bounds paint the same. An element shown by its parent's layout was not keyed: painted.
        if (m_KeyPass != s_KeyPass)
        {
            m_PaintKeyed = false;
            m_PaintStateKey = m_PaintOwnKey = m_PaintTreeKey = 0;
            for (const auto& child : m_Children)
                if (child->Visible)
                    child->KeyPaintTree(this, context);
            return 0;
        }
        if (m_OwnKeyStale)
        {
            UIStateHasher h;
            m_OwnComplete = HashOwnKey(h);
            m_OwnKey = h.Value();
            m_OwnKeyStale = false;
        }
        bool keyed = m_OwnComplete;
        const auto addRect = [](uint64_t key, const UIRect& r)
        {
            return HashCombineFloat(HashCombineFloat(HashCombineFloat(HashCombineFloat(key, r.Min.x), r.Min.y), r.Max.x), r.Max.y);
        };
        uint64_t own = addRect(HashCombine(m_OwnKey, uint64_t{ ClipChildren ? 0xC1u : 0xC0u }), m_Bounds);
        if (ClipChildren)
            own = addRect(own, GetChildClipRect());
        m_PaintStateKey = own;
        if (parent) // (set before its children are keyed)
            own = addRect(HashCombine(own, parent->m_PaintStateKey), parent->m_Bounds);
        if (this == context.Hovered || this == context.Captured || this == context.Focused)
            own = HashCombine(own, context.PointerHash);
        m_PaintOwnKey = own;

        uint64_t tree = HashCombine(m_PaintOwnKey, static_cast<uint64_t>(m_Children.size()));
        for (const auto& child : m_Children)
        {
            tree = HashCombinePointer(tree, child.get());
            if (!child->Visible)
            {
                tree = HashCombine(tree, uint64_t{ 0x48 }); // (hidden)
                continue;
            }
            tree = HashCombine(tree, child->KeyPaintTree(this, context));
            keyed = keyed && child->m_PaintKeyed;
        }
        m_PaintTreeKey = tree;
        m_PaintKeyed = keyed;
        return m_PaintTreeKey;
    }

    void UIElement::PaintCached(PaintPass& pass, uint32_t parentPrim, uint32_t parentClip, uint32_t parentOuter, bool parentOldValid,
                                uint32_t parentOldPrim, uint32_t parentOldClip, uint32_t parentOldOuter)
    {
        PaintRecord& record = m_PaintRecord;
        if (!Visible)
        {
            record.Valid = false;
            return;
        }

        UIDrawList& out = *pass.Out;
        // Where this element's output began in last frame's list (its record is relative to its parent's then).
        const bool oldValid = parentOldValid && record.Valid && record.Generation == pass.Generation;
        const uint32_t oldPrim = parentOldPrim + record.PrimOffset;
        const uint32_t oldClip = parentOldClip + record.ClipOffset;
        const uint32_t oldOuter = record.OuterRel == kParentOuterClip ? parentOldOuter : parentOldClip + record.OuterRel;

        const uint32_t prim = static_cast<uint32_t>(out.GetPrimitives().size());
        const uint32_t clip = static_cast<uint32_t>(out.GetClipRects().size());
        const uint32_t outer = out.GetCurrentClipIndex();
        const UIClipRect outerClip = out.GetClipRects()[outer];
        const uint32_t outerRel = outer == parentOuter ? kParentOuterClip : outer - parentClip;

        // The same key (nothing inside paints differently) under the same clip (the clips inside were cut by it): last frame's output.
        if (pass.Replay && pass.Old && oldValid && m_PaintKeyed && record.Keyed && record.Key == m_PaintTreeKey && std::memcmp(&record.OuterRect, &outerClip.rect, sizeof(glm::vec4)) == 0 &&
            std::memcmp(&record.OuterRadii, &outerClip.cornerRadii, sizeof(glm::vec4)) == 0 && static_cast<size_t>(oldPrim) + record.PrimCount <= pass.Old->GetPrimitives().size() &&
            static_cast<size_t>(oldClip) + record.ClipCount <= pass.Old->GetClipRects().size() && oldOuter < pass.Old->GetClipRects().size())
        {
            out.AppendFrom(*pass.Old, oldPrim, record.PrimCount, oldClip, record.ClipCount, oldOuter);
            if (pass.Segments && record.PrimCount > 0)
                pass.Segments->push_back({ prim, oldPrim, record.PrimCount });
            record.PrimOffset = prim - parentPrim;
            record.ClipOffset = clip - parentClip;
            record.OuterRel = outerRel;
            ++pass.Replayed;
            return;
        }

        // Painted: what it adds must be clipped by the clip it paints under or by clips it pushed (a record copies only those), and
        // the clip stack must be as it found it.
        ++pass.Painted;
        bool valid = true;
        const size_t depth = out.GetClipStackDepth();
        const auto ownClipsOnly = [&](size_t first)
        {
            const auto& prims = out.GetPrimitives();
            for (size_t i = first; i < prims.size(); ++i)
            {
                if (prims[i].clipIndex != outer && prims[i].clipIndex < clip)
                    valid = false;
            }
        };
        size_t first = out.GetPrimitives().size();
        OnPaint(out, *pass.Theme);
        ownClipsOnly(first);
        if (out.GetClipStackDepth() != depth)
            valid = false;

        if (ClipChildren)
        {
            const UIRect childClip = GetChildClipRect();
            out.PushClipRect(childClip.Min, childClip.Max, m_CurrentStyle.CornerRadius.ToVec4());
        }
        for (const auto& child : m_Children)
        {
            child->PaintCached(pass, prim, clip, outer, oldValid, oldPrim, oldClip, oldOuter);
            if (child->Visible && !child->m_PaintRecord.Valid)
                valid = false;
        }
        if (ClipChildren)
            out.PopClipRect();

        first = out.GetPrimitives().size();
        PaintAfterChildren(out, *pass.Theme);
        ownClipsOnly(first);
        if (out.GetClipStackDepth() != depth)
            valid = false;

        record.Key = m_PaintTreeKey;
        record.Keyed = m_PaintKeyed && pass.Replay; // (painted during a drag, from the pointer and the payload: never copied later)
        record.Generation = pass.Generation;
        record.OuterRect = outerClip.rect;
        record.OuterRadii = outerClip.cornerRadii;
        record.PrimOffset = prim - parentPrim;
        record.PrimCount = static_cast<uint32_t>(out.GetPrimitives().size()) - prim;
        record.ClipOffset = clip - parentClip;
        record.ClipCount = static_cast<uint32_t>(out.GetClipRects().size()) - clip;
        record.OuterRel = outerRel;
        record.Valid = valid;
    }

    void UIElement::PaintAfterChildren(UIDrawList& drawList, const UITheme& theme)
    {
        // A disabled container (a settings row, a group) that asks for it is veiled as a whole: its children do not know they
        // are disabled through it (they take no input either way: HitTest stops at a disabled element).
        if (!IsEnabled() && VeilWhenDisabled && !m_Children.empty())
        {
            const UIColor veil = theme.GetToken("DisabledVeil", { 0.09f, 0.10f, 0.12f, 0.55f });
            drawList.AddRoundedRect({ m_Bounds.Min.x, m_Bounds.Min.y }, { m_Bounds.Max.x, m_Bounds.Max.y }, { veil.r, veil.g, veil.b, veil.a },
                                    m_CurrentStyle.CornerRadius.ToVec4());
        }

        OnPaintOverlay(drawList, theme);
    }

    void UIElement::OnPaint(UIDrawList& drawList, const UITheme&)
    {
        if (!DrawBackground)
            return;

        if (m_CurrentStyle.UseGradient)
        {
            drawList.AddRoundedRectGradient(
                m_Bounds.Min, m_Bounds.Max,
                m_CurrentStyle.FillColor, m_CurrentStyle.FillColorBottom,
                m_CurrentStyle.CornerRadius.ToVec4(),
                m_CurrentStyle.BorderThickness, m_CurrentStyle.BorderColor,
                m_CurrentStyle.ShadowColor, m_CurrentStyle.ShadowOffset, m_CurrentStyle.ShadowBlur
            );
        }
        else
        {
            drawList.AddRoundedRect(
                m_Bounds.Min, m_Bounds.Max,
                m_CurrentStyle.FillColor,
                m_CurrentStyle.CornerRadius.ToVec4(),
                m_CurrentStyle.BorderThickness, m_CurrentStyle.BorderColor,
                m_CurrentStyle.ShadowColor, m_CurrentStyle.ShadowOffset, m_CurrentStyle.ShadowBlur
            );
        }
    }

    UIElement* UIElement::HitTest(const UIVec2& pos)
    {
        if (!Visible || HitTestMode == UIHitTestMode::None)
            return nullptr;

        if (ClipChildren && !m_Bounds.Contains(pos))
            return nullptr;

        // Disabled: nothing inside it takes input (the press lands on it, and a disabled element ignores it).
        if (!IsEnabled() && !m_Children.empty())
            return m_Bounds.Contains(pos) ? this : nullptr;

        // Reverse order: last painted child is visually on top.
        for (auto it = m_Children.rbegin(); it != m_Children.rend(); ++it)
        {
            if (UIElement* hit = (*it)->HitTest(pos))
                return hit;
        }

        if (HitTestMode == UIHitTestMode::Self && m_Bounds.Contains(pos))
            return this;

        return nullptr;
    }

    void UIElement::SetStateFlag(UIState flag, bool enabled)
    {
        const uint8_t bit = static_cast<uint8_t>(flag);
        uint8_t val = static_cast<uint8_t>(m_State);
        if (enabled)
            val |= bit;
        else
            val &= ~bit;
        if (val != static_cast<uint8_t>(m_State) && m_KeyPass == s_KeyPass)
            m_OwnKeyStale = true; // (hover set after the style pass keyed it: the paint keys fingerprint it again)
        m_State = static_cast<UIState>(val);
    }
}
