#include "NoxUI/Widgets/UIKeybindButton.h"

#include "NoxUI/Core/UIPlatform.h"

namespace NoxUI
{
    UIKeybindButton::UIKeybindButton(
        int initialKeyCode,
        uint16_t initialMods,
        std::function<void(int, uint16_t)> onKeybindChanged)
        : UIButton("")
        , OnKeybindChanged(std::move(onKeybindChanged))
        , m_KeyCode(initialKeyCode)
        , m_Mods(initialMods)
    {
        OnClick = [this]()
        {
            m_Listening = true;
            StyleVariant = "primary";
            RefreshLabel();
        };
        RefreshLabel();
    }

    void UIKeybindButton::SetKeybind(int keyCode, uint16_t mods, bool notify)
    {
        m_KeyCode = keyCode;
        m_Mods = mods;
        m_Listening = false;
        StyleVariant.clear();
        RefreshLabel();
        if (notify && OnKeybindChanged)
            OnKeybindChanged(m_KeyCode, m_Mods);
    }

    bool UIKeybindButton::OnKeyDown(const UIKeyEvent& e)
    {
        if (!m_Listening)
            return UIButton::OnKeyDown(e);

        const UIKey key = static_cast<UIKey>(e.KeyCode);
        if (key == UIKey::Escape)
        {
            m_Listening = false;
            StyleVariant.clear();
            RefreshLabel();
            return true;
        }

        // Ignore pure modifier keys until a primary key is pressed
        if (IsModifierKey(key))
            return true;

        SetKeybind(e.KeyCode, e.Modifiers, true);
        return true;
    }

    void UIKeybindButton::RefreshLabel()
    {
        if (m_Listening)
            SetText("Press a key... (Esc cancels)");
        else
            SetText(FormatKeybind(m_KeyCode, m_Mods));
    }

    std::string UIKeybindButton::FormatKeybind(int keyCode, uint16_t mods)
    {
        if (keyCode == 0)
            return "<Unbound>";

        std::string out;
        if (mods & UIMod::Ctrl) out += "Ctrl+";
        if (mods & UIMod::Shift) out += "Shift+";
        if (mods & UIMod::Alt) out += "Alt+";

        const char* name = GetKeyName(static_cast<UIKey>(keyCode));
        out += (name && name[0] != '\0') ? name : "Key";
        return out;
    }

    void UIKeybindButton::HashState(UIStateHasher& h) const
    {
        UIButton::HashState(h);
        h(m_KeyCode, m_Mods, m_Listening);
    }
}
