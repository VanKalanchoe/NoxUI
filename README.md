# NoxUI

[![CI](https://github.com/VanKalanchoe/NoxUI/actions/workflows/ci.yml/badge.svg)](https://github.com/VanKalanchoe/NoxUI/actions/workflows/ci.yml)

A standalone C++20 retained-mode SDF UI library with an optional **SDL3** platform backend (`noxui_impl_sdl3.h`) and **[NoxRHI](https://github.com/VanKalanchoe/NoxRHI)** renderer backend (`noxui_impl_noxrhi.h` supporting **Vulkan**, **Direct3D 12**, and **`NRI_GRAPHICS_API_NONE`**).

Core `NoxUI` has **zero** Dear ImGui, SDL3, or GPU SDK dependencies. It performs two-pass declarative layout (`Measure` + `Arrange`), `.nstyle` YAML theme styling with smooth transitions, MSDF font atlas generation (`msdf-atlas-gen`), docking (`UIDockSpace`), and emits backend-agnostic `UIDrawList` primitives (`UIPrimitive` & `UIClipRect`).

---

## Architecture (3 Layers)

1. **`include/NoxUI/` + `src/` (`NoxUI::Core`)** — Pure CPU retained-mode UI toolkit:
   - `Core/`: `UIRef`, `UILog`, `UIPlatform`, `UITypes`, `UITheme` (`.nstyle` YAML themes), `UIElement`, `UIContext`, `UIBuilder` (declarative `VStack`, `HStack`, `Grid`, `Wrap`, `Scroll`, `Card`), `UIViewportManager` (multi-viewport tear-off interface), `UICommandList` (commands with rebindable `UIKeyChord`s, saved bindings), `ThemeColor` / `UIThemeToken` (code colours a theme can change). Hooks on every element: `OnSync` (runs each frame before layout), `OnKey`; `UIContext::Defer`, `GetCurrentModifiers`, `IsMouseButtonHeld`.
   - `Layout/`: `UIAccordion`, `UIDockSpace`, `UIDrawerHost` (slide-up drawer), `UIGroupBox`, `UILayout`, `UISplitter`, `UITabPanel`, `UIWindow`, `UIWrapBox`.
   - `Widgets/`: 40+ retained widgets (`UIAlertBanner`, `UIBadge`, `UIBreadcrumb`, `UIButton`, `UIChannelCurves`, `UICheckBox`, `UICollapsingHeader`, `UIColorPicker`, `UICommandPalette`, `UICurveEditor`, `UIDragScalar`, `UIDropdown`, `UIIconButton`, `UIImage`, `UIKeybindButton`, `UILabel`, `UILevelMeter`, `UILogView`, `UIMenu`, `UIModal`, `UINodeGraph`, `UIPagination`, `UIPanZoom`, `UIPlotGraph`, `UIPopup`, `UIProgressBar`, `UIPropertyGrid`, `UIPropertyRows` (live getter / setter rows, `UIPropertyRow`, `UICollapsibleCard`), `UIRadialMenu`, `UIRadioGroup`, `UIRangeSlider`, `UIRotaryKnob`, `UIScrollBar`, `UIScrollView`, `UISearchBar`, `UISearchPicker`, `UISegmentedControl`, `UISeparator`, `UISlider`, `UISpinBox`, `UISpinner`, `UISplitButton`, `UIStarRating`, `UIStatCard`, `UITableView`, `UITagInput`, `UITextArea`, `UITextInput`, `UITimeline`, `UIToggleSwitch`, `UITreeView`, `UIVectorEdit`).
   - `Rendering/`: `UIDrawList` (CPU draw commands, clip stack, `UIPrimitive` / `UIClipRect`), `UIFont` (MSDF atlas generator), `UIGlyphCache` (FreeType glyphs at the pixel size shown, up to 48 px: crisp small text), `UIIcons`.
   - `Demo/`: `UIDemoGallery` (interactive component gallery; its "Editor Widgets" tab shows the widgets and hooks above working live).
2. **`include/NoxUI/Backends/` (`NoxUI::SDL3`, `NoxUI::NoxRHI`)** — Dear ImGui-style header-only integration backends:
   - `noxui_impl_sdl3.h`: Translates SDL3 input events, manages system cursors, clipboard, text input, and secondary OS windows for `UIViewportManager`.
   - `noxui_impl_noxrhi.h` + `shaders/UIPrimitives.slang`: Cross-API Slang VS+PS SDF renderer built on `NoxRHI::HPP` (Vulkan BDA + D3D12 bindless `ByteAddressBuffer` / `Texture2D.Handle` + `NRI_GRAPHICS_API_NONE`).
3. **`examples/UIDemo/` (`UIDemo`)** — Standalone SDL3 + `NoxRHI::RenderGraph` + `NoxUI` application showcasing `UIDemoGallery`, docking, multi-viewport tear-off, and live GPU telemetry.

---

## Quick Integration Example

```cpp
#include <NoxUI/NoxUI.h>
#include <NoxUI/Backends/noxui_impl_sdl3.h>
#include <NoxUI/Backends/noxui_impl_noxrhi.h>

// 1. Initialize UIContext, Theme, and Backends
NoxUI::UIContext uiContext;
auto theme = NoxUI::UITheme::CreateDefault();
theme->LoadFromFile("assets/themes/DefaultDark.nstyle");
uiContext.SetTheme(theme);

NoxUI_ImplSDL3_Init(window, uiContext);
NoxUI_ImplNoxRHI_Init({
    .device = &device,
    .resourceHeap = &resourceHeap,
    .samplerHeap = &samplerHeap,
    .colorFormat = NRI_FORMAT_SURFACE,
    .framesInFlight = 2
});

// 2. Build Declarative Retained UI Tree
using namespace NoxUI;
uiContext.SetRoot(
    VStack(10.0f, {
        Card("Scene Controls", {
            Label("Retained-mode SDF UI on NoxRHI"),
            Button("Save Scene", [&]() { uiContext.PushToast("Scene saved"); }).Primary(),
            Slider(0.75f, 0.0f, 1.0f)
        }).FillWidth()
    }).Padding(16.0f).Fill()
);

// 3. Per-Frame Update + RenderGraph Passes
NoxUI::UIDrawList drawList;
NoxUI_ImplSDL3_NewFrame(uiContext);
uiContext.UpdateAndRender(drawList, { width, height }, dtSeconds);

const NoxUI_NoxRHIDrawData drawData = NoxUI_ImplNoxRHI_PrepareDrawData(drawList, frameSlot);
// In upload pass:  NoxUI_ImplNoxRHI_CopyData(cmd, drawData);
// In raster pass:  NoxUI_ImplNoxRHI_RenderDrawData(cmd, drawData);
```

---

## Performance Notes

- **Text measurement** reads glyph advances from a per-byte table built at font load (`UIMSDFData::ByteGlyphs`), not msdf-atlas-gen's
  `std::map` lookup per character; **word wrap** adds token widths instead of re-measuring the line for every word (same line breaks:
  checked against the old algorithm over 3.3 million texts / widths / sizes); a **wrapped label keeps its last wrap** while its text,
  width, font size and font stay the same (layout asks for a wrapped label's height several times a frame). The "Editor Widgets" demo tab
  went from 3.4 ms to 0.15 ms of `UpdateAndRender` per frame (Release; 44 ms to 1.1 ms in Debug); the other tabs take 0.02-0.04 ms.
- **UIDemo's telemetry** shows the frame time (one frame to the next: waiting for the GPU and the swapchain included) and, separately,
  `UI CPU` -- `UIContext::UpdateAndRender` alone. Measure in Release: Debug builds are ~10x slower and NoxRHI's validation adds more.

## Requirements & Dependencies

### System Prerequisites (Windows)
- **Visual Studio 2026** (v18) with **ClangCL** (default in scripts) and/or **MSVC** C++20 toolset, plus **Windows 10/11 SDK**.
- **CMake >= 3.25**
- **Git**, **curl**, and **tar**
- **Vulkan SDK** (tested with **1.4.350.0**, `VULKAN_SDK` set in environment)

### Vendored Dependencies (Downloaded into `vendors/` by `scripts\install_dependencies.bat`)

| Dependency | Version | Purpose | Scope | Source |
|---|---|---|---|---|
| **NoxRHI** (+ **VMA-Hpp**, **D3D12MA**, **Slang**) | `0.1.0` | Cross-API RHI (Vulkan, D3D12, `DeviceNone`) & `RenderGraph` | `NoxUI::NoxRHI` & `UIDemo` | [VanKalanchoe/NoxRHI](https://github.com/VanKalanchoe/NoxRHI) |
| **msdf-atlas-gen** (incl. **msdfgen**) | `v1.4` | Runtime MSDF font atlas generation (`UIFont`) | Private (`NoxUI::Core`) | [Chlumsky/msdf-atlas-gen](https://github.com/Chlumsky/msdf-atlas-gen) |
| **vcpkg** + **glm** | `1.0.3` (`x64-windows`) | Vector types (`UIVec2`, `UIColor`, `UIPrimitive`) | Public (`NoxUI::Core`) | [g-truc/glm](https://github.com/g-truc/glm) |
| **yaml-cpp** | `0.9.0` (`x64-windows`) | `.nstyle` theme and dock layout serialization | Private (`NoxUI::Core`) | [jbeder/yaml-cpp](https://github.com/jbeder/yaml-cpp) |
| **freetype** | `2.15.0` (`x64-windows`) | Font glyph outlines for `msdfgen` and the glyph cache's rasterization | Private (`NoxUI::Core`) | [freetype/freetype](https://gitlab.freedesktop.org/freetype/freetype) |
| **SDL3** | `3.4.20` (`x64-windows`) | Windowing, events, cursor, clipboard (`noxui_impl_sdl3.h`) | `NoxUI::SDL3` & `UIDemo` | [libsdl-org/SDL](https://github.com/libsdl-org/SDL) |
| **stb** | `x64-windows` | Offscreen PNG screenshot capture (`stb_image_write.h`) | `examples/UIDemo` only | [nothings/stb](https://github.com/nothings/stb) |

---

## Quick Start (Fresh Clone)

### 1. Install Dependencies into `vendors/`
```bat
scripts\install_dependencies.bat
```
Clones `NoxRHI` (plus `VulkanMemoryAllocator-Hpp`, `D3D12MemoryAllocator`, and `Slang 2026.14.1`), `msdf-atlas-gen (v1.4)`, and bootstraps `vcpkg` (`glm`, `yaml-cpp`, `freetype`, `sdl3`, `stb`) with binary caching in `%TEMP%\vcpkg-cache`.

### 2. Generate the Solution (`.slnx`) for Rider / Visual Studio
```bat
:: Configure with Visual Studio 2026 + ClangCL (default), generating build\NoxUI.slnx:
scripts\setup_build.bat

:: To configure with default MSVC instead of ClangCL:
scripts\setup_build.bat --msvc

:: To pass custom CMake flags (e.g. core-only build without SDL3/NoxRHI):
scripts\setup_build.bat -DNOXUI_BUILD_EXAMPLES=OFF -DNOXUI_BUILD_NOXRHI_BACKEND=OFF -DNOXUI_BUILD_SDL3_BACKEND=OFF
```

Open **`build\NoxUI.slnx`** in **JetBrains Rider** (or Visual Studio 2026) and build/run **`UIDemo`**.

### 3. `UIDemo` CLI Flags
`UIDemo` defaults to the no-GPU backend (`NRI_GRAPHICS_API_NONE`); pass `--vk` (Vulkan) or `--dx12` / `--d3d12` (Direct3D 12) to run on a hardware GPU backend. VSync is off by default (`--vsync` enables it):
```bat
build\Debug\UIDemo.exe --frames 10
build\Debug\UIDemo.exe --vk --frames 10 --resize-test
build\Debug\UIDemo.exe --dx12 --frames 10 --resize-test
build\Debug\UIDemo.exe --vk --frames 10 --minimize-test
build\Debug\UIDemo.exe --vk --frames 5 --screenshot screenshot_vk.png
build\Debug\UIDemo.exe --dx12 --frames 5 --screenshot screenshot_dx12.png
```

---

## CMake Options

| Option | Default | Description |
|---|---|---|
| `NOXUI_BUILD_SDL3_BACKEND` | `ON` | Provide header-only `NoxUI::SDL3` interface target (`noxui_impl_sdl3.h`) |
| `NOXUI_BUILD_NOXRHI_BACKEND` | `ON` | Provide header-only `NoxUI::NoxRHI` interface target (`noxui_impl_noxrhi.h`) |
| `NOXUI_BUILD_EXAMPLES` | `ON` | Build standalone `UIDemo` executable |
| `NOXRHI_DIR` | `vendors/NoxRHI` | Path to the `NoxRHI` repository |
