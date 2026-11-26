#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "NoxUI/Layout/UILayout.h"
#include "NoxUI/Widgets/UIScrollView.h"

namespace NoxUI
{
    class UIContext;

    enum class UIPropertyType : uint8_t
    {
        Bool = 0,
        Int,
        UInt32,
        Float,
        Vec2,
        Vec3,
        Vec4,
        Color3,
        Color4,
        String
    };

    struct UIPropertyMeta
    {
        std::string Name;
        UIPropertyType Type = UIPropertyType::Float;
        float Speed = 0.1f;
        float Min = 0.0f;
        float Max = 0.0f;
        std::string Placeholder;
        std::function<void(void* dst, const void* instance)> Read;
        std::function<void(void* instance, const void* src)> Write;
        std::function<float(const void* instance)> DynamicMin;
        std::function<float(const void* instance)> DynamicMax;
    };

    struct UITypeMeta
    {
        std::string DisplayName;
        std::vector<UIPropertyMeta> Properties;
        std::function<void(void* instance)> OnPostEdit;
    };

    template <typename T>
    class UITypeBuilder
    {
    public:
        explicit UITypeBuilder(std::string displayName)
        {
            m_Meta.DisplayName = std::move(displayName);
        }

        template <typename FieldT>
        UITypeBuilder& AddField(std::string name, UIPropertyType type, FieldT T::* member,
                                float speed = 0.1f, float min = 0.0f, float max = 0.0f, std::string placeholder = {})
        {
            UIPropertyMeta prop{};
            prop.Name = std::move(name);
            prop.Type = type;
            prop.Speed = speed;
            prop.Min = min;
            prop.Max = max;
            prop.Placeholder = std::move(placeholder);
            prop.Read = [member](void* dst, const void* instance)
            {
                *static_cast<FieldT*>(dst) = static_cast<const T*>(instance)->*member;
            };
            prop.Write = [member](void* instance, const void* src)
            {
                static_cast<T*>(instance)->*member = *static_cast<const FieldT*>(src);
            };
            m_Meta.Properties.push_back(std::move(prop));
            return *this;
        }

        UITypeBuilder& Bool(std::string name, bool T::* member) { return AddField(std::move(name), UIPropertyType::Bool, member); }
        UITypeBuilder& Int(std::string name, int T::* member, float speed = 1.0f, float min = 0.0f, float max = 0.0f) { return AddField(std::move(name), UIPropertyType::Int, member, speed, min, max); }
        UITypeBuilder& UInt32(std::string name, uint32_t T::* member, float speed = 1.0f, uint32_t min = 0, uint32_t max = 0) { return AddField(std::move(name), UIPropertyType::UInt32, member, speed, static_cast<float>(min), static_cast<float>(max)); }
        UITypeBuilder& Float(std::string name, float T::* member, float speed = 0.1f, float min = 0.0f, float max = 0.0f) { return AddField(std::move(name), UIPropertyType::Float, member, speed, min, max); }
        UITypeBuilder& Vec2(std::string name, glm::vec2 T::* member, float speed = 0.1f) { return AddField(std::move(name), UIPropertyType::Vec2, member, speed); }
        UITypeBuilder& Vec3(std::string name, glm::vec3 T::* member, float speed = 0.1f) { return AddField(std::move(name), UIPropertyType::Vec3, member, speed); }
        UITypeBuilder& Vec4(std::string name, glm::vec4 T::* member, float speed = 0.1f) { return AddField(std::move(name), UIPropertyType::Vec4, member, speed); }
        UITypeBuilder& Color3(std::string name, glm::vec3 T::* member) { return AddField(std::move(name), UIPropertyType::Color3, member); }
        UITypeBuilder& Color4(std::string name, glm::vec4 T::* member) { return AddField(std::move(name), UIPropertyType::Color4, member); }
        UITypeBuilder& String(std::string name, std::string T::* member, std::string placeholder = {}) { return AddField(std::move(name), UIPropertyType::String, member, 0.0f, 0.0f, 0.0f, std::move(placeholder)); }

        UITypeBuilder& MaxFrom(float T::* maxMember)
        {
            if (!m_Meta.Properties.empty())
            {
                m_Meta.Properties.back().DynamicMax = [maxMember](const void* instance)
                {
                    return static_cast<const T*>(instance)->*maxMember;
                };
            }
            return *this;
        }

        UITypeBuilder& OnPostEdit(std::function<void(T&)> fn)
        {
            m_Meta.OnPostEdit = [fn = std::move(fn)](void* instance)
            {
                if (fn && instance)
                    fn(*static_cast<T*>(instance));
            };
            return *this;
        }

        const UITypeMeta& Build() const { return m_Meta; }

    private:
        UITypeMeta m_Meta;
    };

    // Collapsible 2-column inspector grid with convenience builders and property-descriptor auto-generation.
    class UIPropertyGrid : public UIScrollView
    {
    public:
        NOXUI_STATE_HASH(UIPropertyGrid)
        UIPropertyGrid();

        Ref<UIStackBox> AddSection(const std::string& title, bool expanded = true);
        void AddRow(const std::string& label, const Ref<UIElement>& editor, const Ref<UIStackBox>& section = nullptr);

        void AddBool(const std::string& label, bool value, std::function<void(bool)> onChanged,
                     const Ref<UIStackBox>& section = nullptr);
        void AddFloatSlider(const std::string& label, float value, float min, float max,
                            std::function<void(float)> onChanged, const Ref<UIStackBox>& section = nullptr);
        void AddDragFloat(const std::string& label, float value, float speed,
                          std::function<void(float)> onChanged, const Ref<UIStackBox>& section = nullptr);
        void AddVec3(const std::string& label, glm::vec3 value, float speed,
                     std::function<void(const glm::vec3&)> onChanged, const Ref<UIStackBox>& section = nullptr);
        void AddString(const std::string& label, const std::string& value, const std::string& placeholder,
                       std::function<void(const std::string&)> onChanged, const Ref<UIStackBox>& section = nullptr);

        // Auto-generates rows from UITypeMeta with 2-way sync and optional validity guard.
        Ref<UIStackBox> AddReflected(
            UIContext* context,
            void* instance,
            const UITypeMeta& meta,
            std::function<bool()> isValid = {},
            const std::string& sectionOverride = {});

        void ClearProperties();

        float LabelRatio = 0.38f;

    protected:
        void OnPoll() override; // (each property follows its model, before the fingerprint)

    private:
        struct SyncEntry
        {
            std::function<bool()> IsValid;
            std::function<void()> SyncFromModel;
            Ref<UIElement> SectionContainer;
        };
        std::vector<SyncEntry> m_SyncEntries;
    };
}
