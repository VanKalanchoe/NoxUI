#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace NoxUI
{
    class RefCounted
    {
    public:
        void IncRefCount() const { m_RefCount.fetch_add(1, std::memory_order_relaxed); }
        void DecRefCount() const
        {
            if (m_RefCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
                delete this;
        }

        uint32_t GetRefCount() const { return m_RefCount.load(std::memory_order_acquire); }

    protected:
        RefCounted() = default;
        virtual ~RefCounted() = default;

    private:
        mutable std::atomic<uint32_t> m_RefCount = 0;
    };

    template <typename T>
    class Ref
    {
    public:
        Ref() = default;
        Ref(std::nullptr_t) : m_Ptr(nullptr) {}
        explicit Ref(T* ptr) : m_Ptr(ptr) { if (m_Ptr) m_Ptr->IncRefCount(); }
        Ref(const Ref& other) : m_Ptr(other.m_Ptr) { if (m_Ptr) m_Ptr->IncRefCount(); }
        Ref(Ref&& other) noexcept : m_Ptr(other.m_Ptr) { other.m_Ptr = nullptr; }

        ~Ref() { if (m_Ptr) m_Ptr->DecRefCount(); }

        Ref& operator=(const Ref& other)
        {
            if (m_Ptr != other.m_Ptr)
            {
                if (m_Ptr) m_Ptr->DecRefCount();
                m_Ptr = other.m_Ptr;
                if (m_Ptr) m_Ptr->IncRefCount();
            }
            return *this;
        }

        Ref& operator=(Ref&& other) noexcept
        {
            if (this != &other)
            {
                if (m_Ptr) m_Ptr->DecRefCount();
                m_Ptr = other.m_Ptr;
                other.m_Ptr = nullptr;
            }
            return *this;
        }

        Ref& operator=(std::nullptr_t)
        {
            if (m_Ptr) m_Ptr->DecRefCount();
            m_Ptr = nullptr;
            return *this;
        }

        template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U> || std::is_base_of_v<U, T>>>
        Ref(const Ref<U>& other)
        {
            m_Ptr = static_cast<T*>(other.get());
            if (m_Ptr)
                m_Ptr->IncRefCount();
        }

        T* get() const { return m_Ptr; }
        T& operator*() const { return *m_Ptr; }
        T* operator->() const { return m_Ptr; }
        explicit operator bool() const { return m_Ptr != nullptr; }

        void reset(T* ptr = nullptr)
        {
            if (m_Ptr != ptr)
            {
                if (m_Ptr) m_Ptr->DecRefCount();
                m_Ptr = ptr;
                if (m_Ptr) m_Ptr->IncRefCount();
            }
        }

        bool operator==(const Ref& other) const { return m_Ptr == other.m_Ptr; }
        bool operator!=(const Ref& other) const { return m_Ptr != other.m_Ptr; }
        bool operator==(std::nullptr_t) const { return m_Ptr == nullptr; }
        bool operator!=(std::nullptr_t) const { return m_Ptr != nullptr; }

    private:
        T* m_Ptr = nullptr;
    };

    template <typename T, typename... Args>
    Ref<T> CreateRef(Args&&... args)
    {
        return Ref<T>(new T(std::forward<Args>(args)...));
    }

    template <typename T, typename... Args>
    Ref<T> MakeRef(Args&&... args)
    {
        return Ref<T>(new T(std::forward<Args>(args)...));
    }
}
