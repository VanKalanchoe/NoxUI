#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "NoxUI/Core/UIRef.h"
#include "NoxUI/Core/UITheme.h"
#include "NoxUI/Core/UITypes.h"

// In a widget class's public section: the class fingerprints all of its own state (UIElement::HashState, StateHashClass).
#define NOXUI_STATE_HASH(Class)                                                       \
    void HashState(::NoxUI::UIStateHasher& h) const override;                         \
    const std::type_info& StateHashClass() const override { return typeid(Class); }

namespace NoxUI
{
    // Folds one 64-bit value into a running key (the layout and paint caches combine an element's own key with its children's and
    // its bounds): a multiply-xorshift mix per value, cheaper than a UIStateHasher for a few words.
    inline uint64_t HashCombine(uint64_t seed, uint64_t value)
    {
        value *= 0x9E3779B97F4A7C15ull;
        value ^= value >> 32;
        seed = (seed ^ value) * 0xBF58476D1CE4E5B9ull;
        return seed ^ (seed >> 31);
    }
    inline uint64_t HashCombineFloat(uint64_t seed, float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return HashCombine(seed, static_cast<uint64_t>(bits) | (uint64_t{ 1 } << 32));
    }
    inline uint64_t HashCombinePointer(uint64_t seed, const void* pointer)
    {
        return HashCombine(seed, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer)));
    }

    // The fingerprint of what a UI draws: every element adds the state its layout and paint read (UIElement::HashState). When the
    // fingerprint after the OnSync hooks equals the one the last full frame left, and that frame changed nothing itself, nothing can
    // draw differently: UIContext reuses the last draw list instead of laying out and painting again (retained mode's idle frames).
    // Values are hashed exactly (floats by their bits): a change of any of them is a change.
    class UIStateHasher
    {
    public:
        void Bytes(const void* data, size_t size)
        {
            const auto* p = static_cast<const uint8_t*>(data);
            while (size >= 8)
            {
                uint64_t word;
                std::memcpy(&word, p, 8);
                Mix(word);
                p += 8;
                size -= 8;
            }
            uint64_t tail = 0;
            std::memcpy(&tail, p, size);
            Mix(tail ^ (static_cast<uint64_t>(size) << 56));
        }

        template <typename T>
            requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
        void Add(T value)
        {
            uint64_t word = 0;
            std::memcpy(&word, &value, sizeof(T));
            Mix(word);
        }
        void Add(const void* pointer) { Mix(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer))); }
        template <typename T>
        void Add(const Ref<T>& ref) { Add(static_cast<const void*>(ref.get())); }
        void Add(std::string_view text)
        {
            Mix(text.size());
            Bytes(text.data(), text.size());
        }
        void Add(const std::string& text) { Add(std::string_view(text)); }
        void Add(const char* text) { Add(std::string_view(text ? text : "")); }
        void Add(const glm::vec2& v) { Add(v.x), Add(v.y); }
        void Add(const glm::vec3& v) { Add(v.x), Add(v.y), Add(v.z); }
        void Add(const glm::vec4& v) { Add(v.x), Add(v.y), Add(v.z), Add(v.w); }
        void Add(const UIThickness& t) { Add(t.Left), Add(t.Top), Add(t.Right), Add(t.Bottom); }
        void Add(const UICornerRadius& r) { Add(r.TopLeft), Add(r.TopRight), Add(r.BottomRight), Add(r.BottomLeft); }
        void Add(const UIRect& r) { Add(r.Min), Add(r.Max); }
        void Add(const UIStyleBox& s)
        {
            (*this)(s.FillColor, s.FillColorBottom, s.UseGradient, s.BorderColor, s.BorderThickness, s.ShadowColor, s.ShadowOffset, s.ShadowBlur,
                    s.CornerRadius, s.Padding, s.TextColor, s.FontSize, s.TransitionSeconds);
        }
        template <typename A, typename B>
        void Add(const std::pair<A, B>& pair) { Add(pair.first), Add(pair.second); }
        template <typename T>
        void Add(const std::vector<T>& values)
        {
            Mix(values.size());
            for (const T& value : values)
                Add(value);
        }

        template <typename... T>
        void operator()(const T&... values) { (Add(values), ...); }

        uint64_t Value() const
        {
            uint64_t h = m_Count * 0x9E3779B97F4A7C15ull;
            for (const uint64_t lane : m_Lanes)
            {
                h = (h ^ lane) * 0xFF51AFD7ED558CCDull;
                h ^= h >> 33;
            }
            return h;
        }

        // An element whose class does not fingerprint all of its own state was seen (a widget class that does not override
        // HashState / StateHashClass, a subclass that adds fields): nothing proves the frame unchanged, so it is drawn.
        bool Complete = true;
        // The first element whose class does not (for UIContext::GetLastDrawReason: the class to give a fingerprint).
        const std::type_info* IncompleteType = nullptr;
        // Tracing (UIContext::SetTraceStateChanges): each element's own fingerprint, to name the ones that changed.
        std::vector<std::pair<const void*, uint64_t>>* Trace = nullptr;

    private:
        // Four lanes, word by word in turn: four independent multiply chains instead of one (a fingerprint pass per frame).
        void Mix(uint64_t word)
        {
            uint64_t& lane = m_Lanes[m_Count++ & 3];
            lane = (lane ^ word) * 0x9E3779B97F4A7C15ull;
            lane ^= lane >> 29;
        }

        uint64_t m_Lanes[4] = { 0x6A09E667F3BCC909ull, 0xBB67AE8584CAA73Bull, 0x3C6EF372FE94F82Bull, 0xA54FF53A5F1D36F1ull };
        uint64_t m_Count = 0;
    };
}
