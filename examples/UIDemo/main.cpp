#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#ifdef NOXUI_HAS_STB
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <NRIHPP/NRI.hpp>
#include <NRIHPP/NRIExtensions.hpp>
#include <NRIRenderGraph/RenderGraph.h>

#include <NoxUI/NoxUI.h>
#include <NoxUI/Backends/noxui_impl_noxrhi.h>
#include <NoxUI/Backends/noxui_impl_sdl3.h>

static int g_validationErrors = 0;

static void NRI_CALL onNriLog(NriLogLevel level, const char* message, void* userData)
{
    (void)userData;
    const char* tag = level == NRI_LOG_ERROR ? "ERROR" : level == NRI_LOG_WARN ? "WARN" : "INFO";
    std::fprintf(stderr, "[UIDemo][%s] %s\n", tag, message);
    if (level == NRI_LOG_ERROR)
    {
        if (message && std::strstr(message, "Skipping layer") != nullptr)
            return;
        ++g_validationErrors;
    }
}

static void onRgLog(nri::RGLogLevel level, std::string_view message, void* userData)
{
    (void)userData;
    const char* tag = level == nri::RGLogLevel::Error ? "ERROR" : level == nri::RGLogLevel::Warn ? "WARN" : "INFO";
    std::fprintf(stderr, "[UIDemo][RG][%s] %.*s\n", tag, static_cast<int>(message.size()), message.data());
    if (level == nri::RGLogLevel::Error)
        ++g_validationErrors;
}

// --trace-ui-changes [file]: the trace lines go to the file (not the console), and the elements named are counted for a summary at exit.
static FILE* g_traceFile = nullptr;
static std::map<std::string, uint64_t> g_traceCounts;
static uint64_t g_traceFrames = 0;

static void onUILog(NoxUI::LogLevel level, std::string_view message)
{
    if (message.rfind("UIContext trace:", 0) == 0)
    {
        ++g_traceFrames;
        for (size_t open = message.find(" ["); open != std::string_view::npos; open = message.find(" [", open + 2))
        {
            const size_t close = message.find(']', open);
            if (close == std::string_view::npos)
                break;
            ++g_traceCounts[std::string(message.substr(open + 2, close - open - 2))];
        }
        if (g_traceFile)
        {
            std::fprintf(g_traceFile, "%.*s\n", static_cast<int>(message.size()), message.data());
            return;
        }
    }

    const char* tag = level == NoxUI::LogLevel::Error ? "ERROR" : level == NoxUI::LogLevel::Warn ? "WARN" : "INFO";
    std::fprintf(stderr, "[UIDemo][UI][%s] %.*s\n", tag, static_cast<int>(message.size()), message.data());
    if (level == NoxUI::LogLevel::Error)
        ++g_validationErrors;
}

static nri::NativeWindowHandle getWindowHandle(SDL_Window* window)
{
    nri::NativeWindowHandle handle{};
#ifdef _WIN32
    handle.type = NRI_WINDOW_SYSTEM_WIN32;
    handle.hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#endif
    return handle;
}

static std::filesystem::path resolveAssetPath(const std::filesystem::path& relPath)
{
    std::error_code ec;
    if (std::filesystem::exists(relPath, ec))
        return relPath;
    if (const char* basePath = SDL_GetBasePath())
    {
        auto candidate = std::filesystem::path(basePath) / relPath;
        if (std::filesystem::exists(candidate, ec))
            return candidate;
    }
    return relPath;
}

namespace
{
    constexpr uint32_t kFramesInFlight = 2;
    constexpr size_t kMetricWindow = 60;

    struct PassTimingRecord
    {
        std::string name;
        uint32_t queryId = NRI_GPU_PROFILER_INVALID_QUERY_ID;
    };

    struct FrameTimingSlot
    {
        uint32_t frameBeginQuery = NRI_GPU_PROFILER_INVALID_QUERY_ID;
        uint32_t frameEndQuery = NRI_GPU_PROFILER_INVALID_QUERY_ID;
        std::vector<PassTimingRecord> passes;
        std::vector<size_t> openPassIndices;
    };

    struct ProfilerState
    {
        nri::GpuProfiler* profiler = nullptr;
        uint32_t activeSlot = 0;
        std::array<FrameTimingSlot, kFramesInFlight> slots{};

        double lastGpuFrameMs = 0.0;
        std::vector<std::pair<std::string, double>> lastPassTimesMs;
    };

    void profilerBeginFrame(uint32_t frameSlot, void* userData)
    {
        auto* ps = static_cast<ProfilerState*>(userData);
        if (!ps || !ps->profiler || !ps->profiler->isSupported())
            return;

        ps->activeSlot = frameSlot % kFramesInFlight;
        ps->profiler->beginFrame(ps->activeSlot);

        const auto timestamps = ps->profiler->getReadbackTimestamps();
        const float periodNs = ps->profiler->getTimestampPeriod();
        const uint32_t validBits = ps->profiler->getTimestampValidBits();
        const uint64_t validMask = (validBits >= 64 || validBits == 0) ? ~0ull : ((1ull << validBits) - 1ull);

        auto lookupTicks = [&](uint32_t queryId, uint64_t& outTicks) -> bool
        {
            if (queryId == NRI_GPU_PROFILER_INVALID_QUERY_ID)
                return false;
            for (const auto& ts : timestamps)
            {
                if (ts.queryId == queryId)
                {
                    outTicks = ts.ticks;
                    return true;
                }
            }
            return false;
        };

        auto ticksToMs = [periodNs, validMask](uint64_t beginTicks, uint64_t endTicks) -> double
        {
            const uint64_t delta = (endTicks - beginTicks) & validMask;
            return (static_cast<double>(delta) * static_cast<double>(periodNs)) / 1'000'000.0;
        };

        FrameTimingSlot& completed = ps->slots[ps->activeSlot];
        uint64_t frameStart = 0;
        uint64_t frameEnd = 0;
        if (lookupTicks(completed.frameBeginQuery, frameStart) && lookupTicks(completed.frameEndQuery, frameEnd))
            ps->lastGpuFrameMs = ticksToMs(frameStart, frameEnd);

        ps->lastPassTimesMs.clear();
        for (const auto& p : completed.passes)
        {
            uint64_t s = 0;
            uint64_t e = 0;
            if (p.queryId != NRI_GPU_PROFILER_INVALID_QUERY_ID && lookupTicks(p.queryId, s) && lookupTicks(p.queryId + 1, e))
                ps->lastPassTimesMs.emplace_back(p.name, ticksToMs(s, e));
        }

        completed.frameBeginQuery = NRI_GPU_PROFILER_INVALID_QUERY_ID;
        completed.frameEndQuery = NRI_GPU_PROFILER_INVALID_QUERY_ID;
        completed.passes.clear();
        completed.openPassIndices.clear();
    }

    void profilerRecordFrameBegin(nri::CommandBuffer& cmd, void* userData)
    {
        auto* ps = static_cast<ProfilerState*>(userData);
        if (!ps || !ps->profiler || !ps->profiler->isSupported())
            return;
        ps->profiler->resetQueries(cmd);
        const uint32_t pairId = ps->profiler->allocateQueryPair();
        if (pairId != NRI_GPU_PROFILER_INVALID_QUERY_ID)
        {
            ps->slots[ps->activeSlot].frameBeginQuery = pairId;
            ps->slots[ps->activeSlot].frameEndQuery = pairId + 1;
            ps->profiler->writeTimestamp(cmd, pairId);
        }
    }

    void profilerRecordFrameEnd(nri::CommandBuffer& cmd, void* userData)
    {
        auto* ps = static_cast<ProfilerState*>(userData);
        if (!ps || !ps->profiler || !ps->profiler->isSupported())
            return;
        const uint32_t endId = ps->slots[ps->activeSlot].frameEndQuery;
        if (endId != NRI_GPU_PROFILER_INVALID_QUERY_ID)
            ps->profiler->writeTimestamp(cmd, endId);
    }

    void profilerBeginGpuZone(void*, nri::CommandBuffer& cmd, uint32_t, const char* name, void* userData)
    {
        auto* ps = static_cast<ProfilerState*>(userData);
        if (!ps || !ps->profiler || !ps->profiler->isSupported())
            return;
        auto& slot = ps->slots[ps->activeSlot];
        const uint32_t pairId = ps->profiler->allocateQueryPair();
        slot.openPassIndices.push_back(slot.passes.size());
        slot.passes.push_back({ name ? name : "Pass", pairId });
        if (pairId != NRI_GPU_PROFILER_INVALID_QUERY_ID)
            ps->profiler->writeTimestamp(cmd, pairId);
    }

    void profilerEndGpuZone(void*, nri::CommandBuffer& cmd, void* userData)
    {
        auto* ps = static_cast<ProfilerState*>(userData);
        if (!ps || !ps->profiler || !ps->profiler->isSupported())
            return;
        auto& slot = ps->slots[ps->activeSlot];
        if (slot.openPassIndices.empty())
            return;
        const size_t idx = slot.openPassIndices.back();
        slot.openPassIndices.pop_back();
        if (slot.passes[idx].queryId != NRI_GPU_PROFILER_INVALID_QUERY_ID)
            ps->profiler->writeTimestamp(cmd, slot.passes[idx].queryId + 1);
    }

    struct AppState
    {
        SDL_Window* window = nullptr;
        NriGraphicsAPI api = NRI_GRAPHICS_API_NONE;
        int width = 1280;
        int height = 768;
        int maxFrames = -1;
        bool vsync = false;
        bool resizeTest = false;
        bool minimizeTest = false;
        bool enableViewports = true;
        bool needsResize = false;
        uint32_t frameNumber = 0;
        uint32_t tickNumber = 0;
        bool failed = false;
        bool uiInitialized = false;
        std::string screenshotPath;

        std::array<float, 3> clearColor{ 0.063f, 0.075f, 0.106f };

        // The UI drawn into a texture of its own and redrawn only when its draw list changed (UIDrawList::GetRevision, which an idle
        // UpdateAndRender leaves as it was): other frames show the texture with one full-screen quad. --no-ui-cache: drawn every frame.
        bool uiCacheEnabled = true;
        bool traceUiChanges = false; // (--trace-ui-changes [file]: log what keeps the UI from being idle)
        // Telemetry over the last quarter second, split by kind of frame (an average over a mix of idle and drawn frames jumps):
        // UI CPU on drawn and on idle frames, GPU time of frames that redrew the UI and of frames that only composited it.
        double uiDrawnSum = 0.0, uiIdleSum = 0.0, gpuRedrawSum = 0.0, gpuCompositeSum = 0.0;
        uint32_t uiDrawnCount = 0, uiIdleCount = 0, gpuRedrawCount = 0, gpuCompositeCount = 0;
        double shownUiDrawn = 0.0, shownUiIdle = 0.0, shownGpuRedraw = 0.0, shownGpuComposite = 0.0;
        std::array<bool, kFramesInFlight> slotRedrewUi{}; // (what the frame last run in each slot did: its GPU time comes back later)
        std::array<bool, kFramesInFlight> slotUsed{};
        int startTab = -1;           // (--tab N: the gallery opens on tab N)
        nri::Texture uiCache;
        uint32_t uiCacheSlot = 0;
        nri::Extent2D uiCacheExtent{ 0, 0 };
        uint64_t uiCacheRevision = 0;
        std::array<float, 3> uiCacheClear{};
        bool uiCacheValid = false;
        uint64_t uiCacheRedraws = 0;
        NoxUI::UIDrawList uiCompositeList;
        // The telemetry readouts change four times a second (every frame, their text alone kept every UI frame from being idle).
        std::chrono::steady_clock::time_point lastTelemetry{};

        std::array<double, kMetricWindow> cpuHistoryMs{};
        std::array<double, kMetricWindow> gpuHistoryMs{};
        std::array<double, kMetricWindow> uiHistoryMs{}; // (UIContext::UpdateAndRender alone: the UI's CPU work)
        double lastUiMs = 0.0;
        size_t metricIndex = 0;
        size_t metricCount = 0;
        std::chrono::steady_clock::time_point lastFrameTime = std::chrono::steady_clock::now();

        nri::Device device;
        nri::Swapchain swapchain;
        nri::DescriptorHeap resourceHeap;
        nri::DescriptorHeap samplerHeap;
        nri::GpuProfiler gpuProfiler;
        nri::RenderGraph renderGraph;
        ProfilerState profilerState{};

        NoxUI::UIContext uiContext;
        NoxUI::UIDrawList uiDrawList;
        NoxUI::Ref<NoxUI::UITheme> uiTheme;
        NoxUI::Ref<NoxUI::UIDockSpace> dockSpace;
        std::unique_ptr<NoxUI::UIViewportManager> viewportManager;
        NoxUI::Ref<NoxUI::UILabel> telemetryFpsLabel;
        NoxUI::Ref<NoxUI::UILabel> telemetryGpuLabel;
        NoxUI::Ref<NoxUI::UILabel> telemetryPrimsLabel;
    };
}

static NoxUI::Ref<NoxUI::UIElement> buildTelemetryPanel(AppState& state)
{
    using namespace NoxUI;

    const char* apiLabel = (state.api == NRI_GRAPHICS_API_D3D12) ? "Direct3D 12"
                         : (state.api == NRI_GRAPHICS_API_VULKAN) ? "Vulkan"
                         : "None (No-GPU)";

    auto page = MakeUI<UIScrollView>();
    page->WidthMode = UISizeMode::Fill;
    page->HeightMode = UISizeMode::Fill;
    page->Padding = UIThickness::All(12.0f);
    page->ChildSpacing = 10.0f;

    auto infoCard = MakeUI<UIGroupBox>("NoxRHI + NoxUI Telemetry");
    infoCard->AddContent(MakeUI<UILabel>(std::string("Backend API: ") + apiLabel));
    infoCard->AddContent(MakeUI<UILabel>("Renderer: noxui_impl_noxrhi (SDF VS/PS)"));

    state.telemetryFpsLabel = MakeUI<UILabel>("FPS: --");
    state.telemetryGpuLabel = MakeUI<UILabel>("GPU Frame: -- ms");
    state.telemetryPrimsLabel = MakeUI<UILabel>("UI Primitives: --");
    infoCard->AddContent(state.telemetryFpsLabel);
    infoCard->AddContent(state.telemetryGpuLabel);
    infoCard->AddContent(state.telemetryPrimsLabel);

    auto vsyncCheck = MakeUI<UICheckBox>("Enable VSync", state.vsync, [&state](bool v)
    {
        state.vsync = v;
        state.needsResize = true;
    });
    infoCard->AddContent(vsyncCheck);

    auto reloadThemeBtn = MakeUI<UIButton>("Reload DefaultDark.nstyle", [&state]()
    {
        const auto themePath = resolveAssetPath("assets/themes/DefaultDark.nstyle");
        if (state.uiTheme && state.uiTheme->LoadFromFile(themePath))
            state.uiContext.PushToast("Reloaded DefaultDark.nstyle");
    });
    reloadThemeBtn->StyleVariant = "primary";
    infoCard->AddContent(reloadThemeBtn);

    page->AddChild(infoCard);
    return page;
}

static void initNoxUI(AppState& state)
{
    NoxUI::SetLogCallback(onUILog);
    NoxUI::UIFont::SetDefaultFontPath(resolveAssetPath("assets/fonts/OpenSans-Regular.ttf"));

    state.uiTheme = NoxUI::UITheme::CreateDefault();
    const auto themePath = resolveAssetPath("assets/themes/DefaultDark.nstyle");
    if (std::filesystem::exists(themePath))
        state.uiTheme->LoadFromFile(themePath);
    state.uiContext.SetTheme(state.uiTheme);
    state.uiContext.SetTraceStateChanges(state.traceUiChanges);

    state.dockSpace = NoxUI::MakeUI<NoxUI::UIDockSpace>();
    state.dockSpace->SetContext(&state.uiContext);
    state.uiContext.SetRoot(state.dockSpace);

    if (state.enableViewports)
    {
        state.viewportManager = std::make_unique<NoxUI::UIViewportManager>(state.uiContext, state.dockSpace.get());
        state.dockSpace->SetViewportManager(state.viewportManager.get());
    }

    NoxUI_ImplSDL3_Init(state.window, state.uiContext, state.viewportManager.get());
    NoxUI_ImplNoxRHI_Init({
        .device = &state.device,
        .resourceHeap = &state.resourceHeap,
        .samplerHeap = &state.samplerHeap,
        .samplerSlot = 0,
        .colorFormat = NRI_FORMAT_SURFACE,
        .framesInFlight = kFramesInFlight,
        .linearColorOutput = true,
        .shaderPath = resolveAssetPath("shaders/UIPrimitives.slang").string(),
        .viewportManager = state.viewportManager.get()
    });

    // Ensure default MSDF font atlas is built and uploaded
    NoxUI::UIFont::GetDefault();

    // Populate dockable demo gallery + telemetry panel
    auto gallery = NoxUI::CreateUIDemoGallery(state.uiContext);
    if (state.startTab >= 0)
    {
        // (the gallery's tab panel: the first one in it)
        std::vector<NoxUI::UIElement*> open{ gallery.get() };
        while (!open.empty())
        {
            NoxUI::UIElement* element = open.back();
            open.pop_back();
            if (auto* tabs = dynamic_cast<NoxUI::UITabPanel*>(element))
            {
                tabs->SetActiveTab(static_cast<size_t>(state.startTab));
                break;
            }
            for (auto it = element->GetChildren().rbegin(); it != element->GetChildren().rend(); ++it)
                open.push_back(it->get());
        }
    }
    state.dockSpace->AddPanel("NoxUI Component Gallery", gallery, NoxUI::UIDockDropZone::Center);
    state.dockSpace->AddPanel("NoxRHI Telemetry", buildTelemetryPanel(state), NoxUI::UIDockDropZone::Right, "NoxUI Component Gallery", 0.27f);

    state.uiInitialized = true;
}

static void shutdownNoxUI(AppState& state)
{
    if (!state.uiInitialized)
        return;

    if (state.viewportManager)
        state.viewportManager->Shutdown();
    state.viewportManager.reset();
    state.dockSpace.reset();
    state.uiTheme.reset();
    state.uiContext.SetRoot(nullptr);

    NoxUI_ImplNoxRHI_Shutdown();
    NoxUI_ImplSDL3_Shutdown();
    state.uiInitialized = false;
}

static void captureScreenshotIfRequested(AppState& state, const NoxUI_NoxRHIDrawData& drawData, nri::Extent2D extent)
{
#ifdef NOXUI_HAS_STB
    if (state.screenshotPath.empty() || state.api == NRI_GRAPHICS_API_NONE)
        return;

    // Align width down to multiple of 64 pixels (256 bytes/row) for D3D12 copyToBuffer pitch alignment
    const uint32_t capW = (extent.width >= 64u) ? (extent.width & ~63u) : extent.width;
    const uint32_t capH = extent.height;
    if (capW == 0 || capH == 0)
        return;

    state.device.waitIdle();

    nri::Texture offscreen = state.device.createTexture({
        .width = capW,
        .height = capH,
        .arrayLayers = 1,
        .mipLevels = 1,
        .sampleCount = 1,
        .usage = NRI_TEXTURE_USAGE_COLOR_ATTACHMENT,
        .format = NRI_FORMAT_SURFACE
    });

    const uint64_t readbackBytes = static_cast<uint64_t>(capW) * static_cast<uint64_t>(capH) * 4ull;
    nri::Buffer readback = state.device.createBuffer({
        .size = readbackBytes,
        .usage = NRI_BUFFER_USAGE_READBACK
    });

    nri::CommandAllocator alloc = state.device.createCommandAllocator(NRI_COMMAND_BUFFER_RESET_WITH_ALLOCATOR, NRI_QUEUE_GRAPHICS);
    nri::CommandBuffer cmd = alloc.allocateCommandBuffer(1);

    cmd.begin(0, true);
    NoxUI_ImplNoxRHI_CopyData(cmd, drawData);

    const nri::RenderAttachDesc colorAttach{
        .attachment = offscreen.handle(),
        .loadOp = NRI_LOAD_OP_CLEAR,
        .storeOp = NRI_STORE_OP_STORE,
        .clearColor = { state.clearColor[0], state.clearColor[1], state.clearColor[2], 1.0f }
    };
    const nri::RenderDesc renderDesc{
        .renderArea = { capW, capH },
        .colorAttachments = { colorAttach },
        .isSwapchainPass = false
    };
    cmd.beginRendering(renderDesc);
    NoxUI_ImplNoxRHI_RenderDrawData(cmd, drawData);
    cmd.endRendering();

    cmd.executionBarrier();
    offscreen.copyToBuffer(cmd.handle(), readback.handle(), 0, 0, capW, capH);
    cmd.end(0);

    state.device.submitAndWait(cmd, 0);

    const auto* rawPixels = static_cast<const uint8_t*>(readback.map(0, readbackBytes));
    std::vector<uint8_t> rgba(static_cast<size_t>(readbackBytes));
    std::memcpy(rgba.data(), rawPixels, rgba.size());
    readback.unmap();

    if (state.device.isSurfaceFormatBGRA())
    {
        for (size_t i = 0; i + 3 < rgba.size(); i += 4)
            std::swap(rgba[i + 0], rgba[i + 2]);
    }

    std::filesystem::path outPath(state.screenshotPath);
    if (outPath.has_parent_path())
    {
        std::error_code ec;
        std::filesystem::create_directories(outPath.parent_path(), ec);
    }
    if (stbi_write_png(outPath.string().c_str(), static_cast<int>(capW), static_cast<int>(capH), 4, rgba.data(), static_cast<int>(capW * 4)))
        std::printf("[UIDemo] Wrote screenshot (%ux%u): %s\n", capW, capH, outPath.string().c_str());
    else
        std::fprintf(stderr, "[UIDemo] Failed to write screenshot: %s\n", outPath.string().c_str());
#else
    (void)state;
    (void)drawData;
    (void)extent;
#endif
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv)
{
    auto state = std::make_unique<AppState>();
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            state->maxFrames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            state->screenshotPath = argv[++i];
        else if (std::strcmp(argv[i], "--resize-test") == 0)
            state->resizeTest = true;
        else if (std::strcmp(argv[i], "--minimize-test") == 0)
            state->minimizeTest = true;
        else if (std::strcmp(argv[i], "--vsync") == 0)
            state->vsync = true;
        else if (std::strcmp(argv[i], "--vk") == 0 || std::strcmp(argv[i], "--vulkan") == 0)
            state->api = NRI_GRAPHICS_API_VULKAN;
        else if (std::strcmp(argv[i], "--dx12") == 0 || std::strcmp(argv[i], "--d3d12") == 0)
            state->api = NRI_GRAPHICS_API_D3D12;
        else if (std::strcmp(argv[i], "--tab") == 0 && i + 1 < argc)
            state->startTab = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--trace-ui-changes") == 0)
        {
            state->traceUiChanges = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
            {
                g_traceFile = std::fopen(argv[++i], "w");
                if (!g_traceFile)
                    std::fprintf(stderr, "[UIDemo] cannot write %s; the trace goes to the console\n", argv[i]);
                else
                    std::printf("[UIDemo] UI trace: %s\n", std::filesystem::absolute(argv[i]).string().c_str());
            }
        }
        else if (std::strcmp(argv[i], "--no-ui-cache") == 0)
            state->uiCacheEnabled = false;
        else if (std::strcmp(argv[i], "--no-viewports") == 0)
            state->enableViewports = false;
    }

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->window = SDL_CreateWindow("NoxUI Demo Gallery (NoxRHI Backend)", state->width, state->height, SDL_WINDOW_RESIZABLE);
    if (!state->window)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    try
    {
#ifndef NDEBUG
        constexpr bool kEnableValidation = true;
#else
        constexpr bool kEnableValidation = false;
#endif
        nri::DeviceDesc devDesc{};
        devDesc.api = state->api;
        devDesc.window = getWindowHandle(state->window);
        devDesc.enableValidation = kEnableValidation;
        devDesc.shaderCacheDir = "shader_cache_uidemo";
        devDesc.logCallback = onNriLog;

        state->device = nri::createDevice(devDesc);
        state->swapchain = nri::createSwapchain(state->device, {
            static_cast<uint32_t>(state->width),
            static_cast<uint32_t>(state->height),
            state->vsync
        });

        state->resourceHeap = state->device.createDescriptorHeap({
            .type = NRI_DESCRIPTOR_HEAP_RESOURCE,
            .maxBufferDescriptors = 128,
            .maxImageDescriptors = 128
        });

        nri::SamplerDesc defaultSampler{};
        defaultSampler.magFilter = NRI_FILTER_LINEAR;
        defaultSampler.minFilter = NRI_FILTER_LINEAR;
        defaultSampler.addressModeU = NRI_SAMPLER_ADDRESS_CLAMP_TO_EDGE;
        defaultSampler.addressModeV = NRI_SAMPLER_ADDRESS_CLAMP_TO_EDGE;
        defaultSampler.addressModeW = NRI_SAMPLER_ADDRESS_CLAMP_TO_EDGE;
        defaultSampler.maxLod = 1000.0f;
        state->samplerHeap = state->device.createDescriptorHeap({
            .type = NRI_DESCRIPTOR_HEAP_SAMPLER,
            .maxSamplerDescriptors = 8,
            .samplers = { defaultSampler }
        });

        state->gpuProfiler = nri::createGpuProfiler(state->device, kFramesInFlight);
        state->profilerState.profiler = &state->gpuProfiler;

        nri::RGProfilerHooks hooks{};
        hooks.UserData = &state->profilerState;
        hooks.BeginGpuFrame = &profilerBeginFrame;
        hooks.RecordGpuFrameBegin = &profilerRecordFrameBegin;
        hooks.RecordGpuFrameEnd = &profilerRecordFrameEnd;
        hooks.BeginGpuZone = &profilerBeginGpuZone;
        hooks.EndGpuZone = &profilerEndGpuZone;

        state->renderGraph.SetLogCallback(onRgLog);
        state->renderGraph.SetValidationEnabled(kEnableValidation);
        state->renderGraph.SetProfilerHooks(hooks);
        state->renderGraph.Initialize(state->device, state->resourceHeap, kFramesInFlight);

        initNoxUI(*state);
        state->lastFrameTime = std::chrono::steady_clock::now();
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[UIDemo] Init Exception: %s\n", e.what());
        state->failed = true;
        *appstate = state.release();
        return SDL_APP_FAILURE;
    }

    *appstate = state.release();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    auto* state = static_cast<AppState*>(appstate);
    if (state->uiInitialized)
        NoxUI_ImplSDL3_ProcessEvent(event, state->uiContext, state->viewportManager.get());

    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
        event->window.windowID == SDL_GetWindowID(state->window))
        return SDL_APP_SUCCESS;
    if ((event->type == SDL_EVENT_WINDOW_RESIZED || event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event->type == SDL_EVENT_WINDOW_RESTORED) &&
        event->window.windowID == SDL_GetWindowID(state->window))
    {
        SDL_GetWindowSizeInPixels(state->window, &state->width, &state->height);
        state->needsResize = true;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    auto* state = static_cast<AppState*>(appstate);
    if (state->maxFrames >= 0 && static_cast<int>(state->frameNumber) >= state->maxFrames)
        return g_validationErrors > 0 ? SDL_APP_FAILURE : SDL_APP_SUCCESS;

    try
    {
        ++state->tickNumber;
        if (state->minimizeTest)
        {
            if (state->tickNumber == 3)
                SDL_MinimizeWindow(state->window);
            else if (state->tickNumber == 6)
            {
                SDL_RestoreWindow(state->window);
                SDL_GetWindowSizeInPixels(state->window, &state->width, &state->height);
                state->needsResize = true;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        const double dtMs = std::chrono::duration<double, std::milli>(now - state->lastFrameTime).count();
        const float dtSec = static_cast<float>(std::clamp(dtMs / 1000.0, 0.0001, 0.1));
        state->lastFrameTime = now;
        if (state->frameNumber > 0)
        {
            state->cpuHistoryMs[state->metricIndex] = dtMs;
            state->gpuHistoryMs[state->metricIndex] = state->profilerState.lastGpuFrameMs;
            state->uiHistoryMs[state->metricIndex] = state->lastUiMs;
            state->metricIndex = (state->metricIndex + 1) % kMetricWindow;
            state->metricCount = std::min(state->metricCount + 1, kMetricWindow);
        }

        double cpuAvgMs = 0.0;
        double gpuAvgMs = 0.0;
        double uiAvgMs = 0.0;
        if (state->metricCount > 0)
        {
            for (size_t i = 0; i < state->metricCount; ++i)
            {
                cpuAvgMs += state->cpuHistoryMs[i];
                gpuAvgMs += state->gpuHistoryMs[i];
                uiAvgMs += state->uiHistoryMs[i];
            }
            cpuAvgMs /= static_cast<double>(state->metricCount);
            gpuAvgMs /= static_cast<double>(state->metricCount);
            uiAvgMs /= static_cast<double>(state->metricCount);
        }
        const double fps = cpuAvgMs > 0.001 ? 1000.0 / cpuAvgMs : 0.0;

        if (state->resizeTest && state->frameNumber == 4)
        {
            SDL_SetWindowSize(state->window, 1152, 720);
            state->width = 1152;
            state->height = 720;
            state->needsResize = true;
        }

        const bool minimized = (SDL_GetWindowFlags(state->window) & SDL_WINDOW_MINIMIZED) != 0;
        if (minimized || state->width <= 0 || state->height <= 0)
        {
            state->lastFrameTime = std::chrono::steady_clock::now();
            SDL_Delay(16);
            return SDL_APP_CONTINUE;
        }

        if (state->needsResize)
        {
            state->device.waitIdle();
            state->renderGraph.ReleaseResources();
            state->swapchain.reset();
            state->swapchain = nri::createSwapchain(state->device, {
                static_cast<uint32_t>(state->width),
                static_cast<uint32_t>(state->height),
                state->vsync
            });
            state->needsResize = false;
        }

        state->device.beginFrame(state->frameNumber);

        const uint32_t frameSlot = state->frameNumber % kFramesInFlight;
        uint32_t imageIndex = 0;
        if (state->swapchain.acquireNextImage(frameSlot, imageIndex) == NRI_FRAME_RESULT_RESIZE_REQUIRED)
        {
            state->needsResize = true;
            return SDL_APP_CONTINUE;
        }

        const nri::Extent2D extent = state->swapchain.getExtent();
        if (extent.width == 0 || extent.height == 0)
        {
            state->needsResize = true;
            SDL_Delay(16);
            return SDL_APP_CONTINUE;
        }

        // Update telemetry labels (four times a second: readable, and the frames between can be idle)
        const auto telemetryNow = std::chrono::steady_clock::now();
        const bool telemetryDue = std::chrono::duration<double>(telemetryNow - state->lastTelemetry).count() >= 0.25;
        if (telemetryDue)
        {
            state->lastTelemetry = telemetryNow;
            const auto average = [](double& sum, uint32_t& count, double& shown)
            {
                if (count > 0)
                    shown = sum / count;
                sum = 0.0;
                count = 0;
            };
            average(state->uiDrawnSum, state->uiDrawnCount, state->shownUiDrawn);
            average(state->uiIdleSum, state->uiIdleCount, state->shownUiIdle);
            average(state->gpuRedrawSum, state->gpuRedrawCount, state->shownGpuRedraw);
            average(state->gpuCompositeSum, state->gpuCompositeCount, state->shownGpuComposite);
        }
        if (state->telemetryFpsLabel && telemetryDue)
        {
            char buf[96]{};
            // (the frame time is one frame to the next -- waiting for the GPU and the swapchain included; the UI's CPU work is
            // UpdateAndRender alone)
            const uint64_t frames = std::max<uint64_t>(state->frameNumber, 1);
            (void)uiAvgMs;
            std::snprintf(buf, sizeof(buf), "FPS: %.0f (frame %.2f ms) | UI CPU: drawn %.3f ms, idle %.3f ms | idle %.0f%%", fps, cpuAvgMs,
                          state->shownUiDrawn, state->shownUiIdle,
                          100.0 * static_cast<double>(state->uiContext.GetSkippedFrameCount()) / static_cast<double>(frames));
            state->telemetryFpsLabel->Text = buf;
        }
        if (state->telemetryGpuLabel && telemetryDue)
        {
            char buf[128]{};
            const uint64_t frames = std::max<uint64_t>(state->frameNumber, 1);
            (void)gpuAvgMs;
            std::snprintf(buf, sizeof(buf), "GPU: UI redraw %.3f ms, composite only %.3f ms | UI redrawn %.0f%% of frames", state->shownGpuRedraw,
                          state->shownGpuComposite, 100.0 * static_cast<double>(state->uiCacheRedraws) / static_cast<double>(frames));
            state->telemetryGpuLabel->Text = buf;
        }

        NoxUI_ImplSDL3_NewFrame(state->uiContext, state->viewportManager.get());
        const auto uiStart = std::chrono::steady_clock::now();
        state->uiContext.UpdateAndRender(
            state->uiDrawList,
            { static_cast<float>(extent.width), static_cast<float>(extent.height) },
            dtSec);
        state->lastUiMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uiStart).count();
        if (state->uiContext.WasLastFrameSkipped())
        {
            state->uiIdleSum += state->lastUiMs;
            ++state->uiIdleCount;
        }
        else
        {
            state->uiDrawnSum += state->lastUiMs;
            ++state->uiDrawnCount;
        }

        if (state->telemetryPrimsLabel)
        {
            char buf[96]{};
            std::snprintf(buf, sizeof(buf), "UI Primitives: %zu  |  Clips: %zu",
                          state->uiDrawList.GetPrimitives().size(),
                          state->uiDrawList.GetClipRects().size());
            state->telemetryPrimsLabel->Text = buf;
        }

        const NoxUI_NoxRHIDrawData mainDrawData = NoxUI_ImplNoxRHI_PrepareDrawData(state->uiDrawList, frameSlot);

        // The UI's own texture (UI cache): the swapchain's format and size; made again when the size changes.
        const bool useUiCache = state->uiCacheEnabled && state->api != NRI_GRAPHICS_API_NONE;
        if (useUiCache && (!state->uiCache.handle() || state->uiCacheExtent.width != extent.width || state->uiCacheExtent.height != extent.height))
        {
            state->device.waitIdle(); // (the old texture may still be read by a frame in flight)
            if (state->uiCache.handle())
            {
                state->resourceHeap.unregisterTexture(state->uiCacheSlot);
                state->uiCache.reset();
            }
            state->uiCache = state->device.createTexture({
                .width = extent.width,
                .height = extent.height,
                .arrayLayers = 1,
                .mipLevels = 1,
                .sampleCount = 1,
                .usage = NRI_TEXTURE_USAGE_COLOR_ATTACHMENT, // (sampled too: Vulkan adds eSampled, D3D12 render targets allow shader reads)
                .format = NRI_FORMAT_SURFACE
            });
            state->resourceHeap.registerTexture(state->uiCache, NRI_TEXTURE_USAGE_SHADER_RESOURCE);
            state->uiCacheSlot = state->uiCache.getDescriptorSlot();
            state->uiCacheExtent = extent;
            state->uiCacheValid = false;
        }
        // Redrawn when the draw list or the clear colour changed (an idle UpdateAndRender keeps the list's revision).
        const bool redrawUi = !useUiCache || !state->uiCacheValid || state->uiDrawList.GetRevision() != state->uiCacheRevision ||
                              state->clearColor != state->uiCacheClear;
        if (redrawUi)
            ++state->uiCacheRedraws;

        state->renderGraph.Reset({ state->frameNumber, extent, extent });

        if (redrawUi)
        {
            state->renderGraph.AddPass("NoxUIUpload", nri::RGPassFlags::NeverCull,
                [](nri::RGBuilder&) {},
                [mainDrawData](nri::RGPassContext& ctx)
                {
                    NoxUI_ImplNoxRHI_CopyData(ctx.Cmd(), mainDrawData);
                });
        }

        if (!useUiCache)
        {
            state->renderGraph.AddPass("NoxUIOverlay", nri::RGPassFlags::Raster,
                [&](nri::RGBuilder& b)
                {
                    b.SwapchainTarget(state->swapchain, imageIndex, NRI_LOAD_OP_CLEAR, NRI_STORE_OP_STORE,
                                      { state->clearColor[0], state->clearColor[1], state->clearColor[2], 1.0f });
                    b.SetRenderArea(extent, nri::RGViewport::None);
                },
                [mainDrawData](nri::RGPassContext& ctx)
                {
                    NoxUI_ImplNoxRHI_RenderDrawData(ctx.Cmd(), mainDrawData);
                });
        }
        else
        {
            const nri::RGTexture cache = state->renderGraph.ImportTexture("NoxUICache", state->uiCache);
            if (redrawUi)
            {
                // Over an opaque clear the UI pipeline's alpha (src + dst * (1 - src)) stays 1: the texture is opaque, and the
                // quad below copies it to the swapchain as it would have been drawn there.
                state->renderGraph.AddPass("NoxUIToCache", nri::RGPassFlags::Raster,
                    [&](nri::RGBuilder& b)
                    {
                        b.ColorTarget(cache, NRI_LOAD_OP_CLEAR, NRI_STORE_OP_STORE,
                                      { state->clearColor[0], state->clearColor[1], state->clearColor[2], 1.0f });
                        b.SetRenderArea(extent, nri::RGViewport::None);
                    },
                    [mainDrawData](nri::RGPassContext& ctx)
                    {
                        NoxUI_ImplNoxRHI_RenderDrawData(ctx.Cmd(), mainDrawData);
                    });
                state->uiCacheRevision = state->uiDrawList.GetRevision();
                state->uiCacheClear = state->clearColor;
                state->uiCacheValid = true;
            }

            // The texture on the swapchain: one textured quad over the whole target, through the same pipeline.
            const glm::vec2 size{ static_cast<float>(extent.width), static_cast<float>(extent.height) };
            state->uiCompositeList.Clear(size);
            state->uiCompositeList.AddTexturedQuad({ 0.0f, 0.0f }, size, static_cast<NoxUI::UITextureID>(state->uiCacheSlot));
            constexpr uint32_t kCompositeViewportSlot = 0x7FFFFFFFu; // (its own geometry buffers in the backend)
            const NoxUI_NoxRHIDrawData compositeData = NoxUI_ImplNoxRHI_PrepareDrawData(state->uiCompositeList, frameSlot, kCompositeViewportSlot);
            state->renderGraph.AddPass("NoxUICompositeUpload", nri::RGPassFlags::NeverCull,
                [](nri::RGBuilder&) {},
                [compositeData](nri::RGPassContext& ctx)
                {
                    NoxUI_ImplNoxRHI_CopyData(ctx.Cmd(), compositeData);
                });
            state->renderGraph.AddPass("NoxUIComposite", nri::RGPassFlags::Raster,
                [&](nri::RGBuilder& b)
                {
                    b.Read(cache, nri::RGTextureAccess::Sampled);
                    b.SwapchainTarget(state->swapchain, imageIndex, NRI_LOAD_OP_CLEAR, NRI_STORE_OP_STORE,
                                      { state->clearColor[0], state->clearColor[1], state->clearColor[2], 1.0f });
                    b.SetRenderArea(extent, nri::RGViewport::None);
                },
                [compositeData](nri::RGPassContext& ctx)
                {
                    NoxUI_ImplNoxRHI_RenderDrawData(ctx.Cmd(), compositeData);
                });
        }

        state->renderGraph.Compile();
        const auto submissions = state->renderGraph.Execute(frameSlot);
        // (Execute read back the GPU time of the frame that last ran in this slot: filed under what that frame did)
        if (state->slotUsed[frameSlot] && state->profilerState.lastGpuFrameMs > 0.0)
        {
            if (state->slotRedrewUi[frameSlot])
            {
                state->gpuRedrawSum += state->profilerState.lastGpuFrameMs;
                ++state->gpuRedrawCount;
            }
            else
            {
                state->gpuCompositeSum += state->profilerState.lastGpuFrameMs;
                ++state->gpuCompositeCount;
            }
        }
        state->slotRedrewUi[frameSlot] = redrawUi;
        state->slotUsed[frameSlot] = true;
        for (size_t s = 0; s < submissions.size(); ++s)
        {
            const auto& sub = submissions[s];
            if (s + 1 == submissions.size())
                state->device.submitCommandBuffers(sub.CommandBuffers, state->swapchain.handle(), frameSlot, imageIndex, sub.Waits);
            else
                state->device.submit(sub.Queue, sub.CommandBuffers, sub.Waits, sub.Signals);
        }

        if (state->viewportManager)
            state->viewportManager->UpdateAndRender(dtSec);

        if (state->swapchain.present(frameSlot, imageIndex) == NRI_FRAME_RESULT_RESIZE_REQUIRED)
            state->needsResize = true;

        ++state->frameNumber;

        if (!state->screenshotPath.empty() &&
            ((state->maxFrames > 0 && static_cast<int>(state->frameNumber) == state->maxFrames) ||
             (state->maxFrames < 0 && state->frameNumber == 3)))
        {
            captureScreenshotIfRequested(*state, mainDrawData, extent);
        }
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[UIDemo] Exception: %s\n", e.what());
        state->failed = true;
        return SDL_APP_FAILURE;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    auto* state = static_cast<AppState*>(appstate);
    if (state)
    {
        if (state->device.handle())
        {
            state->device.waitIdle();
            state->renderGraph.ReleaseResources();
        }
        shutdownNoxUI(*state);
        state->uiCache.reset();
        state->gpuProfiler.reset();
        state->samplerHeap.reset();
        state->resourceHeap.reset();
        state->swapchain.reset();
        state->device.reset();
        if (state->window)
            SDL_DestroyWindow(state->window);

        std::printf("[UIDemo] %u frames: UI idle (layout and paint skipped) in %llu, UI redrawn on the GPU in %llu%s\n", state->frameNumber,
                    static_cast<unsigned long long>(state->uiContext.GetSkippedFrameCount()),
                    static_cast<unsigned long long>(state->uiCacheRedraws), state->uiCacheEnabled ? "" : " (UI cache off)");
        if (!g_traceCounts.empty())
        {
            std::vector<std::pair<uint64_t, std::string>> top;
            for (const auto& [name, count] : g_traceCounts)
                top.emplace_back(count, name);
            std::sort(top.rbegin(), top.rend());
            std::printf("[UIDemo] trace: %llu frames had changes; the elements changed most often (in how many of them):\n",
                        static_cast<unsigned long long>(g_traceFrames));
            for (size_t i = 0; i < std::min<size_t>(top.size(), 12); ++i)
                std::printf("[UIDemo]   %6llu  %s\n", static_cast<unsigned long long>(top[i].first), top[i].second.c_str());
        }
        if (g_traceFile)
            std::fclose(g_traceFile);
        using Reason = NoxUI::UIContext::DrawReason;
        const auto count = [&](Reason r) { return static_cast<unsigned long long>(state->uiContext.GetDrawReasonCount(r)); };
        std::printf("[UIDemo] UI frames drawn because of: input / changes %llu, state changed %llu, incomplete fingerprint %llu%s%s, "
                    "draw list changed %llu\n", count(Reason::Changed), count(Reason::StateChanged), count(Reason::Incomplete),
                    count(Reason::Incomplete) ? " -- class " : "", state->uiContext.GetIncompleteClassName(), count(Reason::DrawListChanged));
        const bool hadFailure = state->failed || result == SDL_APP_FAILURE || g_validationErrors > 0;
        delete state;
        if (hadFailure)
        {
            std::fprintf(stderr, "[UIDemo] Failed with %d validation/NRI errors\n", g_validationErrors);
            return;
        }
    }
    std::printf("[UIDemo] Clean exit.\n");
}
