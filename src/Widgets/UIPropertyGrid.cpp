#include "NoxUI/Widgets/UIPropertyGrid.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "NoxUI/Widgets/UIButton.h"
#include "NoxUI/Widgets/UICheckBox.h"
#include "NoxUI/Widgets/UIColorPicker.h"
#include "NoxUI/Widgets/UIDragScalar.h"
#include "NoxUI/Widgets/UILabel.h"
#include "NoxUI/Widgets/UISlider.h"
#include "NoxUI/Widgets/UITextInput.h"
#include "NoxUI/Widgets/UIVectorEdit.h"

namespace NoxUI
{
    UIPropertyGrid::UIPropertyGrid()
    {
        StyleClass = "PropertyGrid";
        ChildSpacing = 6.0f;
    }

    void UIPropertyGrid::ClearProperties()
    {
        m_SyncEntries.clear();
        ClearChildren();
    }

    void UIPropertyGrid::OnPoll()
    {

        for (size_t i = 0; i < m_SyncEntries.size();)
        {
            auto& entry = m_SyncEntries[i];
            if (entry.IsValid && !entry.IsValid())
            {
                if (entry.SectionContainer)
                    RemoveChild(entry.SectionContainer);
                m_SyncEntries.erase(m_SyncEntries.begin() + static_cast<ptrdiff_t>(i));
                continue;
            }
            if (entry.SyncFromModel)
                entry.SyncFromModel();
            ++i;
        }
    }

    Ref<UIStackBox> UIPropertyGrid::AddReflected(
        UIContext* context,
        void* instance,
        const UITypeMeta& meta,
        std::function<bool()> isValid,
        const std::string& sectionOverride)
    {
        if (!instance)
            return nullptr;

        const std::string& title = sectionOverride.empty() ? meta.DisplayName : sectionOverride;
        Ref<UIStackBox> section = AddSection(title, true);
        Ref<UIElement> sectionContainer;
        if (!GetChildren().empty())
            sectionContainer = GetChildren().back();

        auto postEdit = meta.OnPostEdit;
        std::vector<std::function<void()>> rowSyncs;

        for (const UIPropertyMeta& prop : meta.Properties)
        {
            switch (prop.Type)
            {
            case UIPropertyType::Bool:
            {
                bool val = false;
                prop.Read(&val, instance);
                Ref<UICheckBox> cb(new UICheckBox({}, val, [instance, prop, postEdit](bool next)
                {
                    prop.Write(instance, &next);
                    if (postEdit) postEdit(instance);
                }));
                AddRow(prop.Name, cb, section);
                rowSyncs.push_back([cb, instance, prop]()
                {
                    if (HasState(cb->GetState(), UIState::Pressed)) return;
                    bool cur = false;
                    prop.Read(&cur, instance);
                    cb->Checked = cur;
                });
                break;
            }
            case UIPropertyType::Int:
            {
                int val = 0;
                prop.Read(&val, instance);
                Ref<UIDragScalar> drag(new UIDragScalar(static_cast<float>(val), prop.Speed, [instance, prop, postEdit](float next)
                {
                    int iv = static_cast<int>(std::round(next));
                    prop.Write(instance, &iv);
                    if (postEdit) postEdit(instance);
                }));
                drag->Format = "{:.0f}";
                drag->Min = prop.Min;
                drag->Max = prop.Max;
                AddRow(prop.Name, drag, section);
                rowSyncs.push_back([drag, instance, prop]()
                {
                    if (HasState(drag->GetState(), UIState::Focused) || HasState(drag->GetState(), UIState::Pressed)) return;
                    int cur = 0;
                    prop.Read(&cur, instance);
                    drag->Value = static_cast<float>(cur);
                });
                break;
            }
            case UIPropertyType::UInt32:
            {
                uint32_t val = 0;
                prop.Read(&val, instance);
                Ref<UIDragScalar> drag(new UIDragScalar(static_cast<float>(val), prop.Speed, [instance, prop, postEdit](float next)
                {
                    uint32_t uv = static_cast<uint32_t>(std::max(0.0f, std::round(next)));
                    prop.Write(instance, &uv);
                    if (postEdit) postEdit(instance);
                }));
                drag->Format = "{:.0f}";
                drag->Min = prop.Min;
                drag->Max = prop.Max;
                AddRow(prop.Name, drag, section);
                rowSyncs.push_back([drag, instance, prop]()
                {
                    if (HasState(drag->GetState(), UIState::Focused) || HasState(drag->GetState(), UIState::Pressed)) return;
                    uint32_t cur = 0;
                    prop.Read(&cur, instance);
                    drag->Value = static_cast<float>(cur);
                });
                break;
            }
            case UIPropertyType::Float:
            {
                float val = 0.0f;
                prop.Read(&val, instance);
                Ref<UIDragScalar> drag(new UIDragScalar(val, prop.Speed, [instance, prop, postEdit](float next)
                {
                    prop.Write(instance, &next);
                    if (postEdit) postEdit(instance);
                }));
                drag->Min = prop.DynamicMin ? prop.DynamicMin(instance) : prop.Min;
                drag->Max = prop.DynamicMax ? prop.DynamicMax(instance) : prop.Max;
                AddRow(prop.Name, drag, section);
                rowSyncs.push_back([drag, instance, prop]()
                {
                    if (prop.DynamicMin) drag->Min = prop.DynamicMin(instance);
                    if (prop.DynamicMax) drag->Max = prop.DynamicMax(instance);
                    if (HasState(drag->GetState(), UIState::Focused) || HasState(drag->GetState(), UIState::Pressed)) return;
                    float cur = 0.0f;
                    prop.Read(&cur, instance);
                    drag->Value = cur;
                });
                break;
            }
            case UIPropertyType::Vec2:
            {
                glm::vec2 val(0.0f);
                prop.Read(&val, instance);
                Ref<UIVectorEdit> vec(new UIVectorEdit(2, { val.x, val.y, 0.0f, 0.0f }, prop.Speed, [instance, prop, postEdit](const glm::vec4& next)
                {
                    glm::vec2 v2(next.x, next.y);
                    prop.Write(instance, &v2);
                    if (postEdit) postEdit(instance);
                }));
                AddRow(prop.Name, vec, section);
                rowSyncs.push_back([vec, instance, prop]()
                {
                    if (vec->IsEditing()) return;
                    glm::vec2 cur(0.0f);
                    prop.Read(&cur, instance);
                    vec->SetValue({ cur.x, cur.y, 0.0f, 0.0f }, false);
                });
                break;
            }
            case UIPropertyType::Vec3:
            {
                glm::vec3 val(0.0f);
                prop.Read(&val, instance);
                Ref<UIVectorEdit> vec(new UIVectorEdit(3, { val.x, val.y, val.z, 0.0f }, prop.Speed, [instance, prop, postEdit](const glm::vec4& next)
                {
                    glm::vec3 v3(next.x, next.y, next.z);
                    prop.Write(instance, &v3);
                    if (postEdit) postEdit(instance);
                }));
                AddRow(prop.Name, vec, section);
                rowSyncs.push_back([vec, instance, prop]()
                {
                    if (vec->IsEditing()) return;
                    glm::vec3 cur(0.0f);
                    prop.Read(&cur, instance);
                    vec->SetValue({ cur.x, cur.y, cur.z, 0.0f }, false);
                });
                break;
            }
            case UIPropertyType::Vec4:
            {
                glm::vec4 val(0.0f);
                prop.Read(&val, instance);
                Ref<UIVectorEdit> vec(new UIVectorEdit(4, val, prop.Speed, [instance, prop, postEdit](const glm::vec4& next)
                {
                    prop.Write(instance, &next);
                    if (postEdit) postEdit(instance);
                }));
                AddRow(prop.Name, vec, section);
                rowSyncs.push_back([vec, instance, prop]()
                {
                    if (vec->IsEditing()) return;
                    glm::vec4 cur(0.0f);
                    prop.Read(&cur, instance);
                    vec->SetValue(cur, false);
                });
                break;
            }
            case UIPropertyType::Color3:
            {
                glm::vec3 val(1.0f);
                prop.Read(&val, instance);
                Ref<UIColorEdit> color(new UIColorEdit(context, { val.r, val.g, val.b, 1.0f }, [instance, prop, postEdit](const UIColor& c)
                {
                    glm::vec3 v3(c.r, c.g, c.b);
                    prop.Write(instance, &v3);
                    if (postEdit) postEdit(instance);
                }));
                AddRow(prop.Name, color, section);
                rowSyncs.push_back([color, instance, prop]()
                {
                    glm::vec3 cur(1.0f);
                    prop.Read(&cur, instance);
                    const UIColor curC = color->GetColor();
                    if (std::abs(curC.r - cur.r) > 1e-4f || std::abs(curC.g - cur.g) > 1e-4f || std::abs(curC.b - cur.b) > 1e-4f)
                        color->SetColor({ cur.r, cur.g, cur.b, 1.0f }, false);
                });
                break;
            }
            case UIPropertyType::Color4:
            {
                glm::vec4 val(1.0f);
                prop.Read(&val, instance);
                Ref<UIColorEdit> color(new UIColorEdit(context, val, [instance, prop, postEdit](const UIColor& c)
                {
                    prop.Write(instance, &c);
                    if (postEdit) postEdit(instance);
                }));
                AddRow(prop.Name, color, section);
                rowSyncs.push_back([color, instance, prop]()
                {
                    glm::vec4 cur(1.0f);
                    prop.Read(&cur, instance);
                    if (glm::length(color->GetColor() - cur) > 1e-4f)
                        color->SetColor(cur, false);
                });
                break;
            }
            case UIPropertyType::String:
            {
                std::string val;
                prop.Read(&val, instance);
                Ref<UITextInput> input(new UITextInput(val, prop.Placeholder));
                input->OnChanged = [instance, prop, postEdit](const std::string& next)
                {
                    prop.Write(instance, &next);
                    if (postEdit) postEdit(instance);
                };
                AddRow(prop.Name, input, section);
                rowSyncs.push_back([input, instance, prop]()
                {
                    if (HasState(input->GetState(), UIState::Focused)) return;
                    std::string cur;
                    prop.Read(&cur, instance);
                    if (input->Text != cur)
                        input->Text = cur;
                });
                break;
            }
            }
        }

        m_SyncEntries.push_back({
            std::move(isValid),
            [syncs = std::move(rowSyncs)]()
            {
                for (const auto& fn : syncs)
                    fn();
            },
            sectionContainer
        });

        return section;
    }

    Ref<UIStackBox> UIPropertyGrid::AddSection(const std::string& title, bool expanded)
    {
        Ref<UIStackBox> sectionContainer(new UIStackBox(UIOrientation::Vertical, 4.0f));
        sectionContainer->WidthMode = UISizeMode::Fill;

        Ref<UIStackBox> body(new UIStackBox(UIOrientation::Vertical, 4.0f));
        body->WidthMode = UISizeMode::Fill;
        body->Padding = { 8.0f, 4.0f, 4.0f, 6.0f };
        body->Visible = expanded;

        auto makeTitle = [](const std::string& t, bool exp)
        {
            return (exp ? std::string("v  ") : std::string(">  ")) + t;
        };

        Ref<UIButton> header(new UIButton(makeTitle(title, expanded)));
        header->StyleClass = "SectionHeader";
        header->WidthMode = UISizeMode::Fill;

        UIButton* rawHeader = header.get();
        UIStackBox* rawBody = body.get();
        header->OnClick = [rawHeader, rawBody, title, makeTitle]()
        {
            rawBody->Visible = !rawBody->Visible;
            rawHeader->SetText(makeTitle(title, rawBody->Visible));
        };

        sectionContainer->AddChild(header);
        sectionContainer->AddChild(body);
        AddChild(sectionContainer);
        return body;
    }

    void UIPropertyGrid::AddRow(const std::string& label, const Ref<UIElement>& editor, const Ref<UIStackBox>& section)
    {
        Ref<UIStackBox> row(new UIStackBox(UIOrientation::Horizontal, 8.0f));
        row->WidthMode = UISizeMode::Fill;
        row->Padding = { 2.0f, 2.0f, 2.0f, 2.0f };

        Ref<UILabel> lbl(new UILabel(label));
        lbl->WidthMode = UISizeMode::Fill;
        lbl->FlexWeight = std::clamp(LabelRatio, 0.1f, 0.8f);
        lbl->VAlign = UIAlignment::Center;

        if (editor)
        {
            editor->WidthMode = UISizeMode::Fill;
            editor->FlexWeight = 1.0f - lbl->FlexWeight;
        }

        row->AddChild(lbl);
        if (editor)
            row->AddChild(editor);

        if (section)
            section->AddChild(row);
        else
            AddChild(row);
    }

    void UIPropertyGrid::AddBool(const std::string& label, bool value, std::function<void(bool)> onChanged,
                                 const Ref<UIStackBox>& section)
    {
        Ref<UICheckBox> cb(new UICheckBox("", value, std::move(onChanged)));
        AddRow(label, cb, section);
    }

    void UIPropertyGrid::AddFloatSlider(const std::string& label, float value, float min, float max,
                                        std::function<void(float)> onChanged, const Ref<UIStackBox>& section)
    {
        Ref<UISlider> slider(new UISlider(value, min, max, std::move(onChanged)));
        AddRow(label, slider, section);
    }

    void UIPropertyGrid::AddDragFloat(const std::string& label, float value, float speed,
                                      std::function<void(float)> onChanged, const Ref<UIStackBox>& section)
    {
        Ref<UIDragScalar> drag(new UIDragScalar(value, speed, std::move(onChanged)));
        AddRow(label, drag, section);
    }

    void UIPropertyGrid::AddVec3(const std::string& label, glm::vec3 value, float speed,
                                 std::function<void(const glm::vec3&)> onChanged, const Ref<UIStackBox>& section)
    {
        Ref<UIVectorEdit> vec(new UIVectorEdit(3, { value.x, value.y, value.z, 0.0f }, speed, [onChanged = std::move(onChanged)](const glm::vec4& v)
        {
            if (onChanged)
                onChanged({ v.x, v.y, v.z });
        }));
        AddRow(label, vec, section);
    }

    void UIPropertyGrid::AddString(const std::string& label, const std::string& value, const std::string& placeholder,
                                   std::function<void(const std::string&)> onChanged, const Ref<UIStackBox>& section)
    {
        Ref<UITextInput> input(new UITextInput(value, placeholder));
        input->OnChanged = std::move(onChanged);
        AddRow(label, input, section);
    }

    void UIPropertyGrid::HashState(UIStateHasher& h) const
    {
        UIScrollView::HashState(h);
        h(LabelRatio, m_SyncEntries.size());
        for (const SyncEntry& entry : m_SyncEntries)
            h(entry.SectionContainer);
    }
}
