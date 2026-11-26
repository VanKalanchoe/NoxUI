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
- **Idle frames are skipped (retained mode).** Each frame `UIContext::UpdateAndRender` runs the `OnSync` hooks, updates the hover against
  what is on screen (the last layout, as pointer events do), runs the style pass (`OnUpdate`: animations, transitions) and keys every
  visible element: its `HashState` (its fields, interaction state, animation state, values read through callbacks) and the keys of the
  elements inside it. With the context's own state (focus, pointer, toasts, theme version, glyph cache, viewport) that is the UI's
  fingerprint. When no input came since the last frame, the fingerprint equals the last frame's, and the draw list is still the one it
  drew (`UIDrawList::GetRevision`), layout and paint would draw the same: they are skipped and the list keeps what it holds
  (`WasLastFrameSkipped()` -- a backend can skip its upload). A skipped frame costs 7-15 us on the gallery's static tabs (in this container).
- **Drawn frames do only what changed (partial layout and paint).** The same keys drive two caches (`SetLayoutCache`, `SetPaintCache`,
  on by default). *Layout:* an element whose own state, the state of everything inside it, the bounds inside it and its parent's state
  are what they were when it was last measured keeps its size without measuring again (`HeightForWidth` keeps its answers the same way);
  arranged again in the same rect, it keeps every bound inside it. *Paint:* after layout each element's key takes its bounds, its clip,
  its parent's state and, for the element under the pointer (captured, focused), the pointer; an element whose key and clip are last
  frame's is copied from last frame's draw list (kept by the context, swapped, not copied) with everything inside it -- its primitives and
  the clip rects it pushed, their indices moved to where they land. On the Editor Widgets tab, where two level meters and the channel
  curves animate every frame, a drawn frame measures and paints ~14 of its 178 elements and copies the rest: 0.095 ms instead of 0.19 ms
  laid out and painted in full (0.22-0.27 ms at the previous commit, whose fingerprint was a pass of its own). Nothing is copied while a
  drag and drop is in flight (elements paint from the pointer and the payload). The rules a widget follows: layout changes nothing paint
  reads but the bounds (a parent may set a child's `FixedSize` / `Visible` while laying it out -- the child's own state is checked again);
  state read from outside the element and its children goes in `HashPaintDependencies` (the parent's own state is always in the key).
- **Custom widgets.** A class that adds state overrides `HashState` (call the base first, add every field its layout and paint read)
  and `StateHashClass` (its own `typeid`) -- the `NOXUI_STATE_HASH(Class)` macro declares both. An element whose class does not
  (a subclass with fields of its own) never lets a frame be skipped: nothing breaks, it just keeps the old cost. A widget that paints
  from callbacks fingerprints its paint output (`HashPaintOutput`); one that polls outside data every frame does it in `OnPoll` (with
  the syncs, before the fingerprint). A painter reading state outside the UI calls `UIContext::RequestRedraw()` when it changes.
- **Why a frame was drawn.** `UIContext::GetLastDrawReason` / `GetDrawReasonCount` (input or a change, changed state, an incomplete
  fingerprint -- `GetIncompleteClassName` -- or a changed draw list); `SetTraceStateChanges(true)` logs, per frame, the elements whose own
  fingerprint changed. The tooltip timer is not part of the fingerprint, only whether the tooltip shows this frame: a pointer resting on
  an element no longer keeps frames drawn for the 0.45 s delay (a skipped frame advances the timer as a drawn one does).
- **Transitions arrive.** A style transition snaps to its target once every field is within 0.001 (a quarter of an 8-bit colour step,
  a thousandth of a pixel), as the toggle, drawer and scroll-bar fades do: an exponential approach never lands exactly, and a settled
  element must stay settled whatever the frame time.
- **The GPU redraws only what changed (dirty rects).** After a drawn frame `UIContext::GetDirtyRects()` names the pixels it changed
  compared with the frame drawn before (whose list revision is `GetDirtyBaseRevision()`): every primitive painted this frame, or in the
  last frame's list and gone, marks what it can change (`UIDrawList::PrimitiveExtent`: its quad with a shadow's or a curve's padding,
  within its clip's half-pixel edge); copied elements and the unchanged primitives a repainted run begins and ends with (the backgrounds
  of the containers around a changed element) mark nothing -- the order of everything else is as it was. At most 16 disjoint rects;
  `IsDirtyFull()` when there is no base, children were reordered, or half the viewport or more changed. A backend that keeps the UI's
  pixels clears each rect and draws the list scissored to it: `NoxUI_ImplNoxRHI_RenderDrawDataRects` (NoxRHI's `clearColorRects`), as
  UIDemo's UI cache does (`--no-partial-redraw` turns it off). In the replay, nearly every drawn frame redraws under 1% of the viewport.
- **Verified.** `UIFrameReplay` (tests/) replays the gallery headless with seeded input -- tab switches, clicks, drags, wheel, keys,
  typing, idle stretches, resizes, toasts, a frame time that varies -- and hashes each frame's draw list: `--record` / `--compare` check
  one build against another frame by frame (`--no-incremental` records the same build with nothing skipped, reused or copied);
  `--verify` draws every frame that would be skipped, and every drawn frame that copied elements, in full and compares it with what was
  kept, and checks the dirty rects: at every second pixel outside them, the primitives that can change it (in order, with their clips)
  must be the frame before's. UIDemo's `--verify-ui-cache` checks the GPU side: after each partial redraw the whole list is drawn into a
  texture of its own and compared with the cache, byte for byte (with `--fixed-clock` the animations run on a 1/60 s frame clock). Eight seeds (24,000 frames) give exactly the frames of the same build without idle frames and caches, in Release and in a Debug
  build with AddressSanitizer and UndefinedBehaviorSanitizer; CI runs `--verify` and the `--no-incremental` comparison on three.
  One behaviour changed on purpose: the hover follows the last frame's layout (what is on screen) at the start of the frame, not the new
  layout after it -- when the layout moves under a pointer that stands still, the hover follows one frame later, and hover styles apply in
  the frame the hover changes.
- **Layout** remembers `HeightForWidth` answers during a layout pass (nested stacks asked each child twice per level: the Editor
  Widgets tab's arrange went from 142 to 51 us), and **text drawing** reads kerning from a per-font table.
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
build\Release\UIDemo.exe --vk --tab 1                      :: open the gallery on tab 1 (Inputs & Data)
build\Release\UIDemo.exe --vk --trace-ui-changes trace.txt  :: log the elements that keep the UI from being idle (to the file;
                                                            :: without a file, to the console); a summary at exit
build\Release\UIDemo.exe --vk --no-ui-cache                 :: draw the UI on the GPU every frame (no cached texture)
build\Release\UIDemo.exe --vk --no-partial                  :: drawn frames lay out and paint everything (no layout / paint cache)
build\Release\UIDemo.exe --vk --no-partial-redraw           :: the UI cache is redrawn in full (not only the dirty rects)
build\Release\UIDemo.exe --vk --verify-ui-cache --fixed-clock :: compare every partial redraw with drawing everything (slow; test)
```
The telemetry splits its readouts by kind of frame (an average over a mix jumps): UI CPU on drawn and on idle frames, GPU time of frames
that redrew the UI and of frames that only composited it -- each averaged over the last quarter second -- and what the last drawn frame
reused (elements measured, painted, copied). On exit UIDemo prints how many frames were idle, how many redrew the UI on the GPU (and how many of those only the dirty rects, over how much of the window), what
drawn frames measured, painted and copied on average, the GPU time of a frame and of each render-graph pass (the UI redraw, the
composite, the uploads -- what the rest of the frame took is the difference), and why frames were drawn (`UIContext::GetDrawReasonCount`: input, changed state,
an incomplete fingerprint -- with the class -- or a changed draw list). The summary (the command line, API, FPS, UI CPU on drawn and idle frames, GPU time of
redraws and composites, partial redraws, verify results, GPU time per pass, why frames were drawn, the trace's top elements) is also
written to `UIDemo-summary.txt` in the working directory -- one file to send.

**The UI cache (UIDemo).** The UI is drawn into a texture of its own only when its draw list changed (`UIDrawList::GetRevision`, which an
idle `UpdateAndRender` leaves as it was); other frames show that texture with one full-screen textured quad through the same pipeline.
Over an opaque clear the pipeline's alpha (`src + dst * (1 - src)`) stays 1, so the texture is opaque and the quad reproduces what drawing
to the swapchain gave. The telemetry readouts change four times a second (every frame, their text alone kept every frame from being idle).

---

## CMake Options

| Option | Default | Description |
|---|---|---|
| `NOXUI_BUILD_SDL3_BACKEND` | `ON` | Provide header-only `NoxUI::SDL3` interface target (`noxui_impl_sdl3.h`) |
| `NOXUI_BUILD_NOXRHI_BACKEND` | `ON` | Provide header-only `NoxUI::NoxRHI` interface target (`noxui_impl_noxrhi.h`) |
| `NOXUI_BUILD_EXAMPLES` | `ON` | Build standalone `UIDemo` executable |
| `NOXRHI_DIR` | `vendors/NoxRHI` | Path to the `NoxRHI` repository |
