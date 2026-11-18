# NoxUI

A standalone C++20 retained-mode SDF UI library with an optional **SDL3** platform backend (`noxui_impl_sdl3.h`) and **NoxRHI** renderer backend (`noxui_impl_noxrhi.h` supporting **Vulkan**, **Direct3D 12**, and **`NRI_GRAPHICS_API_NONE`**).

Core `NoxUI` has **zero** Dear ImGui, SDL3, or GPU SDK dependencies. It performs two-pass declarative layout (`Measure` + `Arrange`), `.nstyle` YAML theme styling with smooth transitions, MSDF font atlas generation (`msdf-atlas-gen`), docking (`UIDockSpace`), and emits backend-agnostic `UIDrawList` primitives (`UIPrimitive` & `UIClipRect`).

---

## Architecture (3 Layers)

1. **`include/NoxUI/` + `src/` (`NoxUI::Core`)** — Pure CPU retained-mode UI toolkit:
   - `Core/`: `UIRef`, `UILog`, `UIPlatform`, `UITypes`, `UITheme` (`.nstyle` YAML themes), `UIElement`, `UIContext`, `UIBuilder` (declarative `VStack`, `HStack`, `Grid`, `Wrap`, `Scroll`, `Card`), `UIViewportManager` (multi-viewport tear-off interface).
   - `Layout/`: `UIAccordion`, `UIDockSpace`, `UIGroupBox`, `UILayout`, `UISplitter`, `UITabPanel`, `UIWindow`, `UIWrapBox`.
   - `Widgets/`: 40+ retained widgets (`UIAlertBanner`, `UIBadge`, `UIBreadcrumb`, `UIButton`, `UICheckBox`, `UICollapsingHeader`, `UIColorPicker`, `UICommandPalette`, `UICurveEditor`, `UIDragScalar`, `UIDropdown`, `UIImage`, `UIKeybindButton`, `UILabel`, `UILogView`, `UIMenu`, `UIModal`, `UINodeGraph`, `UIPagination`, `UIPlotGraph`, `UIPopup`, `UIProgressBar`, `UIPropertyGrid`, `UIRadialMenu`, `UIRadioGroup`, `UIRangeSlider`, `UIRotaryKnob`, `UIScrollBar`, `UIScrollView`, `UISearchBar`, `UISegmentedControl`, `UISeparator`, `UISlider`, `UISpinBox`, `UISpinner`, `UISplitButton`, `UIStarRating`, `UIStatCard`, `UITableView`, `UITagInput`, `UITextArea`, `UITextInput`, `UITimeline`, `UIToggleSwitch`, `UITreeView`, `UIVectorEdit`).
   - `Rendering/`: `UIDrawList` (CPU draw commands, clip stack, `UIPrimitive` / `UIClipRect`), `UIFont` (MSDF atlas generator), `UIIcons`.
   - `Demo/`: `UIDemoGallery` (interactive component gallery).
2. **`include/NoxUI/Backends/` (`NoxUI::SDL3`, `NoxUI::NoxRHI`)** — Dear ImGui-style header-only integration backends:
   - `noxui_impl_sdl3.h`: Translates SDL3 input events, manages system cursors, clipboard, text input, and secondary OS windows for `UIViewportManager`.
   - `noxui_impl_noxrhi.h` + `shaders/UIPrimitives.slang`: Cross-API Slang VS+PS SDF renderer built on `NoxRHI::HPP` (Vulkan BDA + D3D12 bindless `ByteAddressBuffer` / `Texture2D.Handle` + `NRI_GRAPHICS_API_NONE`).
3. **`examples/UIDemo/` (`UIDemo`)** — Standalone SDL3 + `NoxRHI::RenderGraph` + `NoxUI` application showcasing `UIDemoGallery`, docking, multi-viewport tear-off, and live GPU telemetry.

---

## Requirements & Dependencies

### System Prerequisites (Windows)
- **Visual Studio 2026** (v18) with **ClangCL** (default in scripts) and/or **MSVC** C++20 toolset, plus **Windows 10/11 SDK**.
- **CMake >= 3.25**
- **Git**, **curl**, and **tar**
- **Vulkan SDK** (tested with **1.4.350.0**, `VULKAN_SDK` set in environment)
- **Sibling `NoxRHI` repository** at `../NoxRHI` (or custom path via `-DNOXRHI_DIR=<path>`) when `NOXUI_BUILD_NOXRHI_BACKEND=ON`.

### Vendored Dependencies (Downloaded into `vendors/` by `scripts\install_dependencies.bat`)

| Dependency | Version | Purpose | Scope | Source |
|---|---|---|---|---|
| **msdf-atlas-gen** (incl. **msdfgen**) | `v1.4` | Runtime MSDF font atlas generation (`UIFont`) | Private (`NoxUI::Core`) | [Chlumsky/msdf-atlas-gen](https://github.com/Chlumsky/msdf-atlas-gen) |
| **vcpkg** + **glm** | `1.0.3` (`x64-windows`) | Vector types (`UIVec2`, `UIColor`, `UIPrimitive`) | Public (`NoxUI::Core`) | [g-truc/glm](https://github.com/g-truc/glm) |
| **yaml-cpp** | `0.9.0` (`x64-windows`) | `.nstyle` theme and dock layout serialization | Private (`NoxUI::Core`) | [jbeder/yaml-cpp](https://github.com/jbeder/yaml-cpp) |
| **freetype** | `2.15.0` (`x64-windows`) | Font glyph outline loading for `msdfgen` | Private (`NoxUI::Core`) | [freetype/freetype](https://gitlab.freedesktop.org/freetype/freetype) |
| **SDL3** | `3.4.20` (`x64-windows`) | Windowing, events, cursor, clipboard (`noxui_impl_sdl3.h`) | `NoxUI::SDL3` & `UIDemo` | [libsdl-org/SDL](https://github.com/libsdl-org/SDL) |
| **stb** | `x64-windows` | Offscreen PNG screenshot capture (`stb_image_write.h`) | `examples/UIDemo` only | [nothings/stb](https://github.com/nothings/stb) |

---

## Quick Start (Fresh Clone)

### 1. Install Dependencies into `vendors/`
```bat
scripts\install_dependencies.bat
```
Clones `msdf-atlas-gen (v1.4)` and bootstraps `vcpkg` (`glm`, `yaml-cpp`, `freetype`, `sdl3`, `stb`) with binary caching in `%TEMP%\vcpkg-cache`.

### 2. Generate the Solution (`.slnx`) for Rider / Visual Studio
```bat
:: Configure with Visual Studio 2026 + ClangCL (default), generating build\NoxUI.slnx:
scripts\setup_build.bat

:: To configure with default MSVC instead of ClangCL:
scripts\setup_build.bat --msvc

:: To pass custom CMake flags (e.g. custom NoxRHI path or core-only build):
scripts\setup_build.bat -DNOXRHI_DIR=E:/dev/Modules/NoxRHI
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
| `NOXRHI_DIR` | `../NoxRHI` | Path to the `NoxRHI` repository |
