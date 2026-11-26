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
            m_Children.erase(it);
        }
    }

    void UIElement::ClearChildren()
    {
        for (auto& c : m_Children)
            c->m_Parent = nullptr;
        m_Children.clear();
    }

    UIVec2 UIElement::Measure()
    {
        if (!Visible)
        {
            m_DesiredSize = { 0.0f, 0.0f };
            return m_DesiredSize;
        }

        UIVec2 desired = ComputeDesiredSize();
        if (WidthMode == UISizeMode::Fixed)
            desired.x = FixedSize.x;
        if (HeightMode == UISizeMode::Fixed)
            desired.y = FixedSize.y;

        desired = glm::clamp(SanitizeSize(desired), MinSize, MaxSize);
        m_DesiredSize = {
            desired.x + Margin.Horizontal(),
            desired.y + Margin.Vertical()
        };
        return m_DesiredSize;
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
            h.Complete = false;
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
            m_HeightForWidthCount = 0;
            m_HeightForWidthNext = 0;
        }
        for (uint8_t i = 0; i < m_HeightForWidthCount; ++i)
        {
            if (m_HeightForWidthKey[i] == width)
                return m_HeightForWidthValue[i];
        }
        const float height = ComputeHeightForWidth(width);
        const uint8_t slot = m_HeightForWidthNext;
        m_HeightForWidthKey[slot] = width;
        m_HeightForWidthValue[slot] = height;
        m_HeightForWidthNext = static_cast<uint8_t>((slot + 1) % 2);
        m_HeightForWidthCount = static_cast<uint8_t>(std::min<int>(m_HeightForWidthCount + 1, 2));
        return height;
    }

    void UIElement::Arrange(const UIRect& allottedRect)
    {
        if (!Visible)
        {
            m_Bounds = {};
            return;
        }

        m_Bounds = allottedRect.Inset(Margin);
        const UIRect contentRect = m_Bounds.Inset(Padding);
        ArrangeChildren(contentRect);
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

        const UIStyleBox target = theme.Resolve(StyleClass, StyleVariant, m_State);
        if (!m_HasResolvedStyle || target.TransitionSeconds <= 0.0f)
        {
            m_CurrentStyle = target;
            m_HasResolvedStyle = true;
        }
        else
        {
            const float alpha = 1.0f - std::exp(-dt / std::max(target.TransitionSeconds, 0.001f));
            m_CurrentStyle = UIStyleBox::Lerp(m_CurrentStyle, target, alpha);
        }
        m_StyleSettled = UIStyleBox::Equal(m_CurrentStyle, target);

        for (const auto& child : m_Children)
            child->UpdateStylesRecursive(theme, dt);
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
        m_State = static_cast<UIState>(val);
    }
}
