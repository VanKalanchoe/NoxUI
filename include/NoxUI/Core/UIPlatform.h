#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "NoxUI/Core/UITypes.h"

namespace NoxUI
{
    enum class UIKey : int32_t
    {
        None = 0,

        // Letters
        A, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

        // Digits
        D0, D1, D2, D3, D4, D5, D6, D7, D8, D9,

        // Function keys
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

        // Navigation & editing
        Escape,
        Tab,
        Return,
        KeypadEnter,
        Space,
        Backspace,
        Delete,
        Insert,
        Home,
        End,
        PageUp,
        PageDown,
        Left,
        Right,
        Up,
        Down,

        // Modifiers
        LeftCtrl,
        RightCtrl,
        LeftShift,
        RightShift,
        LeftAlt,
        RightAlt,
        LeftSuper,
        RightSuper,

        // Punctuation
        Minus,
        Equals,
        LeftBracket,
        RightBracket,
        Backslash,
        Semicolon,
        Apostrophe,
        Grave,
        Comma,
        Period,
        Slash,
        CapsLock
    };

    namespace UIMod
    {
        inline constexpr uint16_t None  = 0;
        inline constexpr uint16_t Shift = 1 << 0;
        inline constexpr uint16_t Ctrl  = 1 << 1;
        inline constexpr uint16_t Alt   = 1 << 2;
        inline constexpr uint16_t Super = 1 << 3;
    }

    namespace UIMouseButton
    {
        inline constexpr int Left   = 1;
        inline constexpr int Middle = 2;
        inline constexpr int Right  = 3;
    }

    inline bool IsModifierKey(UIKey key)
    {
        return key == UIKey::LeftCtrl || key == UIKey::RightCtrl ||
               key == UIKey::LeftShift || key == UIKey::RightShift ||
               key == UIKey::LeftAlt || key == UIKey::RightAlt ||
               key == UIKey::LeftSuper || key == UIKey::RightSuper;
    }

    inline const char* GetKeyName(UIKey key)
    {
        switch (key)
        {
        case UIKey::None: return "None";
        case UIKey::A: return "A"; case UIKey::B: return "B"; case UIKey::C: return "C";
        case UIKey::D: return "D"; case UIKey::E: return "E"; case UIKey::F: return "F";
        case UIKey::G: return "G"; case UIKey::H: return "H"; case UIKey::I: return "I";
        case UIKey::J: return "J"; case UIKey::K: return "K"; case UIKey::L: return "L";
        case UIKey::M: return "M"; case UIKey::N: return "N"; case UIKey::O: return "O";
        case UIKey::P: return "P"; case UIKey::Q: return "Q"; case UIKey::R: return "R";
        case UIKey::S: return "S"; case UIKey::T: return "T"; case UIKey::U: return "U";
        case UIKey::V: return "V"; case UIKey::W: return "W"; case UIKey::X: return "X";
        case UIKey::Y: return "Y"; case UIKey::Z: return "Z";
        case UIKey::D0: return "0"; case UIKey::D1: return "1"; case UIKey::D2: return "2";
        case UIKey::D3: return "3"; case UIKey::D4: return "4"; case UIKey::D5: return "5";
        case UIKey::D6: return "6"; case UIKey::D7: return "7"; case UIKey::D8: return "8";
        case UIKey::D9: return "9";
        case UIKey::F1: return "F1"; case UIKey::F2: return "F2"; case UIKey::F3: return "F3";
        case UIKey::F4: return "F4"; case UIKey::F5: return "F5"; case UIKey::F6: return "F6";
        case UIKey::F7: return "F7"; case UIKey::F8: return "F8"; case UIKey::F9: return "F9";
        case UIKey::F10: return "F10"; case UIKey::F11: return "F11"; case UIKey::F12: return "F12";
        case UIKey::Escape: return "Esc";
        case UIKey::Tab: return "Tab";
        case UIKey::Return: return "Enter";
        case UIKey::KeypadEnter: return "NumEnter";
        case UIKey::Space: return "Space";
        case UIKey::Backspace: return "Backspace";
        case UIKey::Delete: return "Delete";
        case UIKey::Insert: return "Insert";
        case UIKey::Home: return "Home";
        case UIKey::End: return "End";
        case UIKey::PageUp: return "PageUp";
        case UIKey::PageDown: return "PageDown";
        case UIKey::Left: return "Left";
        case UIKey::Right: return "Right";
        case UIKey::Up: return "Up";
        case UIKey::Down: return "Down";
        case UIKey::LeftCtrl: return "LCtrl";
        case UIKey::RightCtrl: return "RCtrl";
        case UIKey::LeftShift: return "LShift";
        case UIKey::RightShift: return "RShift";
        case UIKey::LeftAlt: return "LAlt";
        case UIKey::RightAlt: return "RAlt";
        case UIKey::LeftSuper: return "LSuper";
        case UIKey::RightSuper: return "RSuper";
        case UIKey::Minus: return "-";
        case UIKey::Equals: return "=";
        case UIKey::LeftBracket: return "[";
        case UIKey::RightBracket: return "]";
        case UIKey::Backslash: return "\\";
        case UIKey::Semicolon: return ";";
        case UIKey::Apostrophe: return "'";
        case UIKey::Grave: return "`";
        case UIKey::Comma: return ",";
        case UIKey::Period: return ".";
        case UIKey::Slash: return "/";
        case UIKey::CapsLock: return "CapsLock";
        default: return "Key";
        }
    }

    inline uint64_t GetTimeMs()
    {
        static const auto s_Start = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - s_Start).count());
    }

    // Optional platform hooks implemented by noxui_impl_sdl3 (or a custom platform backend).
    struct UIPlatformCallbacks
    {
        std::function<void(UICursorShape)> SetCursorShape;
        std::function<void(bool)> CaptureMouse;
        std::function<std::string()> GetClipboardText;
        std::function<void(const std::string&)> SetClipboardText;
        std::function<void()> StartTextInput;
        std::function<void()> StopTextInput;
    };
}
