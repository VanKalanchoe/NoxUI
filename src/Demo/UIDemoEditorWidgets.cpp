// The gallery's "Editor Widgets" tab: the widgets and hooks taken from the Nox editor, each working live.
#include "NoxUI/Demo/UIDemoGallery.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "NoxUI/Core/UICommandList.h"
#include "NoxUI/Core/UIContext.h"
#include "NoxUI/Core/UIPlatform.h"
#include "NoxUI/Core/UITheme.h"
#include "NoxUI/Layout/UIDrawerHost.h"
#include "NoxUI/Layout/UIGroupBox.h"
#include "NoxUI/Layout/UILayout.h"
#include "NoxUI/Rendering/UIDrawList.h"
#include "NoxUI/Rendering/UIFont.h"
#include "NoxUI/Rendering/UIIcons.h"
#include "NoxUI/Widgets/UIButton.h"
#include "NoxUI/Widgets/UIChannelCurves.h"
#include "NoxUI/Widgets/UICheckBox.h"
#include "NoxUI/Widgets/UICurveEditor.h"
#include "NoxUI/Widgets/UIIconButton.h"
#include "NoxUI/Widgets/UILabel.h"
#include "NoxUI/Widgets/UILevelMeter.h"
#include "NoxUI/Widgets/UIMenu.h"
#include "NoxUI/Widgets/UIModal.h"
#include "NoxUI/Widgets/UIPanZoom.h"
#include "NoxUI/Widgets/UIPropertyRows.h"
#include "NoxUI/Widgets/UIScrollView.h"
#include "NoxUI/Widgets/UISearchPicker.h"

namespace NoxUI
{
    namespace
    {
        float Seconds() { return static_cast<float>(GetTimeMs() % 1000000ull) / 1000.0f; }

        Ref<UILabel> Hint(const std::string& text)
        {
            Ref<UILabel> label(new UILabel(text, 13.0f));
            label->WordWrap = true;
            label->WidthMode = UISizeMode::Fill;
            label->UseColorOverride = true;
            label->ColorOverride = ThemeColor("TextMuted", { 0.58f, 0.62f, 0.69f, 1.0f });
            return label;
        }

        // A canvas with a UIPanZoom: the wheel zooms around the pointer, right-drag pans, F frames the boxes, Home goes back to 100 %.
        class DemoCanvas final : public UIElement
        {
        public:
            void HashState(UIStateHasher& h) const override
            {
                UIElement::HashState(h);
                h(m_View.Pan, m_View.Zoom, m_View.MinZoom, m_View.MaxZoom, m_Panning, m_Framed, m_Last);
            }
            const std::type_info& StateHashClass() const override { return typeid(DemoCanvas); }
            DemoCanvas()
            {
                HitTestMode = UIHitTestMode::Self;
                Focusable = true;
                ClipChildren = true;
                WidthMode = UISizeMode::Fill;
                HeightMode = UISizeMode::Fixed;
                FixedSize = { 0.0f, 220.0f };
                Tooltip = "Wheel: zoom around the pointer. Right-drag: pan. Click, then F: frame the boxes; Home: 100 %.";
            }

            bool OnMouseWheel(const UIPointerEvent& e) override
            {
                m_View.ZoomAt(GetBounds(), e.Position, e.ScrollDelta.y);
                return true;
            }
            bool OnPointerDown(const UIPointerEvent& e) override
            {
                if (UIContext* context = UIContext::GetCurrent())
                {
                    context->CancelPendingDrag();
                    context->SetFocus(this);
                }
                m_Panning = e.Button == UIMouseButton::Right || e.Button == UIMouseButton::Middle;
                m_Last = e.Position;
                return true;
            }
            bool OnPointerMove(const UIPointerEvent& e) override
            {
                if (m_Panning)
                    m_View.Pan += e.Position - m_Last;
                m_Last = e.Position;
                return m_Panning;
            }
            bool OnPointerUp(const UIPointerEvent&) override
            {
                m_Panning = false;
                return true;
            }
            bool OnKeyDown(const UIKeyEvent& e) override
            {
                if (e.KeyCode == static_cast<int>(UIKey::F))
                {
                    Frame();
                    return true;
                }
                if (e.KeyCode == static_cast<int>(UIKey::Home))
                {
                    m_View.ResetZoom(GetBounds());
                    return true;
                }
                return false;
            }

        protected:
            void OnPaint(UIDrawList& d, const UITheme& theme) override
            {
                const UIRect b = GetBounds();
                if (!m_Framed && b.Width() > 10.0f)
                {
                    Frame();
                    m_Framed = true;
                }
                d.AddRoundedRect(b.Min, b.Max, theme.GetToken("Background", { 0.063f, 0.075f, 0.106f, 1.0f }), glm::vec4(4.0f));
                d.PushClipRect(b.Min, b.Max);
                m_View.PaintGrid(d, b);
                const UIColor box = theme.GetToken("Surface2", { 0.16f, 0.19f, 0.27f, 1.0f });
                const UIColor border = theme.GetToken("Accent", { 0.26f, 0.53f, 0.96f, 1.0f });
                const char* names[3] = { "Input", "Blend", "Output" };
                for (int i = 0; i < 3; ++i)
                {
                    const UIVec2 min = m_View.ToScreen(b, { i * 220.0f, (i % 2) * 90.0f });
                    const UIVec2 max = m_View.ToScreen(b, { i * 220.0f + 160.0f, (i % 2) * 90.0f + 80.0f });
                    d.AddRoundedRect(min, max, box, glm::vec4(6.0f * m_View.Zoom), 1.5f, border);
                    d.AddText(names[i], nullptr, min + UIVec2{ 8.0f * m_View.Zoom }, 14.0f * m_View.Zoom, theme.GetToken("Text", { 0.92f, 0.93f, 0.95f, 1.0f }));
                }
                m_View.PaintZoomLabel(d, b, theme);
                d.PopClipRect();
            }

        private:
            void Frame() { m_View.Fit(GetBounds(), { 0.0f, 0.0f }, { 600.0f, 170.0f }, 0.2f, 1.5f, 20.0f); }

            UIPanZoom m_View;
            bool m_Panning = false, m_Framed = false;
            UIVec2 m_Last{ 0.0f };
        };

        // A square painted in a theme token's colour (UIThemeToken: read from the theme each time it is drawn).
        class DemoSwatch final : public UIElement
        {
        public:
            void HashState(UIStateHasher& h) const override
            {
                UIElement::HashState(h);
            }
            const std::type_info& StateHashClass() const override { return typeid(DemoSwatch); }
            DemoSwatch()
            {
                WidthMode = UISizeMode::Fixed;
                HeightMode = UISizeMode::Fixed;
                FixedSize = { 48.0f, 48.0f };
            }

        protected:
            UIVec2 ComputeDesiredSize() override { return FixedSize; }
            void OnPaint(UIDrawList& d, const UITheme&) override
            {
                static constexpr UIThemeToken kSwatch{ "DemoSwatch", { 0.85f, 0.45f, 0.20f, 1.0f } };
                d.AddRoundedRect(GetBounds().Min, GetBounds().Max, kSwatch, glm::vec4(6.0f));
            }
        };

        struct DemoLight
        {
            bool CastShadows = true;
            float Intensity = 25.0f;
            int Samples = 4;
            int Mode = 0;
            glm::vec3 Color{ 1.0f, 0.96f, 0.88f };
            std::string Name = "Key Light";
        };

        struct DemoTrackKey
        {
            float Time;
            float Value[3];
        };
    }

    UIDemoPanel CreateEditorWidgetsDemoPanel(UIContext& context, const std::function<void(const std::string&)>& onStatus)
    {
        const auto status = [onStatus](const std::string& message)
        {
            if (onStatus)
                onStatus(message);
        };

        auto page = MakeUI<UIScrollView>();
        page->WidthMode = UISizeMode::Fill;
        page->HeightMode = UISizeMode::Fill;
        page->Padding = UIThickness::All(12.0f);
        page->ChildSpacing = 12.0f;
        auto grid = MakeUI<UIGridBox>(2, 12.0f, 12.0f);
        grid->WidthMode = UISizeMode::Fill;
        grid->MinColumnWidth = 340.0f;

        // --- UIIconButton ---------------------------------------------------------------------------------------------------------
        {
            auto card = MakeUI<UIGroupBox>("UIIconButton");
            auto row = MakeUI<UIStackBox>(UIOrientation::Horizontal, 6.0f);
            Ref<UIIconButton> play(new UIIconButton([](UIDrawList& d, const UIRect& b, const UIColor& c)
            {
                Icons::DrawChevron(d, b.Center(), 10.0f, false, c, 2.0f);
            }, "A click (OnClick)", [status]() { status("Icon button clicked"); }));
            Ref<UIIconButton> toggle(new UIIconButton([](UIDrawList& d, const UIRect& b, const UIColor& c)
            {
                Icons::DrawCheckmark(d, b.Min + UIVec2{ 6.0f }, b.Max - UIVec2{ 6.0f }, c);
            }, "A toggle (Active: accent fill)"));
            toggle->Toggle = true;
            toggle->OnToggled = [status](bool on) { status(on ? "Toggle on" : "Toggle off"); };
            Ref<UIIconButton> tint(new UIIconButton({}, "A toggle with the Tint look (a chevron that opens something)"));
            UIIconButton* tintRaw = tint.get();
            tint->DrawIcon = [tintRaw](UIDrawList& d, const UIRect& b, const UIColor& c) { Icons::DrawChevron(d, b.Center(), 8.0f, tintRaw->Active, c, 1.7f); };
            tint->Toggle = true;
            tint->ActiveStyle = UIIconButton::ActiveLook::Tint;
            Ref<UIIconButton> more(new UIIconButton([](UIDrawList& d, const UIRect& b, const UIColor& c)
            {
                Icons::DrawMoreDots(d, b.Center(), 12.0f, c, true);
            }, "A menu under the button (OnClickAt)"));
            more->OnClickAt = [status](const UIRect& bounds)
            {
                if (UIContext* ctx = UIContext::GetCurrent())
                    UIMenuPopup::ShowPopupMenu(*ctx, bounds, { { "Rename", "", [status]() { status("Rename"); } }, { "Delete", "", [status]() { status("Delete"); } } }, true);
            };
            Ref<UIIconButton> disabled(new UIIconButton([](UIDrawList& d, const UIRect& b, const UIColor& c)
            {
                Icons::DrawChevron(d, b.Center(), 10.0f, false, c, 2.0f);
            }, "Disabled"));
            disabled->SetEnabled(false);
            for (const Ref<UIIconButton>& button : { play, toggle, tint, more, disabled })
                row->AddChild(button);
            card->AddContent(row);
            card->AddContent(Hint("Icon from a texture or a DrawIcon function, drawn in the state's theme colour. Enter / Space click the focused one."));
            grid->AddChild(card);
        }

        // --- UILevelMeter ---------------------------------------------------------------------------------------------------------
        {
            auto card = MakeUI<UIGroupBox>("UILevelMeter");
            auto row = MakeUI<UIStackBox>(UIOrientation::Horizontal, 12.0f);
            const auto levels = []()
            {
                const float t = Seconds();
                const float l = 0.5f + 0.5f * std::sin(t * 2.3f), r = 0.5f + 0.5f * std::sin(t * 1.7f + 1.0f);
                return UILevelMeter::Values{ l * 0.6f, r * 0.6f, std::min(l * 1.1f, 1.2f), std::min(r * 1.1f, 1.2f) };
            };
            Ref<UILevelMeter> vertical(new UILevelMeter(UIOrientation::Vertical));
            vertical->Source = levels;
            vertical->HeightMode = UISizeMode::Fixed;
            vertical->FixedSize = { 22.0f, 120.0f };
            Ref<UILevelMeter> horizontal(new UILevelMeter(UIOrientation::Horizontal));
            horizontal->Source = levels;
            horizontal->MaxDb = 0.0f;
            horizontal->LoudDb = horizontal->ClipDb = 0.0f;
            horizontal->HeightMode = UISizeMode::Fixed;
            horizontal->FixedSize = { 0.0f, 32.0f };
            horizontal->VAlign = UIAlignment::Center;
            row->AddChild(vertical);
            row->AddChild(horizontal);
            row->WidthMode = UISizeMode::Fill;
            card->AddContent(row);
            card->AddContent(Hint("Left: RMS filled, the peak a line, dB ticks (a mixer strip). Right: the peak with its dB written on it. Rest the pointer for the dB."));
            grid->AddChild(card);
        }

        // --- UICurveEditor (FitKeys) -----------------------------------------------------------------------------------------------
        {
            auto card = MakeUI<UIGroupBox>("UICurveEditor -- FitKeys");
            auto keys = std::make_shared<std::vector<UIVec2>>(std::vector<UIVec2>{ { -2.0f, 0.0f }, { 0.0f, 10.0f }, { 3.0f, 4.0f } });
            Ref<UICurveEditor> curve(new UICurveEditor({}));
            curve->RangeMode = UICurveEditor::Range::FitKeys;
            curve->AddOnDoubleClick = true;
            curve->Keys = [keys]() { return keys.get(); };
            curve->Evaluator = [keys](float x)
            {
                const std::vector<UIVec2>& k = *keys;
                if (k.empty())
                    return 0.0f;
                if (x <= k.front().x)
                    return k.front().y;
                for (size_t i = 1; i < k.size(); ++i)
                    if (x <= k[i].x)
                        return k[i - 1].y + (k[i].y - k[i - 1].y) * (x - k[i - 1].x) / std::max(k[i].x - k[i - 1].x, 1e-6f);
                return k.back().y;
            };
            curve->OnKeyCountChanged = [status, keys]() { status(std::to_string(keys->size()) + " keys"); };
            curve->HeightMode = UISizeMode::Fixed;
            curve->FixedSize = { 0.0f, 140.0f };
            curve->Padding = UIThickness::All(4.0f);
            card->AddContent(curve);
            card->AddContent(Hint("Drag a key (it stays between its neighbours); double-click to add one; right-click a key to remove it. The view follows the keys."));
            grid->AddChild(card);
        }

        // --- UIChannelCurves -------------------------------------------------------------------------------------------------------
        {
            auto card = MakeUI<UIGroupBox>("UIChannelCurves");
            auto track = std::make_shared<std::vector<DemoTrackKey>>(std::vector<DemoTrackKey>{
                { 0.0f, { 0.0f, 1.0f, -1.0f } }, { 1.5f, { 2.0f, 0.5f, 0.0f } }, { 3.0f, { 1.0f, -0.5f, 1.5f } } });
            auto selected = std::make_shared<int>(-1);
            Ref<UIChannelCurves> curves(new UIChannelCurves());
            curves->ChannelCount = []() { return 3; };
            curves->KeyCount = [track]() { return static_cast<int>(track->size()); };
            curves->KeyTime = [track](int k) { return (*track)[k].Time; };
            curves->KeyValue = [track](int k, int c) { return (*track)[k].Value[c]; };
            curves->Evaluate = [track](float t, int c)
            {
                const std::vector<DemoTrackKey>& k = *track;
                if (t <= k.front().Time)
                    return k.front().Value[c];
                for (size_t i = 1; i < k.size(); ++i)
                    if (t <= k[i].Time)
                        return k[i - 1].Value[c] + (k[i].Value[c] - k[i - 1].Value[c]) * (t - k[i - 1].Time) / std::max(k[i].Time - k[i - 1].Time, 1e-6f);
                return k.back().Value[c];
            };
            curves->SetKey = [track](int k, int c, float t, float v) { (*track)[k].Time = t; (*track)[k].Value[c] = v; };
            curves->Duration = []() { return 4.0f; };
            curves->Playhead = []() { return std::fmod(Seconds(), 4.0f); };
            curves->SelectedKey = [selected]() { return *selected; };
            curves->SnapTime = [](float t) { return std::round(t * 10.0f) / 10.0f; };
            curves->OnSelectKey = [selected, status](int k) { *selected = k; status("Key " + std::to_string(k + 1) + " selected"); };
            curves->OnChanged = [track]()
            {
                std::sort(track->begin(), track->end(), [](const DemoTrackKey& a, const DemoTrackKey& b) { return a.Time < b.Time; });
            };
            card->AddContent(curves);
            card->AddContent(Hint("X / Y / Z of one track over 4 s. Click a key's dot, drag it up or down; hold Shift to move its time too (0.1 s steps)."));
            grid->AddChild(card);
        }

        // --- UIPropertyRows ------------------------------------------------------------------------------------------------------
        {
            auto card = MakeUI<UIGroupBox>("UIPropertyRows");
            auto light = std::make_shared<DemoLight>();
            auto body = MakeUI<UIStackBox>(UIOrientation::Vertical, 6.0f);
            body->WidthMode = UISizeMode::Fill;
            auto expanded = std::make_shared<std::unordered_map<std::string, bool>>();
            UIPropertyRows rows(body, &context);
            UIPropertyRows section = rows.Section("Spot Light", true, expanded.get());
            section.String("Name", &light->Name, "Light name");
            section.Check("Cast Shadows", [light]() { return light->CastShadows; }, [light](bool v) { light->CastShadows = v; });
            Ref<UIElement> samples = section.Int("Shadow Samples", [light]() { return light->Samples; }, [light](int v) { light->Samples = v; }, 1, 16);
            section.EnableWhen(samples, [light]() { return light->CastShadows; }, []() { return std::string("(needs Cast Shadows)"); });
            section.Slider("Intensity", [light]() { return light->Intensity; }, [light](float v) { light->Intensity = v; }, 0.0f, 100.0f, "{:.1f}");
            section.Choice("Mode", { "Movable", "Stationary", "Static" }, [light]() { return light->Mode; }, [light](int v) { light->Mode = v; });
            section.Color3("Color", &light->Color);
            Ref<UIElement> bright = section.Note("Over 50: very bright (ShowWhen).", { 0.95f, 0.72f, 0.28f, 1.0f });
            section.ShowWhen(bright, [light]() { return light->Intensity > 50.0f; });
            section.Text([light]() { char t[96]; std::snprintf(t, sizeof(t), "%s: %.1f, %d samples", light->Name.c_str(), light->Intensity, light->Samples); return std::string(t); });
            section.Button("Change from code", [light]()
            {
                light->Intensity = std::fmod(light->Intensity + 37.0f, 100.0f);
                light->CastShadows = !light->CastShadows;
                light->Mode = (light->Mode + 1) % 3;
            }, "The rows read their values every frame: they follow");
            AppendSync(*body, [expanded]() {}); // (the open-state map lives as long as the rows: held by the body's sync)
            card->AddContent(body);
            grid->AddChild(card);
        }

        // --- UISearchPicker ------------------------------------------------------------------------------------------------------
        {
            auto card = MakeUI<UIGroupBox>("UISearchPicker");
            auto picked = std::make_shared<uint64_t>(0);
            auto options = std::make_shared<std::vector<UISearchPickerOption>>();
            for (int i = 0; i < 300; ++i)
                options->push_back({ "Asset_" + std::to_string(i), i % 3 == 0 ? "Textures" : i % 3 == 1 ? "Meshes" : "Materials", static_cast<uint64_t>(i + 1) });
            Ref<UIButton> pick(new UIButton("None"));
            UIButton* pickRaw = pick.get();
            pick->OnClick = [pickRaw, picked, options]()
            {
                if (UIContext* ctx = UIContext::GetCurrent())
                {
                    UISearchPickerSettings settings;
                    settings.Placeholder = "Filter assets...";
                    settings.NoneLabel = "None";
                    UISearchPicker::Show(*ctx, pickRaw->GetBounds(), *options, *picked, [pickRaw, picked, options](uint64_t id)
                    {
                        *picked = id;
                        pickRaw->SetText(id == 0 ? std::string("None") : (*options)[id - 1].Label);
                    }, settings);
                }
            };
            pick->WidthMode = UISizeMode::Fill;
            card->AddContent(pick);
            card->AddContent(Hint("Click it: type to filter 300 rows (label or folder), Enter picks the first, Escape or a click outside closes."));
            grid->AddChild(card);
        }

        // --- UIPanZoom -----------------------------------------------------------------------------------------------------------
        {
            auto card = MakeUI<UIGroupBox>("UIPanZoom");
            card->AddContent(Ref<UIElement>(new DemoCanvas()));
            card->AddContent(Hint("Wheel: zoom around the pointer (the point under it stays). Right-drag: pan. Click it, then F: frame; Home: 100 %."));
            grid->AddChild(card);
        }

        // --- UIDrawerHost --------------------------------------------------------------------------------------------------------
        {
            auto card = MakeUI<UIGroupBox>("UIDrawerHost");
            auto content = MakeUI<UIStackBox>(UIOrientation::Vertical, 6.0f);
            content->WidthMode = UISizeMode::Fill;
            content->HeightMode = UISizeMode::Fill;
            content->Padding = UIThickness::All(8.0f);
            content->AddChild(Hint("The main content. The drawer slides up over it (it covers instead of resizing)."));
            Ref<UIButton> toggle(new UIButton("Open / Close the Drawer"));
            content->AddChild(toggle);
            Ref<UIDrawerHost> host(new UIDrawerHost(content));
            host->WidthMode = UISizeMode::Fill;
            host->HeightMode = UISizeMode::Fixed;
            host->FixedSize = { 0.0f, 200.0f };
            host->HeightFraction = 0.55f;
            auto drawerPage = MakeUI<UIStackBox>(UIOrientation::Vertical, 4.0f);
            drawerPage->WidthMode = UISizeMode::Fill;
            drawerPage->HeightMode = UISizeMode::Fill;
            drawerPage->Padding = UIThickness::All(8.0f);
            drawerPage->AddChild(Ref<UILabel>(new UILabel("The drawer's page", 15.0f)));
            drawerPage->AddChild(Hint("Drag its top edge to resize it (15 % to 90 %)."));
            host->SetPage(drawerPage);
            UIDrawerHost* hostRaw = host.get();
            toggle->OnClick = [hostRaw]() { hostRaw->Open = !hostRaw->Open; };
            card->AddContent(host);
            grid->AddChild(card);
        }

        // --- UICommandList -------------------------------------------------------------------------------------------------------
        auto commands = std::make_shared<UICommandList>();
        {
            auto card = MakeUI<UIGroupBox>("UICommandList");
            UICommand hello;
            hello.Id = "Demo.Hello";
            hello.Label = "Say Hello";
            hello.DefaultKey = { UIKey::H, true };
            hello.Execute = [&context]() { context.PushToast("Hello from a command"); };
            commands->Add(hello);
            UICommand count;
            count.Id = "Demo.Count";
            count.Label = "Count";
            count.DefaultKey = { UIKey::J, true };
            count.DefaultAltKey = { UIKey::J, true, true };
            auto counter = std::make_shared<int>(0);
            count.Execute = [counter, status]() { status("Count: " + std::to_string(++*counter)); };
            commands->Add(count);
            for (const UICommand& command : commands->GetCommands())
            {
                auto row = MakeUI<UIStackBox>(UIOrientation::Horizontal, 8.0f);
                const std::string id = command.Id;
                Ref<UIButton> run(new UIButton(command.Label, [commands, id]() { commands->Execute(id); }));
                row->AddChild(run);
                Ref<UILabel> keys(new UILabel(commands->ShortcutText(id), 13.0f));
                keys->VAlign = UIAlignment::Center;
                row->AddChild(keys);
                card->AddContent(row);
            }
            Ref<UIButton> rebind(new UIButton("Bind Ctrl+H to Count instead", [commands, status]()
            {
                const std::string taken = commands->Bind("Demo.Count", { UIKey::H, true });
                status(taken.empty() ? std::string("Bound") : "Ctrl+H taken from " + taken);
            }));
            card->AddContent(rebind);
            card->AddContent(Hint("Click in this tab, then press Ctrl+H or Ctrl+J (the tab's OnKey asks FindForKey). Rebinding takes the key from its old command."));
            grid->AddChild(card);
        }

        // --- Hooks: OnSync, Defer, modal Enabled, ThemeColor -----------------------------------------------------------------------
        {
            auto card = MakeUI<UIGroupBox>("Hooks: OnSync, Defer, modal Enabled");
            Ref<UILabel> clock(new UILabel("", 14.0f));
            UILabel* clockRaw = clock.get();
            clock->OnSync = [clockRaw]()
            {
                char text[64];
                std::snprintf(text, sizeof(text), "OnSync, every frame: %.1f s", Seconds());
                clockRaw->Text = text;
            };
            card->AddContent(clock);

            // A list whose rows rebuild the list when clicked: the rebuild waits for the next frame (Defer), so the clicked row is not
            // destroyed while its click runs.
            auto list = MakeUI<UIStackBox>(UIOrientation::Horizontal, 6.0f);
            auto rebuild = std::make_shared<std::function<void()>>();
            auto generation = std::make_shared<int>(0);
            UIStackBox* listRaw = list.get();
            std::weak_ptr<std::function<void()>> weakRebuild = rebuild; // (weak: the function must not keep itself alive)
            *rebuild = [listRaw, weakRebuild, generation, &context]()
            {
                listRaw->ClearChildren();
                ++*generation;
                for (int i = 0; i < 3; ++i)
                {
                    std::weak_ptr<std::function<void()>> weak = weakRebuild;
                    listRaw->AddChild(Ref<UIButton>(new UIButton("Rebuild " + std::to_string(*generation) + "." + std::to_string(i + 1), [weak, &context]()
                    {
                        context.Defer([weak]() { if (auto again = weak.lock()) (*again)(); });
                    })));
                }
            };
            (*rebuild)();
            AppendSync(*list, [rebuild]() {}); // (the list holds its rebuild function)
            card->AddContent(list);

            Ref<UIButton> dialog(new UIButton("Open a dialog", [&context, status]()
            {
                auto agreed = std::make_shared<bool>(false);
                auto content = MakeUI<UIStackBox>(UIOrientation::Vertical, 8.0f);
                content->AddChild(Hint("Continue stays disabled until the box is ticked (UIModal::Button::Enabled, checked every frame)."));
                content->AddChild(Ref<UICheckBox>(new UICheckBox("I have read this", false, [agreed](bool v) { *agreed = v; })));
                UIModal::ShowCustom(context, "A dialog", content,
                                    { { "Continue", [status]() { status("Continued"); }, true, [agreed]() { return *agreed; } }, { "Cancel", {} } }, 420.0f);
            }));
            card->AddContent(dialog);

            auto themeRow = MakeUI<UIStackBox>(UIOrientation::Horizontal, 8.0f);
            themeRow->AddChild(Ref<UIElement>(new DemoSwatch()));
            themeRow->AddChild(Ref<UIButton>(new UIButton("Theme: DemoSwatch green", [&context]()
            {
                if (context.GetTheme())
                    context.GetTheme()->SetToken("DemoSwatch", { 0.30f, 0.80f, 0.45f, 1.0f });
            })));
            themeRow->AddChild(Ref<UIButton>(new UIButton("orange", [&context]()
            {
                if (context.GetTheme())
                    context.GetTheme()->SetToken("DemoSwatch", { 0.85f, 0.45f, 0.20f, 1.0f });
            })));
            card->AddContent(themeRow);
            card->AddContent(Hint("The swatch is a UIThemeToken: its colour comes from the theme each frame, the fallback while the theme has none."));
            grid->AddChild(card);
        }

        // --- Small text (the glyph cache) -------------------------------------------------------------------------------------------
        {
            auto card = MakeUI<UIGroupBox>("Text at the size it is shown");
            for (float size : { 9.0f, 10.5f, 12.0f, 13.0f, 14.0f, 16.0f })
            {
                char text[96];
                std::snprintf(text, sizeof(text), "%.1f px: The quick brown fox jumps over the lazy dog 0123456789", size);
                Ref<UILabel> label(new UILabel(text, size));
                card->AddContent(label);
            }
            card->AddContent(Hint("Up to 48 px each glyph is rasterized by FreeType at its pixel size (UIGlyphCache) -- no wavy edges at small sizes."));
            grid->AddChild(card);
        }

        page->AddChild(grid);
        // Keys typed while the focus is in this tab: the commands' keys (UIElement::OnKey, no subclass).
        page->OnKey = [commands](const UIKeyEvent& e)
        {
            const UIKeyChord chord{ static_cast<UIKey>(e.KeyCode), (e.Modifiers & UIMod::Ctrl) != 0, (e.Modifiers & UIMod::Shift) != 0, (e.Modifiers & UIMod::Alt) != 0 };
            if (const UICommand* command = commands->FindForKey(chord))
                return commands->Execute(command->Id);
            return false;
        };
        page->Focusable = true;
        return { "Editor Widgets", page };
    }
}
