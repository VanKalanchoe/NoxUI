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

static void onUILog(NoxUI::LogLevel level, std::string_view message)
{
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

        // Update telemetry labels
        if (state->telemetryFpsLabel)
        {
            char buf[96]{};
            // (the frame time is one frame to the next -- waiting for the GPU and the swapchain included; the UI's CPU work is
            // UpdateAndRender alone)
            std::snprintf(buf, sizeof(buf), "FPS: %.1f (frame: %.2f ms) | UI CPU: %.3f ms", fps, cpuAvgMs, uiAvgMs);
            state->telemetryFpsLabel->Text = buf;
        }
        if (state->telemetryGpuLabel)
        {
            char buf[96]{};
            std::snprintf(buf, sizeof(buf), "GPU Frame: %.3f ms (instant: %.3f ms)", gpuAvgMs, state->profilerState.lastGpuFrameMs);
            state->telemetryGpuLabel->Text = buf;
        }

        NoxUI_ImplSDL3_NewFrame(state->uiContext, state->viewportManager.get());
        const auto uiStart = std::chrono::steady_clock::now();
        state->uiContext.UpdateAndRender(
            state->uiDrawList,
            { static_cast<float>(extent.width), static_cast<float>(extent.height) },
            dtSec);
        state->lastUiMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uiStart).count();

        if (state->telemetryPrimsLabel)
        {
            char buf[96]{};
            std::snprintf(buf, sizeof(buf), "UI Primitives: %zu  |  Clips: %zu",
                          state->uiDrawList.GetPrimitives().size(),
                          state->uiDrawList.GetClipRects().size());
            state->telemetryPrimsLabel->Text = buf;
        }

        const NoxUI_NoxRHIDrawData mainDrawData = NoxUI_ImplNoxRHI_PrepareDrawData(state->uiDrawList, frameSlot);

        state->renderGraph.Reset({ state->frameNumber, extent, extent });

        state->renderGraph.AddPass("NoxUIUpload", nri::RGPassFlags::NeverCull,
            [](nri::RGBuilder&) {},
            [mainDrawData](nri::RGPassContext& ctx)
            {
                NoxUI_ImplNoxRHI_CopyData(ctx.Cmd(), mainDrawData);
            });

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

        state->renderGraph.Compile();
        const auto submissions = state->renderGraph.Execute(frameSlot);
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
        state->gpuProfiler.reset();
        state->samplerHeap.reset();
        state->resourceHeap.reset();
        state->swapchain.reset();
        state->device.reset();
        if (state->window)
            SDL_DestroyWindow(state->window);

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
