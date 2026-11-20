#pragma once

#include <functional>
#include <string>
#include <vector>

#include "NoxUI/Core/UIRef.h"

namespace NoxUI
{
    class UIContext;
    class UIElement;

    struct UIDemoPanel
    {
        std::string Title;
        Ref<UIElement> Content;
    };

    // Builds the individual demo sections as separate dockable panels:
    // "Controls", "Inputs & Data", "Layout & Graphs", "Overlays & Theme" and "Editor Widgets".
    std::vector<UIDemoPanel> CreateUIDemoPanels(UIContext& context, const std::function<void(const std::string&)>& onStatus = {});

    // The "Editor Widgets" panel alone: UIIconButton, UILevelMeter, UICurveEditor (FitKeys), UIChannelCurves, UIPropertyRows,
    // UISearchPicker, UIPanZoom, UIDrawerHost, UICommandList, the OnSync / Defer / OnKey hooks, a dialog button with Enabled,
    // ThemeColor (UIThemeToken) and text from the glyph cache -- each working live.
    UIDemoPanel CreateEditorWidgetsDemoPanel(UIContext& context, const std::function<void(const std::string&)>& onStatus = {});

    // Builds an interactive ImGui-Demo-style gallery showcasing all Nox::UI widgets,
    // layouts, virtualized lists, overlays/modals, and .nstyle theming.
    Ref<UIElement> CreateUIDemoGallery(UIContext& context, const std::function<void(const std::string&)>& onStatus = {});
}
