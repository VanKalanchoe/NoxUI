#pragma once

// SDL3 Platform Backend for NoxUI (input translation, system cursors, clipboard, text input, and secondary OS windows).

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "NoxUI/Core/UIContext.h"
#include "NoxUI/Core/UIPlatform.h"
#include "NoxUI/Core/UIViewportManager.h"

namespace NoxUI::Detail
{
    struct SDL3BackendState
    {
        SDL_Window* MainWindow = nullptr;
        uint32_t MainWindowID = 0;
        SDL_Cursor* Cursors[5]{};
        UICursorShape LastCursor = UICursorShape::Default;
        bool Initialized = false;
    };

    inline SDL3BackendState& GetSDL3State()
    {
        static SDL3BackendState s_State{};
        return s_State;
    }

    inline uint16_t TranslateSDLMods(SDL_Keymod sdlMods)
    {
        uint16_t mods = UIMod::None;
        if (sdlMods & SDL_KMOD_SHIFT) mods |= UIMod::Shift;
        if (sdlMods & SDL_KMOD_CTRL)  mods |= UIMod::Ctrl;
        if (sdlMods & SDL_KMOD_ALT)   mods |= UIMod::Alt;
        if (sdlMods & SDL_KMOD_GUI)   mods |= UIMod::Super;
        return mods;
    }

    inline UIKey TranslateSDLScancode(SDL_Scancode sc)
    {
        switch (sc)
        {
        case SDL_SCANCODE_A: return UIKey::A;
        case SDL_SCANCODE_B: return UIKey::B;
        case SDL_SCANCODE_C: return UIKey::C;
        case SDL_SCANCODE_D: return UIKey::D;
        case SDL_SCANCODE_E: return UIKey::E;
        case SDL_SCANCODE_F: return UIKey::F;
        case SDL_SCANCODE_G: return UIKey::G;
        case SDL_SCANCODE_H: return UIKey::H;
        case SDL_SCANCODE_I: return UIKey::I;
        case SDL_SCANCODE_J: return UIKey::J;
        case SDL_SCANCODE_K: return UIKey::K;
        case SDL_SCANCODE_L: return UIKey::L;
        case SDL_SCANCODE_M: return UIKey::M;
        case SDL_SCANCODE_N: return UIKey::N;
        case SDL_SCANCODE_O: return UIKey::O;
        case SDL_SCANCODE_P: return UIKey::P;
        case SDL_SCANCODE_Q: return UIKey::Q;
        case SDL_SCANCODE_R: return UIKey::R;
        case SDL_SCANCODE_S: return UIKey::S;
        case SDL_SCANCODE_T: return UIKey::T;
        case SDL_SCANCODE_U: return UIKey::U;
        case SDL_SCANCODE_V: return UIKey::V;
        case SDL_SCANCODE_W: return UIKey::W;
        case SDL_SCANCODE_X: return UIKey::X;
        case SDL_SCANCODE_Y: return UIKey::Y;
        case SDL_SCANCODE_Z: return UIKey::Z;

        case SDL_SCANCODE_0: return UIKey::D0;
        case SDL_SCANCODE_1: return UIKey::D1;
        case SDL_SCANCODE_2: return UIKey::D2;
        case SDL_SCANCODE_3: return UIKey::D3;
        case SDL_SCANCODE_4: return UIKey::D4;
        case SDL_SCANCODE_5: return UIKey::D5;
        case SDL_SCANCODE_6: return UIKey::D6;
        case SDL_SCANCODE_7: return UIKey::D7;
        case SDL_SCANCODE_8: return UIKey::D8;
        case SDL_SCANCODE_9: return UIKey::D9;

        case SDL_SCANCODE_F1:  return UIKey::F1;
        case SDL_SCANCODE_F2:  return UIKey::F2;
        case SDL_SCANCODE_F3:  return UIKey::F3;
        case SDL_SCANCODE_F4:  return UIKey::F4;
        case SDL_SCANCODE_F5:  return UIKey::F5;
        case SDL_SCANCODE_F6:  return UIKey::F6;
        case SDL_SCANCODE_F7:  return UIKey::F7;
        case SDL_SCANCODE_F8:  return UIKey::F8;
        case SDL_SCANCODE_F9:  return UIKey::F9;
        case SDL_SCANCODE_F10: return UIKey::F10;
        case SDL_SCANCODE_F11: return UIKey::F11;
        case SDL_SCANCODE_F12: return UIKey::F12;

        case SDL_SCANCODE_ESCAPE:    return UIKey::Escape;
        case SDL_SCANCODE_TAB:       return UIKey::Tab;
        case SDL_SCANCODE_RETURN:    return UIKey::Return;
        case SDL_SCANCODE_KP_ENTER:  return UIKey::KeypadEnter;
        case SDL_SCANCODE_SPACE:     return UIKey::Space;
        case SDL_SCANCODE_BACKSPACE: return UIKey::Backspace;
        case SDL_SCANCODE_DELETE:    return UIKey::Delete;
        case SDL_SCANCODE_INSERT:    return UIKey::Insert;
        case SDL_SCANCODE_HOME:      return UIKey::Home;
        case SDL_SCANCODE_END:       return UIKey::End;
        case SDL_SCANCODE_PAGEUP:    return UIKey::PageUp;
        case SDL_SCANCODE_PAGEDOWN:  return UIKey::PageDown;
        case SDL_SCANCODE_LEFT:      return UIKey::Left;
        case SDL_SCANCODE_RIGHT:     return UIKey::Right;
        case SDL_SCANCODE_UP:        return UIKey::Up;
        case SDL_SCANCODE_DOWN:      return UIKey::Down;

        case SDL_SCANCODE_LCTRL:  return UIKey::LeftCtrl;
        case SDL_SCANCODE_RCTRL:  return UIKey::RightCtrl;
        case SDL_SCANCODE_LSHIFT: return UIKey::LeftShift;
        case SDL_SCANCODE_RSHIFT: return UIKey::RightShift;
        case SDL_SCANCODE_LALT:   return UIKey::LeftAlt;
        case SDL_SCANCODE_RALT:   return UIKey::RightAlt;
        case SDL_SCANCODE_LGUI:   return UIKey::LeftSuper;
        case SDL_SCANCODE_RGUI:   return UIKey::RightSuper;

        case SDL_SCANCODE_MINUS:        return UIKey::Minus;
        case SDL_SCANCODE_EQUALS:       return UIKey::Equals;
        case SDL_SCANCODE_LEFTBRACKET:  return UIKey::LeftBracket;
        case SDL_SCANCODE_RIGHTBRACKET: return UIKey::RightBracket;
        case SDL_SCANCODE_BACKSLASH:    return UIKey::Backslash;
        case SDL_SCANCODE_SEMICOLON:    return UIKey::Semicolon;
        case SDL_SCANCODE_APOSTROPHE:   return UIKey::Apostrophe;
        case SDL_SCANCODE_GRAVE:        return UIKey::Grave;
        case SDL_SCANCODE_COMMA:        return UIKey::Comma;
        case SDL_SCANCODE_PERIOD:       return UIKey::Period;
        case SDL_SCANCODE_SLASH:        return UIKey::Slash;
        case SDL_SCANCODE_CAPSLOCK:     return UIKey::CapsLock;
        default: return UIKey::None;
        }
    }

    inline uint32_t ExtractEventWindowID(const SDL_Event& e)
    {
        switch (e.type)
        {
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        case SDL_EVENT_WINDOW_MOVED:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            return e.window.windowID;
        case SDL_EVENT_MOUSE_MOTION:
            return e.motion.windowID;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return e.button.windowID;
        case SDL_EVENT_MOUSE_WHEEL:
            return e.wheel.windowID;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            return e.key.windowID;
        case SDL_EVENT_TEXT_INPUT:
            return e.text.windowID;
        default:
            return 0;
        }
    }

    inline void ApplySDLCursor(UICursorShape shape)
    {
        if (!SDL_CursorVisible())
            return;

        auto& st = GetSDL3State();
        const size_t idx = static_cast<size_t>(shape);
        if (idx >= 5)
            return;

        if (!st.Cursors[idx])
        {
            SDL_SystemCursor sys = SDL_SYSTEM_CURSOR_DEFAULT;
            switch (shape)
            {
            case UICursorShape::Default:    sys = SDL_SYSTEM_CURSOR_DEFAULT; break;
            case UICursorShape::ResizeEW:   sys = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
            case UICursorShape::ResizeNS:   sys = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
            case UICursorShape::ResizeNWSE: sys = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
            case UICursorShape::ResizeNESW: sys = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
            }
            st.Cursors[idx] = SDL_CreateSystemCursor(sys);
        }

        if (st.Cursors[idx] && st.LastCursor != shape)
        {
            SDL_SetCursor(st.Cursors[idx]);
            st.LastCursor = shape;
        }
    }

    inline bool DispatchSDLEventToContext(const SDL_Event& e, UIContext& ctx)
    {
        ctx.SetModifiers(TranslateSDLMods(SDL_GetModState()));
        switch (e.type)
        {
        case SDL_EVENT_MOUSE_MOTION:
            return ctx.OnPointerMove({ e.motion.x, e.motion.y });
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            return ctx.OnPointerDown({ e.button.x, e.button.y }, static_cast<int>(e.button.button));
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return ctx.OnPointerUp({ e.button.x, e.button.y }, static_cast<int>(e.button.button));
        case SDL_EVENT_MOUSE_WHEEL:
            return ctx.OnScroll({ e.wheel.x, e.wheel.y });
        case SDL_EVENT_KEY_DOWN:
        {
            const UIKey key = TranslateSDLScancode(e.key.scancode);
            const uint16_t mods = TranslateSDLMods(e.key.mod);
            return ctx.OnKeyDown(key, mods, e.key.repeat != 0);
        }
        case SDL_EVENT_KEY_UP:
        {
            const UIKey key = TranslateSDLScancode(e.key.scancode);
            const uint16_t mods = TranslateSDLMods(e.key.mod);
            ctx.OnKeyUp(key, mods);
            return false;
        }
        case SDL_EVENT_TEXT_INPUT:
            if (e.text.text)
                return ctx.OnTextInput(e.text.text);
            return false;
        default:
            return false;
        }
    }
}

inline bool NoxUI_ImplSDL3_Init(SDL_Window* mainWindow, NoxUI::UIContext& mainContext, NoxUI::UIViewportManager* viewportManager = nullptr)
{
    if (!mainWindow)
        return false;

    auto& st = NoxUI::Detail::GetSDL3State();
    st.MainWindow = mainWindow;
    st.MainWindowID = SDL_GetWindowID(mainWindow);
    st.Initialized = true;

    SDL_StartTextInput(mainWindow);

    NoxUI::UIPlatformCallbacks callbacks{};
    callbacks.SetCursorShape = [](NoxUI::UICursorShape shape)
    {
        NoxUI::Detail::ApplySDLCursor(shape);
    };
    callbacks.CaptureMouse = [](bool capture)
    {
        SDL_CaptureMouse(capture);
    };
    callbacks.GetClipboardText = []() -> std::string
    {
        char* text = SDL_GetClipboardText();
        std::string result = text ? text : "";
        if (text)
            SDL_free(text);
        return result;
    };
    callbacks.SetClipboardText = [](const std::string& text)
    {
        SDL_SetClipboardText(text.c_str());
    };
    callbacks.StartTextInput = [mainWindow]()
    {
        SDL_StartTextInput(mainWindow);
    };
    callbacks.StopTextInput = [mainWindow]()
    {
        SDL_StopTextInput(mainWindow);
    };
    mainContext.SetPlatformCallbacks(callbacks);

    if (viewportManager)
    {
        NoxUI::UIViewportPlatformBackend vpPlat{};
        vpPlat.MainWindowHandle = mainWindow;
        vpPlat.MainWindowID = st.MainWindowID;
        vpPlat.CreateWindow = [](const std::string& title, int x, int y, uint32_t w, uint32_t h) -> NoxUI::UIViewportOSWindow
        {
            SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title.c_str());
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, x);
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y);
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, static_cast<Sint64>(w));
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, static_cast<Sint64>(h));
            SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
            SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
            SDL_Window* win = SDL_CreateWindowWithProperties(props);
            SDL_DestroyProperties(props);
            if (!win)
                return {};

            SDL_SetWindowMinimumSize(win, 220, 140);
            SDL_StartTextInput(win);

            void* hwnd = nullptr;
#ifdef _WIN32
            hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(win), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#endif
            return NoxUI::UIViewportOSWindow{
                .WindowHandle = win,
                .NativeRawHandle = hwnd,
                .WindowID = SDL_GetWindowID(win)
            };
        };
        vpPlat.DestroyWindow = [](void* windowHandle)
        {
            if (windowHandle)
                SDL_DestroyWindow(static_cast<SDL_Window*>(windowHandle));
        };
        vpPlat.SetWindowPosition = [](void* windowHandle, int x, int y)
        {
            if (windowHandle)
                SDL_SetWindowPosition(static_cast<SDL_Window*>(windowHandle), x, y);
        };
        vpPlat.GetWindowPosition = [](void* windowHandle, int& outX, int& outY)
        {
            if (windowHandle)
                SDL_GetWindowPosition(static_cast<SDL_Window*>(windowHandle), &outX, &outY);
        };
        vpPlat.SetWindowSize = [](void* windowHandle, int w, int h)
        {
            if (windowHandle)
                SDL_SetWindowSize(static_cast<SDL_Window*>(windowHandle), w, h);
        };
        vpPlat.GetWindowSize = [](void* windowHandle, int& outW, int& outH)
        {
            if (windowHandle)
                SDL_GetWindowSize(static_cast<SDL_Window*>(windowHandle), &outW, &outH);
        };
        vpPlat.GetWindowPixelSize = [](void* windowHandle, int& outPixelW, int& outPixelH)
        {
            if (windowHandle)
                SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(windowHandle), &outPixelW, &outPixelH);
        };
        vpPlat.GetWindowContentScale = [](void* windowHandle) -> float
        {
            if (!windowHandle)
                return 1.0f;
            const float s = SDL_GetWindowDisplayScale(static_cast<SDL_Window*>(windowHandle));
            return s > 0.0f ? s : 1.0f;
        };
        vpPlat.GetGlobalMouseState = [](float& outGlobalX, float& outGlobalY, bool& outLeftDown)
        {
            const SDL_MouseButtonFlags btns = SDL_GetGlobalMouseState(&outGlobalX, &outGlobalY);
            outLeftDown = (btns & SDL_BUTTON_LMASK) != 0;
        };
        viewportManager->SetPlatformBackend(std::move(vpPlat));
    }

    return true;
}

inline void NoxUI_ImplSDL3_Shutdown()
{
    auto& st = NoxUI::Detail::GetSDL3State();
    for (SDL_Cursor*& c : st.Cursors)
    {
        if (c)
        {
            SDL_DestroyCursor(c);
            c = nullptr;
        }
    }
    st.MainWindow = nullptr;
    st.MainWindowID = 0;
    st.Initialized = false;
}

inline bool NoxUI_ImplSDL3_ProcessEvent(const SDL_Event* event, NoxUI::UIContext& mainContext, NoxUI::UIViewportManager* viewportManager = nullptr)
{
    if (!event)
        return false;

    const auto& st = NoxUI::Detail::GetSDL3State();
    const uint32_t winID = NoxUI::Detail::ExtractEventWindowID(*event);

    // Route secondary viewport events first if a viewport manager is active
    if (viewportManager && winID != 0 && winID != st.MainWindowID)
    {
        if (NoxUI::UIViewport* vp = viewportManager->FindByWindowID(winID))
        {
            if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                viewportManager->RequestClose(*vp); // (the dock space may keep it open: unsaved changes to ask about)
                return true;
            }
            if (event->type == SDL_EVENT_WINDOW_RESIZED || event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            {
                vp->NeedsSwapchainResize = true;
                vp->LastResizeRequestTime = std::chrono::steady_clock::now();
                return true;
            }
            return NoxUI::Detail::DispatchSDLEventToContext(*event, vp->Context);
        }
        return false;
    }

    if (winID != 0 && st.MainWindowID != 0 && winID != st.MainWindowID)
        return false;

    return NoxUI::Detail::DispatchSDLEventToContext(*event, mainContext);
}

inline void NoxUI_ImplSDL3_NewFrame(NoxUI::UIContext& mainContext, NoxUI::UIViewportManager* viewportManager = nullptr)
{
    mainContext.SetModifiers(NoxUI::Detail::TranslateSDLMods(SDL_GetModState()));
    if (viewportManager)
        viewportManager->UpdateActiveDrag();
}
