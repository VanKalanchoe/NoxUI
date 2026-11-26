// UIFrameReplay -- the demo gallery driven by a seeded script of input on a fixed frame clock, headless (no GPU, no window): every frame's
// UIDrawList (primitives and clip rects, byte for byte) is hashed. Two builds that must draw the same thing (an optimization of layout,
// paint or frame skipping) give the same hashes:
//
//   UIFrameReplay --record frames.txt      (the reference build)
//   UIFrameReplay --compare frames.txt     (the build under test: the first frame that differs, with what differs; exit code 1)
//   UIFrameReplay --verify                 (UIContext's own verify mode: every frame also drawn the full way and compared)
//   UIFrameReplay --frames N --seed S       (also prints UpdateAndRender's average and worst time)
//
// The script: tab switches, pointer walks and jumps, clicks, drags, right clicks, wheel, keys and typed text, Escape, idle stretches,
// toasts and window resizes -- the same sequence every run for one seed.

#include <NoxUI/NoxUI.h>
#include <NoxUI/Demo/UIDemoGallery.h>
#include <NoxUI/Layout/UITabPanel.h>
#include <NoxUI/Rendering/UIGlyphCache.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace NoxUI;

namespace
{
    uint64_t g_TimeMs = 0;
    uint64_t FrameClock() { return g_TimeMs; }

    uint64_t Hash(const void* data, size_t bytes, uint64_t hash)
    {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < bytes; ++i)
            hash = (hash ^ p[i]) * 1099511628211ull;
        return hash;
    }

    uint64_t HashDrawList(const UIDrawList& list)
    {
        uint64_t h = 1469598103934665603ull;
        const auto& prims = list.GetPrimitives();
        const auto& clips = list.GetClipRects();
        const size_t counts[2] = { prims.size(), clips.size() };
        h = Hash(counts, sizeof(counts), h);
        if (!prims.empty())
            h = Hash(prims.data(), prims.size() * sizeof(UIPrimitive), h);
        if (!clips.empty())
            h = Hash(clips.data(), clips.size() * sizeof(UIClipRect), h);
        return h;
    }

    UITabPanel* FindTabPanel(UIElement* element)
    {
        if (!element)
            return nullptr;
        if (auto* tabs = dynamic_cast<UITabPanel*>(element))
            return tabs;
        for (const auto& child : element->GetChildren())
            if (UITabPanel* found = FindTabPanel(child.get()))
                return found;
        return nullptr;
    }

    std::filesystem::path FindAssets(const char* argv0)
    {
        for (std::filesystem::path dir = std::filesystem::absolute(argv0).parent_path(); !dir.empty(); dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / "assets/fonts/OpenSans-Regular.ttf"))
                return dir / "assets";
            if (dir == dir.parent_path())
                break;
        }
#if defined(NOXUI_SOURCE_DIR)
        return std::filesystem::path(NOXUI_SOURCE_DIR) / "assets";
#else
        return "assets";
#endif
    }
}

int main(int argc, char** argv)
{
    int frames = 3000;
    uint32_t seed = 1;
    std::string recordPath, comparePath;
    bool verify = false, incremental = true;
    int dumpFrame = -1;
    std::string dumpPath;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--record") && i + 1 < argc)
            recordPath = argv[++i];
        else if (!std::strcmp(argv[i], "--compare") && i + 1 < argc)
            comparePath = argv[++i];
        else if (!std::strcmp(argv[i], "--verify"))
            verify = true;
        else if (!std::strcmp(argv[i], "--no-incremental"))
            incremental = false;
        else if (!std::strcmp(argv[i], "--dump-frame") && i + 2 < argc)
        {
            dumpFrame = std::atoi(argv[++i]);
            dumpPath = argv[++i];
        }
    }

    const std::filesystem::path assets = FindAssets(argv[0]);
    SetTimeSource(&FrameClock);
    UIFont::SetDefaultFontPath(assets / "fonts/OpenSans-Regular.ttf");
    UIFont::SetAtlasUploadCallback([](uint32_t, uint32_t, const void*) { return static_cast<UITextureID>(2); }); // (as a backend: text drawn)
    UIGlyphCache::Get().SetTextureID(1);

    UIContext context;
    Ref<UITheme> theme = UITheme::CreateDefault();
    theme->LoadFromFile(assets / "themes/DefaultDark.nstyle");
    context.SetTheme(theme);
    auto dock = MakeUI<UIDockSpace>();
    dock->SetContext(&context);
    context.SetRoot(dock);
    dock->AddPanel("NoxUI Component Gallery", CreateUIDemoGallery(context), UIDockDropZone::Center);
    // A panel whose label changes every frame from its OnSync, as a telemetry readout does.
    auto telemetry = MakeUI<UIScrollView>();
    auto counter = MakeUI<UILabel>("Frame 0");
    UILabel* counterPtr = counter.get();
    int frameNumber = 0;
    counter->OnSync = [counterPtr, &frameNumber]() { counterPtr->Text = "Frame " + std::to_string(frameNumber / 7); };
    telemetry->AddChild(counter);
    dock->AddPanel("Telemetry", telemetry, UIDockDropZone::Right, "NoxUI Component Gallery", 0.27f);
    context.SetVerifyMode(verify);
    context.SetIncremental(incremental);

    std::vector<uint64_t> reference;
    if (!comparePath.empty())
    {
        std::ifstream in(comparePath);
        unsigned long long h;
        while (in >> std::hex >> h)
            reference.push_back(h);
        if (reference.empty())
        {
            std::fprintf(stderr, "[UIFrameReplay] no hashes in %s\n", comparePath.c_str());
            return 2;
        }
    }
    std::ofstream record;
    if (!recordPath.empty())
        record.open(recordPath);

    std::mt19937 rng(seed);
    auto chance = [&](int oneIn) { return std::uniform_int_distribution<int>(0, oneIn - 1)(rng) == 0; };
    auto uniform = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };

    UIVec2 viewport{ 1600.0f, 900.0f };
    UIVec2 pointer{ 400.0f, 300.0f };
    bool buttonDown = false;
    int releaseIn = 0;
    int idleFor = 0;
    const float dt = 1.0f / 60.0f;
    UIDrawList drawList;
    double totalMs = 0.0, worstMs = 0.0;
    size_t primitivesByType[8]{};
    int mismatches = 0;
    const char* typed[] = { "a", "Nox", "1", "-2.5", " ", "x" };
    const UIKey keys[] = { UIKey::Left, UIKey::Right, UIKey::Up, UIKey::Down, UIKey::Tab, UIKey::Return, UIKey::Backspace, UIKey::Home,
                           UIKey::End, UIKey::Space, UIKey::Delete, UIKey::PageDown };

    for (frameNumber = 0; frameNumber < frames; ++frameNumber)
    {
        g_TimeMs = static_cast<uint64_t>(frameNumber) * 1000ull / 60ull;

        // --- input for this frame ---
        if (frameNumber % 400 == 0)
        {
            if (UITabPanel* tabs = FindTabPanel(dock.get()))
                tabs->SetActiveTab(static_cast<size_t>(frameNumber / 400) % 5);
        }
        if (frameNumber == 1700)
            viewport = { 1280.0f, 760.0f };
        if (frameNumber == 2300)
            viewport = { 1600.0f, 900.0f };
        if (frameNumber % 900 == 450)
            context.PushToast("Replay toast " + std::to_string(frameNumber));

        if (idleFor > 0)
            --idleFor;
        else if (chance(90))
            idleFor = std::uniform_int_distribution<int>(20, 90)(rng); // (no input: idle frames)
        else
        {
            if (chance(25))
                pointer = { uniform(0.0f, viewport.x), uniform(0.0f, viewport.y) };
            else
                pointer = glm::clamp(pointer + UIVec2{ uniform(-14.0f, 14.0f), uniform(-10.0f, 10.0f) }, UIVec2{ 0.0f }, viewport - 1.0f);
            context.OnPointerMove(pointer);

            if (buttonDown && --releaseIn <= 0)
            {
                context.OnPointerUp(pointer, 1);
                buttonDown = false;
            }
            else if (!buttonDown && chance(18))
            {
                context.OnPointerDown(pointer, 1);
                buttonDown = true;
                releaseIn = chance(4) ? std::uniform_int_distribution<int>(4, 30)(rng) : 1; // (a click, or a drag)
            }
            if (!buttonDown && chance(150))
            {
                context.OnPointerDown(pointer, 3);
                context.OnPointerUp(pointer, 3);
            }
            if (chance(40))
                context.OnScroll({ 0.0f, chance(2) ? 1.0f : -1.0f });
            if (chance(45))
                context.OnKeyDown(keys[rng() % (sizeof(keys) / sizeof(*keys))], chance(6) ? UIMod::Shift : 0);
            if (chance(35))
                context.OnTextInput(typed[rng() % (sizeof(typed) / sizeof(*typed))]);
            if (chance(120))
                context.OnKeyDown(UIKey::Escape, 0);
        }

        // --- the frame ---
        const auto start = std::chrono::steady_clock::now();
        context.UpdateAndRender(drawList, viewport, dt);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (frameNumber >= 30)
        {
            totalMs += ms;
            worstMs = std::max(worstMs, ms);
        }

        for (const UIPrimitive& prim : drawList.GetPrimitives())
            ++primitivesByType[prim.type < 8 ? prim.type : 7];
        if (frameNumber == dumpFrame)
        {
            std::ofstream dump(dumpPath);
            for (const UIClipRect& clip : drawList.GetClipRects())
                dump << "clip " << clip.rect.x << ' ' << clip.rect.y << ' ' << clip.rect.z << ' ' << clip.rect.w << "\n";
            for (const UIPrimitive& p : drawList.GetPrimitives())
                dump << "prim t" << p.type << " c" << p.clipIndex << " b " << p.bounds.x << ' ' << p.bounds.y << ' ' << p.bounds.z << ' ' << p.bounds.w
                     << " col " << p.color0.r << ' ' << p.color0.g << ' ' << p.color0.b << ' ' << p.color0.a << " uv " << p.uvOrP0P1.x << ' '
                     << p.uvOrP0P1.y << ' ' << p.uvOrP0P1.z << ' ' << p.uvOrP0P1.w << " bc " << p.borderColor.a << " sh " << p.shadowColor.a << "\n";
        }
        const uint64_t hash = HashDrawList(drawList);
        if (record.is_open())
            record << std::hex << hash << "\n";
        if (!reference.empty() && frameNumber < static_cast<int>(reference.size()) && reference[frameNumber] != hash)
        {
            if (++mismatches <= 5)
                std::fprintf(stderr, "[UIFrameReplay] frame %d differs from the reference (%zu primitives, %zu clips)\n", frameNumber,
                             drawList.GetPrimitives().size(), drawList.GetClipRects().size());
        }
    }

    std::printf("[UIFrameReplay] %d frames (seed %u): UpdateAndRender avg %.3f ms, worst %.3f ms; primitives drawn: %zu rects, %zu glyphs, "
                "%zu MSDF glyphs, %zu curves, %zu images\n", frames, seed, totalMs / std::max(1, frames - 30), worstMs, primitivesByType[0],
                primitivesByType[4], primitivesByType[2], primitivesByType[3], primitivesByType[1]);
    std::printf("[UIFrameReplay] idle frames skipped: %llu of %d", static_cast<unsigned long long>(context.GetSkippedFrameCount()), frames);
    if (verify)
        std::printf("; verify: %llu idle frames drawn and compared, %llu differed", static_cast<unsigned long long>(context.GetVerifiedFrameCount()),
                    static_cast<unsigned long long>(context.GetVerifyFailureCount()));
    std::printf("\n");
    if (verify && context.GetVerifyFailureCount() > 0)
        return 1;
    if (!reference.empty())
    {
        if (static_cast<int>(reference.size()) != frames)
            std::printf("[UIFrameReplay] note: the reference has %zu frames\n", reference.size());
        std::printf("[UIFrameReplay] %s: %d of %d frames differ from %s\n", mismatches ? "FAILED" : "identical", mismatches, frames,
                    comparePath.c_str());
        return mismatches ? 1 : 0;
    }
    return 0;
}
